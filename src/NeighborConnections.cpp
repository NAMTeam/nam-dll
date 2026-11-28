#include "NeighborConnections.h"
#include "NeighborConnectionBanding.h"
#include "Patching.h"
#include "NetworkStubs.h"
#include "Masking.h"
#include "cISC4TrafficSimulator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/*
	Neighbor connection patch
	=========================

	SimCity 4 has special return-tile handling for divided networks such as
	Avenue and Highway. When a path crosses a city edge, the game pairs the
	visible NC connection with a nearby tile carrying traffic in the
	opposite direction:

	    city edge
	    v
	    S S S . . T T T
	    ^ source  ^ target return carriageway

	This patch reuses that divided-network path for more networks:

	- RHW/DirtRoad: adds the DirtRoad neighbor connection bit to the game's
	  divided-network mask.

	- OWR: changes OWR's neighbor connection type from "none" to the same
	  DirtRoad/RHW type. Because of that, enabling OWR necessarily also
	  enables the RHW part of this patch.

	- NWM: adds Road to the divided-network mask, but only uses the custom return
	  search when the Road edge tile carries car traffic in one direction only
	  (a Road-network tile such as one half of an AVE-6, not the OWR network).
	  Normal Road neighbor connections are bidirectional, so they are left alone.

	Return-tile matching
	--------------------

	The hook receives the tile being checked at the city edge. For RHW/OWR it
	first asks the original game function for the adjacent tile direction. For NWM
	it scans along the city edge directly. NWM pieces are Road overrides, so the
	Road neighbor connection alone does not tell us which side of the source tile
	contains the matching carriageway. The search therefore tries both directions
	along the edge and accepts the first valid opposite-side match.

	The scan reads a short strip of edge tiles:

	    -8 ... -2 -1  0  1  2 ... 8
	              [ source ] [ target ]

	Tiles with the same traffic direction are grouped into bands. Empty tiles
	inside a group are allowed up to NeighborConnectionMaxGroupingGap, which
	lets separated carriageways and multi-tile NWM setups stay together.

	The source band and lane are then mirrored onto the target side:

	    source bands              target bands
	    far -> near               near -> far
	    [A][B][C]       maps to   [C][B][A]

	Within the selected band, the same mirrored position is used. This keeps a
	tile in a three-lane outgoing side paired with the corresponding tile in a
	three-lane incoming side, while still handling unequal band widths by
	scaling the index.

	Caching
	-------

	PerformPathSearch runs for every path search that crosses a city edge, so the
	scan above is not run there directly: its result is cached per edge tile and
	search direction. The scan only reads neighbor connection entries and the
	cell path connections, and the game writes those in
	cSC4TrafficSimulator::DoConnectionsChanged (lots activated/deactivated, city
	init, connection-changed message) and AddNeighborConnection, so those call
	sites just bump a generation counter. After a change, each edge tile is
	searched once again on first use.
*/

namespace
{
	using namespace NeighborConnectionBanding;

	// cSC4TrafficSimulator::PerformPathSearch checks this mask before using the
	// adjacent tile as the return carriageway for a divided network connection.
	constexpr uint32_t kForwardPathNetworkMaskTestAddress = 0x0071c8c1;
	constexpr uint32_t kReturnPathNetworkMaskTestAddress = 0x0071c93d;
	constexpr uint32_t kForwardPathFindReturnTileCallAddress = 0x0071c8d6;
	constexpr uint32_t kReturnPathFindReturnTileCallAddress = 0x0071c952;
	constexpr uint32_t kOWRNetworkToNcAddress = 0x00aa84ec;

	// Functions that change the data the return-tile search reads. Every call site
	// is hooked to drop the cached results: DoConnectionsChanged is called by
	// DoLotActivated, DoLotDeactivated, OnPostCityInit and DoMessage;
	// AddNeighborConnection by OnPostCityInit and DoMessage.
	constexpr uint32_t kDoConnectionsChanged = 0x0071a860;
	constexpr uint32_t kAddNeighborConnection = 0x007153c0;
	constexpr std::array<uint32_t, 4> kDoConnectionsChangedCallSites = {0x0071e49d, 0x00720568, 0x00723f0d, 0x00724548};
	constexpr std::array<uint32_t, 2> kAddNeighborConnectionCallSites = {0x0072060d, 0x007242aa};

	// Neighbor connection types, as mapped from network types by the table at 0x00aa84c4.
	enum class NcType : uint32_t
	{
		None = 0,
		Road = 2,
		Highway = 3,
		Avenue = 8,
		DirtRoad = 10,
		GroundHighway = 12,
	};

	constexpr uint16_t kVanillaDividedNetworkNcMask = 0x1108;
	static_assert(kVanillaDividedNetworkNcMask == asMask(NcType::Highway, NcType::Avenue, NcType::GroundHighway));
	constexpr uint16_t kRoadNcMask = asMask(NcType::Road);
	constexpr uint16_t kDirtRoadNcMask = asMask(NcType::DirtRoad);
	constexpr uint16_t kOWRMetadataNcMask = 0x2000;

	constexpr uint32_t kMinSearchDistance = 1;
	constexpr uint32_t kMaxGroupingGap = 16;

	SearchLimits gLimits;
	bool gEnableNWM = false;

	uint16_t GetNeighborConnectionMask(cSC4TrafficSimulator* const pTrafficSim, const int32_t x, const int32_t z)
	{
		const void* const pConnection = cSC4TrafficSimulator::GetNeighborConnectionEntry(pTrafficSim, x, z);
		return pConnection ? *static_cast<const uint16_t*>(pConnection) : 0;
	}

	// Converts the outside neighbor coordinate to the city-edge cell and edge index.
	bool TryGetBoundaryCellAndEdge(cSC4TrafficSimulator* const pTrafficSim, const int32_t x, const int32_t z, int32_t& cellX, int32_t& cellZ, uint32_t& edge)
	{
		const int32_t maxX = pTrafficSim->maxCellX;
		const int32_t maxZ = pTrafficSim->maxCellZ;
		const bool xOutside = x < 0 || x > maxX;
		const bool zOutside = z < 0 || z > maxZ;
		if (xOutside == zOutside)
		{
			return false;
		}

		cellX = x;
		cellZ = z;
		if (x < 0)
		{
			cellX = 0;
			edge = 0;
		}
		else if (x > maxX)
		{
			cellX = maxX;
			edge = 2;
		}
		else if (z < 0)
		{
			cellZ = 0;
			edge = 1;
		}
		else
		{
			cellZ = maxZ;
			edge = 3;
		}
		return true;
	}

	PathDir GetPathDirection(cSC4TrafficSimulator* const pTrafficSim, const int32_t x, const int32_t z, uint16_t& pathMatrix)
	{
		int32_t cellX = 0;
		int32_t cellZ = 0;
		uint32_t boundaryEdge = 0;
		if (!TryGetBoundaryCellAndEdge(pTrafficSim, x, z, cellX, cellZ, boundaryEdge))
		{
			return PathDir::None;
		}

		const TrafficSimCellConnections* const cells = pTrafficSim->cellConnections;
		if (!cells)
		{
			return PathDir::None;
		}

		const uint32_t cellIndex = (static_cast<uint32_t>(cellX) << 8) | static_cast<uint32_t>(cellZ);
		pathMatrix = cells[cellIndex].entryExitConnectionsPerTravelType[static_cast<uint32_t>(cISC4TrafficSimulator::TravelType::Car)];

		bool inbound = false;
		bool outbound = false;
		for (uint32_t otherEdge = 0; otherEdge < 4; ++otherEdge)
		{
			// DoConnectionsChanged stores bit (exitEdge + entryEdge * 4).
			outbound |= (pathMatrix & (1u << (boundaryEdge + otherEdge * 4))) != 0;
			inbound |= (pathMatrix & (1u << (otherEdge + boundaryEdge * 4))) != 0;
		}

		if (inbound && outbound)
		{
			return PathDir::Bidi;
		}
		if (inbound)
		{
			return PathDir::Inbound;
		}
		if (outbound)
		{
			return PathDir::Outbound;
		}
		return PathDir::None;
	}

	EdgeTile ReadEdgeTile(cSC4TrafficSimulator* const pTrafficSim, const int32_t originX, const int32_t originZ,
	                      const int32_t stepX, const int32_t stepZ, const uint16_t eligibleNcMask, const int32_t offset)
	{
		EdgeTile tile;
		tile.x = originX + stepX * offset;
		tile.z = originZ + stepZ * offset;
		tile.offset = offset;

		int32_t cellX = 0;
		int32_t cellZ = 0;
		uint32_t edge = 0;
		tile.valid = TryGetBoundaryCellAndEdge(pTrafficSim, tile.x, tile.z, cellX, cellZ, edge);
		if (!tile.valid)
		{
			return tile;
		}

		tile.ncMask = GetNeighborConnectionMask(pTrafficSim, tile.x, tile.z);
		tile.isEligibleNc = (tile.ncMask & eligibleNcMask) != 0;
		tile.direction = GetPathDirection(pTrafficSim, tile.x, tile.z, tile.pathMatrix);
		return tile;
	}

	// Returns the one-tile step along the current city edge.
	bool TryGetEdgeScanStep(cSC4TrafficSimulator* const pTrafficSim, const int32_t x, const int32_t z, int32_t& stepX, int32_t& stepZ)
	{
		int32_t cellX = 0;
		int32_t cellZ = 0;
		uint32_t edge = 0;
		if (!TryGetBoundaryCellAndEdge(pTrafficSim, x, z, cellX, cellZ, edge))
		{
			return false;
		}

		stepX = edge == 1 || edge == 3 ? 1 : 0;
		stepZ = edge == 0 || edge == 2 ? 1 : 0;
		return true;
	}

	bool TryFindPairedReturnTile(cSC4TrafficSimulator* const pTrafficSim, const int32_t originX, const int32_t originZ,
		const int32_t stepX, const int32_t stepZ, const uint16_t eligibleNcMask,
		const bool allowBidiSource, const bool allowBidiTarget,
		int32_t& resultX, int32_t& resultZ)
	{
		if ((stepX == 0 && stepZ == 0) || (stepX != 0 && stepZ != 0))
		{
			return false;
		}

		EdgeScan scannedTiles{};
		for (int32_t offset = -static_cast<int32_t>(gLimits.maxSearchDistance);
			offset <= static_cast<int32_t>(gLimits.maxSearchDistance);
			++offset)
		{
			scannedTiles[static_cast<size_t>(offset + static_cast<int32_t>(kMaxSearchDistance))] =
				ReadEdgeTile(pTrafficSim, originX, originZ, stepX, stepZ, eligibleNcMask, offset);
		}

		int32_t resultOffset = 0;
		if (!TryFindPairedReturnOffset(scannedTiles, gLimits, allowBidiSource, allowBidiTarget, resultOffset))
		{
			return false;
		}

		const EdgeTile& result = GetScannedTile(scannedTiles, resultOffset);
		resultX = result.x;
		resultZ = result.z;
		return true;
	}

	// The Road NC type is shared by every Road piece, so only Road edge tiles that
	// carry car traffic in one direction (NWM pieces such as half an AVE-6) may
	// enter the NWM search. This never covers OWR, which is a DirtRoad NC.
	bool IsOneWayRoadConnection(cSC4TrafficSimulator* const pTrafficSim, const int32_t originX, const int32_t originZ, const uint16_t originMask)
	{
		if (!gEnableNWM ||
			(originMask & kRoadNcMask) == 0 ||
			(originMask & kVanillaDividedNetworkNcMask) != 0 ||
			(originMask & kDirtRoadNcMask) != 0)
		{
			return false;
		}

		uint16_t pathMatrix = 0;
		const auto pathDirection = GetPathDirection(pTrafficSim, originX, originZ, pathMatrix);
		return IsOneWayDirection(pathDirection);
	}

	uint16_t BuildPatchedNeighborConnectionMask(const bool enableRHW, const bool enableNWM)
	{
		uint16_t mask = kVanillaDividedNetworkNcMask;
		if (enableRHW)
		{
			mask |= kDirtRoadNcMask;
		}
		if (enableNWM)
		{
			mask |= kRoadNcMask;
		}
		return mask;
	}

	// Cached result of the return-tile search for one edge tile and search direction.
	struct ReturnTileEntry
	{
		int16_t x = 0;
		int16_t z = 0;  // edge tile coordinates, always well inside int16
		uint16_t kind = 0;  // 0 = never computed
		uint32_t generation = 0;  // 0 = never computed
	};

	enum ReturnTileKind : uint16_t
	{
		kVanillaStep = 1,  // hand the tile to the original FindHighwayReturnTile
		kUseCachedTile = 2,  // use x/z as computed by this patch
	};

	constexpr size_t kEdgeCount = 4;
	constexpr size_t kSearchDirectionCount = 2;

	std::vector<ReturnTileEntry> gReturnTileCache;
	cSC4TrafficSimulator* gCacheOwner = nullptr;
	size_t gCacheCells = 0;
	uint32_t gCacheGeneration = 0;

	// One slot per (city edge, boundary cell, search direction).
	ReturnTileEntry* GetReturnTileSlot(cSC4TrafficSimulator* const pTrafficSim, const int32_t x, const int32_t z, const bool reverseSearch)
	{
		int32_t cellX = 0;
		int32_t cellZ = 0;
		uint32_t edge = 0;
		if (!TryGetBoundaryCellAndEdge(pTrafficSim, x, z, cellX, cellZ, edge))
		{
			return nullptr;
		}

		const size_t cellsX = static_cast<size_t>(pTrafficSim->maxCellX) + 1;
		const size_t cellsZ = static_cast<size_t>(pTrafficSim->maxCellZ) + 1;
		const size_t cells = cellsX * cellsZ;
		if (pTrafficSim != gCacheOwner || cells != gCacheCells)
		{
			gCacheOwner = pTrafficSim;
			gCacheCells = cells;
			gCacheGeneration = 1;  // entries are stamped 0, so a fresh table is empty
			gReturnTileCache.assign(cells * kEdgeCount * kSearchDirectionCount, ReturnTileEntry{});
		}

		const size_t index =
			(static_cast<size_t>(edge) * kSearchDirectionCount + (reverseSearch ? 1 : 0)) * gCacheCells +
			static_cast<size_t>(cellZ) * cellsX + static_cast<size_t>(cellX);
		return &gReturnTileCache[index];
	}

	struct ReturnTileDecision
	{
		uint16_t kind;
		int32_t x;
		int32_t z;
	};

	ReturnTileDecision ResolveReturnTile(cSC4TrafficSimulator* const pTrafficSim, const int32_t originX, const int32_t originZ, const bool reverseSearch)
	{
		const uint16_t originMask = GetNeighborConnectionMask(pTrafficSim, originX, originZ);
		const bool useRHWPairing = (originMask & kDirtRoadNcMask) != 0;
		const bool useNWMPairing = !useRHWPairing && IsOneWayRoadConnection(pTrafficSim, originX, originZ, originMask);

		if (!useRHWPairing && !useNWMPairing)
		{
			// Vanilla divided networks keep the vanilla step. Anything else here is an ordinary
			// Road NC that only passed the mask test because NWM added Road, so it stays unchanged.
			if ((originMask & kVanillaDividedNetworkNcMask) != 0)
			{
				return {kVanillaStep, originX, originZ};
			}
			return {kUseCachedTile, originX, originZ};
		}

		int32_t stepX = 0;
		int32_t stepZ = 0;
		if (useRHWPairing)
		{
			int32_t nextX = originX;
			int32_t nextZ = originZ;
			cSC4TrafficSimulator::FindHighwayReturnTile(pTrafficSim, &nextX, &nextZ, reverseSearch);
			stepX = nextX - originX;
			stepZ = nextZ - originZ;
		}
		else if (!TryGetEdgeScanStep(pTrafficSim, originX, originZ, stepX, stepZ))
		{
			return {kUseCachedTile, originX, originZ};
		}

		const uint16_t eligibleNcMask = useRHWPairing ? kDirtRoadNcMask : kRoadNcMask;
		const bool useOWRPairing = useRHWPairing && (originMask & kOWRMetadataNcMask) != 0;
		const bool allowBidiSource = useOWRPairing;
		const bool allowBidiTarget = useOWRPairing || useNWMPairing;

		int32_t resultX = 0;
		int32_t resultZ = 0;
		bool paired = TryFindPairedReturnTile(
			pTrafficSim, originX, originZ, stepX, stepZ, eligibleNcMask, allowBidiSource, allowBidiTarget, resultX, resultZ);

		if (!paired && useNWMPairing)
		{
			paired = TryFindPairedReturnTile(
				pTrafficSim, originX, originZ, -stepX, -stepZ, eligibleNcMask, allowBidiSource, allowBidiTarget, resultX, resultZ);
		}

		if (!paired)
		{
			return {kUseCachedTile, originX, originZ};
		}
		return {kUseCachedTile, resultX, resultZ};
	}

	void __fastcall FindReturnTile(cSC4TrafficSimulator* const pTrafficSim, void*, int32_t* const pX, int32_t* const pZ, const bool reverseSearch)
	{
		const int32_t originX = *pX;
		const int32_t originZ = *pZ;

		ReturnTileEntry* const slot = GetReturnTileSlot(pTrafficSim, originX, originZ, reverseSearch);
		if (slot != nullptr && slot->generation == gCacheGeneration)
		{
			if (slot->kind == kVanillaStep)
			{
				cSC4TrafficSimulator::FindHighwayReturnTile(pTrafficSim, pX, pZ, reverseSearch);
			}
			else
			{
				*pX = slot->x;
				*pZ = slot->z;
			}
			return;
		}

		const ReturnTileDecision decision = ResolveReturnTile(pTrafficSim, originX, originZ, reverseSearch);
		if (slot != nullptr)
		{
			slot->x = static_cast<int16_t>(decision.x);
			slot->z = static_cast<int16_t>(decision.z);
			slot->kind = decision.kind;
			slot->generation = gCacheGeneration;
		}

		if (decision.kind == kVanillaStep)
		{
			cSC4TrafficSimulator::FindHighwayReturnTile(pTrafficSim, pX, pZ, reverseSearch);
		}
		else
		{
			*pX = decision.x;
			*pZ = decision.z;
		}
	}

	void InvalidateReturnTileCache()
	{
		++gCacheGeneration;
	}

	// Trampolines installed over the calls to the two game functions above: drop the
	// cached tiles, then run the original function with the original arguments.
	void NAKED_FUN ConnectionsChangedHook(void)
	{
		__asm {
			pushad
			call InvalidateReturnTileCache;
			popad;
			push kDoConnectionsChanged;
			ret;
		}
	}

	void NAKED_FUN NeighborConnectionHook(void)
	{
		__asm {
			pushad
			call InvalidateReturnTileCache;
			popad;
			push kAddNeighborConnection;
			ret;
		}
	}
}

void NeighborConnections::Install(const Options& options)
{
	const bool enableRHW = options.enableRHW || options.enableOWR; // For OWR to be enabled, RHW needs to be enabled
	gEnableNWM = options.enableNWM;
	gLimits.maxSearchDistance = std::clamp(options.maxSearchDistance, kMinSearchDistance, kMaxSearchDistance);
	gLimits.maxGroupingGap = std::min(options.maxGroupingGap, kMaxGroupingGap);

	// Keep the cached return tiles from going stale. These hooks only bump a
	// counter, so they go in before anything that changes game behaviour.
	for (const uint32_t callSite : kDoConnectionsChangedCallSites)
	{
		Patching::InstallCallSiteHook(callSite, kDoConnectionsChanged, reinterpret_cast<void (*)()>(ConnectionsChangedHook));
	}
	for (const uint32_t callSite : kAddNeighborConnectionCallSites)
	{
		Patching::InstallCallSiteHook(callSite, kAddNeighborConnection, reinterpret_cast<void (*)()>(NeighborConnectionHook));
	}

	Patching::InstallCallHook(kForwardPathFindReturnTileCallAddress, reinterpret_cast<void (*)()>(FindReturnTile));
	Patching::InstallCallHook(kReturnPathFindReturnTileCallAddress, reinterpret_cast<void (*)()>(FindReturnTile));

	// Add the RHW/DirtRoad connection type to the special search algorithm that exists for Avenue/Highway in vanilla
	const uint16_t patchedMask = BuildPatchedNeighborConnectionMask(enableRHW, options.enableNWM);
	Patching::PatchTestWordPtrEaxImmediate16(kForwardPathNetworkMaskTestAddress, kVanillaDividedNetworkNcMask, patchedMask);
	Patching::PatchTestWordPtrEaxImmediate16(kReturnPathNetworkMaskTestAddress, kVanillaDividedNetworkNcMask, patchedMask);

	if (options.enableOWR)
	{
		// Set the OWR neighbor connection type to the RHW/DirtRoad connection type
		Patching::PatchImmediate32(
			kOWRNetworkToNcAddress,
			static_cast<uint32_t>(NcType::None),
			static_cast<uint32_t>(NcType::DirtRoad));
	}
}
