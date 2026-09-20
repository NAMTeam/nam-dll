#include "DirtRoadAccess.h"
#include "Patching.h"
#include "cISC4NetworkOccupant.h"
#include "Masking.h"

using NN = cISC4NetworkOccupant::eNetworkType;

namespace
{
	constexpr uint32_t kGetLotFacingStreetCountNetworkMaskPushAddress = 0x004be6dc;
	constexpr uint32_t kGetLotFacingStreetCountScoringMaskTestAddress = 0x004be70b;
	constexpr uint32_t kFerryTerminalRoadAccessNetworkMaskPushAddress = 0x006c1726;
	constexpr uint32_t kCalculateRoadAccessNetworkMaskPushAddress = 0x006c1bd1;

	// Road, Street, Avenue and OneWayRoad: the mask the game uses to recognize a road-like connection.
	constexpr uint32_t kVanillaMotorizedVehicleNetworkMask = 0x00000449;
	static_assert(kVanillaMotorizedVehicleNetworkMask == asMask(NN::Road, NN::Street, NN::Avenue, NN::OneWayRoad));
	constexpr uint32_t kAdjustedMotorizedVehicleNetworkMask = kVanillaMotorizedVehicleNetworkMask | asMask(NN::DirtRoad);

	constexpr uint32_t kVanillaLowPrioFacingNetworkMask = 0x00000408;
	static_assert(kVanillaLowPrioFacingNetworkMask == asMask(NN::Street, NN::OneWayRoad));
	constexpr uint32_t kAdjustedLowPriorityFacingNetworkMask = kVanillaLowPrioFacingNetworkMask | asMask(NN::DirtRoad);

	auto sInstalled = false;
}

void DirtRoadAccess::Install() {
  Patching::PatchPushImmediate32(kCalculateRoadAccessNetworkMaskPushAddress,
                                 kVanillaMotorizedVehicleNetworkMask,
                                 kAdjustedMotorizedVehicleNetworkMask);
  Patching::PatchPushImmediate32(kFerryTerminalRoadAccessNetworkMaskPushAddress,
                                 kVanillaMotorizedVehicleNetworkMask,
                                 kAdjustedMotorizedVehicleNetworkMask);
  Patching::PatchPushImmediate32(kGetLotFacingStreetCountNetworkMaskPushAddress,
                                 kVanillaMotorizedVehicleNetworkMask,
                                 kAdjustedMotorizedVehicleNetworkMask);
  Patching::PatchTestEaxImmediate32(
      kGetLotFacingStreetCountScoringMaskTestAddress,
      kVanillaLowPrioFacingNetworkMask, kAdjustedLowPriorityFacingNetworkMask);

  sInstalled = true;
}

uint32_t DirtRoadAccess::GetMotorizedVehicleNetworkMask() {
  return sInstalled ? kAdjustedMotorizedVehicleNetworkMask
                    : kVanillaMotorizedVehicleNetworkMask;
}
