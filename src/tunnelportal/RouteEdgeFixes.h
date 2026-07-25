#pragma once

#include "PortalGeometry.h"

#include <cstdint>

class cISC4TrafficSimulator;

// Custom portal pairs can have their two ends face directions the pathfinder
// wouldn't derive from cell coordinates. This table records the arrival edge
// each end should present; two pathfinder hooks consult it to correct routes
// and subnet flooding across such tunnels.
namespace TunnelPortal::RouteEdgeFixes
{
	// Rebuilds the table for lookup only (no path mutation) by rescanning the
	// city's saved tunnels. The saved-tunnel scanner supplies this; the hooks
	// call it lazily the first time they run against an unpopulated table.
	using RescanForLookupFn = bool (*)(cISC4TrafficSimulator* trafficSimulator);
	void SetRescanForLookup(RescanForLookupFn fn);

	// Clears the table and binds it to a traffic simulator. The next hook call
	// will trigger a lazy rescan unless MarkScanned() is called first.
	void Reset(cISC4TrafficSimulator* trafficSimulator);

	// Marks the table fully populated, e.g. after an eager scan, so the lazy
	// rescan won't fire.
	void MarkScanned();

	// Records the arrival edge each end of a portal pair should present.
	void Register(
		const Endpoint& first,
		const Endpoint& second,
		uint8_t firstArrivalEdge,
		uint8_t secondArrivalEdge);

	// Patches the AddTripNode and FloodSubnetwork call sites to our hooks.
	void InstallHooks();
}
