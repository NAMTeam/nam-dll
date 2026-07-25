#pragma once

class cISC4TrafficSimulator;

// Recovers custom portal pairs that were saved with a city. On load the game
// rebuilds native tunnel state but knows nothing about our facing/path fixes,
// so we walk the tunnel registry and re-apply them.
namespace TunnelPortal::SavedTunnelScanner
{
	// Scans the simulator's tunnel registry and re-registers route-edge fixes for
	// each custom portal pair found. When refreshPathInfo is true it also rebuilds
	// the path maps (only safe outside the pathfinder hooks). Returns false when
	// the registry can't be read.
	bool Scan(cISC4TrafficSimulator* trafficSimulator, bool refreshPathInfo);

	// Registers the lookup-only Scan with RouteEdgeFixes so its hooks can rebuild
	// the fix table lazily. Call once at install time.
	void InstallRescanCallback();
}
