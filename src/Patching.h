#pragma once
#include "stdint.h"

namespace Patching
{
	void OverwriteMemory(void* address, uint8_t newValue);

	void InstallHook(uint32_t address, void (*pfnFunc)(void));
}
