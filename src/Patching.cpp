#include "Patching.h"
#include <cstring>
#include <Windows.h>
#include "wil/result_macros.h"
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

void Patching::PatchImmediate32(const uint32_t address, const uint32_t expectedValue, const uint32_t newValue)
{
	auto* const immediate = reinterpret_cast<uint32_t*>(address);
	if (*immediate == newValue)
	{
		return;
	}

	THROW_HR_IF(E_FAIL, *immediate != expectedValue);

	DWORD oldProtect;
	THROW_IF_WIN32_BOOL_FALSE(VirtualProtect(
		immediate,
		sizeof(newValue),
		PAGE_EXECUTE_READWRITE,
		&oldProtect));

	std::memcpy(immediate, &newValue, sizeof(newValue));
	VirtualProtect(immediate, sizeof(newValue), oldProtect, &oldProtect);
}

void Patching::PatchPushImmediate32(const uint32_t address, const uint32_t expectedValue, const uint32_t newValue)
{
	const uint8_t* const instruction = reinterpret_cast<uint8_t*>(address);
	THROW_HR_IF(E_FAIL, instruction[0] != 0x68);
	PatchImmediate32(address + 1, expectedValue, newValue);
}

void Patching::PatchTestEaxImmediate32(const uint32_t address, const uint32_t expectedValue, const uint32_t newValue)
{
	const uint8_t* const instruction = reinterpret_cast<uint8_t*>(address);
	THROW_HR_IF(E_FAIL, instruction[0] != 0xa9);
	PatchImmediate32(address + 1, expectedValue, newValue);
}

void Patching::InstallHook(uint32_t address, void (*pfnFunc)(void))
{
	DWORD oldProtect;
	THROW_IF_WIN32_BOOL_FALSE(VirtualProtect((void*)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect));
	*((uint8_t*)address) = 0xE9;
	*((uint32_t*)(address + 1)) = ((uint32_t)pfnFunc) - address - 5;
}

void Patching::InstallCallHook(uint32_t address, void (*pfnFunc)(void)) {
	DWORD oldProtect;
	THROW_IF_WIN32_BOOL_FALSE(VirtualProtect((void*)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect));
	*((uint8_t*)address) = 0xE8;
	*((uint32_t*)(address + 1)) = ((uint32_t)pfnFunc) - address - 5;
}
