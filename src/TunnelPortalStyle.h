#pragma once

#include "cISC4NetworkOccupant.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace TunnelPortalStyles
{
	constexpr uint32_t kExemplarType = 0x6534284A;
	constexpr uint32_t kExemplarGroup = 0x4A7B6E40;
	constexpr uint32_t kSchemaVersion = 1;

	// A style is an exemplar in kExemplarGroup. Exemplar Name is the standard
	// 0x00000020 property; the remaining properties are NAM-owned.
	constexpr uint32_t kPropertyExemplarName = 0x00000020;
	constexpr uint32_t kPropertySchemaVersion = 0x4A7B6E41;
	constexpr uint32_t kPropertyNetworkType = 0x4A7B6E42;
	constexpr uint32_t kPropertyTileCount = 0x4A7B6E43;
	constexpr uint32_t kPropertyPortalExemplar0 = 0x4A7B6E44;
	constexpr uint32_t kPropertyPortalExemplar1 = 0x4A7B6E45;
	constexpr uint32_t kPropertyIconGroup = 0x4A7B6E46;
	constexpr uint32_t kPropertyIconInstance = 0x4A7B6E47;

	struct Style
	{
		std::string name;
		cISC4NetworkOccupant::eNetworkType networkType = cISC4NetworkOccupant::Road;
		uint8_t tileCount = 1;
		std::array<uint32_t, 2> portalExemplarIds{};
		uint32_t iconGroup = 0;
		uint32_t iconInstance = 0;
		bool useNativeExemplars = false;
	};

	// Enumerates all resolved style exemplars. Invalid exemplars are ignored
	// and logged, so one broken style package cannot disable the selector.
	std::vector<Style> LoadCompatibleStyles(
		cISC4NetworkOccupant::eNetworkType networkType,
		uint8_t tileCount);
}
