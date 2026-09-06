// The network mask the DLL's own road-access lookups use. The transit access
// patch has to follow the DirtRoad/RHW access setting: 0x449 while that patch is
// off, 0xc49 once it has widened the game's own checks.
#include "doctest/doctest.h"
#include "DirtRoadAccess.h"

TEST_CASE("the dirt road mask widens the vanilla motorized vehicle mask")
{
	static_assert(DirtRoadAccess::kVanillaMotorizedVehicleNetworkMask == 0x449);
	static_assert(DirtRoadAccess::kDirtRoadNetworkMask == 0x800);
	static_assert(DirtRoadAccess::kAdjustedMotorizedVehicleNetworkMask == 0xc49);

	CHECK(DirtRoadAccess::kAdjustedMotorizedVehicleNetworkMask
		== (DirtRoadAccess::kVanillaMotorizedVehicleNetworkMask | DirtRoadAccess::kDirtRoadNetworkMask));
}

// DirtRoadAccess::Install patches the game image, so it cannot run here. What
// this pins down is the default: with that patch not installed, the transit
// access lookups keep using the vanilla mask.
TEST_CASE("the effective mask stays vanilla until the dirt road patch installs")
{
	CHECK(DirtRoadAccess::GetMotorizedVehicleNetworkMask()
		== DirtRoadAccess::kVanillaMotorizedVehicleNetworkMask);
	CHECK(DirtRoadAccess::GetMotorizedVehicleNetworkMask()
		!= DirtRoadAccess::kAdjustedMotorizedVehicleNetworkMask);
}
