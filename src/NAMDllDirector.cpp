/*
 * This file is part of nam-dll, a DLL Plugin for SimCity 4
 * that improves interoperability with the Network Addon Mod.
 *
 * Copyright (c) 2023, 2024 NAM Team contributors
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

#include "Check4GBPatch.h"
#include "Logger.h"
#include "SC4VersionDetection.h"
#include "version.h"
#include "cIGZCOM.h"
#include "cIGZFrameWork.h"
#include "cIGZMessage2.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZWin.h"
#include "cIGZWinMgr.h"
#include "cISC4App.h"
#include "cISC4View3DWin.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include <Windows.h>
#include "wil/resource.h"
#include "wil/win32_helpers.h"

#ifdef __clang__
#define NAKED_FUN __attribute__((naked))
#else
#define NAKED_FUN __declspec(naked)
#endif

static constexpr uint32_t kNAMDllDirectorID = 0x4AC2AEFF;

static constexpr uint32_t kMonorailKeyboardShortcut = 0x8BE098F4;
static constexpr uint32_t kOneWayRoadKeyboardShortcut = 0x4BE098F7;
static constexpr uint32_t kDirtRoadKeyboardShortcut = 0x6BE098FA;
static constexpr uint32_t kGroundHighwayKeyboardShortcut = 0x4BE098FD;

static constexpr uint32_t kGZWin_WinSC4App = 0x6104489a;
static constexpr uint32_t kGZWin_SC4View3DWin = 0x9a47b417;

static constexpr uint32_t kGZIID_cISC4View3DWin = 0xFA47B3F9;

static constexpr std::string_view PluginLogFileName = "NAM.log";

static uint32_t DoTunnelChanged_InjectPoint;
static uint32_t DoTunnelChanged_ContinueJump;
static uint32_t DoTunnelChanged_ReturnJump;

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

	void InstallMemoryPatches(const uint16_t gameVersion)
	{
		// Patch the game's memory to enable a few NAM features.
		// These patches were all developed by memo.
		InstallDiagonalStreetsPatch();
		InstallDisableAutoconnectForStreetsPatch();
		InstallTunnelsPatch(gameVersion);
	}
}

class NAMDllDirector final : public cRZMessage2COMDirector
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
		Check4GBPatch::WritePatchStatusToLogFile();
	}

	uint32_t GetDirectorID() const
	{
		return kNAMDllDirectorID;
	}

	void ProcessKeyboardShortcut(uint32_t dwMessageID)
	{
		cISC4AppPtr pSC4App;

		if (pSC4App)
		{
			cIGZWin* mainWindow = pSC4App->GetMainWindow();

			if (mainWindow)
			{
				cIGZWin* pParentWin = mainWindow->GetChildWindowFromID(kGZWin_WinSC4App);

				if (pParentWin)
				{
					cISC4View3DWin* pView3D = nullptr;

					if (pParentWin->GetChildAs(
						kGZWin_SC4View3DWin,
						kGZIID_cISC4View3DWin,
						reinterpret_cast<void**>(&pView3D)))
					{
						// SC4's keyboard shortcuts work by passing the message ID cast to cIGZCommandParameterSet
						// as the first cIGZCommandParameterSet parameter.
						//
						// The additional parameters (if any) will be wrapped in a cIGZCommandParameterSet and passed
						// in the second cIGZCommandParameterSet parameter.
						// The network tool shortcuts do not use the second parameter, so we use a placeholder value.

						cIGZCommandParameterSet& command1 = *reinterpret_cast<cIGZCommandParameterSet*>(dwMessageID);
						cIGZCommandParameterSet& command2 = *reinterpret_cast<cIGZCommandParameterSet*>(0xDEADBEEF);

						pView3D->ProcessCommand(dwMessageID, command1, command2);

						pView3D->Release();
					}
				}
			}
		}
	}

	bool DoMessage(cIGZMessage2* pMsg)
	{
		cIGZMessage2Standard* pStandardMessage = static_cast<cIGZMessage2Standard*>(pMsg);
		uint32_t msgType = pStandardMessage->GetType();

		switch (msgType)
		{
		case kMonorailKeyboardShortcut:
		case kOneWayRoadKeyboardShortcut:
		case kDirtRoadKeyboardShortcut:
		case kGroundHighwayKeyboardShortcut:
			ProcessKeyboardShortcut(msgType);
			break;
		}

		return true;
	}

	bool PostAppInit()
	{
		Logger& logger = Logger::GetInstance();

		cIGZMessageServer2Ptr pMsgServ;
		if (pMsgServ)
		{
			std::vector<uint32_t> requiredNotifications;
			requiredNotifications.push_back(kMonorailKeyboardShortcut);
			requiredNotifications.push_back(kOneWayRoadKeyboardShortcut);
			requiredNotifications.push_back(kDirtRoadKeyboardShortcut);
			requiredNotifications.push_back(kGroundHighwayKeyboardShortcut);

			for (uint32_t messageID : requiredNotifications)
			{
				if (!pMsgServ->AddNotification(this, messageID))
				{
					logger.WriteLine(LogLevel::Error, "Failed to subscribe to the required notifications.");
					return false;
				}
			}
		}
		else
		{
			logger.WriteLine(LogLevel::Error, "Failed to subscribe to the required notifications.");
			return false;
		}


		return true;
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
				"The memory patches require game version 641, found game version %d.",
				gameVersion);
		}

		cIGZFrameWork* const pFramework = RZGetFrameWork();

		if (pFramework->GetState() < cIGZFrameWork::kStatePreAppInit)
		{
			pFramework->AddHook(this);
		}
		else
		{
			PreAppInit();
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