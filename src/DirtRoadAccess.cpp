#include "DirtRoadAccess.h"
#include "Patching.h"
#include "cISC4NetworkOccupant.h"

namespace
{
	constexpr uint32_t kGetLotFacingStreetCountNetworkMaskPushAddress = 0x004be6dc;
	constexpr uint32_t kGetLotFacingStreetCountScoringMaskTestAddress = 0x004be70b;
	constexpr uint32_t kFerryTerminalRoadAccessNetworkMaskPushAddress = 0x006c1726;
	constexpr uint32_t kCalculateRoadAccessNetworkMaskPushAddress = 0x006c1bd1;

	constexpr uint32_t kVanillaMotorizedVehicleNetworkMask = 0x00000449;
	constexpr uint32_t kVanillaLowPrioFacingNetworkMask = 0x00000408;
	constexpr uint32_t kDirtRoadNetworkMask = 1u << cISC4NetworkOccupant::eNetworkType::DirtRoad;

	constexpr uint32_t kAdjustedMotorizedVehicleNetworkMask = kVanillaMotorizedVehicleNetworkMask | kDirtRoadNetworkMask;
	constexpr uint32_t kAdjustedLowPriorityFacingNetworkMask = kVanillaLowPrioFacingNetworkMask | kDirtRoadNetworkMask;
}

void DirtRoadAccess::Install()
{
	Patching::PatchPushImmediate32(
		kCalculateRoadAccessNetworkMaskPushAddress,
		kVanillaMotorizedVehicleNetworkMask,
		kAdjustedMotorizedVehicleNetworkMask);
	Patching::PatchPushImmediate32(
		kFerryTerminalRoadAccessNetworkMaskPushAddress,
		kVanillaMotorizedVehicleNetworkMask,
		kAdjustedMotorizedVehicleNetworkMask);
	Patching::PatchPushImmediate32(
		kGetLotFacingStreetCountNetworkMaskPushAddress,
		kVanillaMotorizedVehicleNetworkMask,
		kAdjustedMotorizedVehicleNetworkMask);
	Patching::PatchTestEaxImmediate32(
		kGetLotFacingStreetCountScoringMaskTestAddress,
		kVanillaLowPrioFacingNetworkMask,
		kAdjustedLowPriorityFacingNetworkMask);
}
