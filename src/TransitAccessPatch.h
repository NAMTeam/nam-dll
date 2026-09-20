#pragma once

#include "Patching.h"
#include "SC4Vector.h"
#include "TransitAccessSupport.h"

#include <cstdint>

class cISC4Lot;
class cISC4TrafficSimulator;
class cSC4PathFinder;

namespace TransitAccess
{
	// The hooks stay as free functions so the patch can use the game's
	// calling conventions and immediately delegate into the stateful patch class.
	bool __fastcall HookCalculateRoadAccess(cISC4Lot* lot, void*);
	void __fastcall HookGetSubnetworksForLot(cISC4TrafficSimulator* trafficSimulator, void*, cISC4Lot* lot, SC4Vector<uint32_t>& subnetworks);
	uint32_t __fastcall HookGetConnectedDestinationCount(cISC4TrafficSimulator* trafficSimulator, void*, cISC4Lot* lot, int purpose);
	bool __fastcall HookCreateStartNodes(cSC4PathFinder* pathFinder, void*);

	// Implements the TE-lot access patch. Residential lots still ask the vanilla
	// simulator first. The patch only borrows access, subnetworks, destination
	// counts, and start nodes from adjacent transit-enabled lots when the vanilla
	// result is negative or empty.
	class TransitAccessPatch final
	{
	public:
		void Install();

		bool CalculateRoadAccess(cISC4Lot* lot) const;
		void GetSubnetworksForLot(cISC4TrafficSimulator* trafficSimulator, cISC4Lot* lot, SC4Vector<uint32_t>& subnetworks) const;
		uint32_t GetConnectedDestinationCount(cISC4TrafficSimulator* trafficSimulator, cISC4Lot* lot, int purpose) const;
		bool CreateStartNodes(cSC4PathFinder* pathFinder) const;

	private:
		using CalculateRoadAccessFn = bool(__thiscall*)(cISC4Lot*);
		using CreateStartNodesFn = bool(__thiscall*)(cSC4PathFinder*);
		using GetConnectedDestinationCountFn = uint32_t(__thiscall*)(cISC4TrafficSimulator*, cISC4Lot*, int);
		using GetSubnetworksForLotFn = void(__thiscall*)(cISC4TrafficSimulator*, cISC4Lot*, SC4Vector<uint32_t>&);

		bool GetLotSubnetworks(cISC4TrafficSimulator* trafficSimulator, cISC4Lot* lot, SC4Vector<uint32_t>& subnetworks) const;
		bool GatherAdjacentTransitSubnetworks(cISC4TrafficSimulator* trafficSimulator, cISC4Lot* lot, SC4Vector<uint32_t>& subnetworks) const;
		uint32_t GetAdjacentTransitEnabledDestinationCount(cISC4TrafficSimulator* trafficSimulator,
                    const cISC4Lot * lot, int purpose) const;
		bool RetryCreateStartNodesThroughTransitEnabledLot(cSC4PathFinder* pathFinder, const cISC4Lot * sourceLot) const;

		[[nodiscard]] CalculateRoadAccessFn OriginalCalculateRoadAccess() const
		{
			return reinterpret_cast<CalculateRoadAccessFn>(calculateRoadAccessHook.trampoline);
		}

		[[nodiscard]] CreateStartNodesFn OriginalCreateStartNodes() const
		{
			return reinterpret_cast<CreateStartNodesFn>(createStartNodesHook.trampoline);
		}

		[[nodiscard]] GetConnectedDestinationCountFn OriginalGetConnectedDestinationCount() const
		{
			return reinterpret_cast<GetConnectedDestinationCountFn>(getConnectedDestinationCountHook.original);
		}

		[[nodiscard]] GetSubnetworksForLotFn OriginalGetSubnetworksForLot() const
		{
			return reinterpret_cast<GetSubnetworksForLotFn>(getSubnetworksForLotHook.original);
		}

		Patching::InlineHook calculateRoadAccessHook{
			.address = 0x006c1a30,
			.hookFunction = reinterpret_cast<void*>(&HookCalculateRoadAccess),
			.expectedBytes = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8},
			.checkExpectedBytes = true
		};

		Patching::InlineHook createStartNodesHook{
			.address = 0x006d8a90,
			.hookFunction = reinterpret_cast<void*>(&HookCreateStartNodes),
			.expectedBytes = {0x83, 0xec, 0x5c, 0x53, 0x55, 0x56},
			.checkExpectedBytes = true
		};

		Patching::VTableHook getConnectedDestinationCountHook{
			.slotAddress = 0x00ab3468,
			.hookFunction = reinterpret_cast<void*>(&HookGetConnectedDestinationCount),
			.expectedOriginal = 0x007106c0
		};

		Patching::VTableHook getSubnetworksForLotHook{
			.slotAddress = 0x00ab346c,
			.hookFunction = reinterpret_cast<void*>(&HookGetSubnetworksForLot),
			.expectedOriginal = 0x00711b50
		};
	};
}
