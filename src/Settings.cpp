#include "Settings.h"
#include "Logger.h"
#include "mini/ini.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
	// OWR pairing is incomplete (OWR shares NC type 10 with RHW, so wide OWR does not pair and
	// neighboring cities get RHW stubs). Kept disabled until that is resolved.
	constexpr bool kOWRNeighborConnectionsSupported = false;

	std::string Trim(std::string value)
	{
		auto isSpace = [](const unsigned char ch)
		{
			return std::isspace(ch) != 0;
		};

		value.erase(
			value.begin(),
			std::find_if_not(value.begin(), value.end(), isSpace));
		value.erase(
			std::find_if_not(value.rbegin(), value.rend(), isSpace).base(),
			value.end());
		return value;
	}

	std::string ToUpper(std::string value)
	{
		std::transform(
			value.begin(),
			value.end(),
			value.begin(),
			[](const unsigned char ch)
			{
				return static_cast<char>(std::toupper(ch));
			});
		return value;
	}

	void ReadNeighborConnectionNetworks(
		const std::string& networkList,
		bool& enableRHW,
		bool& enableOWR,
		bool& enableNWM)
	{
		enableRHW = false;
		enableOWR = false;
		enableNWM = false;

		size_t tokenStart = 0;
		while (tokenStart <= networkList.size())
		{
			const size_t tokenEnd = networkList.find(',', tokenStart);
			const std::string token = ToUpper(Trim(networkList.substr(
				tokenStart,
				tokenEnd == std::string::npos ? std::string::npos : tokenEnd - tokenStart)));

			if (token == "RHW")
			{
				enableRHW = true;
			}
			else if (token == "OWR")
			{
				if (kOWRNeighborConnectionsSupported)
				{
					enableOWR = true;
				}
				else
				{
					Logger::GetInstance().WriteLine(LogLevel::Info, "OWR neighbor connections are not supported yet, ignoring OWR in NeighborConnectionNetworks.");
				}
			}
			else if (token == "NWM")
			{
				enableNWM = true;
			}

			if (tokenEnd == std::string::npos)
			{
				break;
			}
			tokenStart = tokenEnd + 1;
		}

		if (enableOWR)
		{
			enableRHW = true;
		}
	}
}

Settings::Settings() :
	enableDiagonalStreets(true),
	disableAutoconnect(true),
	enableTunnels(true),
	reduceFerryBridgeHeightPatch(true),
	enableRUL2EnginePatch(true),
	enableNetworkSlopePatch(true),
	enableFlexPuzzlePiecePatch(true),
	enableCommuteLoopPatch(true),
	enableDirtRoadAccessPatch(false),
	enableNeighborConnectionPatch(false),
	enableRHWNeighborConnections(false),
	enableOWRNeighborConnections(false),
	enableNWMNeighborConnections(false),
	neighborConnectionMaxSearchDistance(8),
	neighborConnectionMaxGroupingGap(2),
	enableVerticalPathSeparationPatch(true),
	enableDualNetworkTransitPatch(false),
	enableTransportMenuPatch(true),
	enableKeyboardShortcuts(true) {};

void Settings::Load(std::filesystem::path settingsFilePath)
{
	Logger& logger = Logger::GetInstance();
	try {
		mINI::INIFile file(settingsFilePath);
		mINI::INIStructure ini;
		if (file.read(ini)) {
			auto readBoolProp = [&ini](const std::string propName, bool &propValue) {
				propValue ^= ini.get("NAM").get(propName) == (propValue ? "false" : "true");  // toggle if opposite of default
			};
			readBoolProp("EnableKeyboardShortcuts", enableKeyboardShortcuts);
			readBoolProp("EnableDiagonalStreets", enableDiagonalStreets);
			readBoolProp("DisableAutoconnect", disableAutoconnect);
			readBoolProp("EnableTunnels", enableTunnels);
			readBoolProp("ReduceFerryBridgeHeight", reduceFerryBridgeHeightPatch);
			readBoolProp("EnableRUL2EnginePatch", enableRUL2EnginePatch);
			readBoolProp("EnableNetworkSlopePatch", enableNetworkSlopePatch);
			readBoolProp("EnableFlexPuzzlePiecePatch", enableFlexPuzzlePiecePatch);
			readBoolProp("EnableCommuteLoopPatch", enableCommuteLoopPatch);
			readBoolProp("EnableDirtRoadAccessPatch", enableDirtRoadAccessPatch);
			readBoolProp("EnableNeighborConnectionPatch", enableNeighborConnectionPatch);

			ReadNeighborConnectionNetworks(
				ini.get("NAM").get("NeighborConnectionNetworks"),
				enableRHWNeighborConnections,
				enableOWRNeighborConnections,
				enableNWMNeighborConnections);

			// A bad number must not throw out of Load: that would skip every setting read after it.
			auto readUIntProp = [&ini, &logger](const std::string propName, uint32_t &propValue) {
				const std::string value = ini.get("NAM").get(propName);
				if (value.empty()) {
					return;
				}
				try {
					propValue = std::stoul(value);
				} catch (const std::exception&) {
					logger.WriteLineFormatted(LogLevel::Error, "Ignoring invalid %s value: %s", propName.c_str(), value.c_str());
				}
			};
			readUIntProp("NeighborConnectionMaxSearchDistance", neighborConnectionMaxSearchDistance);
			readUIntProp("NeighborConnectionMaxGroupingGap", neighborConnectionMaxGroupingGap);
			readBoolProp("EnableVerticalPathSeparationPatch", enableVerticalPathSeparationPatch);
			readBoolProp("EnableDualNetworkTransitPatch", enableDualNetworkTransitPatch);
			readBoolProp("EnableTransportMenuPatch", enableTransportMenuPatch);
		} else {
			logger.WriteLine(LogLevel::Info, "Using default settings, as no NAM.ini configuration file was detected.");
		}
	} catch (const std::exception &e) {
		logger.WriteLineFormatted(LogLevel::Error, "Error reading the settings file: %s", e.what());
	}
}
