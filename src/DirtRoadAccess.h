#pragma once
#include <cstdint>
#include "cISC4NetworkOccupant.h"

namespace DirtRoadAccess
{
	// Road, Street, Avenue and OneWayRoad: the mask the game uses to recognize a road-like connection.
	constexpr uint32_t kVanillaMotorizedVehicleNetworkMask = 0x00000449;
	constexpr uint32_t kDirtRoadNetworkMask = 1u << cISC4NetworkOccupant::eNetworkType::DirtRoad;
	constexpr uint32_t kAdjustedMotorizedVehicleNetworkMask = kVanillaMotorizedVehicleNetworkMask | kDirtRoadNetworkMask;

	void Install();

	// The mask that is currently in effect. Useful to determine whether the dirt road access patch is active from other patches
	uint32_t GetMotorizedVehicleNetworkMask();
}
