#include "TransitAccess.h"
#include "NetworkStubs.h"
#include "Patching.h"
#include "SC4Rect.h"
#include "SC4Vector.h"
#include "TransitAccessPatch.h"
#include "TransitAccessSupport.h"
#include "cISC4Lot.h"
#include "cISC4TrafficSimulator.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "wil/resource.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
	using namespace TransitAccessSupport;

	TransitAccess::TransitAccessPatch sPatch;
}

namespace TransitAccess
{
	bool __fastcall HookCalculateRoadAccess(cISC4Lot* lot, void*)
	{
		return sPatch.CalculateRoadAccess(lot);
	}

	void __fastcall HookGetSubnetworksForLot(
		cISC4TrafficSimulator* trafficSimulator,
		void*,
		cISC4Lot* lot,
		SC4Vector<uint32_t>& subnetworks)
	{
		sPatch.GetSubnetworksForLot(trafficSimulator, lot, subnetworks);
	}

	uint32_t __fastcall HookGetConnectedDestinationCount(
		cISC4TrafficSimulator* trafficSimulator,
		void*,
		cISC4Lot* lot,
		int purpose)
	{
		return sPatch.GetConnectedDestinationCount(trafficSimulator, lot, purpose);
	}

	bool __fastcall HookCreateStartNodes(cSC4PathFinder* pathFinder, void*)
	{
		return sPatch.CreateStartNodes(pathFinder);
	}

	bool TransitAccessPatch::GetLotSubnetworks(cISC4TrafficSimulator* trafficSimulator, cISC4Lot* lot, SC4Vector<uint32_t>& subnetworks) const
	{
		subnetworks.clear();
		if (!trafficSimulator || !lot)
		{
			return false;
		}

		OriginalGetSubnetworksForLot()(trafficSimulator, lot, subnetworks);
		return !subnetworks.empty();
	}

	bool TransitAccessPatch::GatherAdjacentTransitSubnetworks(
		cISC4TrafficSimulator* trafficSimulator,
		cISC4Lot* lot,
		SC4Vector<uint32_t>& subnetworks) const
	{
		subnetworks.clear();
		if (!trafficSimulator)
		{
			return false;
		}

		SC4Vector<uint32_t> candidateSubnetworks;
		for (cISC4Lot* candidate : GetEligibleAdjacentTransitLots(lot))
		{
			// Adjacent TE lots already participate in the traffic simulator, so
			// reuse their subnetworks instead of synthesizing new identifiers.
			if (!GetLotSubnetworks(trafficSimulator, candidate, candidateSubnetworks))
			{
				continue;
			}

			for (uint32_t subnetwork : candidateSubnetworks)
			{
				if (std::find(subnetworks.begin(), subnetworks.end(), subnetwork) == subnetworks.end())
				{
					subnetworks.push_back(subnetwork);
				}
			}
		}

		return !subnetworks.empty();
	}

	uint32_t TransitAccessPatch::GetAdjacentTransitEnabledDestinationCount(cISC4TrafficSimulator* trafficSimulator, const cISC4Lot * lot, const int purpose) const
	{
		if (!trafficSimulator)
		{
			return 0;
		}

		const auto originalGetConnectedDestinationCount = OriginalGetConnectedDestinationCount();

		uint32_t bestCount = 0;
		for (cISC4Lot* candidate : GetEligibleAdjacentTransitLots(lot))
		{
			// For commute purposes the blocked residential lot can use the best
			// destination count exposed by a neighboring transit-enabled lot.
			bestCount = std::max(bestCount, originalGetConnectedDestinationCount(trafficSimulator, candidate, purpose));
		}

		return bestCount;
	}

	bool TransitAccessPatch::RetryCreateStartNodesThroughTransitEnabledLot(cSC4PathFinder* pathFinder, const cISC4Lot * sourceLot) const
	{
		const std::vector<cISC4Lot*> candidates = GetEligibleAdjacentTransitLots(sourceLot);
		if (candidates.empty())
		{
			return false;
		}

		// CreateStartNodes reads the source bounds directly from the pathfinder.
		// Temporarily point it at a TE lot footprint, then restore the source lot
		// so the rest of the path search still starts from the residential lot.
		const SC4Rect<int32_t> sourceRect = pathFinder->sourceRect;
		const auto restoreSourceRect = wil::scope_exit([&] { pathFinder->sourceRect = sourceRect; });

		for (const cISC4Lot * candidate : candidates)
		{
			// The candidate came from the lot manager, so its bounds are already inside the city
			SC4Rect<int32_t> candidateBounds;
			if (!candidate->GetBoundingRect(candidateBounds) || !IsValidRect(candidateBounds))
			{
				continue;
			}

			pathFinder->sourceRect = candidateBounds;
			if (OriginalCreateStartNodes()(pathFinder))
			{
				return true;
			}
		}

		return false;
	}

	bool TransitAccessPatch::CalculateRoadAccess(cISC4Lot* lot) const
	{
		if (!lot)
		{
			return false;
		}

		if (OriginalCalculateRoadAccess()(lot))
		{
			return true;
		}

		// Only override the cached road-access bit when the regular road-access
		// calculation failed and a neighboring TE lot has a road-like network.
		if (GetEligibleAdjacentTransitLots(lot).empty())
		{
			return false;
		}

		SetRoadAccessCache(lot, true);
		return true;
	}

	void TransitAccessPatch::GetSubnetworksForLot(
		cISC4TrafficSimulator* trafficSimulator,
		cISC4Lot* lot,
		SC4Vector<uint32_t>& subnetworks) const
	{
		OriginalGetSubnetworksForLot()(trafficSimulator, lot, subnetworks);
		if (!subnetworks.empty())
		{
			return;
		}

		// Lots with TE access need the same subnetwork list the adjacent TE lot
		// would have returned, otherwise later destination checks still fail.
		SC4Vector<uint32_t> fallbackSubnetworks;
		if (GatherAdjacentTransitSubnetworks(trafficSimulator, lot, fallbackSubnetworks))
		{
			subnetworks = fallbackSubnetworks;
		}
	}

	uint32_t TransitAccessPatch::GetConnectedDestinationCount(
		cISC4TrafficSimulator* trafficSimulator,
		cISC4Lot* lot,
		int purpose) const
	{
		const uint32_t vanillaCount = OriginalGetConnectedDestinationCount()(trafficSimulator, lot, purpose);
		if (vanillaCount != 0 || purpose < 0 || purpose > 1)
		{
			return vanillaCount;
		}

		// Purpose 0/1 are the commute destination queries affected by road access.
		return GetAdjacentTransitEnabledDestinationCount(trafficSimulator, lot, purpose);
	}

	bool TransitAccessPatch::CreateStartNodes(cSC4PathFinder* pathFinder) const
	{
		if (!pathFinder)
		{
			return false;
		}

		// The retry is deliberately last: if the vanilla source lot can create
		// pathfinder start nodes, the patch leaves that result untouched.
		if (OriginalCreateStartNodes()(pathFinder))
		{
			return true;
		}

		cISC4Lot* const sourceLot = FindPathFinderSourceLot(pathFinder);
		if (!sourceLot)
		{
			return false;
		}

		return RetryCreateStartNodesThroughTransitEnabledLot(pathFinder, sourceLot);
	}

	void TransitAccessPatch::Install()
	{
		Patching::InstallInlineHook(calculateRoadAccessHook);
		Patching::InstallInlineHook(createStartNodesHook);
		Patching::InstallVTableHook(getConnectedDestinationCountHook);
		Patching::InstallVTableHook(getSubnetworksForLotHook);
	}
}

void TransitAccess::Install()
{
	sPatch.Install();
}
