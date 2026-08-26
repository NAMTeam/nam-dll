#pragma once

#include "cISC4NetworkOccupant.h"

#include <cstdint>

class cSC4NetworkCellInfo;

namespace TunnelPortal
{
	// A network tile a tunnel portal attaches to.
	struct Endpoint
	{
		uint32_t x = 0;
		uint32_t z = 0;
		cISC4NetworkOccupant::eNetworkType networkType = cISC4NetworkOccupant::Road;
	};
}

// Direction math and network-type traits for tunnel portals. No game state,
// no addresses; pure functions on coordinates and direction codes.
//
// Two 2-bit direction encodings appear in the game and never line up:
//   piece rotation : 0=N 1=E 2=S 3=W   (InsertTunnelPiece, exemplar arrays)
//   path direction : 0=W 1=N 2=E 3=S   (kNextX/kNextZ order, all path keys)
// Functions here take and return piece rotation unless the name says "path".
namespace TunnelPortal::Geometry
{
	// Bit for a network type in a cell's networkTypeFlags or a GetNetworkInfo mask.
	constexpr uint32_t NetworkMask(cISC4NetworkOccupant::eNetworkType type)
	{
		return 1u << static_cast<uint32_t>(type);
	}

	// Display name for logging. Never null.
	const char* NetworkTypeName(cISC4NetworkOccupant::eNetworkType type);

	// True for networks whose portals span two tiles: Avenue, Highway,
	// Ground Highway.
	bool IsTwoTileNetwork(cISC4NetworkOccupant::eNetworkType networkType);

	// Piece rotation of the portal at `from` pointing at `to`, taken from the
	// drag vector between the two cells.
	uint8_t InferTunnelPieceDirection(const Endpoint& from, const Endpoint& to);

	// Piece rotation to path direction.
	uint8_t TunnelPieceDirectionToPathDirection(uint8_t tunnelPieceDirection);

	// Low word of the tunnel path key that *leaves* a portal whose mouth points
	// this way: entry byte is the mouth side, exit byte the surface side.
	//
	// This is the key a peer portal must be asked for. MakeTunnelPaths appends
	// the peer path's *first* point to the local path, and for this key that
	// point sits at the peer's tunnel mouth - the deepest point inside the
	// portal tile. Asking for the peer's entering path instead would append its
	// surface-side edge and skip the peer portal tile entirely.
	uint16_t PortalExitPathKeyLowWord(uint8_t pathDirection);

	// Edge-flag bit a surface tile must expose to feed a portal with this
	// piece rotation (i.e. the edge on the side the portal mouth opens toward).
	uint32_t SurfaceApproachEdge(uint8_t tunnelPieceDirection);

	// Coordinate perpendicular to the portal's facing: z for N/S portals, x for
	// E/W. Used to order the two halves of a two-tile portal.
	int32_t CrossAxisCoordinate(const Endpoint& endpoint, uint8_t tunnelPieceDirection);

	// Two-tile sequence order. True when sequence 0 sits at the lower cross-axis
	// coordinate (east/south-facing portals); west/north-facing invert it. The
	// sequence numbers index native exemplar/rotation/path arrays, so this is
	// not a rotation-independent left/right choice.
	bool UseAscendingTwoTileSequence(uint8_t tunnelPieceDirection);

	// Recovers a portal's piece rotation from the one surface-approach edge left
	// on an existing cell. Writes directionOut and returns true only when
	// exactly one edge matches; leaves it untouched otherwise.
	bool TryInferTunnelPieceDirectionFromSurfaceApproach(
		const cSC4NetworkCellInfo* cellInfo,
		cISC4NetworkOccupant::eNetworkType networkType,
		uint8_t& directionOut);
}
