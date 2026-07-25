#pragma once

#include "PortalGeometry.h"
#include "Style.h"
#include "cRZAutoRefCount.h"

#include <cstdint>

class cIGZUnknown;
class cISC4NetworkOccupant;
class cSC4NetworkTool;
class cSC4NetworkCellInfo;

// Placing a single tunnel portal piece: cell lookup, the native insert (with an
// optional custom facade), and the fix-ups the game normally does around it.
namespace TunnelPortal::PieceInserter
{
	// Puts the network tool into single-piece placing mode and clears its
	// failure code for the lifetime of the scope, restoring both afterward.
	// InsertTunnelPiece checks these; a live drag would otherwise fight us.
	class PlacementStateScope
	{
	public:
		explicit PlacementStateScope(cSC4NetworkTool* tool);
		~PlacementStateScope();

		PlacementStateScope(const PlacementStateScope&) = delete;
		PlacementStateScope& operator=(const PlacementStateScope&) = delete;

	private:
		cSC4NetworkTool* tool;
		uint8_t oldPlacingMode;
		uint32_t oldFailureCode;
	};

	// The existing cell at the endpoint, or null when there is none or it does
	// not carry the endpoint's network. Computed on demand, no drag scan needed.
	cSC4NetworkCellInfo* GetEndpointCell(cSC4NetworkTool* tool, const Endpoint& endpoint);

	// Marks a cell as a tunnel endpoint the way native InsertTunnelPieces does,
	// but keeps the cell's own edge topology rather than the drag-solved one.
	void PrepareEndpointCell(cSC4NetworkCellInfo* cellInfo);

	// Inserts one tunnel piece. For a native style this is the plain insert; for
	// a custom style it swaps the exemplar ID in the tool's vector for the
	// duration of the native call so all other native behaviour is preserved.
	// Returns the new occupant, or null on failure.
	cISC4NetworkOccupant* InsertWithStyle(
		cSC4NetworkTool* tool,
		uint8_t direction,
		uint8_t sequenceIndex,
		cSC4NetworkCellInfo* cellInfo,
		const Styles::Style& style);

	// QueryInterfaces an occupant to the tunnel-occupant interface.
	bool QueryTunnelOccupant(
		cISC4NetworkOccupant* occupant,
		cRZAutoRefCount<cIGZUnknown>& tunnelOccupant);

	// Commits an occupant the way native PlaceNetwork's MarkOccupantsUsable pass
	// does; direct insertion skips that pass. `label` is for log messages only.
	void MarkOccupantUsable(cISC4NetworkOccupant* occupant, const char* label);
}
