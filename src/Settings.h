#pragma once
#include <cstdint>
#include <filesystem>

class Settings final
{
public:
	Settings();
	void Load(std::filesystem::path settingsFilePath);

	bool enableDiagonalStreets;
	bool disableAutoconnect;
	bool enableTunnels;
	bool reduceFerryBridgeHeightPatch;
	bool enableRUL2EnginePatch;
	bool enableNetworkSlopePatch;
	bool enableFlexPuzzlePiecePatch;
	bool enableCommuteLoopPatch;
	bool enableDirtRoadAccessPatch;
	bool enableNeighborConnectionPatch;
	bool enableRHWNeighborConnections;
	bool enableOWRNeighborConnections;
	bool enableNWMNeighborConnections;
	uint32_t neighborConnectionMaxSearchDistance;
	uint32_t neighborConnectionMaxGroupingGap;
	bool enableVerticalPathSeparationPatch;
	bool enableDualNetworkTransitPatch;
	bool enableTransportMenuPatch;
	bool enableKeyboardShortcuts;
};
