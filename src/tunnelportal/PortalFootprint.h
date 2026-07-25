#pragma once

#include "PortalGeometry.h"
#include "cRZAutoRefCount.h"

#include <array>
#include <cstddef>
#include <cstdint>

class cIGZUnknown;
class cISC4NetworkOccupant;
class cSC4NetworkTool;
class cSC4NetworkCellInfo;

// One tile of a placed portal. Footprint fills endpoint/cellInfo/sequenceIndex;
// the placement code fills occupant and tunnel once the piece exists.
namespace TunnelPortal
{
	struct PortalCell
	{
		Endpoint endpoint;
		cSC4NetworkCellInfo* cellInfo = nullptr;
		uint8_t sequenceIndex = 0;
		cISC4NetworkOccupant* occupant = nullptr;
		cRZAutoRefCount<cIGZUnknown> tunnel;
	};
}

// Resolves how many tiles a portal occupies and in what order.
namespace TunnelPortal::Footprint
{
	// Fills `cells` for a portal rooted at `endpoint`. One-tile networks yield a
	// single cell; two-tile networks locate the one compatible companion tile
	// and order the pair by native sequence index. `label` is for log messages.
	//
	// Returns the tile count (1 or 2), or 0 when a two-tile portal does not
	// resolve exactly one companion.
	size_t BuildCellList(
		cSC4NetworkTool* tool,
		const char* label,
		const Endpoint& endpoint,
		cSC4NetworkCellInfo* primaryCell,
		uint8_t tunnelPieceDirection,
		std::array<PortalCell, 2>& cells);
}
