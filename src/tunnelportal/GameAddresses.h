#pragma once

#include "NetworkStubs.h"
#include "cIGZUnknown.h"
#include "cISC4NetworkOccupant.h"
#include "cISC4TrafficSimulator.h"

#include <cstddef>
#include <cstdint>

// Game functions, struct offsets, vtable slots and patch sites used by the
// tunnel portal tool. Everything here is specific to the Windows SimCity 4
// 1.1.641 executable.
//
// Keep this file free of logic: it is the one place to check when an address
// turns out to be wrong.
namespace TunnelPortal::Game
{
	// Reference to a field at a raw byte offset in a game object. Used for the
	// fields in the Field namespace below, which have no SDK declaration.
	template <typename T>
	T& FieldAt(void* object, size_t offset)
	{
		return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
	}

	template <typename T>
	const T& FieldAt(const void* object, size_t offset)
	{
		return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(object) + offset);
	}

	// --- Functions we call ---------------------------------------------------

	// cSC4NetworkTool::InsertTunnelPiece
	// Creates one tunnel portal occupant on a prepared cell. Takes the exemplar
	// ID from the vector at cSC4NetworkTool + Field::kToolTunnelExemplarIds.
	using pfn_InsertTunnelPiece = cISC4NetworkOccupant* (__thiscall*)(
		cSC4NetworkTool* tool,
		uint8_t direction,
		uint8_t sequenceIndex,
		cSC4NetworkCellInfo* cellInfo);
	inline const pfn_InsertTunnelPiece InsertTunnelPiece =
		reinterpret_cast<pfn_InsertTunnelPiece>(0x00628390);

	// cSC4NetworkTunnelOccupant::SetOtherEndOccupant
	// Points one portal at the occupant on the far side of the tunnel.
	using pfn_SetOtherEndOccupant = void (__thiscall*)(
		cIGZUnknown* tunnel,
		cISC4NetworkOccupant* otherEnd);
	inline const pfn_SetOtherEndOccupant SetOtherEndOccupant =
		reinterpret_cast<pfn_SetOtherEndOccupant>(0x00647530);

	// cSC4TrafficSimulator::DoTunnelChanged
	// Adds or removes a portal from the simulator's tunnel registry.
	using pfn_DoTunnelChanged = void (__thiscall*)(
		cISC4TrafficSimulator* trafficSimulator,
		cIGZUnknown* tunnel,
		bool added);
	inline const pfn_DoTunnelChanged DoTunnelChanged =
		reinterpret_cast<pfn_DoTunnelChanged>(0x007140e0);

	// cSC4TrafficSimulator::DoConnectionsChanged
	// Re-solves connectivity over the given cell rectangle.
	using pfn_DoConnectionsChanged = void (__thiscall*)(
		cISC4TrafficSimulator* trafficSimulator,
		uint32_t startX,
		uint32_t startZ,
		uint32_t endX,
		uint32_t endZ);
	inline const pfn_DoConnectionsChanged DoConnectionsChanged =
		reinterpret_cast<pfn_DoConnectionsChanged>(0x0071a860);

	// cSC4TrafficSimulator::GetNetworkInfo
	// Also the call FloodSubnetwork makes in its tunnel branch, which we wrap.
	using pfn_TrafficSimGetNetworkInfo = void* (__thiscall*)(
		void* trafficSimulator,
		uint32_t networkType,
		uint32_t x,
		uint32_t z);
	inline const pfn_TrafficSimGetNetworkInfo TrafficSimGetNetworkInfo =
		reinterpret_cast<pfn_TrafficSimGetNetworkInfo>(0x0070FB30);

	// cSC4PathFinder::AddTripNode
	// Queues one step of a route. We hook the call FindPath makes for tunnels.
	using pfn_AddTripNode = void (__thiscall*)(
		void* pathFinder,
		uint32_t x,
		uint32_t z,
		uint8_t edge,
		uint8_t travelMode,
		float cost,
		int currentNode,
		float heuristic,
		char outOfBounds);
	inline const pfn_AddTripNode AddTripNode =
		reinterpret_cast<pfn_AddTripNode>(0x006d8fa0);

	// --- Virtual calls -------------------------------------------------------

	// cSC4NetworkTunnelOccupant::GetPathInfo, vtable slot 0x31 (byte offset 0xC4).
	inline void* GetTunnelPathInfo(cIGZUnknown* tunnel)
	{
		using pfn = void* (__thiscall*)(cIGZUnknown*);
		void** const vtable = *reinterpret_cast<void***>(tunnel);
		return reinterpret_cast<pfn>(vtable[0x31])(tunnel);
	}

	// cSC4PathInfo::InitTunnelPath, vtable slot 0x20 (byte offset 0x80).
	// Rebuilds the path map of `self` by stitching it to `otherEnd`.
	inline void InitTunnelPath(void* pathInfo, cIGZUnknown* self, cIGZUnknown* otherEnd)
	{
		using pfn = void (__thiscall*)(void*, cIGZUnknown*, cIGZUnknown*);
		void** const vtable = *reinterpret_cast<void***>(pathInfo);
		reinterpret_cast<pfn>(vtable[0x20])(pathInfo, self, otherEnd);
	}

	// cSC4TrafficNetworkMap::GetNetworkInfo, vtable slot 8.
	// Returns the map entry for a cell, or null when the cell carries no network
	// matching networkMask (a 1 << eNetworkType bit set).
	inline void* GetTrafficNetworkInfo(
		void* trafficNetworkMap,
		int32_t x,
		int32_t z,
		uint32_t networkMask,
		bool includeUnbuilt)
	{
		using pfn = void* (__thiscall*)(void*, int32_t, int32_t, uint32_t, bool);
		void** const vtable = *reinterpret_cast<void***>(trafficNetworkMap);
		if (!vtable || !vtable[8])
		{
			return nullptr;
		}

		return reinterpret_cast<pfn>(vtable[8])(
			trafficNetworkMap, x, z, networkMask, includeUnbuilt);
	}

	// --- Fields on objects with no full declaration --------------------------

	namespace Field
	{
		// cSC4NetworkTool
		constexpr size_t kToolPlacingMode = 0x50;         // uint8_t
		constexpr size_t kToolFailureCode = 0x20c;        // uint32_t
		constexpr size_t kToolTunnelExemplarIds = 0x2E4;  // uint32_t*, indexed by sequence

		// cSC4NetworkCellInfo. 0x53 is isImmovable in NetworkStubs.h; both bytes
		// are what native InsertTunnelPieces sets before inserting a piece.
		constexpr size_t kCellTunnelMarker = 0x51;        // uint8_t
		constexpr size_t kCellImmovable = 0x53;           // uint8_t

		// cSC4TrafficSimulator
		constexpr size_t kTrafficSimTunnelMap = 0xc8;     // Raw::TunnelMap
		constexpr size_t kTrafficSimTunnelList = 0x104;   // Raw::TunnelListNode* sentinel

		// cSC4PathInfo
		constexpr size_t kPathInfoPathMap = 0x1c;         // Raw::PathMap
	}

	// --- Instructions we overwrite -------------------------------------------

	namespace Site
	{
		// Inside cSC4PathInfo::MakeTunnelPaths (0x0053FD70): the 6-byte
		// "call [edx+0xC4]" right after the direction-assignment branches.
		// Hooked to override the direction the function derived from cell
		// coordinates.
		constexpr uint32_t kMakeTunnelPathsDirection = 0x0053FDEE;

		// Also in MakeTunnelPaths: "push eax; mov ecx,ebx; lea ebp,[esi+8]",
		// immediately before peerPathInfo->GetPath(key, 0). Hooked to rewrite
		// the path key looked up on the peer portal.
		constexpr uint32_t kMakeTunnelPathsPeerKey = 0x0053FE31;

		// Inside cSC4PathFinder::FindPath's tunnel branch: the call to
		// AddTripNode. Redirected so we can correct the arrival edge for custom
		// portal pairs.
		constexpr uint32_t kFindPathTunnelAddTripNodeCall = 0x006d9aCF;
		constexpr uint32_t kFindPathTunnelAddTripNodeCallRel = 0xFFFFF4CC;

		// Inside cSC4TrafficSimulator::FloodSubnetwork (0x00717ec0), tunnel
		// branch: the call to GetNetworkInfo. Redirected for the same reason.
		constexpr uint32_t kFloodSubnetworkGetNetworkInfoCall = 0x00718215;
		constexpr uint32_t kFloodSubnetworkGetNetworkInfoCallRel = 0xFFFF7916;
	}
}
