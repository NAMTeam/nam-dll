#include "DirtRoadRegionalTransportMap.h"
#include "Patching.h"
#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceManager.h"
#include "cISCPropertyHolder.h"
#include "cISCResExemplar.h"
#include "cRZAutoRefCount.h"
#include "GZServPtrs.h"
#include <cstddef>
#include <cstring>
#include "wil/result_macros.h"

// Adds DirtRoad (RHW) to the regional transport map (transport data view and region view thumbnails).
// cSC4NetworkLayoutView::RebuildMap only knows 9 network types. It looks up each network in sMapIndexToNetworkType
// and draws it with the color at view + 0x20 + (map index + 1) * 4, which is a colour table entry for map indices 0 to 13.
// We give it a longer table with DirtRoad at map index 14, and a copy of the view with one more color slot (+0x5c).

namespace
{
	// colour table exemplar of the regional transport map
	constexpr uint32_t colorTableExemplarType = 0x6534284a;
	constexpr uint32_t colorTableExemplarGroup = 0x690f693f;
	constexpr uint32_t colorTableExemplarInstance = 0x0b9ec13d;
	constexpr uint32_t colorTableIndexRoad = 3;
	constexpr uint32_t colorTableIndexHighway = 8;
	// new property with the DirtRoad color (ARGB), else the Highway color is used, or the Road color if Highway is hidden
	constexpr uint32_t dirtRoadColorPropertyId = 0xab9ec144;

	constexpr uint32_t networkTypeDirtRoad = 11;
	constexpr uint32_t networkTypeNeverDrawn = 4;  // WaterPipe, masked out by RebuildMap
	constexpr uint32_t mapIndexDirtRoad = 14;
	constexpr uint32_t mapIndexCount = 15;

	uint32_t mapIndexToNetworkType[mapIndexCount] = {
		3, 1, 10, 0, 6, 9, 8, 12, 2,  // Street, Rail, OneWayRoad, Road, Avenue, Monorail, LightRail, GroundHighway, Highway
		networkTypeNeverDrawn, networkTypeNeverDrawn, networkTypeNeverDrawn, networkTypeNeverDrawn, networkTypeNeverDrawn,  // would use the lot and zone colors
		networkTypeDirtRoad,
	};

	constexpr uint32_t RebuildMap = 0x60acd0;
	constexpr uint32_t sMapIndexToNetworkType = 0xaa76b8;
	constexpr uint32_t InitCustomView_RebuildMap_Call = 0x60c107;
	constexpr uint8_t originalRebuildMapCall[] = {0xe8, 0xc4, 0xeb, 0xff, 0xff};
	constexpr uint32_t RebuildMap_MapIndexToNetworkType = 0x60aed3;  // disp32 of mov eax, [edi*4 + sMapIndexToNetworkType]
	constexpr uint32_t RebuildMap_MapIndexCount = 0x60af29;  // imm8 of cmp edi, 9

	// cSC4NetworkLayoutView: vertices (begin, end, capacity) at +0x0c, colour table (14 entries) at +0x24
	constexpr size_t layoutViewSize = 0x5c;
	constexpr size_t layoutViewVerticesOffset = 0x0c;
	constexpr size_t layoutViewColorTableOffset = 0x24;

	struct LayoutViewWithDirtRoadColor
	{
		uint8_t view[layoutViewSize];
		uint32_t dirtRoadColor;
	};
	static_assert(offsetof(LayoutViewWithDirtRoadColor, dirtRoadColor) == 0x20 + (mapIndexDirtRoad + 1) * 4);

	bool IsVisible(uint32_t argb)
	{
		return (argb >> 24) != 0;
	}

	bool ReadDirtRoadColorProperty(uint32_t& color)
	{
		cIGZPersistResourceManagerPtr pResMan;
		cRZAutoRefCount<cISCResExemplar> pExemplar;
		cGZPersistResourceKey key(colorTableExemplarType, colorTableExemplarGroup, colorTableExemplarInstance);
		if (!pResMan || !pResMan->GetResource(key, GZIID_cISCResExemplar, pExemplar.AsPPVoid(), 0, nullptr)) {
			return false;
		}
		return pExemplar->AsISCPropertyHolder()->GetProperty(dirtRoadColorPropertyId, color);
	}

	uint32_t GetDirtRoadColor(const uint32_t* colorTable)
	{
		uint32_t color;
		if (ReadDirtRoadColorProperty(color)) {
			return color;
		}
		if (IsVisible(colorTable[colorTableIndexHighway])) {
			return colorTable[colorTableIndexHighway];
		}
		return IsVisible(colorTable[colorTableIndexRoad]) ? colorTable[colorTableIndexRoad] : 0;
	}

	// replaces the call of RebuildMap in cSC4NetworkLayoutCustomView::InitCustomView (view in ecx)
	void __fastcall Hook_RebuildMap(void* pView)
	{
		LayoutViewWithDirtRoadColor view;
		std::memcpy(view.view, pView, layoutViewSize);
		view.dirtRoadColor = GetDirtRoadColor(reinterpret_cast<const uint32_t*>(view.view + layoutViewColorTableOffset));
		mapIndexToNetworkType[mapIndexDirtRoad] = IsVisible(view.dirtRoadColor) ? networkTypeDirtRoad : networkTypeNeverDrawn;

		reinterpret_cast<void(__thiscall*)(void*)>(RebuildMap)(&view);

		// RebuildMap only changes the vertices
		std::memcpy(static_cast<uint8_t*>(pView) + layoutViewVerticesOffset, view.view + layoutViewVerticesOffset, 3 * sizeof(void*));
	}
}

void DirtRoadRegionalTransportMap::Install()
{
	// check the call and the loop count first, a mismatch means another DLL already changed this code
	THROW_HR_IF(E_FAIL, std::memcmp(reinterpret_cast<void*>(InitCustomView_RebuildMap_Call), originalRebuildMapCall, sizeof(originalRebuildMapCall)) != 0);
	THROW_HR_IF(E_FAIL, *reinterpret_cast<uint8_t*>(RebuildMap_MapIndexCount) != 9);

	Patching::PatchImmediate32(RebuildMap_MapIndexToNetworkType, sMapIndexToNetworkType, reinterpret_cast<uint32_t>(mapIndexToNetworkType));
	Patching::OverwriteMemory(reinterpret_cast<void*>(RebuildMap_MapIndexCount), static_cast<uint8_t>(mapIndexCount));
	Patching::InstallCallHook(InitCustomView_RebuildMap_Call, reinterpret_cast<void(*)(void)>(&Hook_RebuildMap));
}
