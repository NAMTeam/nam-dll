#include "TransitNetworkMapping.h"
#include "Patching.h"
#include "cISC4NetworkOccupant.h"
#include "cISC4TrafficSimulator.h"
#include "NetworkStubs.h"
#include "Masking.h"

using TT = cISC4TrafficSimulator::TravelType;
using NN = cISC4NetworkOccupant::eNetworkType;
using TN = TransitNetwork;

namespace
{
	constexpr uint32_t networkToTransitNetworkAddr = 0xab3174;
	constexpr uint32_t travelTypeAttrAddr = 0xb09628;

	constexpr uint32_t roadTransitTypesOrig = 0x03;
	static_assert(roadTransitTypesOrig == asMask(TN::Pedestrian, TN::Car));
	constexpr uint32_t roadTransitTypesNew = roadTransitTypesOrig | asMask(TN::Lightrail);
	constexpr uint32_t railTransitTypesOrig = 0x04;
	static_assert(railTransitTypesOrig == asMask(TN::Train));
	constexpr uint32_t railTransitTypesNew = railTransitTypesOrig | asMask(TN::Monorail);

	constexpr uint32_t lightrailTravelNetworksOrig = 0x0300;
	static_assert(lightrailTravelNetworksOrig == asMask(NN::Monorail, NN::LightRail));
	constexpr uint32_t lightrailTravelNetworksNew = lightrailTravelNetworksOrig | asMask(NN::Road, NN::Street, NN::Avenue);
	constexpr uint32_t monorailTravelNetworksOrig = 0x0300;
	static_assert(monorailTravelNetworksOrig == asMask(NN::Monorail, NN::LightRail));
	constexpr uint32_t monorailTravelNetworksNew = monorailTravelNetworksOrig | asMask(NN::Rail);

	const uint32_t railNetworkTravelTypeToLText[][2] = {
		{static_cast<uint32_t>(TT::PassangerTrain), 0x4B8B4693},
		{static_cast<uint32_t>(TT::FreightTrain), 0xCB8C5C08},
		{static_cast<uint32_t>(TT::Monorail), 0xCC05A60F},  // new
		{9, 0},  // terminator
	};

	constexpr uint32_t GetNetworkOccupantTrafficInfo_InjectPoint = 0x4d09d1;
	constexpr uint32_t GetNetworkOccupantTrafficInfo_ReturnJump = 0x4d0b4a;

	void NAKED_FUN Hook_GetNetworkOccupantTrafficInfo(void)
	{
		__asm {
			lea edi, [railNetworkTravelTypeToLText];
			push GetNetworkOccupantTrafficInfo_ReturnJump;
			ret;
		}
	}
}

void TransitNetworkMapping::Install()
{
	// patch kNetworkManagerTypeToTransitNetworkType mapping to allow other transit types on these networks
	for (auto n : {NN::Road, NN::Street, NN::Avenue}) {
		Patching::PatchImmediate32(
				networkToTransitNetworkAddr + 4*static_cast<uint32_t>(n),
				roadTransitTypesOrig, roadTransitTypesNew);
	}
	Patching::PatchImmediate32(
			networkToTransitNetworkAddr + 4*static_cast<uint32_t>(NN::Rail),
			railTransitTypesOrig, railTransitTypesNew);

	// patch sTravelTypeAttrs for MakeRoutePath (otherwise the route arrows can sometimes be invisible)
	Patching::PatchImmediate32(
			travelTypeAttrAddr + offsetof(TravelTypeAttr, networkFlags)
			+ sizeof(TravelTypeAttr) * static_cast<uint32_t>(TT::ElevatedTrain),
			lightrailTravelNetworksOrig,
			lightrailTravelNetworksNew);
	Patching::PatchImmediate32(
			travelTypeAttrAddr + offsetof(TravelTypeAttr, networkFlags)
			+ sizeof(TravelTypeAttr) * static_cast<uint32_t>(TT::Monorail),
			monorailTravelNetworksOrig,
			monorailTravelNetworksNew);

	// add monorail to rail route query text
	Patching::InstallHook(GetNetworkOccupantTrafficInfo_InjectPoint, Hook_GetNetworkOccupantTrafficInfo);
}
