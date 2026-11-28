#pragma once
#include <cstdint>

namespace NeighborConnections
{
	// User-facing settings for the neighbor connection patch.
	struct Options
	{
		uint32_t maxSearchDistance = 8;
		uint32_t maxGroupingGap = 2;
		bool enableRHW = false;
		bool enableOWR = false;
		bool enableNWM = false;
	};

	void Install(const Options& options);
}
