/*
 * This file is part of nam-dll, a DLL Plugin for SimCity 4
 * that improves interoperability with the Network Addon Mod.
 *
 * Copyright (c) 2023 NAM Team contributors
 *
 * nam-dll is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * nam-dll is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with nam-dll.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Logger.h"
#include "SC4VersionDetection.h"
#include "version.h"
#include "cIGZCOM.h"
#include "cRZCOMDllDirector.h"
#include <Windows.h>
#include "wil/resource.h"
#include "wil/win32_helpers.h"

#ifdef __clang__
#define NAKED_FUN __attribute__((naked))
#else
#define NAKED_FUN __declspec(naked)
#endif

static constexpr uint32_t kNAMDllDirectorID = 0x4AC2AEFF;

static constexpr std::string_view PluginLogFileName = "NAM.log";

static uint32_t DoTunnelChanged_InjectPoint;
static uint32_t DoTunnelChanged_ContinueJump;
static uint32_t DoTunnelChanged_ReturnJump;

static uint32_t DoTransportMenu_InjectPoint;
static uint32_t DoTransportMenu_ContinueJump;

namespace
{
	std::filesystem::path GetDllFolderPath()
	{
		wil::unique_cotaskmem_string modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());

		std::filesystem::path temp(modulePath.get());

		return temp.parent_path();
	}

	void OverwriteMemory(void* address, uint8_t newValue)
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

	void InstallHook(uint32_t address, void (*pfnFunc)(void))
	{
		DWORD oldProtect;
		THROW_IF_WIN32_BOOL_FALSE(VirtualProtect((void *)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect));

		*((uint8_t*)address) = 0xE9;
		*((uint32_t*)(address+1)) = ((uint32_t)pfnFunc) - address - 5;
	}

	void InstallDiagonalStreetsPatch()
	{
		Logger& logger = Logger::GetInstance();

		try
		{
			OverwriteMemory((void*)0x637f80, 0xeb);
			OverwriteMemory((void*)0x63aff2, 0xeb);

			logger.WriteLine(
				LogLevel::Info,
				"Installed the Draggable Diagonal Streets patch.");
		}
		catch (const wil::ResultException& e)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"Failed to install the Draggable Diagonal Streets patch.\n%s",
				e.what());
		}
	}

	void InstallDisableAutoconnectForStreetsPatch()
	{
		Logger& logger = Logger::GetInstance();

		try
		{
			OverwriteMemory((void*)0x729fff, 0x00);

			logger.WriteLine(
				LogLevel::Info,
				"Installed the Disable auto-connect for RHW and Streets patch.");
		}
		catch (const wil::ResultException& e)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"Failed to install the Disable auto-connect for RHW and Streets patch.\n%s",
				e.what());
		}
	}

	// TODO Currently this function is invoked with JMP+RET which can lead to suboptimal branch prediction due to imbalanced CALL+RET operations.
	void NAKED_FUN Hook_DoTunnelChanged(void)
	{
		// MSVC-style assembly using Intel syntax with clang-cl
		__asm {
// monorail:
			mov edx, dword ptr [esi];
			push 0x9;
			mov ecx, esi;
			call dword ptr [edx + 0x58];
			test al, al;
			jz dirtroad;
			mov eax, 0x9;
			jmp matchingTunnelNetwork;

dirtroad:
			mov edx, dword ptr [esi];
			push 0xb;
			mov ecx, esi;
			call dword ptr [edx + 0x58];
			test al, al;
			jz street;
			mov eax, 0xb;
			jmp matchingTunnelNetwork;

street:
			mov edx, dword ptr [esi];
			push 0x3;
			mov ecx, esi;
			call dword ptr [edx + 0x58];
			test al, al;
			jz lightrail;
			mov eax, 0x3;
			jmp matchingTunnelNetwork;

lightrail:
			mov edx, dword ptr [esi];
			push 0x8;
			mov ecx, esi;
			call dword ptr [edx + 0x58];
			test al, al;
			jz noMatchingTunnelNetwork;
			mov eax, 0x8;
			// jmp matchingTunnelNetwork;

matchingTunnelNetwork:
			push DoTunnelChanged_ContinueJump;
			ret;

noMatchingTunnelNetwork:
			push DoTunnelChanged_ReturnJump;
			ret;
		}
	}

	void InstallTunnelsPatch(const uint16_t gameVersion)
	{
		Logger& logger = Logger::GetInstance();

		try
		{
			switch (gameVersion)
			{
				case 641:
					DoTunnelChanged_InjectPoint = 0x714222;
					DoTunnelChanged_ContinueJump = 0x714238;
					DoTunnelChanged_ReturnJump = 0x714398;
					break;
				default:
					return;
			}
			InstallHook(DoTunnelChanged_InjectPoint, Hook_DoTunnelChanged);

			logger.WriteLine(
				LogLevel::Info,
				"Installed the Tunnels patch for RHW, Street and Lightrail.");
		}
		catch (const wil::ResultException& e)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"Failed to install the Tunnels patch for RHW, Street and Lightrail.\n%s",
				e.what());
		}
	}

	void NAKED_FUN Hook_DoTransportMenu(void)
	{
		// network menu groups:
		// 0x4000 road menu
		// 0x4001 rail menu
		// 0x4002 subway menu
		// 0x4003 power utility menu
		// 0x4004 water utility menu
		// 0x4005 highway menu
		__asm {
			mov ecx, 0x4006; // new seaport menu group (hopefully unused)
			lea edi, [esi + 0x29c];
			push DoTransportMenu_ContinueJump;
			ret;
		}
	}

	void InstallSeaportMenuPatch(const uint16_t gameVersion)
	{
		Logger& logger = Logger::GetInstance();

		try
		{
			switch (gameVersion)
			{
				case 641:
					DoTransportMenu_InjectPoint = 0x7f3d3b;
					DoTransportMenu_ContinueJump = 0x7f3d43;
					break;
				default:
					return;
			}
			InstallHook(DoTransportMenu_InjectPoint, Hook_DoTransportMenu);

			logger.WriteLine(
				LogLevel::Info,
				"Installed the Seaport Menu patch.");
		}
		catch (const wil::ResultException& e)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"Failed to install the Seaport Menu patch.\n%s",
				e.what());
		}
	}

	void InstallMemoryPatches(const uint16_t gameVersion)
	{
		// Patch the game's memory to enable a few NAM features.
		// These patches were all developed by memo.
		InstallDiagonalStreetsPatch();
		InstallDisableAutoconnectForStreetsPatch();
		InstallTunnelsPatch(gameVersion);
		InstallSeaportMenuPatch(gameVersion);
	}
}

class NAMDllDirector final : public cRZCOMDllDirector
{
public:

	NAMDllDirector()
	{
		std::filesystem::path dllFolderPath = GetDllFolderPath();

		std::filesystem::path logFilePath = dllFolderPath;
		logFilePath /= PluginLogFileName;

		Logger& logger = Logger::GetInstance();
		logger.Init(logFilePath, LogLevel::Error);
		logger.WriteLogFileHeader("NAM DLL v" PLUGIN_VERSION_STR);
	}

	uint32_t GetDirectorID() const
	{
		return kNAMDllDirectorID;
	}

	bool OnStart(cIGZCOM* pCOM)
	{
		const uint16_t gameVersion = versionDetection.GetGameVersion();

		if (gameVersion == 641)
		{
			InstallMemoryPatches(gameVersion);
		}
		else
		{
			Logger& logger = Logger::GetInstance();
			logger.WriteLineFormatted(
				LogLevel::Error,
				"Requires game version 641, found game version %d.",
				gameVersion);
		}

		return true;
	}

private:

	const SC4VersionDetection versionDetection;
};

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static NAMDllDirector sDirector;
	return &sDirector;
}