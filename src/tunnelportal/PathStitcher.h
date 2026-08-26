#pragma once

#include <cstdint>

class cIGZUnknown;

// Rebuilds a tunnel occupant's path map so the two portals stitch together,
// even when their facing directions don't match the coordinate-derived
// direction the game would compute. Works by driving the native
// MakeTunnelPaths through two inline hooks that read the overrides set here.
namespace TunnelPortal::PathStitcher
{
	// Passed as peerPathKeyLowWord to ask the peer-key hook to resolve the
	// peer's uniqueness byte itself instead of using a fixed low word. Needed
	// for two-tile portals whose halves number paths independently.
	constexpr uint16_t kAutomaticPeerPathLookup = 0xFFFE;

	// Reinitialises `self`'s tunnel path map against `otherEnd`.
	//
	// selfLookupPathDirection (path-direction numbering, 0xFF = none) overrides
	// the direction MakeTunnelPaths derives from cell coordinates.
	// peerPathKeyLowWord (0xFFFF = none, or kAutomaticPeerPathLookup) rewrites
	// the key looked up on the peer's path map.
	// peerMouthPathDirection (0xFF = unknown) is the peer portal's facing in
	// path-direction numbering. Automatic lookup prefers peer paths that enter
	// from that side, so the stitch lands at the peer's tunnel mouth rather
	// than at its surface edge.
	//
	// Returns false when the refresh could not run (null occupant or missing
	// path info), meaning `self` has no usable tunnel path map. Callers must
	// treat a false return as an incompletely stitched portal.
	bool RefreshTunnelPathInfo(
		cIGZUnknown* self,
		cIGZUnknown* otherEnd,
		uint8_t selfLookupPathDirection = 0xFF,
		uint16_t peerPathKeyLowWord = 0xFFFF,
		uint8_t peerMouthPathDirection = 0xFF);

	// Installs the two MakeTunnelPaths inline hooks.
	void InstallHooks();
}
