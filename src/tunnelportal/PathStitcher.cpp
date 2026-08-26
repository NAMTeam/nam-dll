#include "PathStitcher.h"

#include "Debug.h"
#include "GameAddresses.h"
#include "Logger.h"
#include "PathMapView.h"
#include "Patching.h"
#include "PortalGeometry.h"
#include "RawLayouts.h"
#include "cIGZUnknown.h"

#include <array>
#include <cstdint>

namespace TunnelPortal::PathStitcher
{
	namespace
	{
		namespace Game = TunnelPortal::Game;
		namespace Site = TunnelPortal::Game::Site;
		namespace Geometry = TunnelPortal::Geometry;
		namespace PathMapView = TunnelPortal::PathMapView;
		namespace Debug = TunnelPortal::Debug;

		// Overrides read by the hooks below, set for the duration of one
		// RefreshTunnelPathInfo call.
		//
		// When non-0xFF, Hook_InitTunnelPath replaces MakeTunnelPaths'
		// coordinate-derived path direction with this path-key direction
		// (kNextX/kNextZ numbering: 0=west, 1=north, 2=east, 3=south).
		uint8_t sCustomPortalFacingOverride = 0xFF;
		uint16_t sCustomPeerPathLookupKeyLowWord = 0xFFFF;
		// Peer portal facing, path-direction numbering, 0xFF when unknown. Used
		// by automatic lookup to prefer a peer path that starts at the peer's
		// tunnel mouth.
		uint8_t sCustomPeerMouthPathDirection = 0xFF;
		void* sCustomSelfPathInfo = nullptr;
		void* sCustomPeerPathInfo = nullptr;

		// Peer-key resolution stats, logged per refresh.
		uint32_t sPeerPathExactKeyCount = 0;
		uint32_t sPeerPathRemappedUniqueKeyCount = 0;
		uint32_t sPeerPathUnresolvedKeyCount = 0;
		std::array<uint32_t, 4> sPeerPathUnresolvedOriginalKeys{};

		// Trampoline pointers for the naked stubs, set by InstallHooks.
		void* sInitTunnelPathTrampolinePtr = nullptr;
		void* sPeerPathLookupTrampolinePtr = nullptr;

		// Path key layout confirmed from cISC4PathInfo::ExtractFullPathKey:
		//   bits 31..28 key type, 27..24 path type, 23..16 uniqueness index,
		//   15..8 entry direction, 7..0 exit direction.
		//
		// Avenue halves can number otherwise-compatible paths differently. Keep
		// the key/path type byte and requested peer directions, but resolve the
		// peer's actual uniqueness byte when the direct rewritten key is absent.
		//
		// MakeTunnelPaths appends the *first* point of whichever peer path this
		// returns, so the choice decides where automata surface. A peer path
		// entering from the peer's mouth side starts at the mouth, the deepest
		// point inside the portal tile; any other peer path starts at a surface
		// edge and makes traffic skip the peer portal. Rank mouth-side entry
		// above the uniqueness index, and use geometry only to break ties.
		uint32_t __stdcall ResolvePeerPathLookupKey(uint32_t originalKey)
		{
			if (sCustomPeerPathLookupKeyLowWord == 0xFFFF)
			{
				return originalKey;
			}

			const bool automaticPeerLookup =
				sCustomPeerPathLookupKeyLowWord == kAutomaticPeerPathLookup;
			// Automatic lookup preserves the original key's type and uniqueness
			// bytes. Its low word only defaults to the original when the peer
			// facing is unknown: for opposite-facing portals the two are the
			// same key anyway, while for same-facing portals reusing the
			// original would land on the peer's *entering* path and stitch to
			// its surface edge.
			const uint16_t peerLookupLowWord = !automaticPeerLookup
				? sCustomPeerPathLookupKeyLowWord
				: (sCustomPeerMouthPathDirection != 0xFF
					? Geometry::PortalExitPathKeyLowWord(sCustomPeerMouthPathDirection)
					: static_cast<uint16_t>(originalKey));
			const uint32_t rewrittenKey =
				(originalKey & 0xFFFF0000u) | peerLookupLowWord;
			const Raw::PathMap* const peerMap = PathMapView::GetPathMap(sCustomPeerPathInfo);
			const Raw::PathMapNode* const exactNode = PathMapView::FindKey(peerMap, rewrittenKey);
			if (PathMapView::HasPoints(exactNode))
			{
				++sPeerPathExactKeyCount;
				return rewrittenKey;
			}
			if (!PathMapView::IsUsable(peerMap))
			{
				if (sPeerPathUnresolvedKeyCount < sPeerPathUnresolvedOriginalKeys.size())
				{
					sPeerPathUnresolvedOriginalKeys[sPeerPathUnresolvedKeyCount] = originalKey;
				}
				++sPeerPathUnresolvedKeyCount;
				return rewrittenKey;
			}

			const uint32_t typeMask = 0xFF000000u;
			const Raw::PathMapNode* const sourceNode =
				PathMapView::FindKey(PathMapView::GetPathMap(sCustomSelfPathInfo), originalKey);
			const Raw::PathPoint* const sourceLastPoint = PathMapView::LastPoint(sourceNode);
			const uint32_t originalUniqueIndex = originalKey & 0x00FF0000u;
			const Raw::PathMapNode* bestNode = nullptr;
			uint32_t bestScore = 0;
			float bestDistanceSquared = 3.4e38f;
			uint32_t visitedNodes = 0;
			for (Raw::PathMapNode** bucket = peerMap->start;
				bucket != peerMap->end && visitedNodes < Raw::kMaxPathMapNodes;
				++bucket)
			{
				for (Raw::PathMapNode* node = *bucket;
					node && visitedNodes < Raw::kMaxPathMapNodes;
					node = node->next)
				{
					++visitedNodes;
					if ((node->key & typeMask) == (originalKey & typeMask)
						&& (automaticPeerLookup
							|| static_cast<uint16_t>(node->key) == sCustomPeerPathLookupKeyLowWord)
						&& PathMapView::HasPoints(node))
					{
						const Raw::PathPoint* const peerFirstPoint = PathMapView::FirstPoint(node);
						float distanceSquared = 0.0f;
						if (sourceLastPoint && peerFirstPoint)
						{
							const float dx = peerFirstPoint->x - sourceLastPoint->x;
							const float dz = peerFirstPoint->z - sourceLastPoint->z;
							distanceSquared = dx * dx + dz * dz;
						}

						const uint8_t entryDirection = static_cast<uint8_t>(node->key >> 8);
						const bool startsAtPeerMouth =
							sCustomPeerMouthPathDirection != 0xFF
								&& entryDirection == sCustomPeerMouthPathDirection;
						const bool keepsUniqueIndex =
							(node->key & 0x00FF0000u) == originalUniqueIndex;
						const uint32_t score =
							(startsAtPeerMouth ? 2u : 0u) + (keepsUniqueIndex ? 1u : 0u);

						if (!bestNode
							|| score > bestScore
							|| (score == bestScore && distanceSquared < bestDistanceSquared))
						{
							bestNode = node;
							bestScore = score;
							bestDistanceSquared = distanceSquared;
						}
					}
				}
			}

			if (bestNode)
			{
				++sPeerPathRemappedUniqueKeyCount;
				return bestNode->key;
			}

			if (sPeerPathUnresolvedKeyCount < sPeerPathUnresolvedOriginalKeys.size())
			{
				sPeerPathUnresolvedOriginalKeys[sPeerPathUnresolvedKeyCount] = originalKey;
			}
			++sPeerPathUnresolvedKeyCount;
			return rewrittenKey;
		}

		// Mid-function hook targeting the 6-byte "call [edx+0xC4]" at 0x0053FDEE inside
		// cSC4PathInfo::MakeTunnelPaths (confirmed function entry: 0x0053FD70).
		//
		// MakeTunnelPaths computes a cardinal direction from A's cell to B's cell and stores it
		// as a byte at [esp+0x58] (param_1 slot reused as a local after the arg is loaded into
		// registers). Prologue: sub esp,0x44 + push ebx,ebp,esi,edi = 0x54 total; param_1 at
		// entry+4 → [esp+0x58] inside the function. All five direction-assignment instructions
		// (C6 44 24 58 00/01/02/03) complete before 0x0053FDEE. By overwriting [esp+0x58] here,
		// after the coordinate comparison but before the loop reads it, we redirect path stitching
		// to the portal's actual facing direction instead of the coordinate-derived one.
		//
		// Register state at 0x0053FDEE (set by the two preceding instructions at 0x0053FDEA/EC):
		//   edx = vtable of esi (otherEnd's vtable) → used by the original call [edx+0xC4]
		//   ecx = esi = otherEnd                    → thiscall receiver for GetPathInfo
		//   eax = clobbered freely by our mov al    → overwritten by the call's return value
		//   esi = otherEnd (preserved, not touched)
		//   edi = pathInfo/this (preserved, not touched)
		NAKED_FUN void Hook_InitTunnelPath()
		{
			__asm {
				cmp  byte ptr [sCustomPortalFacingOverride], 0xFF
				je   done
				mov  al, byte ptr [sCustomPortalFacingOverride]
				mov  byte ptr [esp+0x58], al
			done:
				jmp  dword ptr [sInitTunnelPathTrampolinePtr]
			}
		}

		// Hook site: 0x0053FDEE — the 6-byte "call [edx+0xC4]" instruction inside MakeTunnelPaths.
		// This is the first instruction after all direction-assignment branches have completed.
		Patching::InlineHook sInitTunnelPathHook{
			Site::kMakeTunnelPathsDirection,
			reinterpret_cast<void*>(&Hook_InitTunnelPath),
			{ 0xFF, 0x92, 0xC4, 0x00, 0x00, 0x00 },
			true
		};

		// Hook site: 0x0053FE31 - "push eax; mov ecx,ebx; lea ebp,[esi+8]".
		// This is immediately before MakeTunnelPaths calls peerPathInfo->GetPath(key, 0).
		// For mixed-facing portals, the peer path uses a different low word:
		// low byte = peer exit direction, next byte = opposite peer direction.
		NAKED_FUN void Hook_PeerPathLookupKey()
		{
			__asm {
				cmp  word ptr [sCustomPeerPathLookupKeyLowWord], 0xFFFF
				je   done
				push edx
				push ecx
				push eax
				call ResolvePeerPathLookupKey
				pop  ecx
				pop  edx
			done:
				jmp  dword ptr [sPeerPathLookupTrampolinePtr]
			}
		}

		Patching::InlineHook sPeerPathLookupHook{
			Site::kMakeTunnelPathsPeerKey,
			reinterpret_cast<void*>(&Hook_PeerPathLookupKey),
			{ 0x50, 0x8B, 0xCB, 0x8D, 0x6E, 0x08 },
			true
		};
	}

	// Windows InsertTunnelPieces (0x006287d0) QueryInterfaces both occupants to
	// kcSC4NetworkTunnelOccupant, then calls tunnel->GetPathInfo (+0xc4) and
	// pathInfo->InitTunnelPath (+0x80) with the two tunnel-interface pointers.
	bool RefreshTunnelPathInfo(
		cIGZUnknown* self,
		cIGZUnknown* otherEnd,
		uint8_t selfLookupPathDirection,
		uint16_t peerPathKeyLowWord,
		uint8_t peerMouthPathDirection)
	{
		Logger& logger = Logger::GetInstance();

		if (!self || !otherEnd)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"TunnelPortalTool: cannot refresh path info, self=%p otherEnd=%p.",
				self,
				otherEnd);
			return false;
		}

		void* pathInfo = Game::GetTunnelPathInfo(self);
		if (!pathInfo)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"TunnelPortalTool: tunnel occupant %p has no path info; portal left unstitched.",
				self);
			return false;
		}

		sCustomPortalFacingOverride = selfLookupPathDirection;
		sCustomPeerPathLookupKeyLowWord = peerPathKeyLowWord;
		sCustomPeerMouthPathDirection = peerMouthPathDirection;
		sCustomSelfPathInfo = pathInfo;
		sCustomPeerPathInfo = Game::GetTunnelPathInfo(otherEnd);
		sPeerPathExactKeyCount = 0;
		sPeerPathRemappedUniqueKeyCount = 0;
		sPeerPathUnresolvedKeyCount = 0;
		sPeerPathUnresolvedOriginalKeys.fill(0);
		Game::InitTunnelPath(pathInfo, self, otherEnd);
		if (peerPathKeyLowWord != 0xFFFF)
		{
			Debug::LogPathRefresh(
				selfLookupPathDirection,
				peerPathKeyLowWord,
				sPeerPathExactKeyCount,
				sPeerPathRemappedUniqueKeyCount,
				sPeerPathUnresolvedKeyCount,
				sPeerPathUnresolvedOriginalKeys);
		}
		sCustomPortalFacingOverride = 0xFF;
		sCustomPeerPathLookupKeyLowWord = 0xFFFF;
		sCustomPeerMouthPathDirection = 0xFF;
		sCustomSelfPathInfo = nullptr;
		sCustomPeerPathInfo = nullptr;
		return true;
	}

	void InstallHooks()
	{
		Patching::InstallInlineHook(sInitTunnelPathHook);
		sInitTunnelPathTrampolinePtr = sInitTunnelPathHook.trampoline;
		Patching::InstallInlineHook(sPeerPathLookupHook);
		sPeerPathLookupTrampolinePtr = sPeerPathLookupHook.trampoline;
	}
}
