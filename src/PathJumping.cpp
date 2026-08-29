#include "PathJumping.h"
#include "Patching.h"
#include "NetworkStubs.h"
#include "SC4Point.h"
#include "SC4List.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantManager.h"
#include "cISC4TrafficSimulator.h"


namespace
{
	cISC4OccupantManager** const sppOccupantManager = reinterpret_cast<cISC4OccupantManager**>(0xb43d0c);

	const uint32_t kPathType[] = {2, 1, 3, 4, 6, 7};  // indexed by TransitNetwork

	const uint32_t kTravelModeToTransitNetwork[] = {0, 1, 1, 2, 1, 2, 3, 4, 5};  // indexed by TravelType

	// We deliberately do not include pedestrians here, as they often contain too many combinations of connections to be useful in priorCarConnectionsPerEntrySide
	constexpr uint32_t multiLevelTransitNetworksMask
		= (1 << (uint32_t)TransitNetwork::Car)
		| (1 << (uint32_t)TransitNetwork::Train)
		| (1 << (uint32_t)TransitNetwork::Lightrail)
		| (1 << (uint32_t)TransitNetwork::Monorail);
	constexpr uint32_t multiLevelTravelModesMask
		= (1 << (uint32_t)cISC4TrafficSimulator::TravelType::Car)
		| (1 << (uint32_t)cISC4TrafficSimulator::TravelType::Bus)
		| (1 << (uint32_t)cISC4TrafficSimulator::TravelType::PassangerTrain)
		| (1 << (uint32_t)cISC4TrafficSimulator::TravelType::FreightTruck)
		| (1 << (uint32_t)cISC4TrafficSimulator::TravelType::FreightTrain)
		| (1 << (uint32_t)cISC4TrafficSimulator::TravelType::ElevatedTrain)
		| (1 << (uint32_t)cISC4TrafficSimulator::TravelType::Monorail);

	// largely imitates RemoveBadElevatedPaths, without masking out earlier bits
	void __thiscall RemoveBadElevatedPaths2(
			cISC4TrafficSimulator* pThis,
			int32_t cellX,
			int32_t cellZ,
			cSC4PathInfo* pathInfo,
			int32_t entrySide,
			int32_t exitSide,
			TrafficSimCellConnections &cellConnections,
			TransitNetwork transitNetwork)
	{
		auto pathType = kPathType[(uint32_t)transitNetwork];
		for (auto item = pathInfo->pathMap.begin(); item != pathInfo->pathMap.end(); item++) {
			auto& key = item->first;
			if (((key & 0xf0000000) == 0) &&  // not a stop point(?)
				(((key >> 24) & 0xf) == pathType) &&
				((uint8_t)(key >> 8) == (uint8_t)entrySide) &&
				((uint8_t)key == (uint8_t)exitSide))
			{
				SC4Point<int32_t> priorCell = {cellX + kNextX[entrySide], cellZ + kNextZ[entrySide]};
				SC4List<cISC4Occupant*> occupants = SC4List<cISC4Occupant*>();
				(*sppOccupantManager)->GetOccupantsByStandardCityCells(occupants, &priorCell, &priorCell, (uint32_t)0xc772bf98, -1);
				if (!occupants.empty()) {
					int32_t priorExitSide = (entrySide + 2) & 0x3;  // opposite of entrySide
					// uint16_t mask = ~((uint16_t)0xf << (exitSide * 4));
					for (auto ppOccupant = occupants.begin(); ppOccupant != occupants.end(); ppOccupant++) {
						auto&& occupant = *ppOccupant;
						cISC4NetworkOccupant* priorNetworkOccupant = nullptr;
						if (occupant->QueryInterface(0xa821ef94, reinterpret_cast<void**>(&priorNetworkOccupant))) {
							if (priorNetworkOccupant->HasAnyNetworkFlag(0x1f4f)) {  // transit networks except Subway
								auto& path = item->second;
								auto priorPathInfo = (cSC4PathInfo*)priorNetworkOccupant->GetPathInfo();
								if (priorPathInfo != nullptr && !path.coords.empty()) {
									uint16_t flagsGoodPriorPaths = 0;
									constexpr float tolerance = 4.0f;
									auto priorPathsKeys = SC4Vector<uint32_t>();
									cSC4PathInfo::GetPathsNearEndPoint(priorPathInfo, priorPathsKeys, (cISC4PathInfo::tPathType)pathType, path.coords[0], -1, priorExitSide, tolerance);
									for (auto priorPathKey = priorPathsKeys.begin(); priorPathKey != priorPathsKeys.end(); priorPathKey++) {
										uint32_t priorEntrySide = (*priorPathKey >> 8) & 0xff;
										flagsGoodPriorPaths |= (1 << priorEntrySide);
									}
									cellConnections.priorCarConnectionsPerEntrySide[entrySide] |= (flagsGoodPriorPaths & 0xf) << (exitSide * 4);  // here we do NOT mask out previously set bits to allow encoding connections for multiple path types
								}
							}
							priorNetworkOccupant->Release();
						}
						// occupant->Release();  // automatically called by SC4List destructor
					}
				}
			}
		}
	}

	void SetConnections(
			cISC4TrafficSimulator* trafficSimulator,
			int32_t cellX,
			int32_t cellZ,
			cISC4NetworkOccupant &networkOccupant,
			TrafficSimCellConnections &cellConnections,
			int32_t entrySide,
			int32_t exitSide,
			TransitNetwork transitNetwork)
	{
		for (uint32_t travelMode = (uint32_t)cISC4TrafficSimulator::TravelType::Walk; travelMode <= (uint32_t)cISC4TrafficSimulator::TravelType::Monorail; travelMode++) {
			if ((uint32_t)transitNetwork == kTravelModeToTransitNetwork[travelMode]) {
				cellConnections.entryExitConnectionsPerTravelType[travelMode] |= (uint16_t)1 << (entrySide * 4 + exitSide);
			}
		}
		if (((1 << (uint32_t)transitNetwork) & multiLevelTransitNetworksMask) != 0) {  // TODO in highly connected cases, this might clear too much
			// initially, clear connections for entry/exit
			uint16_t mask = ~((uint16_t)0xf << (exitSide * 4));
			if ((cellConnections.priorCarConnectionsPerEntrySide[entrySide] | mask) == (uint16_t)0xffff) {
				cellConnections.priorCarConnectionsPerEntrySide[entrySide] &= mask;
			}
			auto pathInfo = (cSC4PathInfo*)networkOccupant.GetPathInfo();
			RemoveBadElevatedPaths2(trafficSimulator, cellX, cellZ, pathInfo, entrySide, exitSide, cellConnections, transitNetwork);
		}
	}

	constexpr uint32_t DoConnectionsChanged_Inject = 0x71ab60;
	constexpr uint32_t DoConnectionsChanged_Continue = 0x71abab;

	void NAKED_FUN DoConnectionsChanged_Hook(void) {
		__asm {
			push eax;  // store
			push ecx;  // store
			push edx;  // store
			push dword ptr [esp + 0xc + 0x6c];  // transitNetwork
			push ebx;  // exitSide
			push ebp;  // entrySide
			push edi;  // &cellConnections for cellX,cellZ
			push dword ptr [esp + 0x1c + 0x70];  // networkOccupant
			push dword ptr [esp + 0x20 + 0x78];  // cellZ
			push dword ptr [esp + 0x24 + 0x74];  // cellX
			push dword ptr [esp + 0x28 + 0x10];  // trafficSimulator
			call SetConnections;
			add esp, 0x20;
			pop edx;  // restore
			pop ecx;  // restore
			pop eax;  // restore
			push DoConnectionsChanged_Continue;
			ret;
		}
	}

	constexpr uint32_t FindPath_Inject = 0x6d95b1;
	constexpr uint32_t FindPath_Continue = 0x6d95b6;

	void NAKED_FUN FindPath_Hook(void)
	{
		__asm {  // this branch is reached only for rail-type networks
			mov byte ptr [esp + 0x13], 0x1;  // set checkPreviousTripConnection = true (by default, this is only active for car)
			mov edi, 0x1;
			push FindPath_Continue;
			ret;
		}
	}

	constexpr uint32_t FullSetPathNodeConnection_Inject = 0x70eae7;
	constexpr uint32_t FullSetPathNodeConnection_Continue = 0x70eaf1;
	constexpr uint32_t FullSetPathNodeConnection_ContinueSkip = 0x70eb59;

	void NAKED_FUN FullSetPathNodeConnection_Hook(void)
	{
		__asm {
			push ecx;  // store
			push edx;  // store
			mov edx, 0x1;
			mov ecx, esi;  // travelMode
			shl edx, cl;
			test edx, multiLevelTravelModesMask;
			pop edx;  // restore
			pop ecx;  // restore
			jnz handleMultiLevel;
			push FullSetPathNodeConnection_ContinueSkip;
			ret;
handleMultiLevel:
			push FullSetPathNodeConnection_Continue;
			ret;
		}
	}
}

void PathJumping::Install()
{
	Patching::InstallHook(FindPath_Inject, FindPath_Hook);
	Patching::InstallHook(FullSetPathNodeConnection_Inject, FullSetPathNodeConnection_Hook);
	Patching::InstallHook(DoConnectionsChanged_Inject, DoConnectionsChanged_Hook);
}
