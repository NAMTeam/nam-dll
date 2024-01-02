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

static constexpr uint32_t kNAMDllDirectorID = 0x4AC2AEFF;

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

		return true;
	}

private:

	const SC4VersionDetection versionDetection;
};

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static NAMDllDirector sDirector;
	return &sDirector;
}