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
#include "SC4Vector.h"
#include <vector>
#include <array>
#include <algorithm>
#include "cSC4NetworkTileConflictRule.h"
#include "NetworkStubs.h"
#include <unordered_set>
#include "RuleEquivalence.h"
#include <utility>

std::ostream& operator<<(std::ostream& os, const cSC4NetworkTool::tSolvedCell& t)
{
    return os << "0x" << std::hex << t.id << "," << (t.rf & 0xff) << ":(" << (t.xz & 0xffff) << "," << (t.xz >> 16) << ")";
}

struct OverrideRuleNode
{
	uint32_t RESERVED;  // red/black
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
	constexpr int32_t maxRepetitions = 100;

	// OverrideRuleNode* const sTileConflictRules = *(reinterpret_cast<OverrideRuleNode**>(0xb466d0));
	std::unordered_set<cSC4NetworkTileConflictRule, RuleEquivalenceHash, RuleEquivalence> sTileConflictRules2 = {};

	enum Rul2PatchResult : uint32_t { NoMatch, Matched, Prevent };
	typedef Rul2PatchResult (__thiscall* pfn_cSC4NetworkTool_PatchTilePair)(cSC4NetworkTool* pThis, MultiMapRange const& range, cSC4NetworkTool::tSolvedCell& cell1, cSC4NetworkTool::tSolvedCell& cell2, int8_t dir);
	pfn_cSC4NetworkTool_PatchTilePair PatchTilePair = reinterpret_cast<pfn_cSC4NetworkTool_PatchTilePair>(0x6337e0);

	void addRuleOverride(cSC4NetworkTileConflictRule* rule) {
		if (rule->_2.id != 0) {  // we don't check _1.id != 0 as vanilla doesn't do that either, presumably
			sTileConflictRules2.insert(*rule);
		} else {
			// For the (few) overrides with 0 in 2nd tile (e.g. next to bridges), we add all rotations, to simplify lookup.
			// (TODO A different solution would special-case the implementation of RuleEquivalence to handle ID 0, but that would be more complex.)
			for (const auto rf : rotFlipValues) {
				cSC4NetworkTileConflictRule tmpRule = *rule;
				tmpRule._2.rf = rf;
				sTileConflictRules2.insert(tmpRule);
			}
		}
	}

	// Lookup an override rule matching the two tiles and apply it if it exists.
	Rul2PatchResult PatchTilePair2(cSC4NetworkTool::tSolvedCell& cell1, cSC4NetworkTool::tSolvedCell& cell2, int8_t dir)
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

	const std::vector<Tile> orthogonalSurrogateTiles = {
		{0x00004B00, R1F0},  // Road
		{0x57000000, R1F0},  // Dirtroad
		{0x05004B00, R1F0},  // Street
		{0x5D540000, R1F0},  // Rail
		{0x08031500, R1F0},  // Lightrail
		{0x09004B00, R1F0},  // Onewayroad
		{0x04006100, R1F0},  // Avenue
		{0x0D031500, R1F0},  // Monorail
		{0x02001500, R0F0},  // Highway
		{0x0A001500, R0F0},  // Groundhighway
	};

	const std::vector<std::pair<Tile, Tile>> diagonalSurrogateTiles = {  // diagonals in west-south direction on first tile, north-east on second tile
		std::make_pair<Tile, Tile>({0x00000A00, R1F0}, {0x00000A00, R3F0}),  // Road
		std::make_pair<Tile, Tile>({0x57000200, R1F0}, {0x57000200, R3F0}),  // Dirtroad
		std::make_pair<Tile, Tile>({0x5F500200, R1F0}, {0x5F500200, R3F0}),  // Street
		std::make_pair<Tile, Tile>({0x5D540100, R1F0}, {0x5D540100, R3F0}),  // Rail
		std::make_pair<Tile, Tile>({0x08001A00, R1F0}, {0x08001A00, R3F0}),  // Lightrail
		std::make_pair<Tile, Tile>({0x09000A00, R1F0}, {0x09000A00, R3F0}),  // Onewayroad
		std::make_pair<Tile, Tile>({0x04000200, R2F0}, {0x04003800, R0F0}),  // Avenue~SW | Avenue~SharedDiagLeft (we don't need 0x04003800,R2F0 as this duplication should already be part of the RUL2 file)
		std::make_pair<Tile, Tile>({0x04003800, R0F0}, {0x04000200, R0F0}),  // Avenue~SharedDiagLeft | Avenue~NE
		std::make_pair<Tile, Tile>({0x0D001A00, R1F0}, {0x0D001A00, R3F0}),  // Monorail
		std::make_pair<Tile, Tile>({0x02002200, R1F0}, {0x02002100, R1F0}),  // Highway~SW | Highway~SharedDiagLeft
		std::make_pair<Tile, Tile>({0x02002100, R1F0}, {0x02002200, R3F0}),  // Highway~SharedDiagLeft | Highway~NE
		std::make_pair<Tile, Tile>({0x0A002200, R1F0}, {0x0A002100, R1F0}),  // Groundhighway~SW | Groundhighway~SharedDiagLeft
		std::make_pair<Tile, Tile>({0x0A002100, R1F0}, {0x0A002200, R3F0}),  // Groundhighway~SharedDiagLeft | Groundhighway~NE
	};

	// Try to find a surrogate tile that fits between the two tiles with two suitable override rules.
	// The override is then applied from the first to the last tile.
	// This avoids the need for direct adjacencies between the two tiles.
	// For diagonals, this employs two surrogate tiles instead of one.
	Rul2PatchResult tryAdjacencies(cSC4NetworkTool::tSolvedCell& cell1, cSC4NetworkTool::tSolvedCell& cell2, int8_t dir)
	{
		for (auto&& surrogate : orthogonalSurrogateTiles) {
			for (auto&& opposite : {false, true}) {
				cSC4NetworkTool::tSolvedCell a = cell1;
				cSC4NetworkTool::tSolvedCell b = {surrogate.id, relativeToAbsolute(surrogate.rf, opposite ? dir+2 : dir), 0xffffffff};
				cSC4NetworkTool::tSolvedCell c = cell2;

				Rul2PatchResult result = PatchTilePair2(a, b, dir);
				if (result != Matched ||
					a.id != cell1.id || a.rf != cell1.rf ||  // a must remain unchanged for a proper adjacency
					a.id == b.id) {  // a must not be an orthogonal override network
					// TODO also check that b has changed?
					continue;  // next surrogate tile
				}
				cSC4NetworkTool::tSolvedCell bBackup = b;

				result = PatchTilePair2(b, c, dir);
				if (result != Matched ||
					b.id != bBackup.id || b.rf != bBackup.rf ||  //  b must remain unchanged (in 2nd override) for a proper adjacency
					b.id == c.id ||  // c must not be an orthogonal override network
					c.id == cell2.id && c.rf == cell2.rf) {  // c must change (in 2nd override) for a proper adjacency
					continue;  // next surrogate tile
				}

				cell1 = a;
				cell2 = c;
				return Matched;
			}
		}

		for (auto&& surrogatePair : diagonalSurrogateTiles) {
			for (auto&& southBound : {true, false}) {
				cSC4NetworkTool::tSolvedCell a = cell1;
				cSC4NetworkTool::tSolvedCell b = {surrogatePair.first.id, relativeToAbsolute(surrogatePair.first.rf, southBound ? dir : dir+1), 0xffffffff};
				cSC4NetworkTool::tSolvedCell c = {surrogatePair.second.id, relativeToAbsolute(surrogatePair.second.rf, southBound ? dir : dir+1), 0xffffffff};
				cSC4NetworkTool::tSolvedCell d = cell2;

				Rul2PatchResult result = PatchTilePair2(a, b, dir);
				if (result != Matched ||
					a.id != cell1.id || a.rf != cell1.rf ||  // a must remain unchanged for a proper adjacency
					a.id == b.id) {  // a must not be a straight diagonal override network
					continue;
				}
				cSC4NetworkTool::tSolvedCell bBackup = b;

				// Assuming dir == 2, then c is south of b if southBound (dir+1) or north of b if northBound (dir-1).
				result = PatchTilePair2(b, c, (dir + (southBound ? 1 : -1)) & 3);
				if (result != Matched ||
					b.id != bBackup.id || b.rf != bBackup.rf ||  // b must remain unchanged (in 2nd override) for a proper adjacency
					c.id == cell1.id && c.rf == cell1.rf) {  // otherwise we haven't gone anywhere
					continue;
				}
				cSC4NetworkTool::tSolvedCell cBackup = c;

				result = PatchTilePair2(c, d, dir);
				if (result != Matched ||
					c.id != cBackup.id || c.rf != cBackup.rf ||  // c must remain unchanged (in 3rd override) for a proper adjacency
					c.id == d.id || b.id == d.id ||  // d must not be a pure diagonal
					d.id == cell2.id && d.rf == cell2.rf) {  // d must change (in 3rd override) for a proper adjacency
					continue;
				}

				cell1 = a;
				cell2 = d;
				return Matched;
			}
		}

		return NoMatch;
	}

	bool AdjustTileSubsets2(cSC4NetworkTool* networkTool, SC4Vector<cSC4NetworkTool::tSolvedCell>& cellsBuffer)
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
		cSC4NetworkTool::tSolvedCell* cell = cellsBuffer.begin();
		bool foundMatch = false;

		int32_t countPatchesCurrentCell = 0;
		while (true) {
mainLoop:
			if (countPatchesCurrentCell <= maxRepetitions) {
				for (uint32_t dir = 0; dir < 4; dir++) {
					uint32_t z = cell->xz >> 16;
					uint32_t x = cell->xz & 0xffff;
					uint32_t nextCellXZ = (kNextZ[dir] + z) * 0x10000 + (kNextX[dir] + x);

					cSC4NetworkCellInfo* cell2Info = networkTool->networkWorldCache.GetCell(nextCellXZ);
					if (cell2Info == nullptr) {
						continue;  // next direction
					}

					cSC4NetworkTool::tSolvedCell temp;
					cSC4NetworkTool::tSolvedCell* cell2 = nullptr;
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
						cell2 = cellsBuffer.begin() + (cell2Info->idxInCellsBuffer);
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
						uint32_t idx = cell - cellsBuffer.begin();
						cell2Info->idxInCellsBuffer = cellsBuffer.size();
						cellsBuffer.push_back(*cell2);
						cell = cellsBuffer.begin() + idx;  // push_back might have triggered reallocation of the cells, so we retrieve the current address again
					}

					countPatchesCurrentCell++;
					goto mainLoop;
				}
			}

			cell = cell + 1;
			countPatchesCurrentCell = 0;
			if (cell != cellsBuffer.begin() + cellsBuffer.size()) {  // if not reached end
				continue;  // main loop
			} else if (foundMatch) {  // reached end, but also foundMatch, so continue until all cells remain unchanged
				cell = cellsBuffer.begin();
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
	// sTileConflictRules2.reserve(4500 * 1000);  // TODO
	Patching::InstallHook(AdjustTileSubsets_InjectPoint, Hook_AdjustTileSubsets);
	Patching::InstallHook(AddRuleOverrides_InjectPoint, Hook_AddRuleOverrides);
}
