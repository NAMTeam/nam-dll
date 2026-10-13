#pragma once

#include "cISC4App.h"
#include "cISC4BuildingOccupant.h"
#include "cISC4City.h"
#include "cISC4Lot.h"
#include "cISC4LotManager.h"
#include "cISC4NetworkOccupant.h"
#include "cISC4Occupant.h"
#include "cISC4ZoneManager.h"
#include "cISCPropertyHolder.h"
#include "DirtRoadAccess.h"
#include "GZServPtrs.h"
#include "NetworkStubs.h"
#include "SC4Rect.h"
#include "TransitAccessGeometry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace TransitAccessSupport
{
	using namespace TransitAccessGeometry;

	// Transit-enabled lots carry this exemplar property, on the lot itself or on
	// its building occupant.
	constexpr uint32_t kTransitSwitchPointProperty = 0xe90e25a1;

	// SimCity 4.exe sets this flag on the network occupants it creates for network
	// (transit-enabled) lot cells: 0x0061148b calls SetNetworkFlag(0x200000) on a
	// freshly created occupant right after resolving the lot under the cell.
	constexpr uint32_t kTransitEnabledCellNetworkFlag = 0x00200000;

	// cSC4Lot keeps its road-access results in a std::map<PurposeType, bool> at
	// offset 0x40. FUN_006c0f30 is that map's operator[]: it walks the red-black
	// tree and inserts a false-valued entry when the key is missing.
	// cSC4Lot::CalculateRoadAccess writes the PurposeType::None entry, which is
	// exactly the entry cSC4Lot::HasRoadAccess() reads back - it is compiled as
	// HasOrganicRoadAccess(0).
	constexpr uint32_t kRoadAccessCacheLookupAddress = 0x006c0f30;
	constexpr ptrdiff_t kLotRoadAccessCacheOffset = 0x40;

	using RoadAccessCacheLookupFn = uint8_t*(__thiscall*)(void*, const cISC4BuildingOccupant::PurposeType*);

	inline cISC4City* GetCity()
	{
		const cISC4AppPtr app;
		return app ? app->GetCity() : nullptr;
	}

	inline cISC4LotManager* GetLotManager()
	{
		cISC4City* const city = GetCity();
		return city ? city->GetLotManager() : nullptr;
	}

	// The city objects and dimensions a candidate scan needs, resolved just once
	struct CityContext
	{
		cISC4LotManager* lotManager = nullptr;
		cSC4TrafficNetworkMap* trafficNetworkMap = nullptr;
		int32_t cellCountX = 0;
		int32_t cellCountZ = 0;

		[[nodiscard]] bool IsUsable() const
		{
			return lotManager && trafficNetworkMap && cellCountX > 0 && cellCountZ > 0;
		}
	};

	inline CityContext GetCityContext()
	{
		CityContext context;

		if (cISC4City *const city = GetCity())
		{
			context.lotManager = city->GetLotManager();
			context.trafficNetworkMap = reinterpret_cast<cSC4TrafficNetworkMap*>(city->GetTrafficNetwork());
			context.cellCountX = static_cast<int32_t>(city->CellCountX());
			context.cellCountZ = static_cast<int32_t>(city->CellCountZ());
		}

		return context;
	}

	inline bool IsResidentialZoneType(cISC4ZoneManager::ZoneType zoneType)
	{
		switch (zoneType)
		{
		case cISC4ZoneManager::ZoneType::ResidentialLowDensity:
		case cISC4ZoneManager::ZoneType::ResidentialMediumDensity:
		case cISC4ZoneManager::ZoneType::ResidentialHighDensity:
			return true;
		default:
			return false;
		}
	}

	// cSC4Lot::GetZoneType is a single byte load from lot+0x81, and vanilla
	// cSC4Lot::CalculateRoadAccess branches on that very byte to decide which lot
	// edges to scan, so it is both cheap and authoritative here.
	inline bool IsResidentialLot(const cISC4Lot * lot)
	{
		return lot && IsResidentialZoneType(lot->GetZoneType());
	}

	inline bool IsTransitEnabledLot(cISC4Lot* lot)
	{
		if (!lot)
		{
			return false;
		}

		cISCPropertyHolder* const lotPropertyHolder = lot->AsPropertyHolder();
		if (lotPropertyHolder && lotPropertyHolder->HasProperty(kTransitSwitchPointProperty))
		{
			return true;
		}

		cISC4BuildingOccupant* const building = lot->GetBuilding();
		if (!building)
		{
			return false;
		}

		cISC4Occupant* const buildingOccupant = building->AsOccupant();
		cISCPropertyHolder* const buildingPropertyHolder = buildingOccupant ? buildingOccupant->AsPropertyHolder() : nullptr;
		return buildingPropertyHolder && buildingPropertyHolder->HasProperty(kTransitSwitchPointProperty);
	}

	inline bool HasRoadLikeNetworkAtCell(const CityContext& city, const uint32_t networkMask, const int32_t x, const int32_t z)
	{
		cISC4NetworkOccupant* const occupant = city.trafficNetworkMap->FindNetworkOccupant(x, z, networkMask, true);
		return occupant && !occupant->HasNetworkFlag(kTransitEnabledCellNetworkFlag);
	}

	// A candidate only counts when a road-like network runs directly along one of
	// its sides.
	inline bool HasRoadLikeNetworkBesideLot(const CityContext& city, const cISC4Lot * lot)
	{
		SC4Rect<int32_t> bounds;
		if (!lot || !lot->GetBoundingRect(bounds))
		{
			return false;
		}

		// Follows the DirtRoad/RHW access setting
		const uint32_t networkMask = DirtRoadAccess::GetMotorizedVehicleNetworkMask();
		return AnyOrthogonalNeighborCell(
			bounds,
			city.cellCountX,
			city.cellCountZ,
			[&city, networkMask](int32_t x, int32_t z)
			{
				return HasRoadLikeNetworkAtCell(city, networkMask, x, z);
			});
	}

	// The single candidate rule shared by all four fallbacks: a road-connected,
	// transit-enabled lot that shares a full side with this residential lot.
	// Borrowing road access, subnetworks, destination counts, or pathfinder start
	// nodes therefore always accepts and rejects exactly the same neighbours.
	inline std::vector<cISC4Lot*> GetEligibleAdjacentTransitLots(const cISC4Lot * lot)
	{
		std::vector<cISC4Lot*> candidates;

		SC4Rect<int32_t> sourceBounds;
		if (!IsResidentialLot(lot) || !lot->GetBoundingRect(sourceBounds))
		{
			return candidates;
		}

		const CityContext city = GetCityContext();
		if (!city.IsUsable())
		{
			return candidates;
		}

		// One candidate usually occupies several perimeter cells, so deduplicate.
		std::vector<cISC4Lot*> visited;
		ForEachOrthogonalNeighborCell(
			sourceBounds,
			city.cellCountX,
			city.cellCountZ,
			[&](int32_t x, int32_t z)
			{
				cISC4Lot* const candidate = city.lotManager->GetLot(x, z, false);
				if (!candidate
					|| candidate == lot
					|| std::find(visited.begin(), visited.end(), candidate) != visited.end())
				{
					return;
				}

				visited.push_back(candidate);
				if (IsTransitEnabledLot(candidate) && HasRoadLikeNetworkBesideLot(city, candidate))
				{
					candidates.push_back(candidate);
				}
			});

		return candidates;
	}

	// Updates the general road-access entry the game caches on the lot, so later
	// cSC4Lot::HasRoadAccess() calls agree with the fallback result.
	inline void SetRoadAccessCache(cISC4Lot* lot, const bool value)
	{
		const auto lookup = reinterpret_cast<RoadAccessCacheLookupFn>(kRoadAccessCacheLookupAddress);
		constexpr auto key = cISC4BuildingOccupant::PurposeType::None;
		uint8_t* const cachedValue = lookup(reinterpret_cast<uint8_t*>(lot) + kLotRoadAccessCacheOffset, &key);
		if (cachedValue)
		{
			*cachedValue = value ? 1 : 0;
		}
	}

	// cSC4PathFinder::CreateStartNodes only receives the pathfinder, but it reads
	// its source rectangle from sourceRect, bounds-checks it against cityCellCount
	// and resolves it with the very same GetLot(x, z, false) call, so the source
	// lot can be recovered the same way. Requiring an exact bounds match keeps
	// source rectangles that are not a whole lot out of the retry.
	inline cISC4Lot* FindPathFinderSourceLot(const cSC4PathFinder * pathFinder)
	{
		if (!pathFinder)
		{
			return nullptr;
		}

		// cityCellCount holds the cell counts along x and z, in that order.
		const SC4Rect<int32_t> sourceRect = pathFinder->sourceRect;
		if (!RectIsInsideCity(sourceRect, pathFinder->cityCellCount.x, pathFinder->cityCellCount.y))
		{
			return nullptr;
		}

		cISC4LotManager* const lotManager = GetLotManager();
		if (!lotManager)
		{
			return nullptr;
		}

		cISC4Lot* const lot = lotManager->GetLot(sourceRect.topLeftX, sourceRect.topLeftY, false);
		SC4Rect<int32_t> lotBounds;
		if (!lot || !lot->GetBoundingRect(lotBounds) || !RectsEqual(lotBounds, sourceRect))
		{
			return nullptr;
		}

		return lot;
	}
}
