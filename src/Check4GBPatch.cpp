/*
 * This file is part of nam-dll, a DLL Plugin for SimCity 4
 * that improves interoperability with the Network Addon Mod.
 *
 * Copyright (c) 2024 NAM Team contributors
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
#include <Windows.h>

namespace
{
	bool Is4GBPatchInstalled()
	{
		const HMODULE executableBaseAddress = GetModuleHandle(nullptr);
		if (executableBaseAddress == nullptr)
		{
			DWORD lastError = GetLastError();

			char errorMessage[1024]{};

			snprintf(
				errorMessage,
				sizeof(errorMessage),
				"GetModuleHandle failed with error code 0x%08X.",
				lastError);

			throw std::runtime_error(errorMessage);
		}

		// The memory address returned by GetModuleHandle points to start of the IMAGE_DOS_HEADER,
		// the first header in a Windows executable.
		// We parse the various headers to read the 4GB patch status.
		//
		// See the following resources for more information on the Windows executable file format:
		// https://learn.microsoft.com/en-us/archive/msdn-magazine/2002/february/inside-windows-win32-portable-executable-file-format-in-detail
		// https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_file_header

		const IMAGE_DOS_HEADER* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(executableBaseAddress);

		if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
		{
			throw std::runtime_error("Invalid DOS header signature.");
		}

		// The e_lfanew member of the IMAGE_DOS_HEADER gives the offset of the second
		// header in a Windows executable, the Portable Executable (PE) header.
		// For historical reasons, Microsoft refers to this header as the NT header.
		// See https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_nt_headers32

		const BYTE* peHeaderStartOffset = reinterpret_cast<BYTE*>(executableBaseAddress) + dosHeader->e_lfanew;

		const IMAGE_NT_HEADERS32* peHeader = reinterpret_cast<const IMAGE_NT_HEADERS32*>(peHeaderStartOffset);

		if (peHeader->Signature != IMAGE_NT_SIGNATURE)
		{
			throw std::runtime_error("Invalid PE header signature.");
		}

		// The 4GB patch sets a flag in the Characteristics field of the IMAGE_FILE_HEADER.
		// This flag tells the OS that a 32-bit application can handle more than 2GB of address space.
		// See https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_file_header

		return (peHeader->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
	}
}

void Check4GBPatch::WritePatchStatusToLogFile()
{
	Logger& logger = Logger::GetInstance();

	try
	{
		if (Is4GBPatchInstalled())
		{
			logger.WriteLine(LogLevel::Info, "The 4GB patch is installed.");
		}
		else
		{
			logger.WriteLine(LogLevel::Info, "The 4GB patch is not installed.");
		}
	}
	catch (const std::exception& e)
	{
		logger.WriteLineFormatted(
			LogLevel::Error,
			"An error occurred when checking for the 4GB patch: %s",
			e.what());
	}
}
