#include "CommuteLoop.h"
#include "Patching.h"

namespace
{
	constexpr uint32_t FindNearestStandardDest_InjectPoint1 = 0x6d7ebe;
	constexpr uint32_t FindNearestStandardDest_ReturnJump1 = 0x6d7ec5;
	constexpr uint32_t FindNearestStandardDest_InjectPoint2 = 0x6d7ef0;
	constexpr uint32_t FindNearestStandardDest_ReturnJump2 = 0x6d7ef7;
	constexpr uint32_t AtGoal_InjectPoint = 0x6d79bf;
	constexpr uint32_t AtGoal_ReturnJump_NC = 0x6d79c5;
	constexpr uint32_t AtGoal_ReturnJump_lot = 0x6d7a42;

	// For routes coming from West, reduce maximum search area for standard destinations to exclude Northern city edge
	void NAKED_FUN Hook_FindNearestStandardDest1(void)
	{
		__asm {
			mov dword ptr [esp + 0x4c], esi;  // x0 = 0
			mov dword ptr [esp + 0x50], esi;  // z0 = 0 (instead of -1)
			mov dword ptr [ebx + 0x4c], eax;
			push FindNearestStandardDest_ReturnJump1;
			ret;
		}
	}

	// For routes coming from North, reduce maximum search area for standard destinations to exclude Western city edge
	void NAKED_FUN Hook_FindNearestStandardDest2(void)
	{
		__asm {
			mov dword ptr [esp + 0x4c], esi;  // x0 = 0 (instead of -1)
			mov dword ptr [esp + 0x50], esi;  // z0 = 0
			mov dword ptr [ebx + 0x4c], eax;
			push FindNearestStandardDest_ReturnJump2;
			ret;
		}
	}

	// Filter the NC branch to avoid terminating with a neighbor-to-neighbor route between North and West.
	// (This is mainly relevant if Nearest Destination Attractiveness property is set to a small value,
	// so path finder might reach a different goal than nearest standard destination from above.)
	void NAKED_FUN Hook_AtGoal(void)
	{
		__asm {
			// check condition: (x == -1 || z == -1) && originatingEdge.topLeftX == -1 && originatingEdge.topLeftY == -1
			cmp ebx, -1;  // x == -1
			je checkOriginatingEdgeNW;
			cmp edi, -1;  // z == -1
			jne conditionFailed;
checkOriginatingEdgeNW:
			cmp dword ptr [esi + 0x48 + 0x0], -1;  // pathFinder->originatingEdge.topLeftX == -1
			jne conditionFailed;
			cmp dword ptr [esi + 0x48 + 0x4], -1;  // pathFinder->originatingEdge.topLeftY == -1
			jne conditionFailed;
			// condition holds: We have a neighbor-to-neighbor route between North and West, so don't choose NC branch
			push AtGoal_ReturnJump_lot;
			ret;
conditionFailed:
			// continue with regular NC branch
			mov ecx, dword ptr [esi + 0xc];
			mov edx, dword ptr [ecx + 0x4c];
			push AtGoal_ReturnJump_NC;
			ret;
		}
	}
}

void CommuteLoop::Install()
{
	// these patches only affect path searches involving neighbor connections
	Patching::InstallHook(FindNearestStandardDest_InjectPoint1, Hook_FindNearestStandardDest1);
	Patching::InstallHook(FindNearestStandardDest_InjectPoint2, Hook_FindNearestStandardDest2);
	Patching::InstallHook(AtGoal_InjectPoint, Hook_AtGoal);
}
