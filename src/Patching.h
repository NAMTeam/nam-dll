#pragma once
#include "stdint.h"

#ifdef __clang__
#define NAKED_FUN __attribute__((naked))
#else
#define NAKED_FUN __declspec(naked)
#endif

namespace Patching
{
	void OverwriteMemory(void* address, uint8_t newValue);
	void OverwriteMemory(void* address, uint32_t newValue);
	void PatchImmediate16(uint32_t address, uint16_t expectedValue, uint16_t newValue);
	void PatchImmediate32(uint32_t address, uint32_t expectedValue, uint32_t newValue);
	void PatchPushImmediate32(uint32_t address, uint32_t expectedValue, uint32_t newValue);
	void PatchTestEaxImmediate32(uint32_t address, uint32_t expectedValue, uint32_t newValue);
	void PatchTestWordPtrEaxImmediate16(uint32_t address, uint16_t expectedValue, uint16_t newValue);

	// Installs a call hook over the call instruction at callSite, but only if that
	// call really goes to expectedTarget (or to pfnHook itself, when the patch is
	// installed again for another city).
	void InstallCallSiteHook(uint32_t callSite, uint32_t expectedTarget, void (*pfnFunc)(void));

	void InstallHook(uint32_t address, void (*pfnFunc)(void));
	void InstallCallHook(uint32_t address, void (*pfnFunc)(void));
}
