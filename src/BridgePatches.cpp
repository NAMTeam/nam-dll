#include "BridgePatches.h"
#include "Patching.h"
#include "NetworkStubs.h"
#include "cISTETerrain.h"

// IdentifyBridgeCells (0x632830) looks for the first water step, starting at step 2. The step before
// it is the near abutment. SelectBridgeStartAndEnd (0x630570) then checks each step over the water
// until it finds a dry step, which becomes the far abutment.
namespace
{
	// bool IsStepGoodForBridge(cISC4OccupantFilter*, int x1, int z1, int x2, int z2)
	// cdecl, with the occupant manager in esi
	constexpr uint32_t IsStepGoodForBridge = 0x62fdb0;

	typedef bool (__thiscall* pfn_CheckBridgeStepEdgeTypes)(cSC4NetworkTool* pThis, int32_t step, int32_t networkType);
	const auto CheckBridgeStepEdgeTypes = reinterpret_cast<pfn_CheckBridgeStepEdgeTypes>(0x62eb80);

	bool sShoreAbutmentInstalled = false;

	bool isWaterStep(const cSC4NetworkTool* networkTool, int32_t step)
	{
		const cSC4NetworkTool::tDraggedStep draggedStep = networkTool->draggedSteps[step];
		for (uint32_t i = draggedStep.firstCellIndex; i <= draggedStep.lastCellIndex; i++) {
			const SC4Point<uint32_t> cell = networkTool->draggedCells[i];
			if (networkTool->terrain->CellHasWater(cell.x, cell.y)) {
				return true;
			}
		}
		return false;
	}

	// true if the drag ends on the first dry step, which then stays a normal network tile
	bool isShoreStubStep(const cSC4NetworkTool* networkTool, int32_t step)
	{
		return sShoreAbutmentInstalled
			&& networkTool->terrain != nullptr
			&& step == networkTool->validStepCount - 1
			&& !isWaterStep(networkTool, step);
	}

	// Shore abutment patch
	//
	// The far abutment must come before the last dragged step (step < validStepCount - 1), so a drag
	// had to go one tile past the far bank. This check appears twice in SelectBridgeStartAndEnd, so we
	// nop both `dec`s. If the drag ends on the first dry step, the far abutment is moved back one step
	// to the shore and the dry step stays a normal network tile, like step 0 on the near side.
	constexpr uint32_t SelectBridgeStartAndEnd_DecValidSteps = 0x6305b2;
	constexpr uint32_t SelectBridgeStartAndEnd_LoopDecValidSteps = 0x6306b2;
	constexpr uint32_t SelectBridgeStartAndEnd_CheckBridgeStepEdgeTypesCall = 0x630616;
	constexpr uint32_t SelectBridgeStartAndEnd_FarAbutmentFound = 0x630662;
	constexpr uint32_t SelectBridgeStartAndEnd_FarAbutmentFound_Continue = 0x630667;

	bool __fastcall Hook_SelectBridgeStartAndEnd_CheckBridgeStepEdgeTypes(cSC4NetworkTool* pThis, void* edx, int32_t step, int32_t networkType)
	{
		if (isShoreStubStep(pThis, step)) {
			return true;
		}
		return CheckBridgeStepEdgeTypes(pThis, step, networkType);
	}

	void NAKED_FUN Hook_SelectBridgeStartAndEnd_FarAbutmentFound(void)
	{
		// ebx: int32_t* end, edi: cSC4NetworkTool*
		__asm {
			mov edx, dword ptr [edi + 0x78];  // validStepCount
			dec edx;
			cmp dword ptr [ebx], edx;
			jne notLastStep;
			dec dword ptr [ebx];  // move the far abutment back to the shore

notLastStep:
			mov edx, dword ptr [ebx];  // replaced instructions
			cmp edx, dword ptr [edi + 0x74];  // straightStepCount
			push SelectBridgeStartAndEnd_FarAbutmentFound_Continue;
			ret;
		}
	}

	// Far abutment clearance patch
	//
	// IsStepGoodForBridge fails if a network is within one tile of the step. It is also called for the
	// far abutment, so a network right behind the bridge blocks it. The near abutment isn't checked, so
	// we skip the check for the far abutment too.
	constexpr uint32_t SelectBridgeStartAndEnd_IsStepGoodForBridgeCall = 0x630604;

	bool __cdecl isFarAbutmentStep(const cSC4NetworkTool* networkTool, int32_t step)
	{
		if (networkTool->terrain == nullptr) {
			return false;
		}
		return !isWaterStep(networkTool, step) || isShoreStubStep(networkTool, step + 1);
	}

	void NAKED_FUN Hook_SelectBridgeStartAndEnd_IsStepGoodForBridge(void)
	{
		// edi: cSC4NetworkTool*, ebx: int32_t* step
		__asm {
			push dword ptr [ebx];
			push edi;
			call isFarAbutmentStep;
			add esp, 0x8;
			test al, al;
			jz vanillaCheck;
			ret;

vanillaCheck:
			push IsStepGoodForBridge;  // tail call with the original arguments
			ret;
		}
	}

	// Parallel bridges patch (experimental)
	//
	// IsStepGoodForBridge looks for networks within one tile of the step. We remove that margin on the
	// sides of the drag, but keep it at the front and back.
	constexpr uint32_t IsStepGoodForBridge_GetOccupantsCall = 0x62fe87;

	// true if the drag runs along z
	const bool* const sbPenDirIsX = reinterpret_cast<const bool*>(0xb464f0);

	void __cdecl removeLateralClearance(SC4Point<int32_t>* northWestCell, SC4Point<int32_t>* southEastCell)
	{
		if (*sbPenDirIsX) {
			northWestCell->x += 1;
			southEastCell->x -= 1;
		} else {
			northWestCell->y += 1;
			southEastCell->y -= 1;
		}
	}

	void NAKED_FUN Hook_IsStepGoodForBridge_GetOccupants(void)
	{
		// ecx: cISC4OccupantManager*
		// [esp+0x4] occupant list, [esp+0x8] northWestCell, [esp+0xc] southEastCell
		__asm {
			push ecx;
			push dword ptr [esp + 0x10];  // southEastCell
			push dword ptr [esp + 0x10];  // northWestCell
			call removeLateralClearance;
			add esp, 0x8;
			pop ecx;
			mov eax, dword ptr [ecx];
			jmp dword ptr [eax + 0xa0];  // GetOccupantsByStandardCityCells
		}
	}
}

void BridgePatches::InstallShoreAbutmentPatch()
{
	Patching::OverwriteMemory(reinterpret_cast<void*>(SelectBridgeStartAndEnd_DecValidSteps), uint8_t{0x90});  // nop
	Patching::OverwriteMemory(reinterpret_cast<void*>(SelectBridgeStartAndEnd_LoopDecValidSteps), uint8_t{0x90});  // nop
	Patching::InstallCallHook(SelectBridgeStartAndEnd_CheckBridgeStepEdgeTypesCall, reinterpret_cast<void(*)(void)>(Hook_SelectBridgeStartAndEnd_CheckBridgeStepEdgeTypes));
	Patching::InstallHook(SelectBridgeStartAndEnd_FarAbutmentFound, Hook_SelectBridgeStartAndEnd_FarAbutmentFound);
	sShoreAbutmentInstalled = true;
}

void BridgePatches::InstallFarAbutmentClearancePatch()
{
	Patching::InstallCallHook(SelectBridgeStartAndEnd_IsStepGoodForBridgeCall, Hook_SelectBridgeStartAndEnd_IsStepGoodForBridge);
}

void BridgePatches::InstallParallelBridgesPatch()
{
	Patching::InstallCallHook(IsStepGoodForBridge_GetOccupantsCall, Hook_IsStepGoodForBridge_GetOccupants);
	Patching::OverwriteMemory(reinterpret_cast<void*>(IsStepGoodForBridge_GetOccupantsCall + 5), uint8_t{0x90});  // the original call is 6 bytes
}
