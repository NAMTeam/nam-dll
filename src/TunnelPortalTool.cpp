// TunnelPortalTool
//
// Enables the experimental tunnel portal tool. The behaviour lives in
// src/tunnelportal/; this file only turns the patches on and forwards the
// entry points NAMDllDirector uses. Activate() is defined alongside the UI in
// tunnelportal/ViewInputControl.cpp.
//
// Patch sites installed here, by owning unit (SC4 1.1.641):
//   PathStitcher    0x0053FDEE  MakeTunnelPaths  direction override
//                   0x0053FE31  MakeTunnelPaths  peer path key lookup
//   RouteEdgeFixes  0x006D9ACF  FindPath         tunnel AddTripNode call
//                   0x00718215  FloodSubnetwork  tunnel GetNetworkInfo call

#include "TunnelPortalTool.h"

#include "tunnelportal/GameAccess.h"
#include "tunnelportal/PathStitcher.h"
#include "tunnelportal/RouteEdgeFixes.h"
#include "tunnelportal/SavedTunnelScanner.h"

#include "cISC4TrafficSimulator.h"

namespace
{
	namespace GameAccess = TunnelPortal::GameAccess;
	namespace RouteEdgeFixes = TunnelPortal::RouteEdgeFixes;
	namespace PathStitcher = TunnelPortal::PathStitcher;
	namespace SavedTunnelScanner = TunnelPortal::SavedTunnelScanner;
}

void TunnelPortalTool::Install()
{
	PathStitcher::InstallHooks();
	SavedTunnelScanner::InstallRescanCallback();
	RouteEdgeFixes::InstallHooks();
}

void TunnelPortalTool::RefreshCity()
{
	// Safe lifecycle point: rebuild the route-edge table eagerly, doing the
	// path-vector refresh the lazy in-hook rescan skips.
	cISC4TrafficSimulator* const trafficSimulator = GameAccess::GetTrafficSimulator();
	RouteEdgeFixes::Reset(trafficSimulator);
	if (trafficSimulator && SavedTunnelScanner::Scan(trafficSimulator, true))
	{
		RouteEdgeFixes::MarkScanned();
	}
}
