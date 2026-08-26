#pragma once

class cSC4NetworkTool;

// Keeps committed tunnel portals intact when an unrelated network drag runs
// next to them. Two distinct game behaviours have to be dealt with.
//
// 1. The portal is destroyed by a nearby drag.
//
//    cSC4NetworkTool::PlaceNetwork ends by calling
//    MarkTerrainChangedExistingOccupantsAsNewlySolved with the vertices the
//    drag moved. Every existing occupant touching one of those vertices is
//    pushed into the solved-cell set and rebuilt - and a rebuilt tunnel cell
//    comes back as a plain network piece, because only InsertTunnelPieces
//    creates tunnel occupants and it walks the drag's own tTunnel vector.
//
//    The cell is spared when cSC4NetworkCellInfo::isImmovable (+0x53) is set.
//    That byte is not durable state: ComputeAndStoreCellInfo recomputes it from
//    cISC4NetworkOccupant::IsImmovable() every time cSC4NetworkWorldCache
//    rebuilds the entry, and IsImmovable is just HasNetworkFlag(0x400000).
//    InsertTunnelPiece never sets that flag, so we set it on the occupant
//    ourselves and the pin then survives every cache rebuild.
//
// 2. The drag itself is refused with "Failed Smoothing Terrain".
//
//    That is failure code 0xF0000008, set when CanTilesBeSupportedOnTerrain
//    returns false *after* SmoothenNetwork has already succeeded. For each
//    vertex of each cell being smoothed it requires
//
//        networkHeight >= terrainHeight - MaxTerrainHtDecrease
//        networkHeight <= terrainHeight + MaxTerrainHtIncrease
//
//    A portal raises the terrain around its mouth by TunnelModelHeights, so a
//    road being dragged alongside sits far below that raised terrain and blows
//    the MaxTerrainHtDecrease budget. The cell that fails is the road, not the
//    portal, so marking the portal immovable does not help; the limits
//    themselves have to give. We widen them for the duration of the check, and
//    only for drags that actually run near a portal.
namespace TunnelPortal::TerrainPinning
{
	// Patches the CanTilesBeSupportedOnTerrain call inside PlaceNetwork.
	void InstallHooks();

	// Sets the immovable network flag on every portal in the traffic
	// simulator's tunnel registry. Safe to call repeatedly; call after placing a
	// portal and once per city load so portals saved before this existed get
	// upgraded too.
	void MarkCommittedPortalsImmovable();

	// Toggles both behaviours without uninstalling the hook, for A/B testing
	// against stock. Enabled by default.
	void SetEnabled(bool enabled);
	bool IsEnabled();
}
