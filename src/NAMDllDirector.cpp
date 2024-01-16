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

static constexpr uint32_t kNAMDllDirectorID = 0x4AC2AEFF;

static constexpr uint32_t kMonorailKeyboardShortcut = 0x8BE098F4;
static constexpr uint32_t kOneWayRoadKeyboardShortcut = 0x4BE098F7;
static constexpr uint32_t kDirtRoadKeyboardShortcut = 0x6BE098FA;
static constexpr uint32_t kGroundHighwayKeyboardShortcut = 0x4BE098FD;

static constexpr uint32_t kGZWin_WinSC4App = 0x6104489a;
static constexpr uint32_t kGZWin_SC4View3DWin = 0x9a47b417;

static constexpr uint32_t kGZIID_cISC4View3DWin = 0xFA47B3F9;

static constexpr std::string_view PluginLogFileName = "NAM.log";

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

	void InstallRHWTunnelsPatch()
	{
		Logger& logger = Logger::GetInstance();

		try
		{
			OverwriteMemory((void*)0x7141ac, 0x0b);
			OverwriteMemory((void*)0x7141bb, 0x0b);

			logger.WriteLine(
				LogLevel::Info,
				"Installed the RHW Tunnels patch.");
		}
		catch (const wil::ResultException& e)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"Failed to install the RHW Tunnels patch.\n%s",
				e.what());
		}
	}

	void InstallMemoryPatches()
	{
		// Patch the game's memory to enable a few NAM features.
		// These patches were all developed by memo.
		InstallDiagonalStreetsPatch();
		InstallDisableAutoconnectForStreetsPatch();
		InstallRHWTunnelsPatch();
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
	}

	uint32_t GetDirectorID() const
	{
		return kNAMDllDirectorID;
	}

	void ProcessKeyboardShortcut(intptr_t shortcut)
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

						uint32_t dwMessageID = static_cast<uint32_t>(shortcut);
						cIGZCommandParameterSet& command1 = *reinterpret_cast<cIGZCommandParameterSet*>(shortcut);
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
			InstallMemoryPatches();
		}
		else
		{
			Logger& logger = Logger::GetInstance();
			logger.WriteLineFormatted(
				LogLevel::Error,
				"Requires game version 641, found game version %d.",
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