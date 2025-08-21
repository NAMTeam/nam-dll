#include "FlexPieces.h"
#include "Patching.h"
#include "NetworkStubs.h"

namespace
{

	void handleFlexPieceRul0(cSC4NetworkTool* networkTool, uint32_t x, uint32_t z, nSC4Networks::cIntRule &rule)
	{
		SC4Point<uint32_t> dummyCell = {x, z};  // simulates a 1×1-cell drag when placing the puzzle piece
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
			mov ecx, dword ptr [esp + 0x78 + 0xc];
			push ecx;  // cIntRule
			push edx;  // z
			push ebx;  // x
			push esi;  // networkTool
			call handleFlexPieceRul0;  // (cdecl)
			add esp, 0x10;
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
	Patching::OverwriteMemory((void*)0x6099c4, (uint32_t)0);  // network type at origin = Road as fallback
}
