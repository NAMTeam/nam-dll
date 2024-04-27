#include "Rul2Engine.h"
#include "Patching.h"
#include <cstddef>
#include <iostream>
#include "cISC4NetworkOccupant.h"
#include "GZServPtrs.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4NetworkManager.h"
#include <Windows.h>
#include "wil/win32_helpers.h"
#include "EASTLConfigSC4.h"
#include "EASTL/vector.h"
#include <array>
#include <algorithm>

#ifdef __clang__
#define NAKED_FUN __attribute__((naked))
#else
#define NAKED_FUN __declspec(naked)
#endif

enum RotFlip : uint8_t { R0F0 = 0, R1F0 = 1, R2F0 = 2, R3F0 = 3, R0F1 = 0x80, R1F1 = 0x81, R2F1 = 0x82, R3F1 = 0x83 };

RotFlip rotate(RotFlip x, uint32_t rotation)
{
	return static_cast<RotFlip>((x + (0x1 | (x >> 6)) * (rotation & 0x3)) & 0x83);
}

RotFlip operator*(RotFlip x, RotFlip y)
{
	return static_cast<RotFlip>(rotate(x, y) ^ (y & 0x80));
}

struct Tile
{
	uint32_t id;
	RotFlip rf;
};
static_assert(sizeof(Tile) == 0x8);

struct tSolvedCell
{
	uint32_t id;
	RotFlip rf;
	uint32_t xz;
};
static_assert(sizeof(tSolvedCell) == 0xc);
static_assert(offsetof(tSolvedCell, xz) == 0x8);

std::ostream& operator<<(std::ostream& os, const tSolvedCell& t)
{
    return os << "0x" << std::hex << t.id << "," << (t.rf & 0xff) << ":(" << (t.xz & 0xffff) << "," << (t.xz >> 16) << ")";
}

class cSC4NetworkCellInfo
{
	public:
		intptr_t unknown[3];
		cISC4NetworkOccupant* networkOccupant;
		uint8_t unknown2[0x43];
		bool isImmovable;
		uint8_t unknown3[2];
		bool isNetworkLot;
		uint8_t unknown4[168-4-0x43];
		int32_t idxInCellsBuffer;
};
static_assert(offsetof(cSC4NetworkCellInfo, isImmovable) == 0x53);
static_assert(offsetof(cSC4NetworkCellInfo, isNetworkLot) == 0x56);
static_assert(offsetof(cSC4NetworkCellInfo, idxInCellsBuffer) == 0xb8);

class cSC4NetworkWorldCache
{
	public:
		intptr_t unknown[10];
};

namespace
{
	typedef cSC4NetworkCellInfo* (__thiscall* pfn_cSC4NetworkWorldCache_GetCell)(cSC4NetworkWorldCache* pThis, uint32_t xz);
	pfn_cSC4NetworkWorldCache_GetCell GetCell = reinterpret_cast<pfn_cSC4NetworkWorldCache_GetCell>(0x647a20);
}

class cSC4NetworkTool
{
	public:
		void* vtable;
		intptr_t unknown[6];
		cSC4NetworkWorldCache networkWorldCache;
};
static_assert(offsetof(cSC4NetworkTool, networkWorldCache) == 0x1c);

struct cSC4NetworkTileConflictRule
{
	Tile _1;
	Tile _2;
	Tile _3;
	Tile _4;
};
static_assert(sizeof(cSC4NetworkTileConflictRule) == 32);

struct OverrideRuleNode
{
	uint32_t unknown;  // red/black
	OverrideRuleNode* next;
	OverrideRuleNode* childLeft;
	OverrideRuleNode* childRight;
	uint32_t lookupId1;
	cSC4NetworkTileConflictRule rule;
};

struct MultiMapRange
{
	OverrideRuleNode* first;
	OverrideRuleNode* last;
};

namespace
{
	const int32_t kNextX[] = {-1, 0, 1, 0};
	const int32_t kNextZ[] = {0, -1, 0, 1};

	constexpr int32_t maxRepetitions = 100;

	OverrideRuleNode* const sTileConflictRules = *(reinterpret_cast<OverrideRuleNode**>(0xb466d0));

	enum Rul2PatchResult : uint32_t { NoMatch, Matched, Prevent };
	typedef Rul2PatchResult (__thiscall* pfn_cSC4NetworkTool_PatchTilePair)(cSC4NetworkTool* pThis, MultiMapRange const& range, tSolvedCell& cell1, tSolvedCell& cell2, int8_t dir);
	pfn_cSC4NetworkTool_PatchTilePair PatchTilePair = reinterpret_cast<pfn_cSC4NetworkTool_PatchTilePair>(0x6337e0);

	// equal range of multi map
	void tileConflictRulesForId(uint32_t id, MultiMapRange& range)
	{
		range.last = sTileConflictRules;
		OverrideRuleNode* node = sTileConflictRules->next;
		while (node) {
			if (id < node->lookupId1) {
				range.last = node;
				node = node->childLeft;
			} else {
				node = node->childRight;
			}
		}

		range.first = sTileConflictRules;
		node = sTileConflictRules->next;
		while (node) {
			if (id <= node->lookupId1) {
				range.first = node;
				node = node->childLeft;
			} else {
				node = node->childRight;
			}
		}
	}

	bool AdjustTileSubsets2(cSC4NetworkTool* networkTool, eastl::vector<tSolvedCell>& cellsBuffer)
	{
		if (sTileConflictRules == nullptr) {
			return true;  // success as RUL2 file was not yet loaded
		}

		int32_t countMatchesDown = cellsBuffer.size() * 8;  // 4 directions * {non-swapped,swapped}
		if (countMatchesDown <= maxRepetitions) {
			countMatchesDown = maxRepetitions;
		}

		if (cellsBuffer.empty()) {
			return true;
		}
		tSolvedCell* cell = &(cellsBuffer[0]);
		bool foundMatch = false;

		int32_t countPatchesCurrentCell = 0;
		while (true) {
mainLoop:
			MultiMapRange range;
			tileConflictRulesForId(cell->id, range);

			if (countPatchesCurrentCell <= maxRepetitions) {
				for (uint32_t dir = 0; dir < 4; dir++) {
					uint32_t z = cell->xz >> 16;
					uint32_t x = cell->xz & 0xffff;
					uint32_t nextCellXZ = (kNextZ[dir] + z) * 0x10000 + (kNextX[dir] + x);

					cSC4NetworkCellInfo* cellInfo = GetCell(&(networkTool->networkWorldCache), nextCellXZ);
					if (cellInfo == nullptr) {
						continue;  // next direction
					}

					tSolvedCell temp;
					tSolvedCell* cell2 = nullptr;
					bool isCell2StackLocal = false;

					if (cellInfo->idxInCellsBuffer < 0) {
						cISC4NetworkOccupant* networkOccupant = cellInfo->networkOccupant;
						if (networkOccupant == nullptr) {
							continue;  // next direction
						}
						cell2 = &temp;
						temp.xz = nextCellXZ;
						temp.id = networkOccupant->PieceId();
						temp.rf = static_cast<RotFlip>(networkOccupant->GetRotationAndFlip());
						isCell2StackLocal = true;
					} else {
						cell2 = &(cellsBuffer[cellInfo->idxInCellsBuffer]);
						if (cell2 == nullptr) {
							continue;  // next direction
						}
					}
					// now cell2 is not nullptr

					if (cellInfo->isImmovable || cellInfo->isNetworkLot) {
						if (!isCell2StackLocal) {
							temp = *cell2;
							cell2 = &temp;
						}
						temp.id = 0;
						isCell2StackLocal = true;
					}

					if (cell2 == nullptr) {
						continue;  // next direction
					}

					Rul2PatchResult patchResult = PatchTilePair(networkTool, range, *cell, *cell2, dir);  // non-swapped evaluation

					if (patchResult == NoMatch) {
						if (!isCell2StackLocal) {  // if cell2 is not new and hence is queued in buffer, we will process it from cell2's point of view anyway
							continue;  // next direction
						}
						MultiMapRange rangeSwapped;
						tileConflictRulesForId(cell2->id, rangeSwapped);
						patchResult = PatchTilePair(networkTool, rangeSwapped, *cell2, *cell, (dir - 2) & 3);  // swapped evaluation
						if (patchResult == NoMatch) {
							continue;  // next direction
						}
					}

					if (patchResult == Prevent) {
						return false; // Prevent
					}
					// Matched and no Prevent

					countMatchesDown--;
					if (countMatchesDown < 0) {
						return false;  // Prevent
					}

					foundMatch = true;

					if (isCell2StackLocal) {  // cell is not in buffer
						uint32_t idx = cell - &(cellsBuffer.front());
						cellInfo->idxInCellsBuffer = cellsBuffer.size();
						cellsBuffer.push_back(*cell2);
						cell = &(cellsBuffer[idx]);  // push_back might have triggered reallocation of the cells, so we retrieve the current address again
					}

					countPatchesCurrentCell++;
					goto mainLoop;
				}
			}

			cell = cell + 1;
			countPatchesCurrentCell = 0;
			if (cell != &(cellsBuffer.front()) + cellsBuffer.size()) {  // if not reached end
				continue;  // main loop
			} else if (foundMatch) {  // reached end, but also foundMatch, so continue until all cells remain unchanged
				cell = &(cellsBuffer[0]);
				foundMatch = false;
				continue;  // main loop
			} else {
				return true;
			}
		}
	}

	constexpr uint32_t AdjustTileSubsets_InjectPoint = 0x634d79;
	constexpr uint32_t AdjustTileSubsets_Return = 0x635282;

	void NAKED_FUN Hook_AdjustTileSubsets(void)
	{
		__asm {
			push ecx;  // store
			push edx;  // store
			mov edx, dword ptr [ebp + 0x8];  // cellsBuffer
			push edx;  // cellsBuffer
			push ecx;  // networkTool (this)
			call AdjustTileSubsets2;  // (cdecl)
			add esp, 0x08;
			pop edx;  // restore
			pop ecx;  // restore
			// al is true or false
			push AdjustTileSubsets_Return;
			ret;
		}
	}

}

void Rul2Engine::Install()
{
	// sanity check
	// const std::array<RotFlip, 8> rotFlipValues = {R0F0, R1F0, R2F0, R3F0, R0F1, R3F1, R2F1, R1F1};
	// const std::array<int, 8> rotFlipIndexes = {0,1,2,3,4,5,6,7};
	// const int multiplicationTable[8][8] = {
	// 	{0,1,2,3,4,5,6,7},
	// 	{1,2,3,0,7,4,5,6},
	// 	{2,3,0,1,6,7,4,5},
	// 	{3,0,1,2,5,6,7,4},
	// 	{4,5,6,7,0,1,2,3},
	// 	{5,6,7,4,3,0,1,2},
	// 	{6,7,4,5,2,3,0,1},
	// 	{7,4,5,6,1,2,3,0}
	// };
	// bool passed = std::all_of(rotFlipIndexes.cbegin(), rotFlipIndexes.cend(), [&rotFlipValues, &rotFlipIndexes, &multiplicationTable](int i) {
	// 	return std::all_of(rotFlipIndexes.cbegin(), rotFlipIndexes.cend(), [&rotFlipValues, &multiplicationTable, &i](int j) {
	// 		return rotFlipValues[i] * rotFlipValues[j] == rotFlipValues[multiplicationTable[i][j]];
	// 	});
	// });
	// if (!passed) {
	// 	std::cout << "RotFlip sanity check failed.\n";
	// }

	Patching::InstallHook(AdjustTileSubsets_InjectPoint, Hook_AdjustTileSubsets);
}
