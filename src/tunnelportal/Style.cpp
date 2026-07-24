#include "Style.h"

#include "Logger.h"
#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZPersistResourceManager.h"
#include "cISCPropertyHolder.h"
#include "cISCResExemplar.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"

#include <algorithm>
#include <vector>

namespace
{
	constexpr uint32_t kMaximumNetworkType =
		static_cast<uint32_t>(cISC4NetworkOccupant::GroundHighway);

	bool ReadStyle(
		cIGZPersistResourceManager* resourceManager,
		const cGZPersistResourceKey& key,
		TunnelPortal::Styles::Style& style)
	{
		cRZAutoRefCount<cISCResExemplar> exemplar;
		if (!resourceManager->GetResource(
			key,
			GZIID_cISCResExemplar,
			exemplar.AsPPVoid(),
			0,
			nullptr)
			|| !exemplar)
		{
			return false;
		}

		const cISCPropertyHolder* const properties = exemplar->AsISCPropertyHolder();
		uint32_t schemaVersion = 0;
		uint32_t networkType = 0;
		uint32_t tileCount = 0;
		cRZBaseString name;
		if (!properties
			|| !properties->GetProperty(
				TunnelPortal::Styles::kPropertySchemaVersion,
				schemaVersion)
			|| schemaVersion != TunnelPortal::Styles::kSchemaVersion
			|| !properties->GetProperty(
				TunnelPortal::Styles::kPropertyNetworkType,
				networkType)
			|| networkType > kMaximumNetworkType
			|| !properties->GetProperty(
				TunnelPortal::Styles::kPropertyTileCount,
				tileCount)
			|| (tileCount != 1 && tileCount != 2)
			|| !properties->GetProperty(
				TunnelPortal::Styles::kPropertyPortalExemplar0,
				style.portalExemplarIds[0])
			|| style.portalExemplarIds[0] == 0
			|| !properties->GetProperty(
				TunnelPortal::Styles::kPropertyExemplarName,
				name)
			|| name.Strlen() == 0)
		{
			return false;
		}

		if (tileCount == 2
			&& (!properties->GetProperty(
				TunnelPortal::Styles::kPropertyPortalExemplar1,
				style.portalExemplarIds[1])
				|| style.portalExemplarIds[1] == 0))
		{
			return false;
		}

		properties->GetProperty(
			TunnelPortal::Styles::kPropertyIconGroup,
			style.iconGroup);
		properties->GetProperty(
			TunnelPortal::Styles::kPropertyIconInstance,
			style.iconInstance);

		style.name = name.Data();
		style.networkType =
			static_cast<cISC4NetworkOccupant::eNetworkType>(networkType);
		style.tileCount = static_cast<uint8_t>(tileCount);
		return true;
	}
}

std::vector<TunnelPortal::Styles::Style> TunnelPortal::Styles::LoadCompatibleStyles(
	cISC4NetworkOccupant::eNetworkType networkType,
	uint8_t tileCount)
{
	std::vector<Style> styles;

	Style nativeStyle;
	nativeStyle.name = "Default";
	nativeStyle.networkType = networkType;
	nativeStyle.tileCount = tileCount;
	nativeStyle.useNativeExemplars = true;
	styles.push_back(nativeStyle);

	cIGZPersistResourceManagerPtr resourceManager;
	if (!resourceManager)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Error,
			"TunnelPortalTool: style discovery failed because the resource manager is unavailable.");
		return styles;
	}

	cRZAutoRefCount<cIGZPersistResourceKeyList> keys;
	resourceManager->GetAvailableResourceListForType(keys.AsPPObj(), kExemplarType);
	if (!keys)
	{
		return styles;
	}

	Logger& logger = Logger::GetInstance();
	for (uint32_t i = 0; i < keys->Size(); ++i)
	{
		const cGZPersistResourceKey& key = keys->GetKey(i);
		if (key.group != kExemplarGroup)
		{
			continue;
		}

		Style style;
		if (!ReadStyle(resourceManager, key, style))
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"TunnelPortalTool: ignored invalid portal style exemplar %08X-%08X-%08X.",
				key.type,
				key.group,
				key.instance);
			continue;
		}

		if (style.networkType != networkType || style.tileCount != tileCount)
		{
			continue;
		}

		styles.push_back(std::move(style));
	}

	std::sort(
		styles.begin() + 1,
		styles.end(),
		[](const Style& left, const Style& right)
		{
			return left.name < right.name;
		});

	// styles[0] is the always-present native "Default"; report only the custom
	// styles discovered from packages.
	logger.WriteLineFormatted(
		LogLevel::Info,
		"TunnelPortalTool: found %u compatible facade style(s) for network %u (%u tile portal).",
		static_cast<uint32_t>(styles.size() - 1),
		static_cast<uint32_t>(networkType),
		static_cast<uint32_t>(tileCount));
	return styles;
}
