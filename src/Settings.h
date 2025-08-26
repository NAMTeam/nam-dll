#pragma once
#include <filesystem>

class Settings final
{
public:
	Settings();
	void Load(std::filesystem::path settingsFilePath);

	bool enableKeyboardShortcuts;
	bool enableDiagonalStreets;
	bool disableAutoconnect;
	bool enableTunnels;
	bool reduceFerryBridgeHeightPatch;
	bool enableRUL2EnginePatch;
	bool enableNetworkSlopePatch;
	bool enableFlexPuzzlePiecePatch;
};
