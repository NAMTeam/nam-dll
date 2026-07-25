#include "RouteEdgeFixes.h"

#include "GameAccess.h"
#include "GameAddresses.h"
#include "Logger.h"
#include "Patching.h"
#include "cISC4TrafficSimulator.h"

#include <array>
#include <cstdint>

namespace TunnelPortal::RouteEdgeFixes
{
	namespace
	{
		namespace Game = TunnelPortal::Game;
		namespace Site = TunnelPortal::Game::Site;

		struct Fix
		{
			Endpoint first;
			Endpoint second;
			uint8_t firstArrivalEdge = 0xFF;
			uint8_t secondArrivalEdge = 0xFF;
			bool active = false;
		};

		// One entry per custom portal pair. Sized far above any realistic city's
		// portal count so the table never wraps in practice, but small enough that
		// the fixed reservation stays trivial (~32 B/entry, ~256 KB total).
		constexpr uint32_t kMaxFixes = 8192;

		std::array<Fix, kMaxFixes> sFixes{};
		uint32_t sActiveFixCount = 0;
		cISC4TrafficSimulator* sRegisteredTrafficSimulator = nullptr;
		bool sScanned = false;
		RescanForLookupFn sRescanForLookup = nullptr;

		bool SameCell(const Endpoint& endpoint, uint32_t x, uint32_t z)
		{
			return endpoint.x == x && endpoint.z == z;
		}

		// True when `fix` records the same portal pair as (a,b), in either order.
		bool SamePair(const Fix& fix, const Endpoint& a, const Endpoint& b)
		{
			return (SameCell(fix.first, a.x, a.z) && SameCell(fix.second, b.x, b.z))
				|| (SameCell(fix.first, b.x, b.z) && SameCell(fix.second, a.x, a.z));
		}

		void ClearAll()
		{
			for (Fix& fix : sFixes)
			{
				fix = {};
			}
			sActiveFixCount = 0;
		}

		// Arrival edge for a step from (currentX,currentZ) to (destX,destZ) across
		// a registered portal pair, in either direction. False when no fix applies.
		bool TryGetArrivalEdge(
			uint32_t currentX,
			uint32_t currentZ,
			uint32_t destinationX,
			uint32_t destinationZ,
			uint8_t& edgeOut)
		{
			cISC4TrafficSimulator* const trafficSimulator = GameAccess::GetTrafficSimulator();
			if (trafficSimulator != sRegisteredTrafficSimulator)
			{
				Reset(trafficSimulator);
			}
			if (!sScanned && trafficSimulator && sRescanForLookup)
			{
				// This can run inside pathfinder hooks, so only rebuild the lookup
				// table here. The eager scan does path-vector mutations at a safe
				// lifecycle point instead.
				sScanned = sRescanForLookup(trafficSimulator);
			}

			for (uint32_t i = 0; i < sActiveFixCount; ++i)
			{
				const Fix& fix = sFixes[i];
				if (!fix.active)
				{
					continue;
				}

				if (SameCell(fix.first, currentX, currentZ) && SameCell(fix.second, destinationX, destinationZ))
				{
					edgeOut = fix.secondArrivalEdge;
					return true;
				}
				if (SameCell(fix.second, currentX, currentZ) && SameCell(fix.first, destinationX, destinationZ))
				{
					edgeOut = fix.firstArrivalEdge;
					return true;
				}
			}

			return false;
		}

		// FindPath queues tunnel steps through AddTripNode. We correct the arrival
		// edge for registered portal pairs before the native call.
		void __fastcall Hook_AddTunnelTripNode(
			void* pathFinder,
			void*,
			uint32_t x,
			uint32_t z,
			uint8_t edge,
			uint8_t travelMode,
			float cost,
			int currentNode,
			float heuristic,
			char outOfBounds)
		{
			const uint32_t currentX = currentNode
				? static_cast<uint32_t>(static_cast<uint16_t>(Game::FieldAt<int16_t>(reinterpret_cast<void*>(currentNode), 0x14)))
				: 0xFFFF;
			const uint32_t currentZ = currentNode
				? static_cast<uint32_t>(static_cast<uint16_t>(Game::FieldAt<int16_t>(reinterpret_cast<void*>(currentNode), 0x16)))
				: 0xFFFF;

			uint8_t replacementEdge = 0xFF;
			if (currentNode && TryGetArrivalEdge(currentX, currentZ, x, z, replacementEdge))
			{
				if ((edge & 3) != replacementEdge)
				{
					edge = static_cast<uint8_t>((edge & ~0x03u) | replacementEdge);
				}
			}

			Game::AddTripNode(pathFinder, x, z, edge, travelMode, cost, currentNode, heuristic, outOfBounds);
		}

		// FloodSubnetwork tunnel edge fix.
		//
		// FloodSubnetwork (Windows: 0x00717ec0) stamps subnet IDs on reachable
		// network edges using a DFS. Its tunnel branch (pseudo-direction 5) looks up
		// a tunnel connection via the map at this+0xc8, then jumps the flood to the
		// peer portal cell. It reuses the current node's "side" as the arrival side
		// at the peer, which can be wrong for custom portal pairs.
		//
		// We fix this by redirecting the CALL to GetNetworkInfo at 0x00718215 (inside
		// the tunnel branch) to a wrapper that calls the original, then overwrites
		// uStack_68 on the caller's stack with the registered destination arrival
		// edge when one of our custom tunnel portal pairs is involved.
		//
		// Stack layout at hook entry (ESP = E):
		//   [E+0x00] return address (0x71821A)
		//   [E+0x04] networkType (PUSH EDX)
		//   [E+0x08] peerX       (PUSH EBX)
		//   [E+0x0C] peerY       (PUSH ECX)
		//   [E+0x10..] FloodSubnetwork's adjusted_ESP locals
		//     [E+0x24] = packed state (byte0=x, byte1=y, byte2=side, byte3=nextDir)
		//     [E+0x30] = uStack_68 (the edge value to fix)
		//   ECX = this (cSC4TrafficSimulator*)
		//   EBX = peerX (preserved across calls)

		// Jump target for the naked FloodSubnetwork hook. Same function as
		// Game::TrafficSimGetNetworkInfo; held in a variable so the asm can
		// "jmp dword ptr [sOriginalFloodGetNetworkInfo]".
		void* sOriginalFloodGetNetworkInfo =
			reinterpret_cast<void*>(Game::TrafficSimGetNetworkInfo);

		bool __stdcall ShouldFixFloodSubnetworkTunnelEdge(
			uint32_t currentX,
			uint32_t currentY,
			uint32_t peerX,
			uint32_t peerY)
		{
			uint8_t arrivalEdge = 0xFF;
			return TryGetArrivalEdge(currentX, currentY, peerX, peerY, arrivalEdge);
		}

		void __stdcall FloodSubnetworkFixTunnelEdge(
			uint32_t currentX,
			uint32_t currentY,
			uint32_t peerX,
			uint32_t peerY,
			uint32_t* edgePtr)
		{
			uint8_t arrivalEdge = 0xFF;
			if (edgePtr && TryGetArrivalEdge(currentX, currentY, peerX, peerY, arrivalEdge))
			{
				const uint32_t currentEdge = *edgePtr & 0xFF;
				if (currentEdge != arrivalEdge)
				{
					*edgePtr = (*edgePtr & ~0x03u) | arrivalEdge;
				}
			}
		}

		NAKED_FUN void Hook_FloodSubnetworkTunnelGetNetworkInfo()
		{
			__asm {
				// Native tunnels take the original call path. Registered custom portal
				// pairs use the wrapper with their explicit destination arrival edge.
				push ecx
				push edx
				push dword ptr [esp+0x14]       // peerY, ESP = E-12
				push dword ptr [esp+0x14]       // peerX, ESP = E-16
				movzx eax, byte ptr [esp+0x35]  // currentY, byte 1 of packed state at E+0x25
				push eax
				movzx eax, byte ptr [esp+0x38]  // currentX, byte 0 of packed state at E+0x24
				push eax
				call ShouldFixFloodSubnetworkTunnelEdge
				pop  edx
				pop  ecx
				test al, al
				jnz  customTunnelEdgeFix
				jmp  dword ptr [sOriginalFloodGetNetworkInfo]

			customTunnelEdgeFix:
				// Phase 1: Call original GetNetworkInfo(this, networkType, peerX, peerY).
				// ECX = this, stack = [retaddr, netType, peerX, peerY].
				// Re-push args for the real thiscall (callee cleans 12 bytes).
				push dword ptr [esp+0x0C]
				push dword ptr [esp+0x0C]
				push dword ptr [esp+0x0C]
				call dword ptr [sOriginalFloodGetNetworkInfo]
				// EAX = peer NetworkInfo*, ESP = E (3 re-pushed args cleaned by callee).

				// Phase 2: Fix uStack_68 on the caller's stack.
				push eax                          // save GetNetworkInfo result, ESP = E-4
				push ecx                          // ESP = E-8
				push edx                          // ESP = E-12

				// Key locations relative to hook entry ESP (= E):
				//   [E+0x08]  = peerX  (original caller arg)
				//   [E+0x0C]  = peerY  (original caller arg)
				//   [E+0x24]  = uStack_74 byte 0 = currentX
				//   [E+0x25]  = uStack_74 byte 1 = currentY
				//   [E+0x30]  = uStack_68 (arrival edge to fix)
				// NOTE: ESP shifts with each push; offsets below account for that.

				lea  eax, [esp + 0x3C]            // &uStack_68,       ESP = E-12
				push eax                          // arg5: edgePtr,    ESP = E-16
				push dword ptr [esp + 0x1C]       // arg4: peerY,      ESP = E-20  [E-16+0x1C]=[E+0x0C]
				push dword ptr [esp + 0x1C]       // arg3: peerX,      ESP = E-24  [E-20+0x1C]=[E+0x08]
				movzx eax, byte ptr [esp + 0x3D]  // currentY (byte 1 of packed state at E+0x25) ESP = E-24
				push eax                          // arg2: currentY,   ESP = E-28
				movzx eax, byte ptr [esp + 0x40]  // currentX (byte 0 of packed state at E+0x24) ESP = E-28
				push eax                          // arg1: currentX,   ESP = E-32
				call FloodSubnetworkFixTunnelEdge  // __stdcall, cleans 20 bytes → ESP = E-12

				pop  edx
				pop  ecx
				pop  eax                          // restored GetNetworkInfo result, ESP = E

				ret  0x0C                         // clean 3 original args, like the real GetNetworkInfo
			}
		}
	}

	void SetRescanForLookup(RescanForLookupFn fn)
	{
		sRescanForLookup = fn;
	}

	void Reset(cISC4TrafficSimulator* trafficSimulator)
	{
		ClearAll();
		sRegisteredTrafficSimulator = trafficSimulator;
		sScanned = false;
	}

	void MarkScanned()
	{
		sScanned = true;
	}

	void Register(
		const Endpoint& first,
		const Endpoint& second,
		uint8_t firstArrivalEdge,
		uint8_t secondArrivalEdge)
	{
		// Update an existing entry for the same portal pair rather than appending
		// a duplicate. Re-registration happens on every rescan of a saved city.
		for (uint32_t i = 0; i < sActiveFixCount; ++i)
		{
			Fix& fix = sFixes[i];
			if (!fix.active || !SamePair(fix, first, second))
			{
				continue;
			}

			if (SameCell(fix.first, first.x, first.z))
			{
				fix.firstArrivalEdge = firstArrivalEdge & 3;
				fix.secondArrivalEdge = secondArrivalEdge & 3;
			}
			else
			{
				// Stored endpoints are in the opposite order.
				fix.firstArrivalEdge = secondArrivalEdge & 3;
				fix.secondArrivalEdge = firstArrivalEdge & 3;
			}
			return;
		}

		if (sActiveFixCount >= sFixes.size())
		{
			// Drop the new fix rather than silently overwrite an unrelated existing
			// one (which would misroute a different portal pair).
			Logger::GetInstance().WriteLineFormatted(
				LogLevel::Error,
				"TunnelPortal: route-edge fix table full (%u entries); dropping fix for (%u,%u)-(%u,%u). "
				"Routing across this portal pair may be incorrect.",
				static_cast<uint32_t>(sFixes.size()),
				first.x,
				first.z,
				second.x,
				second.z);
			return;
		}

		Fix& fix = sFixes[sActiveFixCount++];
		fix.first = first;
		fix.second = second;
		fix.firstArrivalEdge = firstArrivalEdge & 3;
		fix.secondArrivalEdge = secondArrivalEdge & 3;
		fix.active = true;
	}

	void InstallHooks()
	{
		const uint32_t addTripNodeHookRel =
			reinterpret_cast<uint32_t>(&Hook_AddTunnelTripNode)
				- (Site::kFindPathTunnelAddTripNodeCall + 5);
		Patching::PatchImmediate32(
			Site::kFindPathTunnelAddTripNodeCall + 1,
			Site::kFindPathTunnelAddTripNodeCallRel,
			addTripNodeHookRel);

		const uint32_t floodTunnelHookRel =
			reinterpret_cast<uint32_t>(&Hook_FloodSubnetworkTunnelGetNetworkInfo)
				- (Site::kFloodSubnetworkGetNetworkInfoCall + 5);
		Patching::PatchImmediate32(
			Site::kFloodSubnetworkGetNetworkInfoCall + 1,
			Site::kFloodSubnetworkGetNetworkInfoCallRel,
			floodTunnelHookRel);
	}
}
