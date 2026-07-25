#pragma once

#include "cISC4NetworkOccupant.h"

class cISC4City;
class cISC4TrafficSimulator;
class cISC4NetworkManager;
class cSC4NetworkTool;

// Lookups for the live city objects the tool needs. Each returns null when the
// object isn't available (no city loaded, etc.), so callers null-check rather
// than assume.
namespace TunnelPortal::GameAccess
{
	cISC4City* GetCity();

	// cISC4TrafficNetwork for the current city, as an opaque pointer (its type
	// has no SDK header). Null when no city is loaded.
	void* GetTrafficNetworkMap();

	cISC4TrafficSimulator* GetTrafficSimulator();

	// The initialised network tool for a network type. Prefers the built-in
	// tool, falls back to the on-demand one, and calls Init() before returning.
	cSC4NetworkTool* GetNetworkTool(
		cISC4NetworkManager* networkManager,
		cISC4NetworkOccupant::eNetworkType networkType);

	// Finds the network occupying tile (x,z), if any. Probes the candidate
	// network types in a fixed order and writes the first match to
	// networkTypeOut. Returns false when the tile carries no known network.
	bool FindNetworkAtTile(
		uint32_t x,
		uint32_t z,
		cISC4NetworkOccupant::eNetworkType& networkTypeOut);
}
