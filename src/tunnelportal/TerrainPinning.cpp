#include "TerrainPinning.h"

#include "GameAccess.h"
#include "GameAddresses.h"
#include "Logger.h"
#include "NetworkStubs.h"
#include "Patching.h"
#include "RawLayouts.h"
#include "TrafficSimTunnels.h"
#include "GZCLSIDDefs.h"
#include "cISC4City.h"
#include "cISC4NetworkManager.h"
#include "cISC4NetworkOccupant.h"
#include "cISC4TrafficSimulator.h"
#include "cIGZUnknown.h"
#include "cRZAutoRefCount.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace TunnelPortal::TerrainPinning
{
	namespace
	{
		namespace Game = TunnelPortal::Game;
		namespace Site = TunnelPortal::Game::Site;
		namespace Field = TunnelPortal::Game::Field;
		namespace TypeInfo = TunnelPortal::Game::NetworkTypeInfo;

		constexpr uint32_t kNetworkTunnelOccupantID = GZCLSID::kcSC4NetworkTunnelOccupant;

		// Cells this far from a portal, in Chebyshev distance, count as "near"
		// it. One ring covers the shared edge; the second covers the mirrored
		// back vertex InsertTunnelPiece raises one row beyond the portal tile.
		constexpr int32_t kPortalInfluenceRadius = 2;

		// What MaxTerrainHtIncrease/Decrease become while a nearby drag is
		// checked. The properties document 100000.0 as their ceiling, so this
		// lifts the constraint without inventing an out-of-range value.
		constexpr float kRelaxedTerrainHeightLimit = 100000.0f;

		// Flip either of these to isolate one fix while testing.
		bool sEnabled = true;                  // both fixes
		bool sRelaxTerrainHeightLimits = true; // fix B only
		bool sMarkPortalsImmovable = false;     // fix A only

		// Last failure code seen at the top of the hook, so a repeated verdict is
		// only logged once instead of once per preview frame.
		uint32_t sLastLoggedFailureCode = 0;

		const char* FailureCodeName(uint32_t failureCode)
		{
			switch (failureCode)
			{
			case 0:          return "none";
			case 0xF0000001: return "cannot place network on this terrain type";
			case 0xF0000002: return "cannot place on top of reserved tiles";
			case 0xF0000003: return "bridge drag guidance";
			case 0xF0000004: return "tunnel drag guidance";
			case 0xF0000005: return "cannot build highway intersection";
			case 0xF0000006: return "unsuitable grade for construction (SmoothenNetwork)";
			case 0xF0000007: return "unsuitable area to build network (SolveNetwork)";
			case 0xF0000008: return "failed smoothing terrain (CanTilesBeSupportedOnTerrain)";
			default:         return "unknown";
			}
		}

		// One cell of the current drag. Matches SC4Point<unsigned int>.
		struct DraggedCell
		{
			uint32_t x;
			uint32_t z;
		};

		bool IsTunnelOccupant(cISC4NetworkOccupant* occupant)
		{
			if (!occupant)
			{
				return false;
			}

			cRZAutoRefCount<cIGZUnknown> tunnel;
			return occupant->QueryInterface(kNetworkTunnelOccupantID, tunnel.AsPPVoid()) && tunnel;
		}

		// Calls `visit(x, z)` for every portal cell in the traffic simulator's
		// tunnel registry. The registry holds one entry per portal cell, so a
		// two-tile portal contributes both of its lanes.
		template <typename Visitor>
		void ForEachRegisteredPortalCell(Visitor&& visit)
		{
			cISC4TrafficSimulator* const trafficSimulator = GameAccess::GetTrafficSimulator();
			const Raw::TunnelMap* const map = TrafficSimTunnels::GetMap(trafficSimulator);
			if (!TrafficSimTunnels::IsUsableMap(map))
			{
				return;
			}

			uint32_t visitedNodes = 0;
			for (Raw::TunnelMapNode** bucket = map->start;
				bucket != map->end && visitedNodes < Raw::kMaxTunnelMapNodes;
				++bucket)
			{
				for (Raw::TunnelMapNode* node = *bucket;
					node && visitedNodes < Raw::kMaxTunnelMapNodes;
					node = node->next)
				{
					++visitedNodes;
					// Key layout matches SavedTunnelScanner: (x << 8) | z.
					visit(
						static_cast<uint32_t>(node->key >> 8),
						static_cast<uint32_t>(node->key & 0xFF));
				}
			}
		}

		// True when any cell of the drag sits within kPortalInfluenceRadius of a
		// registered portal. Keeps the relaxed limits off every drag that has
		// nothing to do with a portal.
		bool DragRunsNearPortal(cSC4NetworkTool* tool)
		{
			if (!tool)
			{
				return false;
			}

			const DraggedCell* const begin =
				Game::FieldAt<DraggedCell*>(tool, Field::kToolDraggedCells);
			const DraggedCell* const end =
				Game::FieldAt<DraggedCell*>(tool, Field::kToolDraggedCellsEnd);
			if (!begin || !end || end < begin)
			{
				return false;
			}

			bool nearPortal = false;
			ForEachRegisteredPortalCell([&] (uint32_t portalX, uint32_t portalZ)
			{
				if (nearPortal)
				{
					return;
				}

				for (const DraggedCell* cell = begin; cell != end; ++cell)
				{
					const int32_t dx =
						static_cast<int32_t>(cell->x) - static_cast<int32_t>(portalX);
					const int32_t dz =
						static_cast<int32_t>(cell->z) - static_cast<int32_t>(portalZ);
					if (dx >= -kPortalInfluenceRadius && dx <= kPortalInfluenceRadius
						&& dz >= -kPortalInfluenceRadius && dz <= kPortalInfluenceRadius)
					{
						nearPortal = true;
						return;
					}
				}
			});

			return nearPortal;
		}

		// Widens MaxTerrainHtIncrease/Decrease on one network type's
		// cSC4NetworkTypeInfo, restoring the originals on destruction.
		class RelaxedTerrainHeightLimits
		{
		public:
			explicit RelaxedTerrainHeightLimits(uint32_t networkType)
				: typeInfo(Game::NetworkTypeInfoFor(networkType))
			{
				originalIncrease =
					Game::FieldAt<float>(typeInfo, TypeInfo::kMaxTerrainHeightIncrease);
				originalDecrease =
					Game::FieldAt<float>(typeInfo, TypeInfo::kMaxTerrainHeightDecrease);
				Game::FieldAt<float>(typeInfo, TypeInfo::kMaxTerrainHeightIncrease) =
					kRelaxedTerrainHeightLimit;
				Game::FieldAt<float>(typeInfo, TypeInfo::kMaxTerrainHeightDecrease) =
					kRelaxedTerrainHeightLimit;
			}

			~RelaxedTerrainHeightLimits()
			{
				Game::FieldAt<float>(typeInfo, TypeInfo::kMaxTerrainHeightIncrease) =
					originalIncrease;
				Game::FieldAt<float>(typeInfo, TypeInfo::kMaxTerrainHeightDecrease) =
					originalDecrease;
			}

			RelaxedTerrainHeightLimits(const RelaxedTerrainHeightLimits&) = delete;
			RelaxedTerrainHeightLimits& operator=(const RelaxedTerrainHeightLimits&) = delete;

		private:
			void* typeInfo;
			float originalIncrease = 0.0f;
			float originalDecrease = 0.0f;
		};

		// Replaces the "call [edx+0xe0]" inside PlaceNetwork. __fastcall matches
		// the virtual's __thiscall shape: this in ECX, the one stack argument
		// callee-cleaned, so the 4-byte cleanup stays correct.
		//
		// The original is still reached through the vtable rather than by direct
		// address, so cSC4UndergroundNetworkTool's override still wins for
		// subway and pipe drags.
		bool __fastcall Hook_CanTilesBeSupportedOnTerrain(
			cSC4NetworkTool* tool,
			void*,
			uint32_t networkType)
		{
			// The code from the previous PlaceNetwork attempt: it is only written
			// once all four smoothing levels have been tried, so at this point it
			// still describes the last completed drag.
			const uint32_t previousFailureCode =
				Game::FieldAt<uint32_t>(tool, Field::kToolFailureCode);
			if (previousFailureCode != sLastLoggedFailureCode)
			{
				sLastLoggedFailureCode = previousFailureCode;
				Logger::GetInstance().WriteLineFormatted(
					LogLevel::Debug,
					"TunnelPortal: last network placement failure code 0x%08X (%s).",
					previousFailureCode,
					FailureCodeName(previousFailureCode));
			}

			if (!sEnabled || !sRelaxTerrainHeightLimits || !DragRunsNearPortal(tool))
			{
				return Game::CanTilesBeSupportedOnTerrain(tool, networkType);
			}

			// With both limits at the property ceiling the two range gates inside
			// CanTilesBeSupportedOnTerrain cannot trip, so a false here is its
			// closing cSC4VertexHtConstraintSatisfier::Solve failing instead - a
			// conflict between fixed vertices, which widening ranges cannot fix.
			const RelaxedTerrainHeightLimits relaxedLimits(networkType);
			const bool supported = Game::CanTilesBeSupportedOnTerrain(tool, networkType);
			if (!supported)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Debug,
					"TunnelPortal: drag beside a portal still rejected with terrain height limits "
					"relaxed; the vertex height solve is the blocker, not the height budget.");
			}
			return supported;
		}
	}

	void MarkCommittedPortalsImmovable()
	{
		if (!sEnabled || !sMarkPortalsImmovable)
		{
			return;
		}

		cISC4City* const city = GameAccess::GetCity();
		cISC4NetworkManager* const networkManager = city ? city->GetNetworkManager() : nullptr;
		if (!networkManager)
		{
			return;
		}

		uint32_t markedCount = 0;
		ForEachRegisteredPortalCell([&] (uint32_t x, uint32_t z)
		{
			cISC4NetworkOccupant::eNetworkType networkType = cISC4NetworkOccupant::Road;
			if (!GameAccess::FindNetworkAtTile(x, z, networkType))
			{
				return;
			}

			cSC4NetworkTool* const tool =
				GameAccess::GetNetworkTool(networkManager, networkType);
			if (!tool)
			{
				return;
			}

			cSC4NetworkCellInfo* const cellInfo = tool->GetCellInfo((z << 16) | x);
			cISC4NetworkOccupant* const occupant = cellInfo ? cellInfo->networkOccupant : nullptr;
			if (!IsTunnelOccupant(occupant))
			{
				return;
			}

			if (!occupant->IsImmovable())
			{
				occupant->SetImmovable(true);
				++markedCount;
			}

			// The cached cell keeps the stale value until the world cache
			// rebuilds the entry, so bring it forward now.
			cellInfo->isImmovable = true;
		});

		if (markedCount != 0)
		{
			Logger::GetInstance().WriteLineFormatted(
				LogLevel::Info,
				"TunnelPortal: marked %u tunnel portal occupant(s) immovable.",
				markedCount);
		}
	}

	void SetEnabled(bool enabled)
	{
		sEnabled = enabled;
	}

	bool IsEnabled()
	{
		return sEnabled;
	}

	void InstallHooks()
	{
		// The site is a 6-byte indirect call; a 5-byte relative call plus a NOP
		// replaces it exactly.
		auto* const site =
			reinterpret_cast<uint8_t*>(Site::kPlaceNetworkCanTilesBeSupportedCall);
		for (size_t i = 0; i < Site::kPlaceNetworkCanTilesBeSupportedBytes.size(); ++i)
		{
			if (site[i] != Site::kPlaceNetworkCanTilesBeSupportedBytes[i])
			{
				Logger::GetInstance().WriteLineFormatted(
					LogLevel::Error,
					"TunnelPortal: CanTilesBeSupportedOnTerrain call site at 0x%08X does not match "
					"the expected bytes; leaving it unpatched.",
					Site::kPlaceNetworkCanTilesBeSupportedCall);
				return;
			}
		}

		const uint32_t hookRel =
			reinterpret_cast<uint32_t>(&Hook_CanTilesBeSupportedOnTerrain)
				- (Site::kPlaceNetworkCanTilesBeSupportedCall + 5);
		Patching::OverwriteMemory(site, static_cast<uint8_t>(0xE8));
		Patching::OverwriteMemory(site + 1, hookRel);
		Patching::OverwriteMemory(site + 5, static_cast<uint8_t>(0x90));
	}
}
