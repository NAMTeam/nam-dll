#include "TransitNetworkMapping.h"
#include "Patching.h"
#include "cISC4NetworkOccupant.h"
#include "NetworkStubs.h"

#define TN_MASK(n) (1 << static_cast<uint32_t>(TransitNetwork::n))

namespace
{
	constexpr uint32_t networkToTransitNetworkAddr = 0xab3174;

	constexpr uint32_t roadTransitTypesOrig = 0x03;
	static_assert(roadTransitTypesOrig == (TN_MASK(Pedestrian) | TN_MASK(Car)));
	constexpr uint32_t roadTransitTypesNew = roadTransitTypesOrig | TN_MASK(Lightrail);
	constexpr uint32_t railTransitTypesOrig = 0x04;
	static_assert(railTransitTypesOrig == TN_MASK(Train));
	constexpr uint32_t railTransitTypesNew = railTransitTypesOrig | TN_MASK(Monorail);
}

void TransitNetworkMapping::Install()
{
	for (auto n : {
			cISC4NetworkOccupant::eNetworkType::Road,
			cISC4NetworkOccupant::eNetworkType::Street,
			cISC4NetworkOccupant::eNetworkType::Avenue,
	}) {
		Patching::PatchImmediate32(
				networkToTransitNetworkAddr + 4*static_cast<uint32_t>(n),
				roadTransitTypesOrig, roadTransitTypesNew);
	}
	Patching::PatchImmediate32(
			networkToTransitNetworkAddr + 4*static_cast<uint32_t>(cISC4NetworkOccupant::eNetworkType::Rail),
			railTransitTypesOrig, railTransitTypesNew);
}
