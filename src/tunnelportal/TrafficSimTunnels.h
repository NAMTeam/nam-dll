#pragma once

#include "PortalGeometry.h"
#include "RawLayouts.h"

#include <cstdint>

class cIGZUnknown;
class cISC4TrafficSimulator;

// Access to the traffic simulator's tunnel registry, plus the commit that makes
// a freshly linked portal pair visible to route solving.
namespace TunnelPortal::TrafficSimTunnels
{
	// The tunnel hash map embedded in the simulator, or null.
	const Raw::TunnelMap* GetMap(cISC4TrafficSimulator* trafficSimulator);

	// True when a map has a sane bucket range and can be walked.
	bool IsUsableMap(const Raw::TunnelMap* map);

	// Registry key for a portal cell: (x << 8) | z, low bytes only.
	uint16_t PackedCellKey(uint32_t x, uint32_t z);

	// Tells the simulator two tunnel occupants were (re)linked and re-solves
	// connectivity over their cells. Temporarily injects a connection record so
	// the re-solve sees the pair even before the registry is rebuilt.
	void NotifyLinkedTunnels(
		const Endpoint& first,
		const Endpoint& second,
		cIGZUnknown* firstTunnel,
		cIGZUnknown* secondTunnel);
}
