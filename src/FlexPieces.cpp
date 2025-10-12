#include "FlexPieces.h"
#include "Patching.h"
#include "NetworkStubs.h"
#include "Logger.h"
#include <limits>
#include <tuple>

namespace
{
	constexpr uint32_t vclAutoTileBase = 0x55387000;

	constexpr uint32_t cacheSize = 37;  // prime > 32 = typical tab-loop rotation count
	std::tuple<uint32_t, SC4Point<int32_t>, cISC4NetworkOccupant::eNetworkType> sHidOriginCache[cacheSize];
	std::tuple<uint32_t, SC4Point<int32_t>, cISC4NetworkOccupant::eNetworkType>* getCachedOrigin(uint32_t hid) {
		auto item = &sHidOriginCache[hid % cacheSize];
		return std::get<0>(*item) == hid ? item : nullptr;
	}
	void putCachedOrigin(uint32_t hid, SC4Point<int32_t> origin, cISC4NetworkOccupant::eNetworkType networkAtOrigin) {
		sHidOriginCache[hid % cacheSize] = std::tuple(hid, origin, networkAtOrigin);
	}

	void handleFlexPieceRul0(cSC4NetworkTool* networkTool, uint32_t x, uint32_t z, nSC4Networks::cIntRule &rule, cISC4NetworkOccupant::eNetworkType &networkAtOrigin)
	{
		// First, remove static cell from FLEX pieces
		if (rule.autoTileBase == vclAutoTileBase && rule.staticCells.size() == 1 && rule.checkCells.size() > 1) {  // FLEX piece different from Eraser
			// remove static cells from checkCells
			for (auto sc = rule.staticCells.begin(); sc != rule.staticCells.end(); sc++) {
				for (auto cc = rule.checkCells.begin(); cc != rule.checkCells.end(); ) {
					if (sc->x == cc->cell.x && sc->y == cc->cell.y) {
						cc = rule.checkCells.erase(cc);
					} else {
						cc++;
					}
				}
				// TODO consider erasing static cell from rule.checkTypes as well
			}
			rule.staticCells.clear();  // non-optional tiles
			rule.unnamedStaticCells.clear();  // `+`-letter tiles
			for (auto pIdx = rule.constraints.begin(); pIdx != rule.constraints.end(); pIdx++) {
				*pIdx = 0;  // no slope constraints
			}
			for (auto pIdx = rule.autoTileIndices.begin(); pIdx != rule.autoTileIndices.end(); pIdx++) {
				*pIdx = 0;  // no static auto-tile pieces
			}
		}

		// Next, fix handle-offset if puzzle piece origin does not point to a cell (e.g. when the static cell at FLEX piece origin has been removed)
		SC4Point<int32_t> origin = {0, 0};
		auto cachedItem = getCachedOrigin(rule.hid);
		if (cachedItem != nullptr) {
			origin = std::get<1>(*cachedItem);
			networkAtOrigin = std::get<2>(*cachedItem);
		} else {
			bool foundOrigin = false;
			for (auto cc = rule.checkCells.begin(); cc != rule.checkCells.end(); cc++) {
				if (cc->cell.x == origin.x && cc->cell.y == origin.y) {
					foundOrigin = true;
					putCachedOrigin(rule.hid, origin, networkAtOrigin);
					break;
				}
			}
			if (!foundOrigin) {
				// pick a different cell as new origin (one that is close to the origin, as this cell will not leave the map boundaries)
				int32_t bestDist = std::numeric_limits<int32_t>::max();
				for (auto cc = rule.checkCells.begin(); cc != rule.checkCells.end(); cc++) {
					if (auto item = rule.checkTypes.find(cc->letter); item != rule.checkTypes.end()) {
						int32_t dist = std::abs(cc->cell.x) + std::abs(cc->cell.y);
						if (dist < bestDist) {
							bestDist = dist;
							origin.x = cc->cell.x;
							origin.y = cc->cell.y;
							networkAtOrigin = static_cast<cISC4NetworkOccupant::eNetworkType>(item->second.networks[0]);
							putCachedOrigin(rule.hid, origin, networkAtOrigin);
						}
					}
				}
				if (origin.x == 0 && origin.y == 0) {  // something's wrong about this RUL0 entry
					Logger& logger = Logger::GetInstance();
					logger.WriteLineFormatted(LogLevel::Error, "Failed to fix the handle-offset of RUL0 HID 0x%08X due to unexpected CheckTypes or CellLayout.", rule.hid);
				}
			}
		}

		SC4Point<uint32_t> dummyCell = {x + origin.x, z + origin.y};  // simulates a 1×1-cell drag when placing the puzzle piece (now with the origin offset, it points to a cell contained in the puzzle piece)
		if (dummyCell.x >= networkTool->numCellsX || dummyCell.y >= networkTool->numCellsZ) {
			dummyCell = {x, z};  // at city boundary, revert to original origin within city bounds to avoid sporadic crash (in the worst case, this results in a red PP cursor or an unnecessary 1×1 stub at origin)
		}
		networkTool->draggedCells.clear();
		networkTool->draggedCells.push_back(dummyCell);
	}

	constexpr uint32_t InsertIsolatedHighwayIntersection_InjectPoint = 0x62cedc;
	constexpr uint32_t InsertIsolatedHighwayIntersection_ReturnJump = 0x62cf42;

	void NAKED_FUN Hook_InsertIsolatedHighwayIntersection(void)
	{
		__asm {
			push eax;  // store
			push ecx;  // store
			push edx;  // store
			lea eax, dword ptr [esp + 0x20 + 0xc];
			mov ecx, dword ptr [esp + 0x78 + 0xc];
			push eax;  // &networkAtOrigin
			push ecx;  // cIntRule
			push edx;  // z
			push ebx;  // x
			push esi;  // networkTool
			call handleFlexPieceRul0;  // (cdecl)
			add esp, 0x14;
			pop edx;  // restore
			pop ecx;  // restore
			pop eax;  // restore
			push InsertIsolatedHighwayIntersection_ReturnJump;
			ret;
		}
	}
}

void FlexPieces::Install()
{
	Patching::InstallHook(InsertIsolatedHighwayIntersection_InjectPoint, Hook_InsertIsolatedHighwayIntersection);

	// Mainly for debugging, set fallback for network type at origin to Road instead of Highway,
	// as Highway does not support placing single 1×1 tiles, leading to a red puzzle piece cursor
	// in case something in this patch is wrong.
	Patching::OverwriteMemory((void*)0x6099c4, (uint32_t)cISC4NetworkOccupant::eNetworkType::Road);  // network type at origin = Road as fallback
}
