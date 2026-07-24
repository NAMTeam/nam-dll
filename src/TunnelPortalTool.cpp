#include "TunnelPortalTool.h"

#include "TunnelPortalToolPlacement.h"

#include "Logger.h"
#include "NetworkStubs.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4NetworkManager.h"
#include "cISC4NetworkTool.h"
#include "cISC4Occupant.h"
#include "cISC4TrafficSimulator.h"
#include "cIGZUnknown.h"
#include "cRZAutoRefCount.h"
#include "GZCLSIDDefs.h"
#include "Patching.h"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace
{
	constexpr uint32_t kNetworkTunnelOccupantID = GZCLSID::kcSC4NetworkTunnelOccupant;
	constexpr size_t kNetworkToolPlacingModeOffset = 0x50;
	constexpr size_t kNetworkToolFailureCodeOffset = 0x20c;
	constexpr uint32_t kTunnelCellNetworkFlag = 0x00080000;
	constexpr std::array<uint32_t, 4> kSurfaceApproachEdgeForTunnelDirection = {
		0x02000000, // tunnel faces north, surface approach is south
		0x00000002, // tunnel faces east, surface approach is west
		0x00000200, // tunnel faces south, surface approach is north
		0x00020000, // tunnel faces west, surface approach is east
	};

	constexpr uint32_t NetworkMask(cISC4NetworkOccupant::eNetworkType type)
	{
		return 1u << static_cast<uint32_t>(type);
	}

	using TunnelPortalToolPlacement::Endpoint;

	const char* NetworkTypeNameInternal(cISC4NetworkOccupant::eNetworkType type)
	{
		switch (type)
		{
		case cISC4NetworkOccupant::Road: return "Road";
		case cISC4NetworkOccupant::Rail: return "Rail";
		case cISC4NetworkOccupant::Highway: return "Highway";
		case cISC4NetworkOccupant::Street: return "Street";
		case cISC4NetworkOccupant::Avenue: return "Avenue";
		case cISC4NetworkOccupant::LightRail: return "Light rail";
		case cISC4NetworkOccupant::Monorail: return "Monorail";
		case cISC4NetworkOccupant::OneWayRoad: return "One-way road";
		case cISC4NetworkOccupant::DirtRoad: return "Dirt road";
		case cISC4NetworkOccupant::GroundHighway: return "Ground highway";
		default: return "Network";
		}
	}

	constexpr std::array<cISC4NetworkOccupant::eNetworkType, 10> kCandidateNetworks = {
		cISC4NetworkOccupant::Road,
		cISC4NetworkOccupant::Street,
		cISC4NetworkOccupant::DirtRoad,
		cISC4NetworkOccupant::OneWayRoad,
		cISC4NetworkOccupant::Avenue,
		cISC4NetworkOccupant::Highway,
		cISC4NetworkOccupant::GroundHighway,
		cISC4NetworkOccupant::Rail,
		cISC4NetworkOccupant::LightRail,
		cISC4NetworkOccupant::Monorail,
	};

	struct PortalCell
	{
		Endpoint endpoint;
		cSC4NetworkCellInfo* cellInfo = nullptr;
		uint8_t sequenceIndex = 0;
		cISC4NetworkOccupant* occupant = nullptr;
		cRZAutoRefCount<cIGZUnknown> tunnel;
	};

	template <typename T>
	T& FieldAt(void* object, size_t offset)
	{
		return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
	}

	template <typename T>
	const T& FieldAt(const void* object, size_t offset)
	{
		return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(object) + offset);
	}

	bool IsTwoTileNetwork(cISC4NetworkOccupant::eNetworkType networkType);
	int32_t CrossAxisCoordinate(const Endpoint& endpoint, uint8_t tunnelPieceDirection);
	bool IsSameNetworkStructure(cSC4NetworkCellInfo* first, cSC4NetworkCellInfo* second);
	bool UseAscendingTwoTileSequence(uint8_t tunnelPieceDirection);
	bool TryInferTunnelPieceDirectionFromSurfaceApproach(
		const cSC4NetworkCellInfo* cellInfo,
		cISC4NetworkOccupant::eNetworkType networkType,
		uint8_t& directionOut);
	bool IsCompatiblePortalCompanionCell(
		cSC4NetworkCellInfo* first,
		cSC4NetworkCellInfo* second,
		cISC4NetworkOccupant::eNetworkType networkType,
		uint8_t tunnelPieceDirection);
	bool RegisterSavedCustomTunnelRouteEdgeFixes(
		cISC4TrafficSimulator* trafficSimulator,
		bool refreshPathInfo);
	void RefreshTunnelPathInfo(
		cIGZUnknown* self,
		cIGZUnknown* otherEnd,
		uint8_t selfLookupPathDirection = 0xFF,
		uint16_t peerPathKeyLowWord = 0xFFFF);

	class NetworkToolPlacementStateScope
	{
	public:
		explicit NetworkToolPlacementStateScope(cSC4NetworkTool* tool)
			: tool(tool),
			  oldPlacingMode(tool ? FieldAt<uint8_t>(tool, kNetworkToolPlacingModeOffset) : 0),
			  oldFailureCode(tool ? FieldAt<uint32_t>(tool, kNetworkToolFailureCodeOffset) : 0)
		{
			if (tool)
			{
				FieldAt<uint8_t>(tool, kNetworkToolPlacingModeOffset) = 1;
				FieldAt<uint32_t>(tool, kNetworkToolFailureCodeOffset) = 0;
			}
		}

		~NetworkToolPlacementStateScope()
		{
			if (tool)
			{
				FieldAt<uint8_t>(tool, kNetworkToolPlacingModeOffset) = oldPlacingMode;
				FieldAt<uint32_t>(tool, kNetworkToolFailureCodeOffset) = oldFailureCode;
			}
		}

	private:
		cSC4NetworkTool* tool;
		uint8_t oldPlacingMode;
		uint32_t oldFailureCode;
	};

	cISC4City* GetCity()
	{
		cISC4AppPtr app;
		return app ? app->GetCity() : nullptr;
	}

	void* GetTrafficNetworkMap()
	{
		cISC4City* const city = GetCity();
		return city ? reinterpret_cast<void*>(city->GetTrafficNetwork()) : nullptr;
	}

	cISC4TrafficSimulator* GetTrafficSimulator()
	{
		cISC4City* const city = GetCity();
		return city ? city->GetTrafficSimulator() : nullptr;
	}

	cSC4NetworkTool* GetNetworkTool(cISC4NetworkManager* networkManager, cISC4NetworkOccupant::eNetworkType networkType)
	{
		if (!networkManager)
		{
			return nullptr;
		}

		cISC4NetworkTool* tool = networkManager->GetNetworkTool(static_cast<int32_t>(networkType), true);
		if (!tool)
		{
			tool = networkManager->GetNetworkTool(static_cast<int32_t>(networkType), false);
		}

		if (tool)
		{
			tool->Init();
		}

		return reinterpret_cast<cSC4NetworkTool*>(tool);
	}

	// Queries the existing cell at (endpoint.x, endpoint.z) via GetCellInfo (0x633220),
	// which computes on demand from the live occupant manager without needing a drag scan.
	cSC4NetworkCellInfo* GetEndpointCell(
		cSC4NetworkTool* tool,
		const Endpoint& endpoint)
	{
		if (!tool)
		{
			return nullptr;
		}

		const uint32_t xz = (endpoint.z << 16) | endpoint.x;
		cSC4NetworkCellInfo* const cellInfo = tool->GetCellInfo(xz);
		if (!cellInfo)
		{
			return nullptr;
		}

		if ((cellInfo->networkTypeFlags & NetworkMask(endpoint.networkType)) == 0)
		{
			return nullptr;
		}

		return cellInfo;
	}

	size_t BuildPortalCellList(
		cSC4NetworkTool* tool,
		const char* label,
		const Endpoint& endpoint,
		cSC4NetworkCellInfo* primaryCell,
		uint8_t tunnelPieceDirection,
		std::array<PortalCell, 2>& cells)
	{
		cells[0] = {};
		cells[1] = {};
		cells[0].endpoint = endpoint;
		cells[0].cellInfo = primaryCell;

		size_t count = 1;
		if (!IsTwoTileNetwork(endpoint.networkType) || !primaryCell)
		{
			cells[0].sequenceIndex = 0;
			return count;
		}

		Logger& logger = Logger::GetInstance();
		const int32_t offsets[2] = { -1, 1 };
		size_t companionCount = 0;
		for (const int32_t offset : offsets)
		{
			Endpoint candidate = endpoint;
			if ((tunnelPieceDirection & 1) != 0)
			{
				const int32_t z = static_cast<int32_t>(endpoint.z) + offset;
				if (z < 0)
				{
					continue;
				}
				candidate.z = static_cast<uint32_t>(z);
			}
			else
			{
				const int32_t x = static_cast<int32_t>(endpoint.x) + offset;
				if (x < 0)
				{
					continue;
				}
				candidate.x = static_cast<uint32_t>(x);
			}

			cSC4NetworkCellInfo* const candidateCell = GetEndpointCell(tool, candidate);
			if (IsCompatiblePortalCompanionCell(
				primaryCell,
				candidateCell,
				endpoint.networkType,
				tunnelPieceDirection))
			{
				++companionCount;
				if (companionCount == 1)
				{
					cells[1].endpoint = candidate;
					cells[1].cellInfo = candidateCell;
					count = 2;
				}
			}
		}

		if (companionCount != 1)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"TunnelPortalTool: %s %s endpoint at (%u,%u) resolved %u same-structure companion tiles; exactly one is required.",
				label,
				NetworkTypeNameInternal(endpoint.networkType),
				endpoint.x,
				endpoint.z,
				static_cast<uint32_t>(companionCount));
			return 0;
		}

		if (count == 2)
		{
			const bool ascending = UseAscendingTwoTileSequence(tunnelPieceDirection);
			const int32_t firstCrossAxis = CrossAxisCoordinate(cells[0].endpoint, tunnelPieceDirection);
			const int32_t secondCrossAxis = CrossAxisCoordinate(cells[1].endpoint, tunnelPieceDirection);
			if ((ascending && secondCrossAxis < firstCrossAxis)
				|| (!ascending && firstCrossAxis < secondCrossAxis))
			{
				std::swap(cells[0], cells[1]);
			}
		}

		for (size_t i = 0; i < count; ++i)
		{
			cells[i].sequenceIndex = static_cast<uint8_t>(i);
		}

		return count;
	}

	bool TryFindNetworkAtTileInternal(
		uint32_t x,
		uint32_t z,
		cISC4NetworkOccupant::eNetworkType& networkTypeOut)
	{
		void* const trafficNetworkMap = GetTrafficNetworkMap();
		if (!trafficNetworkMap)
		{
			return false;
		}

		using GetNetworkInfoFn = void* (__thiscall*)(void*, int32_t, int32_t, uint32_t, bool);
		auto** const vtable = *reinterpret_cast<void***>(trafficNetworkMap);
		if (!vtable || !vtable[8])
		{
			return false;
		}
		const auto getNetworkInfo = reinterpret_cast<GetNetworkInfoFn>(vtable[8]);

		for (const cISC4NetworkOccupant::eNetworkType networkType : kCandidateNetworks)
		{
			void* const entry = getNetworkInfo(
				trafficNetworkMap,
				static_cast<int32_t>(x),
				static_cast<int32_t>(z),
				NetworkMask(networkType),
				true);
			if (entry)
			{
				networkTypeOut = networkType;
				return true;
			}
		}

		return false;
	}

	uint8_t InferTunnelPieceDirection(const Endpoint& from, const Endpoint& to)
	{
		const int32_t dx = static_cast<int32_t>(to.x) - static_cast<int32_t>(from.x);
		const int32_t dz = static_cast<int32_t>(to.z) - static_cast<int32_t>(from.z);

		// InsertTunnelPieces uses a tunnel-piece rotation code, not the
		// kNextX/kNextZ direction numbering used by path code.
		if (std::abs(dx) >= std::abs(dz))
		{
			return dx >= 0 ? 1 : 3;
		}

		return dz >= 0 ? 2 : 0;
	}

	bool IsTwoTileNetwork(cISC4NetworkOccupant::eNetworkType networkType)
	{
		switch (networkType)
		{
		case cISC4NetworkOccupant::Avenue:
		case cISC4NetworkOccupant::Highway:
		case cISC4NetworkOccupant::GroundHighway:
			return true;
		default:
			return false;
		}
	}

	bool UseAscendingTwoTileSequence(const uint8_t tunnelPieceDirection)
	{
		// Preserve the native two-tile sequence transform. East- and
		// south-facing portals assign sequence 0 to the lower cross-axis
		// coordinate; west- and north-facing portals assign it to the higher
		// coordinate. The sequence numbers select native exemplar/rotation/path
		// arrays and are not a rotation-independent left/right designation.
		return tunnelPieceDirection == 1 || tunnelPieceDirection == 2;
	}

	int32_t CrossAxisCoordinate(const Endpoint& endpoint, uint8_t tunnelPieceDirection)
	{
		return (tunnelPieceDirection & 1) != 0
			? static_cast<int32_t>(endpoint.z)
			: static_cast<int32_t>(endpoint.x);
	}

	bool IsSameNetworkStructure(
		cSC4NetworkCellInfo* first,
		cSC4NetworkCellInfo* second)
	{
		if (!first || !second || !first->networkOccupant || !second->networkOccupant)
		{
			return false;
		}

		return first->networkOccupant == second->networkOccupant
			|| first->networkOccupant->IsPartOfTheSameStructure(second->networkOccupant);
	}

	bool IsCompatiblePortalCompanionCell(
		cSC4NetworkCellInfo* first,
		cSC4NetworkCellInfo* second,
		cISC4NetworkOccupant::eNetworkType networkType,
		uint8_t tunnelPieceDirection)
	{
		if (!first || !second)
		{
			return false;
		}

		if (IsSameNetworkStructure(first, second))
		{
			return true;
		}

		// Ordinary Avenue carriageways are separate occupants, so
		// IsPartOfTheSameStructure is false even though the two cells form one
		// network piece. Identify that relationship from the solved topology:
		// both halves must face the same portal direction and expose reciprocal
		// cross-axis connections to each other.
		const uint32_t firstEdges = first->edgesPerNetwork[static_cast<uint32_t>(networkType)];
		const uint32_t secondEdges = second->edgesPerNetwork[static_cast<uint32_t>(networkType)];
		const uint32_t surfaceApproachEdge =
			kSurfaceApproachEdgeForTunnelDirection[tunnelPieceDirection & 3];
		if ((firstEdges & surfaceApproachEdge) == 0
			|| (secondEdges & surfaceApproachEdge) == 0)
		{
			return false;
		}

		const int32_t dx = static_cast<int32_t>(second->x) - static_cast<int32_t>(first->x);
		const int32_t dz = static_cast<int32_t>(second->z) - static_cast<int32_t>(first->z);
		uint8_t firstToSecondDirection = 0xFF;
		if (dx == -1 && dz == 0)
		{
			firstToSecondDirection = 0; // west
		}
		else if (dx == 0 && dz == -1)
		{
			firstToSecondDirection = 1; // north
		}
		else if (dx == 1 && dz == 0)
		{
			firstToSecondDirection = 2; // east
		}
		else if (dx == 0 && dz == 1)
		{
			firstToSecondDirection = 3; // south
		}
		else
		{
			return false;
		}

		const uint8_t secondToFirstDirection = firstToSecondDirection ^ 2;
		const uint8_t firstCrossEdge =
			static_cast<uint8_t>((firstEdges >> (firstToSecondDirection * 8)) & 0xFF);
		const uint8_t secondCrossEdge =
			static_cast<uint8_t>((secondEdges >> (secondToFirstDirection * 8)) & 0xFF);
		return firstCrossEdge != 0 && secondCrossEdge != 0;
	}

	uint8_t TunnelPieceDirectionToPathDirection(uint8_t tunnelPieceDirection)
	{
		// Tunnel-piece rotations are 0=N, 1=E, 2=S, 3=W.
		// Path keys use kNextX/kNextZ numbering: 0=W, 1=N, 2=E, 3=S.
		static constexpr std::array<uint8_t, 4> kPathDirectionForTunnelPieceDirection = { 1, 2, 3, 0 };
		return kPathDirectionForTunnelPieceDirection[tunnelPieceDirection & 3];
	}

	uint16_t TunnelPathKeyLowWord(uint8_t pathDirection)
	{
		const uint8_t direction = pathDirection & 3;
		return static_cast<uint16_t>(((direction ^ 2) << 8) | direction);
	}

	bool TryInferTunnelPieceDirectionFromSurfaceApproach(
		const cSC4NetworkCellInfo* cellInfo,
		cISC4NetworkOccupant::eNetworkType networkType,
		uint8_t& directionOut)
	{
		if (!cellInfo)
		{
			return false;
		}

		const uint32_t edgeFlags = cellInfo->edgesPerNetwork[static_cast<uint32_t>(networkType)];
		uint8_t matchedDirection = 0;
		uint32_t matchCount = 0;

		for (uint8_t direction = 0; direction < kSurfaceApproachEdgeForTunnelDirection.size(); ++direction)
		{
			const uint32_t edge = kSurfaceApproachEdgeForTunnelDirection[direction];
			if ((edgeFlags & edge) != 0)
			{
				matchedDirection = direction;
				++matchCount;
			}
		}

		if (matchCount == 1)
		{
			directionOut = matchedDirection;
			return true;
		}

		return false;
	}

	using InsertTunnelPieceFn = cISC4NetworkOccupant* (__thiscall*)(
		cSC4NetworkTool* tool,
		uint8_t direction,
		uint8_t sequenceIndex,
		cSC4NetworkCellInfo* cellInfo);

	using SetOtherEndOccupantFn = void (__thiscall*)(cIGZUnknown* self, cISC4NetworkOccupant* otherEnd);
	using GetPathInfoFn = void* (__thiscall*)(cIGZUnknown* self);
	using InitTunnelPathFn = void (__thiscall*)(void* pathInfo, cIGZUnknown* self, cIGZUnknown* otherEnd);
	using DoTunnelChangedFn = void (__thiscall*)(cISC4TrafficSimulator* trafficSimulator, cIGZUnknown* tunnelOccupant, bool added);
	using DoConnectionsChangedFn = void (__thiscall*)(cISC4TrafficSimulator* trafficSimulator, uint32_t startX, uint32_t startZ, uint32_t endX, uint32_t endZ);
	using AddTripNodeFn = void (__thiscall*)(
		void* pathFinder,
		uint32_t x,
		uint32_t z,
		uint8_t edge,
		uint8_t travelMode,
		float cost,
		int currentNode,
		float heuristic,
		char outOfBounds);

	// When non-0xFF, Hook_InitTunnelPath replaces MakeTunnelPaths' coordinate-derived
	// path direction with this path-key direction. This uses kNextX/kNextZ numbering:
	// 0=west, 1=north, 2=east, 3=south.
	uint8_t sCustomPortalFacingOverride = 0xFF;
	constexpr uint16_t kAutomaticPeerPathLookup = 0xFFFE;
	uint16_t sCustomPeerPathLookupKeyLowWord = 0xFFFF;
	void* sCustomSelfPathInfo = nullptr;
	void* sCustomPeerPathInfo = nullptr;
	uint32_t sPeerPathExactKeyCount = 0;
	uint32_t sPeerPathRemappedUniqueKeyCount = 0;
	uint32_t sPeerPathUnresolvedKeyCount = 0;
	std::array<uint32_t, 4> sPeerPathUnresolvedOriginalKeys{};

	InsertTunnelPieceFn InsertTunnelPiece = reinterpret_cast<InsertTunnelPieceFn>(0x00628390);
	AddTripNodeFn AddTripNode = reinterpret_cast<AddTripNodeFn>(0x006d8fa0);
	SetOtherEndOccupantFn SetOtherEndOccupant = reinterpret_cast<SetOtherEndOccupantFn>(0x00647530);
	DoTunnelChangedFn DoTunnelChanged = reinterpret_cast<DoTunnelChangedFn>(0x007140e0);
	DoConnectionsChangedFn DoConnectionsChanged = reinterpret_cast<DoConnectionsChangedFn>(0x0071a860);

	cISC4NetworkOccupant* InsertTunnelPieceWithStyle(
		cSC4NetworkTool* tool,
		uint8_t direction,
		uint8_t sequenceIndex,
		cSC4NetworkCellInfo* cellInfo,
		const TunnelPortalStyles::Style& style)
	{
		if (style.useNativeExemplars)
		{
			return InsertTunnelPiece(tool, direction, sequenceIndex, cellInfo);
		}
		if (sequenceIndex >= style.tileCount)
		{
			return nullptr;
		}

		// The paired facade models use a fixed north/south half order, while
		// the native sequence slots also select direction-dependent path,
		// rotation, and height arrays. A quarter-turn onto the east/west axis
		// reverses only the facade model halves; keep sequenceIndex for every
		// native array.
		const uint8_t styleExemplarIndex =
			style.tileCount == 2 && (direction & 1) != 0
				? static_cast<uint8_t>(sequenceIndex ^ 1)
				: sequenceIndex;
		if (style.portalExemplarIds[styleExemplarIndex] == 0)
		{
			return nullptr;
		}

		// Windows 1.1.641 cSC4NetworkTool::InsertTunnelPiece (0x00628390)
		// obtains the exemplar ID from the vector at cSC4NetworkTool + 0x2E4.
		// Substituting one element around the native call preserves all native
		// rotation, height, occupant, and path initialization behavior.
		uint32_t* const exemplarIds =
			*reinterpret_cast<uint32_t**>(
				reinterpret_cast<uint8_t*>(tool) + 0x2E4);
		if (!exemplarIds)
		{
			return nullptr;
		}

		const uint32_t nativeExemplarId = exemplarIds[sequenceIndex];
		exemplarIds[sequenceIndex] = style.portalExemplarIds[styleExemplarIndex];
		cISC4NetworkOccupant* const occupant =
			InsertTunnelPiece(tool, direction, sequenceIndex, cellInfo);
		exemplarIds[sequenceIndex] = nativeExemplarId;
		return occupant;
	}

	struct CustomTunnelRouteEdgeFix
	{
		Endpoint first;
		Endpoint second;
		uint8_t firstArrivalEdge = 0xFF;
		uint8_t secondArrivalEdge = 0xFF;
		bool active = false;
	};

	std::array<CustomTunnelRouteEdgeFix, 128> sCustomTunnelRouteEdgeFixes{};
	uint32_t sNextCustomTunnelRouteEdgeFix = 0;
	cISC4TrafficSimulator* sRegisteredTrafficSimulator = nullptr;
	bool sSavedTunnelRouteEdgeFixesScanned = false;

	bool SameCell(const Endpoint& endpoint, uint32_t x, uint32_t z)
	{
		return endpoint.x == x && endpoint.z == z;
	}

	void ClearCustomTunnelRouteEdgeFixes()
	{
		for (CustomTunnelRouteEdgeFix& fix : sCustomTunnelRouteEdgeFixes)
		{
			fix = {};
		}
		sNextCustomTunnelRouteEdgeFix = 0;
	}

	void ResetCustomTunnelRouteEdgeFixes(cISC4TrafficSimulator* trafficSimulator)
	{
		ClearCustomTunnelRouteEdgeFixes();
		sRegisteredTrafficSimulator = trafficSimulator;
		sSavedTunnelRouteEdgeFixesScanned = false;
	}

	void RegisterCustomTunnelRouteEdgeFix(
		const Endpoint& first,
		const Endpoint& second,
		uint8_t firstArrivalEdge,
		uint8_t secondArrivalEdge)
	{
		CustomTunnelRouteEdgeFix& fix =
			sCustomTunnelRouteEdgeFixes[sNextCustomTunnelRouteEdgeFix % sCustomTunnelRouteEdgeFixes.size()];

		fix.first = first;
		fix.second = second;
		fix.firstArrivalEdge = firstArrivalEdge & 3;
		fix.secondArrivalEdge = secondArrivalEdge & 3;
		fix.active = true;
		++sNextCustomTunnelRouteEdgeFix;
	}

	bool TryGetCustomTunnelArrivalEdge(
		uint32_t currentX,
		uint32_t currentZ,
		uint32_t destinationX,
		uint32_t destinationZ,
		uint8_t& edgeOut)
	{
		cISC4TrafficSimulator* const trafficSimulator = GetTrafficSimulator();
		if (trafficSimulator != sRegisteredTrafficSimulator)
		{
			ResetCustomTunnelRouteEdgeFixes(trafficSimulator);
		}
		if (!sSavedTunnelRouteEdgeFixesScanned && trafficSimulator)
		{
			// This can run inside pathfinder hooks, so only rebuild the edge-fix
			// lookup here. RefreshCity performs path-vector mutations at a safe
			// lifecycle point.
			sSavedTunnelRouteEdgeFixesScanned =
				RegisterSavedCustomTunnelRouteEdgeFixes(trafficSimulator, false);
		}

		for (const CustomTunnelRouteEdgeFix& fix : sCustomTunnelRouteEdgeFixes)
		{
			if (!fix.active)
			{
				continue;
			}

			if (SameCell(fix.first, currentX, currentZ) && SameCell(fix.second, destinationX, destinationZ))
			{
				edgeOut = fix.secondArrivalEdge;
				return true;
			}
			if (SameCell(fix.second, currentX, currentZ) && SameCell(fix.first, destinationX, destinationZ))
			{
				edgeOut = fix.firstArrivalEdge;
				return true;
			}
		}

		return false;
	}

	constexpr size_t kTrafficSimulatorTunnelMapOffset = 0xc8;
	constexpr size_t kTrafficSimulatorTunnelListOffset = 0x104;
	constexpr size_t kMaxTunnelMapBuckets = 4096;
	constexpr size_t kMaxTunnelMapNodes = 16384;

	struct RawTunnelMapNode
	{
		RawTunnelMapNode* next;
		uint16_t key;
		uint16_t padding;
		uint32_t value[11];
	};

	struct RawTunnelMap
	{
		uint32_t reserved;
		RawTunnelMapNode** start;
		RawTunnelMapNode** end;
		RawTunnelMapNode** capacity;
		uint32_t size;
	};

	static_assert(sizeof(RawTunnelMapNode) == 0x34);
	static_assert(sizeof(RawTunnelMap) == 0x14);

	struct RawTunnelListNode
	{
		RawTunnelListNode* next;
		RawTunnelListNode* previous;
		uint32_t value[11];
	};

	static_assert(offsetof(RawTunnelListNode, value) == 0x08);

	const RawTunnelMap* GetTunnelMap(cISC4TrafficSimulator* trafficSimulator)
	{
		return trafficSimulator
			? reinterpret_cast<const RawTunnelMap*>(reinterpret_cast<const uint8_t*>(trafficSimulator) + kTrafficSimulatorTunnelMapOffset)
			: nullptr;
	}

	RawTunnelListNode* GetTunnelListSentinel(cISC4TrafficSimulator* trafficSimulator)
	{
		return trafficSimulator
			? *reinterpret_cast<RawTunnelListNode**>(reinterpret_cast<uint8_t*>(trafficSimulator) + kTrafficSimulatorTunnelListOffset)
			: nullptr;
	}

	bool IsUsableTunnelMap(const RawTunnelMap* map)
	{
		if (!map || !map->start || !map->end || map->end < map->start)
		{
			return false;
		}

		const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
		return bucketCount > 0 && bucketCount <= kMaxTunnelMapBuckets;
	}

	uint16_t PackedCellKey(uint32_t x, uint32_t z)
	{
		return static_cast<uint16_t>(((x & 0xff) << 8) | (z & 0xff));
	}

	const RawTunnelMapNode* FindTunnelRecord(
		cISC4TrafficSimulator* trafficSimulator,
		const Endpoint& endpoint)
	{
		const RawTunnelMap* const map = GetTunnelMap(trafficSimulator);
		if (!IsUsableTunnelMap(map))
		{
			return nullptr;
		}

		const uint16_t key = PackedCellKey(endpoint.x, endpoint.z);
		const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
		for (RawTunnelMapNode* node = map->start[key % bucketCount]; node; node = node->next)
		{
			if (node->key == key)
			{
				return node;
			}
		}

		return nullptr;
	}

	void FillTemporaryTunnelListNode(
		RawTunnelListNode& node,
		const Endpoint& first,
		const Endpoint& second,
		const RawTunnelMapNode* sourceRecord,
		uint16_t mask)
	{
		std::fill(std::begin(node.value), std::end(node.value), 0);
		if (sourceRecord)
		{
			std::copy(std::begin(sourceRecord->value), std::end(sourceRecord->value), std::begin(node.value));
		}

		uint8_t* const bytes = reinterpret_cast<uint8_t*>(node.value);
		bytes[0] = static_cast<uint8_t>(first.x);
		bytes[1] = static_cast<uint8_t>(first.z);
		bytes[2] = static_cast<uint8_t>(second.x);
		bytes[3] = static_cast<uint8_t>(second.z);
		*reinterpret_cast<uint16_t*>(bytes + 12) = mask;
	}

	class TemporaryTunnelConnectionRecordScope
	{
	public:
		TemporaryTunnelConnectionRecordScope(
			cISC4TrafficSimulator* trafficSimulator,
			const Endpoint& first,
			const Endpoint& second)
			: sentinel(GetTunnelListSentinel(trafficSimulator)),
			  oldNext(nullptr),
			  oldPrevious(nullptr),
			  linked(false)
		{
			if (!sentinel)
			{
				return;
			}

			const RawTunnelMapNode* const forwardRecord = FindTunnelRecord(trafficSimulator, first);
			const RawTunnelMapNode* const reverseRecord = FindTunnelRecord(trafficSimulator, second);
			const uint16_t forwardMask = forwardRecord ? static_cast<uint16_t>(forwardRecord->value[0] >> 16) : 0x01FE;
			const uint16_t reverseMask = reverseRecord ? static_cast<uint16_t>(reverseRecord->value[0] >> 16) : 0x01FE;
			FillTemporaryTunnelListNode(forwardNode, first, second, forwardRecord, forwardMask);
			FillTemporaryTunnelListNode(reverseNode, second, first, reverseRecord, reverseMask);
			oldNext = sentinel->next;
			oldPrevious = sentinel->previous;
			if (!oldNext)
			{
				oldNext = sentinel;
			}
			if (!oldPrevious)
			{
				oldPrevious = sentinel;
			}

			forwardNode.next = &reverseNode;
			forwardNode.previous = sentinel;
			reverseNode.next = oldNext;
			reverseNode.previous = &forwardNode;
			oldNext->previous = &reverseNode;
			sentinel->next = &forwardNode;
			if (oldPrevious == sentinel)
			{
				sentinel->previous = &reverseNode;
			}
			linked = true;
		}

		~TemporaryTunnelConnectionRecordScope()
		{
			if (linked)
			{
				sentinel->next = oldNext;
				sentinel->previous = oldPrevious;
				if (oldNext)
				{
					oldNext->previous = sentinel;
				}
			}
		}

	private:
		RawTunnelListNode forwardNode{};
		RawTunnelListNode reverseNode{};
		RawTunnelListNode* sentinel;
		RawTunnelListNode* oldNext;
		RawTunnelListNode* oldPrevious;
		bool linked;
	};

	bool TryGetTunnelAtEndpoint(
		Endpoint& endpoint,
		uint8_t& directionOut,
		cRZAutoRefCount<cIGZUnknown>& tunnelOut)
	{
		if (!TryFindNetworkAtTileInternal(endpoint.x, endpoint.z, endpoint.networkType))
		{
			return false;
		}

		cISC4City* city = GetCity();
		cISC4NetworkManager* networkManager = city ? city->GetNetworkManager() : nullptr;
		cSC4NetworkTool* tool = GetNetworkTool(networkManager, endpoint.networkType);
		cSC4NetworkCellInfo* cellInfo = GetEndpointCell(tool, endpoint);
		cISC4NetworkOccupant* occupant = cellInfo ? cellInfo->networkOccupant : nullptr;
		if (occupant && occupant->QueryInterface(kNetworkTunnelOccupantID, tunnelOut.AsPPVoid()) && tunnelOut)
		{
			// The placed occupant stores the exemplar/model rotation, which is
			// one quarter-turn ahead of InsertTunnelPiece's direction code for
			// the native tunnel arrays. Prefer the surviving surface approach
			// edge because it expresses the original portal direction directly;
			// use the inverse rotation transform only when the edge is ambiguous.
			directionOut = static_cast<uint8_t>((occupant->GetRotation() + 3) & 3);
			TryInferTunnelPieceDirectionFromSurfaceApproach(
				cellInfo,
				endpoint.networkType,
				directionOut);
			return true;
		}

		return false;
	}

	bool RegisterSavedCustomTunnelRouteEdgeFixes(
		cISC4TrafficSimulator* trafficSimulator,
		bool refreshPathInfo)
	{
		const RawTunnelMap* const map = GetTunnelMap(trafficSimulator);
		if (!IsUsableTunnelMap(map))
		{
			return false;
		}

		const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
		uint32_t visitedNodes = 0;
		for (RawTunnelMapNode** bucket = map->start; bucket != map->end && visitedNodes < kMaxTunnelMapNodes; ++bucket)
		{
			for (RawTunnelMapNode* node = *bucket; node && visitedNodes < kMaxTunnelMapNodes; node = node->next)
			{
				++visitedNodes;
				Endpoint first;
				first.x = static_cast<uint8_t>(node->key >> 8);
				first.z = static_cast<uint8_t>(node->key & 0xFF);
				Endpoint second;
				second.x = static_cast<uint8_t>(node->value[0] & 0xFF);
				second.z = static_cast<uint8_t>((node->value[0] >> 8) & 0xFF);

				const uint16_t firstKey = PackedCellKey(first.x, first.z);
				const uint16_t secondKey = PackedCellKey(second.x, second.z);
				if (node->key != firstKey || firstKey > secondKey)
				{
					continue;
				}

				uint8_t firstDirection = 0xFF;
				uint8_t secondDirection = 0xFF;
				cRZAutoRefCount<cIGZUnknown> firstTunnel;
				cRZAutoRefCount<cIGZUnknown> secondTunnel;
				if (!TryGetTunnelAtEndpoint(first, firstDirection, firstTunnel)
					|| !TryGetTunnelAtEndpoint(second, secondDirection, secondTunnel))
				{
					continue;
				}

				const uint8_t firstVectorDirection = InferTunnelPieceDirection(first, second);
				const uint8_t secondVectorDirection = InferTunnelPieceDirection(second, first);
				const bool requiresCustomPathStitch =
					firstDirection != firstVectorDirection
						|| secondDirection != secondVectorDirection;
				const bool requiresAvenuePathKeyResolution =
					IsTwoTileNetwork(first.networkType)
						|| IsTwoTileNetwork(second.networkType);
				if (!requiresCustomPathStitch && !requiresAvenuePathKeyResolution)
				{
					continue;
				}

				if (requiresCustomPathStitch)
				{
					RegisterCustomTunnelRouteEdgeFix(
						first,
						second,
						TunnelPieceDirectionToPathDirection(firstDirection),
						TunnelPieceDirectionToPathDirection(secondDirection));
				}
				if (refreshPathInfo)
				{
					const uint16_t firstPeerLookup = requiresAvenuePathKeyResolution
						? kAutomaticPeerPathLookup
						: TunnelPathKeyLowWord(TunnelPieceDirectionToPathDirection(secondDirection));
					const uint16_t secondPeerLookup = requiresAvenuePathKeyResolution
						? kAutomaticPeerPathLookup
						: TunnelPathKeyLowWord(TunnelPieceDirectionToPathDirection(firstDirection));
					RefreshTunnelPathInfo(
						firstTunnel,
						secondTunnel,
						TunnelPieceDirectionToPathDirection(firstDirection),
						firstPeerLookup);
					RefreshTunnelPathInfo(
						secondTunnel,
						firstTunnel,
						TunnelPieceDirectionToPathDirection(secondDirection),
						secondPeerLookup);
				}
			}
		}

		return true;
	}

	void PrepareTunnelEndpointCell(cSC4NetworkCellInfo* cellInfo)
	{
		if (!cellInfo)
		{
			return;
		}

		// Native InsertTunnelPieces only marks byte +0x51 before calling
		// InsertTunnelPiece. Its cell edge topology comes from the solved drag.
		// Our source cells are existing network tiles, so retain their
		// network-specific topology instead of substituting Avenue-derived masks
		// for Highway, Ground Highway, or perpendicular portal orientations.
		cellInfo->networkTypeFlags |= kTunnelCellNetworkFlag;
		FieldAt<uint8_t>(cellInfo, 0x51) = 1;
		FieldAt<uint8_t>(cellInfo, 0x53) = 1;
	}

	bool QueryTunnelOccupant(cISC4NetworkOccupant* occupant, cRZAutoRefCount<cIGZUnknown>& tunnelOccupant)
	{
		if (!occupant)
		{
			return false;
		}

		return occupant->QueryInterface(kNetworkTunnelOccupantID, tunnelOccupant.AsPPVoid()) && tunnelOccupant;
	}

	// Native PlaceNetwork calls cSC4NetworkConstructionCrew::MarkOccupantsUsable
	// after InsertTunnelPieces. Direct insertion bypasses that commit-list pass.
	void MarkNetworkOccupantUsable(cISC4NetworkOccupant* occupant, const char* label)
	{
		Logger& logger = Logger::GetInstance();

		if (!occupant)
		{
			logger.WriteLineFormatted(LogLevel::Error,
				"TunnelPortalTool: cannot mark %s occupant usable, occupant is null.", label);
			return;
		}

		cISC4Occupant* const baseOccupant = occupant->AsOccupant();
		if (!baseOccupant)
		{
			logger.WriteLineFormatted(LogLevel::Error,
				"TunnelPortalTool: cannot mark %s occupant visible, AsOccupant returned null.", label);
			return;
		}

		occupant->ClearNetworkFlag(0x4000);
		baseOccupant->SetVisibility(true, true);
		occupant->SetNetworkFlag(0x10000000);
	}

	// Trampoline pointer used by the naked assembly stub below.
	// Set by Install after the path-info hooks are installed.
	void* sInitTunnelPathTrampolinePtr = nullptr;
	void* sPeerPathLookupTrampolinePtr = nullptr;

	constexpr size_t kPathInfoPathMapOffset = 0x1c;
	constexpr size_t kMaxPathInfoBuckets = 4096;
	constexpr size_t kMaxPathInfoNodes = 16384;

	struct RawPathVector
	{
		uint8_t* start;
		uint8_t* end;
		uint8_t* capacity;
	};

	struct RawPathMapNode
	{
		RawPathMapNode* next;
		uint32_t key;
		RawPathVector path;
	};

	struct RawPathMap
	{
		uint32_t reserved;
		RawPathMapNode** start;
		RawPathMapNode** end;
		RawPathMapNode** capacity;
	};

	static_assert(sizeof(RawPathVector) == 0x0c);
	static_assert(sizeof(RawPathMapNode) == 0x14);
	static_assert(sizeof(RawPathMap) == 0x10);

	struct RawPathPoint
	{
		float x;
		float y;
		float z;
	};

	static_assert(sizeof(RawPathPoint) == 0x0c);

	const RawPathMap* GetPathMap(void* pathInfo)
	{
		return pathInfo
			? reinterpret_cast<const RawPathMap*>(
				reinterpret_cast<const uint8_t*>(pathInfo) + kPathInfoPathMapOffset)
			: nullptr;
	}

	bool IsUsablePathMap(const RawPathMap* map)
	{
		if (!map || !map->start || !map->end || map->end < map->start)
		{
			return false;
		}

		const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
		return bucketCount > 0 && bucketCount <= kMaxPathInfoBuckets;
	}

	bool HasPathPoints(const RawPathMapNode* node)
	{
		return node
			&& node->path.start
			&& node->path.end
			&& node->path.end > node->path.start;
	}

	uint32_t CountPathPoints(const RawPathMapNode* node)
	{
		if (!HasPathPoints(node))
		{
			return 0;
		}

		const ptrdiff_t byteCount = node->path.end - node->path.start;
		return byteCount >= static_cast<ptrdiff_t>(sizeof(RawPathPoint))
			&& byteCount % sizeof(RawPathPoint) == 0
			? static_cast<uint32_t>(byteCount / sizeof(RawPathPoint))
			: 0;
	}

	const RawPathPoint* FirstPathPoint(const RawPathMapNode* node)
	{
		return CountPathPoints(node) > 0
			? reinterpret_cast<const RawPathPoint*>(node->path.start)
			: nullptr;
	}

	const RawPathPoint* LastPathPoint(const RawPathMapNode* node)
	{
		if (!HasPathPoints(node))
		{
			return nullptr;
		}

		const ptrdiff_t byteCount = node->path.end - node->path.start;
		if (byteCount < static_cast<ptrdiff_t>(sizeof(RawPathPoint))
			|| byteCount % sizeof(RawPathPoint) != 0)
		{
			return nullptr;
		}

		return reinterpret_cast<const RawPathPoint*>(
			node->path.end - sizeof(RawPathPoint));
	}

	void TracePathMap(const char* label, cIGZUnknown* tunnel)
	{
		if (!tunnel)
		{
			return;
		}

		void** tunnelVtable = *reinterpret_cast<void***>(tunnel);
		GetPathInfoFn getPathInfo = reinterpret_cast<GetPathInfoFn>(tunnelVtable[0x31]);
		void* const pathInfo = getPathInfo(tunnel);
		const RawPathMap* const map = GetPathMap(pathInfo);
		Logger& logger = Logger::GetInstance();
		if (!IsUsablePathMap(map))
		{
			logger.WriteLineFormatted(
				LogLevel::Debug,
				"TunnelPortalTool: %s path map unavailable pathInfo=%p.",
				label,
				pathInfo);
			return;
		}

		uint32_t pathIndex = 0;
		for (RawPathMapNode** bucket = map->start;
			bucket != map->end && pathIndex < 24;
			++bucket)
		{
			for (RawPathMapNode* node = *bucket; node && pathIndex < 24; node = node->next)
			{
				const RawPathPoint* const first = FirstPathPoint(node);
				const RawPathPoint* const last = LastPathPoint(node);
				logger.WriteLineFormatted(
					LogLevel::Debug,
					"TunnelPortalTool: %s path[%u] key=0x%08X keyType=%u pathType=%u unique=%u entry=%u exit=%u points=%u first=(%.2f,%.2f,%.2f) last=(%.2f,%.2f,%.2f).",
					label,
					pathIndex,
					node->key,
					(node->key >> 28) & 0x0F,
					(node->key >> 24) & 0x0F,
					(node->key >> 16) & 0xFF,
					(node->key >> 8) & 0xFF,
					node->key & 0xFF,
					CountPathPoints(node),
					first ? first->x : 0.0f,
					first ? first->y : 0.0f,
					first ? first->z : 0.0f,
					last ? last->x : 0.0f,
					last ? last->y : 0.0f,
					last ? last->z : 0.0f);
				++pathIndex;
			}
		}

		if (pathIndex == 0)
		{
			logger.WriteLineFormatted(
				LogLevel::Debug,
				"TunnelPortalTool: %s path map is empty pathInfo=%p.",
				label,
				pathInfo);
		}
	}

	uint32_t CountPathsExitingTowardMouth(
		cIGZUnknown* tunnel,
		uint8_t tunnelPieceDirection)
	{
		if (!tunnel)
		{
			return 0;
		}

		void** tunnelVtable = *reinterpret_cast<void***>(tunnel);
		GetPathInfoFn getPathInfo = reinterpret_cast<GetPathInfoFn>(tunnelVtable[0x31]);
		const RawPathMap* const map = GetPathMap(getPathInfo(tunnel));
		if (!IsUsablePathMap(map))
		{
			return 0;
		}

		const uint8_t exitDirection =
			TunnelPieceDirectionToPathDirection(tunnelPieceDirection);
		uint32_t matchingPathCount = 0;
		uint32_t visitedNodes = 0;
		for (RawPathMapNode** bucket = map->start;
			bucket != map->end && visitedNodes < kMaxPathInfoNodes;
			++bucket)
		{
			for (RawPathMapNode* node = *bucket;
				node && visitedNodes < kMaxPathInfoNodes;
				node = node->next)
			{
				++visitedNodes;
				if (HasPathPoints(node)
					&& static_cast<uint8_t>(node->key) == exitDirection)
				{
					++matchingPathCount;
				}
			}
		}

		return matchingPathCount;
	}

	const RawPathMapNode* FindPathKey(const RawPathMap* map, uint32_t key)
	{
		if (!IsUsablePathMap(map))
		{
			return nullptr;
		}

		const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
		for (RawPathMapNode* node = map->start[key % bucketCount]; node; node = node->next)
		{
			if (node->key == key)
			{
				return node;
			}
		}

		return nullptr;
	}

	// Path key layout confirmed from cISC4PathInfo::ExtractFullPathKey:
	//   bits 31..28 key type, 27..24 path type, 23..16 uniqueness index,
	//   15..8 entry direction, 7..0 exit direction.
	//
	// Avenue halves can number otherwise-compatible paths differently. Keep
	// the key/path type byte and requested peer directions, but resolve the
	// peer's actual uniqueness byte when the direct rewritten key is absent.
	uint32_t __stdcall ResolvePeerPathLookupKey(uint32_t originalKey)
	{
		if (sCustomPeerPathLookupKeyLowWord == 0xFFFF)
		{
			return originalKey;
		}

		const bool automaticPeerLookup =
			sCustomPeerPathLookupKeyLowWord == kAutomaticPeerPathLookup;
		const uint32_t rewrittenKey = automaticPeerLookup
			? originalKey
			: (originalKey & 0xFFFF0000u) | sCustomPeerPathLookupKeyLowWord;
		const RawPathMap* const peerMap = GetPathMap(sCustomPeerPathInfo);
		const RawPathMapNode* const exactNode = FindPathKey(peerMap, rewrittenKey);
		if (HasPathPoints(exactNode))
		{
			++sPeerPathExactKeyCount;
			return rewrittenKey;
		}
		if (!IsUsablePathMap(peerMap))
		{
			if (sPeerPathUnresolvedKeyCount < sPeerPathUnresolvedOriginalKeys.size())
			{
				sPeerPathUnresolvedOriginalKeys[sPeerPathUnresolvedKeyCount] = originalKey;
			}
			++sPeerPathUnresolvedKeyCount;
			return rewrittenKey;
		}

		const uint32_t typeMask = 0xFF000000u;
		const RawPathMapNode* const sourceNode =
			FindPathKey(GetPathMap(sCustomSelfPathInfo), originalKey);
		const RawPathPoint* const sourceLastPoint = LastPathPoint(sourceNode);
		const uint32_t originalUniqueIndex = originalKey & 0x00FF0000u;
		const RawPathMapNode* bestSameUniqueNode = nullptr;
		float bestSameUniqueDistanceSquared = 3.4e38f;
		const RawPathMapNode* bestNode = nullptr;
		float bestDistanceSquared = 3.4e38f;
		uint32_t visitedNodes = 0;
		for (RawPathMapNode** bucket = peerMap->start;
			bucket != peerMap->end && visitedNodes < kMaxPathInfoNodes;
			++bucket)
		{
			for (RawPathMapNode* node = *bucket;
				node && visitedNodes < kMaxPathInfoNodes;
				node = node->next)
			{
				++visitedNodes;
				if ((node->key & typeMask) == (originalKey & typeMask)
					&& (automaticPeerLookup
						|| static_cast<uint16_t>(node->key) == sCustomPeerPathLookupKeyLowWord)
					&& HasPathPoints(node))
				{
					const RawPathPoint* const peerFirstPoint = FirstPathPoint(node);
					float distanceSquared = 0.0f;
					if (sourceLastPoint && peerFirstPoint)
					{
						const float dx = peerFirstPoint->x - sourceLastPoint->x;
						const float dz = peerFirstPoint->z - sourceLastPoint->z;
						distanceSquared = dx * dx + dz * dz;
					}
					if (!bestNode || distanceSquared < bestDistanceSquared)
					{
						bestNode = node;
						bestDistanceSquared = distanceSquared;
					}
					if ((node->key & 0x00FF0000u) == originalUniqueIndex
						&& (!bestSameUniqueNode
							|| distanceSquared < bestSameUniqueDistanceSquared))
					{
						bestSameUniqueNode = node;
						bestSameUniqueDistanceSquared = distanceSquared;
					}
				}
			}
		}

		const RawPathMapNode* const selectedNode =
			bestSameUniqueNode ? bestSameUniqueNode : bestNode;
		if (selectedNode)
		{
			++sPeerPathRemappedUniqueKeyCount;
			return selectedNode->key;
		}

		if (sPeerPathUnresolvedKeyCount < sPeerPathUnresolvedOriginalKeys.size())
		{
			sPeerPathUnresolvedOriginalKeys[sPeerPathUnresolvedKeyCount] = originalKey;
		}
		++sPeerPathUnresolvedKeyCount;
		return rewrittenKey;
	}

	// Mid-function hook targeting the 6-byte "call [edx+0xC4]" at 0x0053FDEE inside
	// cSC4PathInfo::MakeTunnelPaths (confirmed function entry: 0x0053FD70).
	//
	// MakeTunnelPaths computes a cardinal direction from A's cell to B's cell and stores it
	// as a byte at [esp+0x58] (param_1 slot reused as a local after the arg is loaded into
	// registers). Prologue: sub esp,0x44 + push ebx,ebp,esi,edi = 0x54 total; param_1 at
	// entry+4 → [esp+0x58] inside the function. All five direction-assignment instructions
	// (C6 44 24 58 00/01/02/03) complete before 0x0053FDEE. By overwriting [esp+0x58] here,
	// after the coordinate comparison but before the loop reads it, we redirect path stitching
	// to the portal's actual facing direction instead of the coordinate-derived one.
	//
	// Register state at 0x0053FDEE (set by the two preceding instructions at 0x0053FDEA/EC):
	//   edx = vtable of esi (otherEnd's vtable) → used by the original call [edx+0xC4]
	//   ecx = esi = otherEnd                    → thiscall receiver for GetPathInfo
	//   eax = clobbered freely by our mov al    → overwritten by the call's return value
	//   esi = otherEnd (preserved, not touched)
	//   edi = pathInfo/this (preserved, not touched)
	NAKED_FUN void Hook_InitTunnelPath()
	{
		__asm {
			cmp  byte ptr [sCustomPortalFacingOverride], 0xFF
			je   done
			mov  al, byte ptr [sCustomPortalFacingOverride]
			mov  byte ptr [esp+0x58], al
		done:
			jmp  dword ptr [sInitTunnelPathTrampolinePtr]
		}
	}

	// Hook site: 0x0053FDEE — the 6-byte "call [edx+0xC4]" instruction inside MakeTunnelPaths.
	// This is the first instruction after all direction-assignment branches have completed.
	Patching::InlineHook sInitTunnelPathHook{
		0x0053FDEE,
		reinterpret_cast<void*>(&Hook_InitTunnelPath),
		{ 0xFF, 0x92, 0xC4, 0x00, 0x00, 0x00 },
		true
	};

	// Hook site: 0x0053FE31 - "push eax; mov ecx,ebx; lea ebp,[esi+8]".
	// This is immediately before MakeTunnelPaths calls peerPathInfo->GetPath(key, 0).
	// For mixed-facing portals, the peer path uses a different low word:
	// low byte = peer exit direction, next byte = opposite peer direction.
	NAKED_FUN void Hook_PeerPathLookupKey()
	{
		__asm {
			cmp  word ptr [sCustomPeerPathLookupKeyLowWord], 0xFFFF
			je   done
			push edx
			push ecx
			push eax
			call ResolvePeerPathLookupKey
			pop  ecx
			pop  edx
		done:
			jmp  dword ptr [sPeerPathLookupTrampolinePtr]
		}
	}

	Patching::InlineHook sPeerPathLookupHook{
		0x0053FE31,
		reinterpret_cast<void*>(&Hook_PeerPathLookupKey),
		{ 0x50, 0x8B, 0xCB, 0x8D, 0x6E, 0x08 },
		true
	};

	void __fastcall Hook_AddTunnelTripNode(
		void* pathFinder,
		void*,
		uint32_t x,
		uint32_t z,
		uint8_t edge,
		uint8_t travelMode,
		float cost,
		int currentNode,
		float heuristic,
		char outOfBounds)
	{
		const uint32_t currentX = currentNode
			? static_cast<uint32_t>(static_cast<uint16_t>(FieldAt<int16_t>(reinterpret_cast<void*>(currentNode), 0x14)))
			: 0xFFFF;
		const uint32_t currentZ = currentNode
			? static_cast<uint32_t>(static_cast<uint16_t>(FieldAt<int16_t>(reinterpret_cast<void*>(currentNode), 0x16)))
			: 0xFFFF;

		uint8_t replacementEdge = 0xFF;
		if (currentNode && TryGetCustomTunnelArrivalEdge(currentX, currentZ, x, z, replacementEdge))
		{
			if ((edge & 3) != replacementEdge)
			{
				edge = static_cast<uint8_t>((edge & ~0x03u) | replacementEdge);
			}
		}

		AddTripNode(pathFinder, x, z, edge, travelMode, cost, currentNode, heuristic, outOfBounds);
	}

	// FloodSubnetwork tunnel edge fix.
	//
	// FloodSubnetwork (Windows: 0x00717ec0) stamps subnet IDs on reachable
	// network edges using a DFS. Its tunnel branch (pseudo-direction 5) looks up
	// a tunnel connection via the map at this+0xc8, then jumps the flood to the
	// peer portal cell. It reuses the current node's "side" as the arrival side
	// at the peer, which can be wrong for custom portal pairs.
	//
	// We fix this by redirecting the CALL to GetNetworkInfo at 0x00718215 (inside
	// the tunnel branch) to a wrapper that calls the original, then overwrites
	// uStack_68 on the caller's stack with the registered destination arrival
	// edge when one of our custom tunnel portal pairs is involved.
	//
	// Stack layout at hook entry (ESP = E):
	//   [E+0x00] return address (0x71821A)
	//   [E+0x04] networkType (PUSH EDX)
	//   [E+0x08] peerX       (PUSH EBX)
	//   [E+0x0C] peerY       (PUSH ECX)
	//   [E+0x10..] FloodSubnetwork's adjusted_ESP locals
	//     [E+0x24] = packed state (byte0=x, byte1=y, byte2=side, byte3=nextDir)
	//     [E+0x30] = uStack_68 (the edge value to fix)
	//   ECX = this (cSC4TrafficSimulator*)
	//   EBX = peerX (preserved across calls)

	using FloodGetNetworkInfoFn = void* (__thiscall*)(void*, uint32_t, uint32_t, uint32_t);
	FloodGetNetworkInfoFn sOriginalFloodGetNetworkInfo =
		reinterpret_cast<FloodGetNetworkInfoFn>(0x0070FB30);

	bool __stdcall ShouldFixFloodSubnetworkTunnelEdge(
		uint32_t currentX,
		uint32_t currentY,
		uint32_t peerX,
		uint32_t peerY)
	{
		uint8_t arrivalEdge = 0xFF;
		return TryGetCustomTunnelArrivalEdge(currentX, currentY, peerX, peerY, arrivalEdge);
	}

	void __stdcall FloodSubnetworkFixTunnelEdge(
		uint32_t currentX,
		uint32_t currentY,
		uint32_t peerX,
		uint32_t peerY,
		uint32_t* edgePtr)
	{
		uint8_t arrivalEdge = 0xFF;
		if (edgePtr && TryGetCustomTunnelArrivalEdge(currentX, currentY, peerX, peerY, arrivalEdge))
		{
			const uint32_t currentEdge = *edgePtr & 0xFF;
			if (currentEdge != arrivalEdge)
			{
				*edgePtr = (*edgePtr & ~0x03u) | arrivalEdge;
			}
		}
	}

	NAKED_FUN void Hook_FloodSubnetworkTunnelGetNetworkInfo()
	{
		__asm {
			// Native tunnels take the original call path. Registered custom portal
			// pairs use the wrapper with their explicit destination arrival edge.
			push ecx
			push edx
			push dword ptr [esp+0x14]       // peerY, ESP = E-12
			push dword ptr [esp+0x14]       // peerX, ESP = E-16
			movzx eax, byte ptr [esp+0x35]  // currentY, byte 1 of packed state at E+0x25
			push eax
			movzx eax, byte ptr [esp+0x38]  // currentX, byte 0 of packed state at E+0x24
			push eax
			call ShouldFixFloodSubnetworkTunnelEdge
			pop  edx
			pop  ecx
			test al, al
			jnz  customTunnelEdgeFix
			jmp  dword ptr [sOriginalFloodGetNetworkInfo]

		customTunnelEdgeFix:
			// Phase 1: Call original GetNetworkInfo(this, networkType, peerX, peerY).
			// ECX = this, stack = [retaddr, netType, peerX, peerY].
			// Re-push args for the real thiscall (callee cleans 12 bytes).
			push dword ptr [esp+0x0C]
			push dword ptr [esp+0x0C]
			push dword ptr [esp+0x0C]
			call dword ptr [sOriginalFloodGetNetworkInfo]
			// EAX = peer NetworkInfo*, ESP = E (3 re-pushed args cleaned by callee).

			// Phase 2: Fix uStack_68 on the caller's stack.
			push eax                          // save GetNetworkInfo result, ESP = E-4
			push ecx                          // ESP = E-8
			push edx                          // ESP = E-12

			// Key locations relative to hook entry ESP (= E):
			//   [E+0x08]  = peerX  (original caller arg)
			//   [E+0x0C]  = peerY  (original caller arg)
			//   [E+0x24]  = uStack_74 byte 0 = currentX
			//   [E+0x25]  = uStack_74 byte 1 = currentY
			//   [E+0x30]  = uStack_68 (arrival edge to fix)
			// NOTE: ESP shifts with each push; offsets below account for that.

			lea  eax, [esp + 0x3C]            // &uStack_68,       ESP = E-12
			push eax                          // arg5: edgePtr,    ESP = E-16
			push dword ptr [esp + 0x1C]       // arg4: peerY,      ESP = E-20  [E-16+0x1C]=[E+0x0C]
			push dword ptr [esp + 0x1C]       // arg3: peerX,      ESP = E-24  [E-20+0x1C]=[E+0x08]
			movzx eax, byte ptr [esp + 0x3D]  // currentY (byte 1 of packed state at E+0x25) ESP = E-24
			push eax                          // arg2: currentY,   ESP = E-28
			movzx eax, byte ptr [esp + 0x40]  // currentX (byte 0 of packed state at E+0x24) ESP = E-28
			push eax                          // arg1: currentX,   ESP = E-32
			call FloodSubnetworkFixTunnelEdge  // __stdcall, cleans 20 bytes → ESP = E-12

			pop  edx
			pop  ecx
			pop  eax                          // restored GetNetworkInfo result, ESP = E

			ret  0x0C                         // clean 3 original args, like the real GetNetworkInfo
		}
	}

	// Windows InsertTunnelPieces (0x006287d0) QueryInterfaces both occupants to
	// kcSC4NetworkTunnelOccupant, then calls tunnel->GetPathInfo (+0xc4) and
	// pathInfo->InitTunnelPath (+0x80) with the two tunnel-interface pointers.
	//
	// selfFacing: the portal's actual facing direction (0=N 1=E 2=S 3=W), used to set
	// sCustomPortalFacingOverride so Hook_InitTunnelPath can substitute the correct axis
	// instead of the coordinate-derived direction that MakeTunnelPaths computes internally.
	void RefreshTunnelPathInfo(
		cIGZUnknown* self,
		cIGZUnknown* otherEnd,
		uint8_t selfLookupPathDirection,
		uint16_t peerPathKeyLowWord)
	{
		Logger& logger = Logger::GetInstance();

		if (!self || !otherEnd)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"TunnelPortalTool: cannot refresh path info, self=%p otherEnd=%p.",
				self,
				otherEnd);
			return;
		}

		void** vtable = *reinterpret_cast<void***>(self);
		GetPathInfoFn getPathInfo = reinterpret_cast<GetPathInfoFn>(vtable[0x31]);
		void* pathInfo = getPathInfo(self);
		if (!pathInfo)
		{
			return;
		}

		void** pathInfoVtable = *reinterpret_cast<void***>(pathInfo);
		InitTunnelPathFn initTunnelPath = reinterpret_cast<InitTunnelPathFn>(pathInfoVtable[0x20]);
		void** otherEndVtable = *reinterpret_cast<void***>(otherEnd);
		GetPathInfoFn getOtherPathInfo = reinterpret_cast<GetPathInfoFn>(otherEndVtable[0x31]);

		sCustomPortalFacingOverride = selfLookupPathDirection;
		sCustomPeerPathLookupKeyLowWord = peerPathKeyLowWord;
		sCustomSelfPathInfo = pathInfo;
		sCustomPeerPathInfo = getOtherPathInfo(otherEnd);
		sPeerPathExactKeyCount = 0;
		sPeerPathRemappedUniqueKeyCount = 0;
		sPeerPathUnresolvedKeyCount = 0;
		sPeerPathUnresolvedOriginalKeys.fill(0);
		initTunnelPath(pathInfo, self, otherEnd);
		if (peerPathKeyLowWord != 0xFFFF)
		{
			logger.WriteLineFormatted(
				LogLevel::Debug,
				"TunnelPortalTool: path refresh selfDirection=%u peerLowWord=0x%04X peer lookups exact=%u remappedUnique=%u unresolved=%u unresolvedKeys=0x%08X/0x%08X/0x%08X/0x%08X.",
				static_cast<uint32_t>(selfLookupPathDirection),
				static_cast<uint32_t>(peerPathKeyLowWord),
				sPeerPathExactKeyCount,
				sPeerPathRemappedUniqueKeyCount,
				sPeerPathUnresolvedKeyCount,
				sPeerPathUnresolvedOriginalKeys[0],
				sPeerPathUnresolvedOriginalKeys[1],
				sPeerPathUnresolvedOriginalKeys[2],
				sPeerPathUnresolvedOriginalKeys[3]);
		}
		sCustomPortalFacingOverride = 0xFF;
		sCustomPeerPathLookupKeyLowWord = 0xFFFF;
		sCustomSelfPathInfo = nullptr;
		sCustomPeerPathInfo = nullptr;
	}

	void NotifyTrafficSimulatorForLinkedTunnels(
		const Endpoint& first,
		const Endpoint& second,
		cIGZUnknown* firstTunnel,
		cIGZUnknown* secondTunnel)
	{
		cISC4TrafficSimulator* const trafficSimulator = GetTrafficSimulator();

		if (!trafficSimulator)
		{
			Logger::GetInstance().WriteLine(LogLevel::Error, "TunnelPortalTool: cannot notify traffic simulator, no traffic simulator is available.");
			return;
		}

		DoTunnelChanged(trafficSimulator, firstTunnel, false);
		DoTunnelChanged(trafficSimulator, secondTunnel, false);
		DoTunnelChanged(trafficSimulator, firstTunnel, true);
		DoTunnelChanged(trafficSimulator, secondTunnel, true);

		TemporaryTunnelConnectionRecordScope temporaryTunnelConnectionRecord(trafficSimulator, first, second);
		DoConnectionsChanged(trafficSimulator, first.x, first.z, first.x, first.z);
		DoConnectionsChanged(trafficSimulator, second.x, second.z, second.x, second.z);
		DoConnectionsChanged(
			trafficSimulator,
			std::min(first.x, second.x),
			std::min(first.z, second.z),
			std::max(first.x, second.x),
			std::max(first.z, second.z));
	}

	bool PlacePortalPairInternal(
		const Endpoint& first,
		const Endpoint& second,
		const TunnelPortalStyles::Style& style)
	{
		Logger& logger = Logger::GetInstance();

		logger.WriteLineFormatted(
			LogLevel::Debug,
			"TunnelPortalTool: attempting to place %s portal pair at (%u,%u) -> (%u,%u), facade style=\"%s\".",
			NetworkTypeNameInternal(first.networkType),
			first.x, first.z,
			second.x, second.z,
			style.name.c_str());

		if (first.networkType != second.networkType)
		{
			logger.WriteLine(LogLevel::Error, "Tunnel portal endpoints must be on the same network type.");
			return false;
		}
		if (style.networkType != first.networkType
			|| style.tileCount != (IsTwoTileNetwork(first.networkType) ? 2 : 1))
		{
			logger.WriteLine(
				LogLevel::Error,
				"TunnelPortalTool: selected facade style is incompatible with the portal network or width.");
			return false;
		}

		cISC4City* city = GetCity();
		cISC4NetworkManager* networkManager = city ? city->GetNetworkManager() : nullptr;
		cSC4NetworkTool* tool = GetNetworkTool(networkManager, first.networkType);

		if (!tool)
		{
			logger.WriteLine(LogLevel::Error, "Could not acquire the network tool for tunnel portal placement.");
			return false;
		}

		cSC4NetworkCellInfo* firstCell = GetEndpointCell(tool, first);
		cSC4NetworkCellInfo* secondCell = GetEndpointCell(tool, second);

		if (!firstCell || !secondCell)
		{
			logger.WriteLine(LogLevel::Error, "Tunnel portal placement requires both endpoints to be existing compatible network tiles.");
			return false;
		}

		const uint8_t firstVectorDirection = InferTunnelPieceDirection(first, second);
		const uint8_t secondVectorDirection = InferTunnelPieceDirection(second, first);
		uint8_t firstDirection = firstVectorDirection;
		uint8_t secondDirection = secondVectorDirection;
		std::array<PortalCell, 2> firstPortalCells{};
		std::array<PortalCell, 2> secondPortalCells{};
		size_t portalCellCount = 1;

		{
			NetworkToolPlacementStateScope placementState(tool);
			TryInferTunnelPieceDirectionFromSurfaceApproach(
				firstCell,
				first.networkType,
				firstDirection);
			TryInferTunnelPieceDirectionFromSurfaceApproach(
				secondCell,
				second.networkType,
				secondDirection);

			const size_t firstPortalCellCount = BuildPortalCellList(
				tool,
				"first",
				first,
				firstCell,
				firstDirection,
				firstPortalCells);
			const size_t secondPortalCellCount = BuildPortalCellList(
				tool,
				"second",
				second,
				secondCell,
				secondDirection,
				secondPortalCells);

			if (firstPortalCellCount == 0 || secondPortalCellCount == 0
				|| firstPortalCellCount != secondPortalCellCount)
			{
				logger.WriteLineFormatted(
					LogLevel::Error,
					"TunnelPortalTool: portal endpoints did not resolve one compatible shape, first tile count=%u second tile count=%u.",
					static_cast<uint32_t>(firstPortalCellCount),
					static_cast<uint32_t>(secondPortalCellCount));
				return false;
			}
			portalCellCount = firstPortalCellCount;

			for (size_t i = 0; i < portalCellCount; ++i)
			{
				PrepareTunnelEndpointCell(firstPortalCells[i].cellInfo);
				firstPortalCells[i].occupant = InsertTunnelPieceWithStyle(
					tool,
					firstDirection,
					firstPortalCells[i].sequenceIndex,
					firstPortalCells[i].cellInfo,
					style);
			}
			for (size_t i = 0; i < portalCellCount; ++i)
			{
				PrepareTunnelEndpointCell(secondPortalCells[i].cellInfo);
				secondPortalCells[i].occupant = InsertTunnelPieceWithStyle(
					tool,
					secondDirection,
					secondPortalCells[i].sequenceIndex,
					secondPortalCells[i].cellInfo,
					style);
			}
		}

		for (size_t i = 0; i < portalCellCount; ++i)
		{
			if (!firstPortalCells[i].occupant || !secondPortalCells[i].occupant)
			{
				logger.WriteLineFormatted(
					LogLevel::Error,
					"Tunnel portal placement did not create two tunnel occupants for lane %u.",
					static_cast<uint32_t>(i));
				return false;
			}
		}

		for (size_t i = 0; i < portalCellCount; ++i)
		{
			if (!QueryTunnelOccupant(firstPortalCells[i].occupant, firstPortalCells[i].tunnel)
				|| !QueryTunnelOccupant(secondPortalCells[i].occupant, secondPortalCells[i].tunnel))
			{
				logger.WriteLineFormatted(
					LogLevel::Error,
					"TunnelPortalTool: created occupants are not tunnel occupants for lane %u, first=%p second=%p firstTunnel=%p secondTunnel=%p.",
					static_cast<uint32_t>(i),
					firstPortalCells[i].occupant,
					secondPortalCells[i].occupant,
					static_cast<cIGZUnknown*>(firstPortalCells[i].tunnel),
					static_cast<cIGZUnknown*>(secondPortalCells[i].tunnel));
				return false;
			}
		}

		bool reverseFirstPortalPairing = true;
		if (portalCellCount == 2)
		{
			const auto distanceSquared = [](const Endpoint& a, const Endpoint& b)
			{
				const int64_t dx = static_cast<int64_t>(b.x) - static_cast<int64_t>(a.x);
				const int64_t dz = static_cast<int64_t>(b.z) - static_cast<int64_t>(a.z);
				return dx * dx + dz * dz;
			};
			const int64_t directPairingDistance =
				distanceSquared(firstPortalCells[0].endpoint, secondPortalCells[0].endpoint)
				+ distanceSquared(firstPortalCells[1].endpoint, secondPortalCells[1].endpoint);
			const int64_t reversePairingDistance =
				distanceSquared(firstPortalCells[1].endpoint, secondPortalCells[0].endpoint)
				+ distanceSquared(firstPortalCells[0].endpoint, secondPortalCells[1].endpoint);

			const std::array<uint32_t, 2> firstFacingExitPathCounts = {
				CountPathsExitingTowardMouth(firstPortalCells[0].tunnel, firstDirection),
				CountPathsExitingTowardMouth(firstPortalCells[1].tunnel, firstDirection),
			};
			const std::array<uint32_t, 2> secondFacingExitPathCounts = {
				CountPathsExitingTowardMouth(secondPortalCells[0].tunnel, secondDirection),
				CountPathsExitingTowardMouth(secondPortalCells[1].tunnel, secondDirection),
			};
			const auto pairingScore = [&] (bool reverse)
			{
				uint32_t score = 0;
				for (size_t i = 0; i < portalCellCount; ++i)
				{
					const size_t firstIndex = reverse
						? portalCellCount - 1 - i
						: i;
					const bool firstHasFacingExitPath =
						firstFacingExitPathCounts[firstIndex] != 0;
					const bool secondHasFacingExitPath =
						secondFacingExitPathCounts[i] != 0;
					if (firstHasFacingExitPath != secondHasFacingExitPath)
					{
						++score;
					}
				}
				return score;
			};
			const uint32_t directPathScore = pairingScore(false);
			const uint32_t reversePathScore = pairingScore(true);
			const bool pathSemanticsDistinguishPairing =
				directPathScore != reversePathScore;

			if (pathSemanticsDistinguishPairing)
			{
				// A one-way Avenue lane must join one path half that exits
				// toward its mouth to one half that enters from its mouth.
				// This distinguishes perpendicular and same-facing mouths,
				// where geometric distance alone can tie or select the wrong
				// carriageway association.
				reverseFirstPortalPairing = reversePathScore > directPathScore;
			}
			else
			{
				// Preserve native LIFO on an otherwise unresolved tie.
				reverseFirstPortalPairing =
					reversePairingDistance <= directPairingDistance;
			}

			logger.WriteLineFormatted(
				LogLevel::Debug,
				"TunnelPortalTool: two-tile pairing selected=%s source=%s pathScores direct=%u reverse=%u facingExitPaths first=%u/%u second=%u/%u distances direct=%lld reverse=%lld.",
				reverseFirstPortalPairing ? "reverse" : "direct",
				pathSemanticsDistinguishPairing ? "paths" : "geometry",
				directPathScore,
				reversePathScore,
				firstFacingExitPathCounts[0],
				firstFacingExitPathCounts[1],
				secondFacingExitPathCounts[0],
				secondFacingExitPathCounts[1],
				directPairingDistance,
				reversePairingDistance);
		}

		for (size_t i = 0; i < portalCellCount; ++i)
		{
			const size_t firstPortalCellIndex = reverseFirstPortalPairing
				? portalCellCount - 1 - i
				: i;
			PortalCell& firstPortalCell = firstPortalCells[firstPortalCellIndex];
			PortalCell& secondPortalCell = secondPortalCells[i];

			if (portalCellCount > 1)
			{
				const uint32_t networkIndex = static_cast<uint32_t>(first.networkType);
				logger.WriteLineFormatted(
					LogLevel::Debug,
					"TunnelPortalTool: lane pair %u mode=%s first=(%u,%u) seq=%u piece=0x%08X rf=0x%02X edges=0x%08X second=(%u,%u) seq=%u piece=0x%08X rf=0x%02X edges=0x%08X portalDirections=%u/%u.",
					static_cast<uint32_t>(i),
					reverseFirstPortalPairing ? "reverse" : "direct",
					firstPortalCell.endpoint.x,
					firstPortalCell.endpoint.z,
					static_cast<uint32_t>(firstPortalCell.sequenceIndex),
					firstPortalCell.occupant->PieceId(),
					static_cast<uint32_t>(firstPortalCell.occupant->GetRotationAndFlip()),
					firstPortalCell.cellInfo->edgesPerNetwork[networkIndex],
					secondPortalCell.endpoint.x,
					secondPortalCell.endpoint.z,
					static_cast<uint32_t>(secondPortalCell.sequenceIndex),
					secondPortalCell.occupant->PieceId(),
					static_cast<uint32_t>(secondPortalCell.occupant->GetRotationAndFlip()),
					secondPortalCell.cellInfo->edgesPerNetwork[networkIndex],
					static_cast<uint32_t>(firstDirection),
					static_cast<uint32_t>(secondDirection));
				TracePathMap("first lane before stitch", firstPortalCell.tunnel);
				TracePathMap("second lane before stitch", secondPortalCell.tunnel);
			}

			SetOtherEndOccupant(firstPortalCell.tunnel, secondPortalCell.occupant);
			SetOtherEndOccupant(secondPortalCell.tunnel, firstPortalCell.occupant);

			const uint8_t pairedFirstVectorDirection =
				InferTunnelPieceDirection(firstPortalCell.endpoint, secondPortalCell.endpoint);
			const uint8_t pairedSecondVectorDirection =
				InferTunnelPieceDirection(secondPortalCell.endpoint, firstPortalCell.endpoint);
			const bool requiresCustomPathStitch =
				firstDirection != pairedFirstVectorDirection
					|| secondDirection != pairedSecondVectorDirection;
			const bool requiresTwoTilePathKeyResolution = portalCellCount > 1;

			if (requiresCustomPathStitch)
			{
				const uint8_t firstPathDirection = TunnelPieceDirectionToPathDirection(firstDirection);
				const uint8_t secondPathDirection = TunnelPieceDirectionToPathDirection(secondDirection);
				RegisterCustomTunnelRouteEdgeFix(
					firstPortalCell.endpoint,
					secondPortalCell.endpoint,
					firstPathDirection,
					secondPathDirection);
			}

			if (requiresCustomPathStitch || requiresTwoTilePathKeyResolution)
			{
				const uint8_t firstPathDirection = TunnelPieceDirectionToPathDirection(firstDirection);
				const uint8_t secondPathDirection = TunnelPieceDirectionToPathDirection(secondDirection);
				const uint16_t firstPeerLookup = requiresTwoTilePathKeyResolution
					? kAutomaticPeerPathLookup
					: TunnelPathKeyLowWord(secondPathDirection);
				const uint16_t secondPeerLookup = requiresTwoTilePathKeyResolution
					? kAutomaticPeerPathLookup
					: TunnelPathKeyLowWord(firstPathDirection);
				RefreshTunnelPathInfo(
					firstPortalCell.tunnel,
					secondPortalCell.tunnel,
					firstPathDirection,
					firstPeerLookup);
				RefreshTunnelPathInfo(
					secondPortalCell.tunnel,
					firstPortalCell.tunnel,
					secondPathDirection,
					secondPeerLookup);
			}
			else
			{
				// A native-compatible single-tile pair can use each full path
				// key unchanged.
				RefreshTunnelPathInfo(firstPortalCell.tunnel, secondPortalCell.tunnel);
				RefreshTunnelPathInfo(secondPortalCell.tunnel, firstPortalCell.tunnel);
			}
			if (portalCellCount > 1)
			{
				TracePathMap("first lane after stitch", firstPortalCell.tunnel);
				TracePathMap("second lane after stitch", secondPortalCell.tunnel);
			}
			MarkNetworkOccupantUsable(firstPortalCell.occupant, "first");
			MarkNetworkOccupantUsable(secondPortalCell.occupant, "second");
			NotifyTrafficSimulatorForLinkedTunnels(
				firstPortalCell.endpoint,
				secondPortalCell.endpoint,
				firstPortalCell.tunnel,
				secondPortalCell.tunnel);
		}

		logger.WriteLineFormatted(
			LogLevel::Info,
			"Placed experimental %s tunnel portal pair at (%u,%u) and (%u,%u).",
			NetworkTypeNameInternal(first.networkType),
			first.x,
			first.z,
			second.x,
			second.z);

		return true;
	}

}

const char* TunnelPortalToolPlacement::NetworkTypeName(cISC4NetworkOccupant::eNetworkType type)
{
	return NetworkTypeNameInternal(type);
}

bool TunnelPortalToolPlacement::TryFindNetworkAtTile(
	uint32_t x,
	uint32_t z,
	cISC4NetworkOccupant::eNetworkType& networkTypeOut)
{
	return TryFindNetworkAtTileInternal(x, z, networkTypeOut);
}

uint8_t TunnelPortalToolPlacement::ExpectedPortalTileCount(
	cISC4NetworkOccupant::eNetworkType networkType)
{
	return IsTwoTileNetwork(networkType) ? 2 : 1;
}

bool TunnelPortalToolPlacement::PlacePortalPair(
	const Endpoint& first,
	const Endpoint& second,
	const TunnelPortalStyles::Style& style)
{
	return PlacePortalPairInternal(first, second, style);
}

void TunnelPortalTool::RefreshCity()
{
	cISC4TrafficSimulator* const trafficSimulator = GetTrafficSimulator();
	ResetCustomTunnelRouteEdgeFixes(trafficSimulator);
	if (trafficSimulator)
	{
		sSavedTunnelRouteEdgeFixesScanned =
			RegisterSavedCustomTunnelRouteEdgeFixes(trafficSimulator, true);
	}
}

void TunnelPortalTool::Install()
{
	Patching::InstallInlineHook(sInitTunnelPathHook);
	sInitTunnelPathTrampolinePtr = sInitTunnelPathHook.trampoline;
	Patching::InstallInlineHook(sPeerPathLookupHook);
	sPeerPathLookupTrampolinePtr = sPeerPathLookupHook.trampoline;

	constexpr uint32_t kFindPathTunnelAddTripNodeCall = 0x006d9aCF;
	constexpr uint32_t kFindPathTunnelAddTripNodeOriginalRel = 0xFFFFF4CC;
	const uint32_t tunnelAddTripNodeHookRel =
		reinterpret_cast<uint32_t>(&Hook_AddTunnelTripNode) - (kFindPathTunnelAddTripNodeCall + 5);
	Patching::PatchImmediate32(
		kFindPathTunnelAddTripNodeCall + 1,
		kFindPathTunnelAddTripNodeOriginalRel,
		tunnelAddTripNodeHookRel);

	// Patch FloodSubnetwork tunnel branch GetNetworkInfo call to our hook.
	constexpr uint32_t kFloodSubnetworkTunnelGetNetworkInfoCall = 0x00718215;
	constexpr uint32_t kFloodSubnetworkTunnelGetNetworkInfoOriginalRel = 0xFFFF7916;
	const uint32_t floodTunnelHookRel =
		reinterpret_cast<uint32_t>(&Hook_FloodSubnetworkTunnelGetNetworkInfo) - (kFloodSubnetworkTunnelGetNetworkInfoCall + 5);
	Patching::PatchImmediate32(
		kFloodSubnetworkTunnelGetNetworkInfoCall + 1,
		kFloodSubnetworkTunnelGetNetworkInfoOriginalRel,
		floodTunnelHookRel);
}
