#include "GameAccess.h"

#include "GameAddresses.h"
#include "NetworkStubs.h"
#include "PortalGeometry.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4NetworkManager.h"
#include "cISC4NetworkTool.h"
#include "cISC4TrafficSimulator.h"

#include <array>

namespace TunnelPortal::GameAccess
{
	namespace
	{
		// Order in which tiles are probed for a network. Roads before rails,
		// single-tile before two-tile within each family.
		constexpr std::array<cISC4NetworkOccupant::eNetworkType, 10> kCandidateNetworks = {
			cISC4NetworkOccupant::Road,
			cISC4NetworkOccupant::Street,
			cISC4NetworkOccupant::DirtRoad,
			cISC4NetworkOccupant::OneWayRoad,
			cISC4NetworkOccupant::Avenue,
			cISC4NetworkOccupant::Highway,
			cISC4NetworkOccupant::GroundHighway,
			cISC4NetworkOccupant::Rail,
			cISC4NetworkOccupant::LightRail,
			cISC4NetworkOccupant::Monorail,
		};
	}

	cISC4City* GetCity()
	{
		cISC4AppPtr app;
		return app ? app->GetCity() : nullptr;
	}

	void* GetTrafficNetworkMap()
	{
		cISC4City* const city = GetCity();
		return city ? reinterpret_cast<void*>(city->GetTrafficNetwork()) : nullptr;
	}

	cISC4TrafficSimulator* GetTrafficSimulator()
	{
		cISC4City* const city = GetCity();
		return city ? city->GetTrafficSimulator() : nullptr;
	}

	cSC4NetworkTool* GetNetworkTool(
		cISC4NetworkManager* networkManager,
		cISC4NetworkOccupant::eNetworkType networkType)
	{
		if (!networkManager)
		{
			return nullptr;
		}

		cISC4NetworkTool* tool = networkManager->GetNetworkTool(static_cast<int32_t>(networkType), true);
		if (!tool)
		{
			tool = networkManager->GetNetworkTool(static_cast<int32_t>(networkType), false);
		}

		if (tool)
		{
			tool->Init();
		}

		return reinterpret_cast<cSC4NetworkTool*>(tool);
	}

	bool FindNetworkAtTile(
		uint32_t x,
		uint32_t z,
		cISC4NetworkOccupant::eNetworkType& networkTypeOut)
	{
		void* const trafficNetworkMap = GetTrafficNetworkMap();
		if (!trafficNetworkMap)
		{
			return false;
		}

		for (const cISC4NetworkOccupant::eNetworkType networkType : kCandidateNetworks)
		{
			void* const entry = Game::GetTrafficNetworkInfo(
				trafficNetworkMap,
				static_cast<int32_t>(x),
				static_cast<int32_t>(z),
				Geometry::NetworkMask(networkType),
				true);
			if (entry)
			{
				networkTypeOut = networkType;
				return true;
			}
		}

		return false;
	}
}
