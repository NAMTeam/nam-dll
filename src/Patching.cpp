#include "Patching.h"
#include <Windows.h>
#include "wil/win32_helpers.h"

void Patching::OverwriteMemory(void* address, uint8_t newValue)
{
	DWORD oldProtect;
	// Allow the executable memory to be written to.
	THROW_IF_WIN32_BOOL_FALSE(VirtualProtect(
		address,
		sizeof(newValue),
		PAGE_EXECUTE_READWRITE,
		&oldProtect));

	// Patch the memory at the specified address.
	*((uint8_t*)address) = newValue;
}

void Patching::OverwriteMemory(void* address, uint32_t newValue)
{
	DWORD oldProtect;
	// Allow the executable memory to be written to.
	THROW_IF_WIN32_BOOL_FALSE(VirtualProtect(
		address,
		sizeof(newValue),
		PAGE_EXECUTE_READWRITE,
		&oldProtect));

	// Patch the memory at the specified address.
	*((uint32_t*)address) = newValue;
}

void Patching::InstallHook(uint32_t address, void (*pfnFunc)(void))
{
	DWORD oldProtect;
	THROW_IF_WIN32_BOOL_FALSE(VirtualProtect((void*)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect));

	*((uint8_t*)address) = 0xE9;
	*((uint32_t*)(address + 1)) = ((uint32_t)pfnFunc) - address - 5;
}
