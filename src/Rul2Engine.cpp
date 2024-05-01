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
#include <vector>
#include <array>
#include <algorithm>
#include "cSC4NetworkTileConflictRule.h"
#include <unordered_set>
#include "RuleEquivalence.h"

#ifdef __clang__
#define NAKED_FUN __attribute__((naked))
#else
#define NAKED_FUN __declspec(naked)
#endif

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

	// OverrideRuleNode* const sTileConflictRules = *(reinterpret_cast<OverrideRuleNode**>(0xb466d0));
	std::unordered_set<cSC4NetworkTileConflictRule, RuleEquivalenceHash, RuleEquivalence> sTileConflictRules2 = {};

	enum Rul2PatchResult : uint32_t { NoMatch, Matched, Prevent };
	typedef Rul2PatchResult (__thiscall* pfn_cSC4NetworkTool_PatchTilePair)(cSC4NetworkTool* pThis, MultiMapRange const& range, tSolvedCell& cell1, tSolvedCell& cell2, int8_t dir);
	pfn_cSC4NetworkTool_PatchTilePair PatchTilePair = reinterpret_cast<pfn_cSC4NetworkTool_PatchTilePair>(0x6337e0);

	void addRuleOverride(cSC4NetworkTileConflictRule* rule) {
		sTileConflictRules2.insert(*rule);
	}

	// Lookup an override rule matching the two tiles and apply it if it exists.
	Rul2PatchResult PatchTilePair2(tSolvedCell& cell1, tSolvedCell& cell2, int8_t dir)
	{
		// First we need to convert the absolute rotations of cell1 and cell2 to the relative rotations of RUL2:
		// dir = 0 (cell2 is west of cell1)
		// dir = 1 (cell2 is north of cell1)
		// dir = 2 (cell2 is east of cell1) (this is the case we usually think of when writing RUL2)
		// dir = 3 (cell2 is south of cell1)
		cSC4NetworkTileConflictRule dummy = {cell1.id, absoluteToRelative(cell1.rf, dir), cell2.id, absoluteToRelative(cell2.rf, dir)};  // tile 3 and 4 uninitialized
		const auto pRule = sTileConflictRules2.find(dummy);
		if (pRule == sTileConflictRules2.end()) {
			return NoMatch;
		} else if (pRule->_3.id == 0) {
			return Prevent;
		} else {
			const cSC4NetworkTileConflictRule& rule = *pRule;
			Tile& a = dummy._1;
			Tile& b = dummy._2;
			// Here we check not only `a.rf` but also `b.rf` as this could be relevant if `a.id == b.id`.
			// If `b.id == 0`, its rotation does not matter, which is used for overriding tiles adjacent to bridges for example.
			if (a.id == rule._1.id && a.rf == rule._1.rf && (b.rf == rule._2.rf || b.id == 0)) {
				// case R0F0
				cell1.id = rule._3.id; cell1.rf = relativeToAbsolute(rule._3.rf, dir);
				cell2.id = rule._4.id; cell2.rf = relativeToAbsolute(rule._4.rf, dir);
				return Matched;
			} else if (a.id == rule._1.id && a.rf == flipVertically(rule._1.rf) && (b.rf == flipVertically(rule._2.rf) || b.id == 0)) {
				// case R2F1
				cell1.id = rule._3.id; cell1.rf = relativeToAbsolute(flipVertically(rule._3.rf), dir);
				cell2.id = rule._4.id; cell2.rf = relativeToAbsolute(flipVertically(rule._4.rf), dir);
				return Matched;
			} else if (a.id == rule._2.id && a.rf == rotate180(rule._2.rf) && (b.rf == rotate180(rule._1.rf) || b.id == 0)) {
				// case R2F0
				cell1.id = rule._4.id; cell1.rf = relativeToAbsolute(rotate180(rule._4.rf), dir);
				cell2.id = rule._3.id; cell2.rf = relativeToAbsolute(rotate180(rule._3.rf), dir);
				return Matched;
			} else if (a.id == rule._2.id && a.rf == flipHorizontally(rule._2.rf) && (b.rf == flipHorizontally(rule._1.rf) || b.id == 0)) {
				// case R0F1
				cell1.id = rule._4.id; cell1.rf = relativeToAbsolute(flipHorizontally(rule._4.rf), dir);
				cell2.id = rule._3.id; cell2.rf = relativeToAbsolute(flipHorizontally(rule._3.rf), dir);
				return Matched;
			} else {
				return NoMatch;  // should not happen
			}
		}
	}

	constexpr uint32_t AddRuleOverrides_InjectPoint = 0x63d316;
	constexpr uint32_t AddRuleOverrides_Return = 0x63d33c;

	void NAKED_FUN Hook_AddRuleOverrides(void)
	{
		__asm {
			push eax;  // store
			push ecx;  // store
			push edx;  // store
			lea edx, [esp + 0x28 + 0xc];  // rule
			push edx;  // rule
			call addRuleOverride;  // (cdecl)
			add esp, 0x4;
			pop edx;  // restore
			pop ecx;  // restore
			pop eax;  // restore
			push AddRuleOverrides_Return;
			ret;
		}
	}

	const std::vector<Tile> adjacencySurrogateTiles = {
		{0x00004B00, R1F0}, {0x00004B00, R3F0},  // Road
		{0x57000000, R1F0}, {0x57000000, R3F0},  // Dirtroad
		{0x05004B00, R1F0}, {0x05004B00, R3F0},  // Street
		{0x5D540000, R1F0}, {0x5D540000, R3F0},  // Rail
		{0x08031500, R1F0}, {0x08031500, R3F0},  // Lightrail
		{0x09004B00, R1F0}, {0x09004B00, R3F0},  // Onewayroad  (TODO or 0x5f940300?)
		{0x04006100, R3F0}, {0x04006100, R1F0},  // Avenue
		{0x0D031500, R1F0}, {0x0D031500, R3F0},  // Monorail
	};

	// Try to find a surrogate tile that fits between the two tiles with two suitable override rules.
	// The override is then applied from the first to the last tile.
	// This avoids the need for direct adjacencies between the two tiles.
	Rul2PatchResult tryAdjacencies(tSolvedCell& cell0, tSolvedCell& cell2, int8_t dir)
	{
		for (auto&& surrogate : adjacencySurrogateTiles) {
			tSolvedCell x = cell0;
			tSolvedCell y = {surrogate.id, relativeToAbsolute(surrogate.rf, dir), 0};
			tSolvedCell z = cell2;

			Rul2PatchResult result = PatchTilePair2(x, y, dir);
			if (result != Matched) {
				continue;  // next surrogate tile
			}
			// result == Matched, and x and y have potentially been modifed
			if (x.id != cell0.id || x.rf != cell0.rf ||  // x must remain unchanged for a proper adjacency
				x.id == y.id) {  // x must not be an orthogonal override network
				// TODO also check that y has changed?
				continue;
			}
			auto yIdNew = y.id;
			auto yRfNew = y.rf;

			result = PatchTilePair2(y, z, dir);
			if (result != Matched) {
				continue;  // next surrogate tile
			}
			// result == Matched, and y and z have potentially been modified
			if (y.id != yIdNew || y.rf != yRfNew ||  //  y must remain unchanged (in 2nd override) for a proper adjacency
				y.id == z.id ||  // z must not be an orthogonal override network
				z.id == cell2.id && z.rf == cell2.rf) {  // z must change (in 2nd override) for a proper adjacency
				continue;
			}

			cell0 = x;
			cell2 = z;
			return Matched;
		}
		return NoMatch;
	}

	bool AdjustTileSubsets2(cSC4NetworkTool* networkTool, eastl::vector<tSolvedCell>& cellsBuffer)
	{
		// if (sTileConflictRules == nullptr) {
		// 	return true;  // success as RUL2 file was not yet loaded
		// }

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
			if (countPatchesCurrentCell <= maxRepetitions) {
				for (uint32_t dir = 0; dir < 4; dir++) {
					uint32_t z = cell->xz >> 16;
					uint32_t x = cell->xz & 0xffff;
					uint32_t nextCellXZ = (kNextZ[dir] + z) * 0x10000 + (kNextX[dir] + x);

					cSC4NetworkCellInfo* cell2Info = GetCell(&(networkTool->networkWorldCache), nextCellXZ);
					if (cell2Info == nullptr) {
						continue;  // next direction
					}

					tSolvedCell temp;
					tSolvedCell* cell2 = nullptr;
					bool isCell2StackLocal = false;

					if (cell2Info->idxInCellsBuffer < 0) {
						cISC4NetworkOccupant* networkOccupant = cell2Info->networkOccupant;
						if (networkOccupant == nullptr) {
							continue;  // next direction
						}
						cell2 = &temp;
						temp.xz = nextCellXZ;
						temp.id = networkOccupant->PieceId();
						temp.rf = static_cast<RotFlip>(networkOccupant->GetRotationAndFlip());
						isCell2StackLocal = true;
					} else {
						cell2 = &(cellsBuffer[cell2Info->idxInCellsBuffer]);
						if (cell2 == nullptr) {
							continue;  // next direction
						}
					}
					// now cell2 is not nullptr

					if (cell2Info->isImmovable || cell2Info->isNetworkLot) {
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

					Rul2PatchResult patchResult = PatchTilePair2(*cell, *cell2, dir);

					if (patchResult == NoMatch) {
						patchResult = tryAdjacencies(*cell, *cell2, dir);  // cell -> surrogate -> cell2
						if (patchResult != Matched) {  // potential Prevents from adjacencies are discarded
							if (isCell2StackLocal) {  // otherwise, cell2 is queued in buffer, so we will eventually process it from cell2's point of view anyway
								patchResult = tryAdjacencies(*cell2, *cell, (dir - 2) & 3);  // cell2 -> surrogate -> cell
							}
							if (patchResult != Matched) {
								continue;  // next direction
							}
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
						cell2Info->idxInCellsBuffer = cellsBuffer.size();
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

	// sTileConflictRules2.reserve(4500 * 1000);  // TODO
	Patching::InstallHook(AdjustTileSubsets_InjectPoint, Hook_AdjustTileSubsets);
	Patching::InstallHook(AddRuleOverrides_InjectPoint, Hook_AddRuleOverrides);
}
