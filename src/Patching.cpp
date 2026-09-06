#include "Patching.h"
#include <cstring>
#include <limits>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "wil/result_macros.h"
#include "wil/win32_helpers.h"

namespace
{
	constexpr size_t kJumpByteCount = 5;

	int32_t GetRelativeJumpOffset(uint32_t from, uint32_t to)
	{
		const auto delta = static_cast<intptr_t>(to) - static_cast<intptr_t>(from + kJumpByteCount);
		THROW_HR_IF(E_FAIL, delta < static_cast<intptr_t>(std::numeric_limits<int32_t>::min()));
		THROW_HR_IF(E_FAIL, delta > static_cast<intptr_t>(std::numeric_limits<int32_t>::max()));
		return static_cast<int32_t>(delta);
	}
}

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

void Patching::InstallInlineHook(InlineHook& hook)
{
	if (hook.installed)
	{
		return;
	}

	auto* const target = reinterpret_cast<uint8_t*>(hook.address);

	if (hook.checkExpectedBytes)
	{
		THROW_HR_IF(
			E_FAIL,
			std::memcmp(target, hook.expectedBytes.data(), hook.expectedBytes.size()) != 0);
	}

	std::memcpy(hook.original.data(), target, hook.original.size());

	auto* const trampoline = static_cast<uint8_t*>(VirtualAlloc(
		nullptr,
		kInlineHookPatchByteCount + kJumpByteCount,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE));
	THROW_LAST_ERROR_IF_NULL(trampoline);

	try
	{
		std::memcpy(trampoline, target, kInlineHookPatchByteCount);
		trampoline[kInlineHookPatchByteCount] = 0xe9;
		const int32_t trampolineRel = GetRelativeJumpOffset(
			reinterpret_cast<uint32_t>(trampoline + kInlineHookPatchByteCount),
			reinterpret_cast<uint32_t>(target + kInlineHookPatchByteCount));
		std::memcpy(trampoline + kInlineHookPatchByteCount + 1, &trampolineRel, sizeof(trampolineRel));

		const int32_t hookRel = GetRelativeJumpOffset(
			reinterpret_cast<uint32_t>(target),
			reinterpret_cast<uint32_t>(hook.hookFunction));

		DWORD trampolineOldProtect;
		THROW_IF_WIN32_BOOL_FALSE(VirtualProtect(
			trampoline,
			kInlineHookPatchByteCount + kJumpByteCount,
			PAGE_EXECUTE_READ,
			&trampolineOldProtect));

		DWORD oldProtect;
		THROW_IF_WIN32_BOOL_FALSE(VirtualProtect(
			target,
			kInlineHookPatchByteCount,
			PAGE_EXECUTE_READWRITE,
			&oldProtect));

		hook.trampoline = trampoline;

		target[0] = 0xe9;
		std::memcpy(target + 1, &hookRel, sizeof(hookRel));
		for (size_t i = kJumpByteCount; i < kInlineHookPatchByteCount; ++i)
		{
			target[i] = 0x90;
		}

		VirtualProtect(target, kInlineHookPatchByteCount, oldProtect, &oldProtect);

		hook.installed = true;
	}
	catch (...)
	{
		hook.trampoline = nullptr;
		VirtualFree(trampoline, 0, MEM_RELEASE);
		throw;
	}
}

void Patching::InstallVTableHook(VTableHook& hook)
{
	if (hook.installed)
	{
		return;
	}

	auto* const slot = reinterpret_cast<void**>(hook.slotAddress);
	THROW_HR_IF(E_FAIL, reinterpret_cast<uint32_t>(*slot) != hook.expectedOriginal);

	DWORD oldProtect;
	THROW_IF_WIN32_BOOL_FALSE(VirtualProtect(
		slot,
		sizeof(void*),
		PAGE_EXECUTE_READWRITE,
		&oldProtect));

	// Publish the original before the slot starts routing calls into the hook.
	hook.original = *slot;
	*slot = hook.hookFunction;
	VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);

	hook.installed = true;
}
