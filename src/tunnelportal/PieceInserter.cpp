#include "PieceInserter.h"

#include "GameAddresses.h"
#include "Logger.h"
#include "NetworkStubs.h"
#include "GZCLSIDDefs.h"
#include "cISC4NetworkOccupant.h"
#include "cISC4Occupant.h"
#include "cIGZUnknown.h"

namespace TunnelPortal::PieceInserter
{
	namespace
	{
		namespace Game = TunnelPortal::Game;
		namespace Field = TunnelPortal::Game::Field;

		constexpr uint32_t kNetworkTunnelOccupantID = GZCLSID::kcSC4NetworkTunnelOccupant;

		// networkTypeFlags bit InsertTunnelPieces sets on a tunnel endpoint cell.
		constexpr uint32_t kTunnelCellNetworkFlag = 0x00080000;
	}

	PlacementStateScope::PlacementStateScope(cSC4NetworkTool* tool)
		: tool(tool),
		  oldPlacingMode(tool ? Game::FieldAt<uint8_t>(tool, Field::kToolPlacingMode) : 0),
		  oldFailureCode(tool ? Game::FieldAt<uint32_t>(tool, Field::kToolFailureCode) : 0)
	{
		if (tool)
		{
			Game::FieldAt<uint8_t>(tool, Field::kToolPlacingMode) = 1;
			Game::FieldAt<uint32_t>(tool, Field::kToolFailureCode) = 0;
		}
	}

	PlacementStateScope::~PlacementStateScope()
	{
		if (tool)
		{
			Game::FieldAt<uint8_t>(tool, Field::kToolPlacingMode) = oldPlacingMode;
			Game::FieldAt<uint32_t>(tool, Field::kToolFailureCode) = oldFailureCode;
		}
	}

	cSC4NetworkCellInfo* GetEndpointCell(cSC4NetworkTool* tool, const Endpoint& endpoint)
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

		if ((cellInfo->networkTypeFlags & Geometry::NetworkMask(endpoint.networkType)) == 0)
		{
			return nullptr;
		}

		return cellInfo;
	}

	void PrepareEndpointCell(cSC4NetworkCellInfo* cellInfo)
	{
		if (!cellInfo)
		{
			return;
		}

		// Native InsertTunnelPieces only marks these bytes before inserting; its
		// cell edge topology comes from the solved drag. Our source cells are
		// existing network tiles, so retain their network-specific topology
		// instead of substituting Avenue-derived masks for Highway, Ground
		// Highway, or perpendicular portal orientations.
		cellInfo->networkTypeFlags |= kTunnelCellNetworkFlag;
		Game::FieldAt<uint8_t>(cellInfo, Field::kCellTunnelMarker) = 1;
		Game::FieldAt<uint8_t>(cellInfo, Field::kCellImmovable) = 1;
	}

	cISC4NetworkOccupant* InsertWithStyle(
		cSC4NetworkTool* tool,
		uint8_t direction,
		uint8_t sequenceIndex,
		cSC4NetworkCellInfo* cellInfo,
		const Styles::Style& style)
	{
		if (style.useNativeExemplars)
		{
			return Game::InsertTunnelPiece(tool, direction, sequenceIndex, cellInfo);
		}
		if (sequenceIndex >= style.tileCount)
		{
			return nullptr;
		}

		// The paired facade models use a fixed north/south half order, while the
		// native sequence slots also select direction-dependent path, rotation,
		// and height arrays. A quarter-turn onto the east/west axis reverses only
		// the facade model halves; keep sequenceIndex for every native array.
		const uint8_t styleExemplarIndex =
			style.tileCount == 2 && (direction & 1) != 0
				? static_cast<uint8_t>(sequenceIndex ^ 1)
				: sequenceIndex;
		if (style.portalExemplarIds[styleExemplarIndex] == 0)
		{
			return nullptr;
		}

		// InsertTunnelPiece takes the exemplar ID from a vector on the tool.
		// Substituting one element around the native call preserves all native
		// rotation, height, occupant, and path initialization behavior.
		uint32_t* const exemplarIds =
			Game::FieldAt<uint32_t*>(tool, Field::kToolTunnelExemplarIds);
		if (!exemplarIds)
		{
			return nullptr;
		}

		const uint32_t nativeExemplarId = exemplarIds[sequenceIndex];
		exemplarIds[sequenceIndex] = style.portalExemplarIds[styleExemplarIndex];
		cISC4NetworkOccupant* const occupant =
			Game::InsertTunnelPiece(tool, direction, sequenceIndex, cellInfo);
		exemplarIds[sequenceIndex] = nativeExemplarId;
		return occupant;
	}

	bool QueryTunnelOccupant(
		cISC4NetworkOccupant* occupant,
		cRZAutoRefCount<cIGZUnknown>& tunnelOccupant)
	{
		if (!occupant)
		{
			return false;
		}

		return occupant->QueryInterface(kNetworkTunnelOccupantID, tunnelOccupant.AsPPVoid()) && tunnelOccupant;
	}

	void MarkOccupantUsable(cISC4NetworkOccupant* occupant, const char* label)
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

		// Mirrors cSC4NetworkConstructionCrew::MarkOccupantUsable. The immovable
		// flag committed portals also need is applied separately, once the pair
		// is registered with the traffic simulator; see
		// TerrainPinning::MarkCommittedPortalsImmovable.
		occupant->ClearNetworkFlag(0x4000);
		baseOccupant->SetVisibility(true, true);
		occupant->SetNetworkFlag(0x10000000);
	}
}
