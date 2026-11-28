#pragma once

namespace BridgePatches
{
	// lets a bridge drag end on the first tile of the far bank, as on the near bank
	void InstallShoreAbutmentPatch();

	// allows networks directly behind the far end of a bridge, as on the near end
	void InstallFarAbutmentClearancePatch();

	// experimental: allows bridges directly next to other bridges and networks
	void InstallParallelBridgesPatch();
}
