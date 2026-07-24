#pragma once

#include "cISC4NetworkOccupant.h"
#include "PortalGeometry.h"
#include "Style.h"

#include <cstdint>

namespace TunnelPortal::PortalPlacer
{
	// Minimal API shared between the UI control and the placement implementation.
	// Hook state, raw game layouts, and traffic repair details stay private to
	// TunnelPortalTool.cpp.
	using TunnelPortal::Endpoint;

	const char* NetworkTypeName(cISC4NetworkOccupant::eNetworkType type);
	bool TryFindNetworkAtTile(
		uint32_t x,
		uint32_t z,
		cISC4NetworkOccupant::eNetworkType& networkTypeOut);
	uint8_t ExpectedPortalTileCount(cISC4NetworkOccupant::eNetworkType networkType);
	bool PlacePortalPair(
		const Endpoint& first,
		const Endpoint& second,
		const TunnelPortal::Styles::Style& style);
}
