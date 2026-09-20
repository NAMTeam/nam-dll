#pragma once
#include <cstdint>
#include "cISC4NetworkOccupant.h"

namespace DirtRoadAccess
{
	void Install();

	// The mask that is currently in effect. Useful to determine whether the dirt road access patch is active from other patches
	uint32_t GetMotorizedVehicleNetworkMask();
}
