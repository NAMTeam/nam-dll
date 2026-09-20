#include "doctest/doctest.h"
#include "DirtRoadAccess.h"
#include "cISC4NetworkOccupant.h"
#include "Masking.h"

// DirtRoadAccess::Install patches the game image, so it cannot run here. What
// this pins down is the default: with that patch not installed, the transit
// access lookups keep using the vanilla mask.
TEST_CASE("the effective mask stays vanilla until the dirt road patch installs")
{
	CHECK(DirtRoadAccess::GetMotorizedVehicleNetworkMask() & asMask(cISC4NetworkOccupant::eNetworkType::DirtRoad) == 0);
}
