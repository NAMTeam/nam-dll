#include "SavedTunnelScanner.h"

#include "GameAccess.h"
#include "PathStitcher.h"
#include "PieceInserter.h"
#include "PortalGeometry.h"
#include "RawLayouts.h"
#include "RouteEdgeFixes.h"
#include "TrafficSimTunnels.h"
#include "GZCLSIDDefs.h"
#include "NetworkStubs.h"
#include "cISC4City.h"
#include "cISC4NetworkManager.h"
#include "cISC4NetworkOccupant.h"
#include "cISC4TrafficSimulator.h"
#include "cIGZUnknown.h"
#include "cRZAutoRefCount.h"

#include <cstdint>

namespace TunnelPortal::SavedTunnelScanner
{
	namespace
	{
		namespace Geometry = TunnelPortal::Geometry;

		constexpr uint32_t kNetworkTunnelOccupantID = GZCLSID::kcSC4NetworkTunnelOccupant;

		// Resolves the tunnel occupant at an endpoint and its portal facing.
		// Writes endpoint.networkType, directionOut and tunnelOut on success.
		bool TryGetTunnelAtEndpoint(
			Endpoint& endpoint,
			uint8_t& directionOut,
			cRZAutoRefCount<cIGZUnknown>& tunnelOut)
		{
			if (!GameAccess::FindNetworkAtTile(endpoint.x, endpoint.z, endpoint.networkType))
			{
				return false;
			}

			cISC4City* city = GameAccess::GetCity();
			cISC4NetworkManager* networkManager = city ? city->GetNetworkManager() : nullptr;
			cSC4NetworkTool* tool = GameAccess::GetNetworkTool(networkManager, endpoint.networkType);
			cSC4NetworkCellInfo* cellInfo = PieceInserter::GetEndpointCell(tool, endpoint);
			cISC4NetworkOccupant* occupant = cellInfo ? cellInfo->networkOccupant : nullptr;
			if (occupant && occupant->QueryInterface(kNetworkTunnelOccupantID, tunnelOut.AsPPVoid()) && tunnelOut)
			{
				// The placed occupant stores the exemplar/model rotation, which is
				// one quarter-turn ahead of InsertTunnelPiece's direction code for
				// the native tunnel arrays. Prefer the surviving surface approach
				// edge because it expresses the original portal direction directly;
				// use the inverse rotation transform only when the edge is ambiguous.
				directionOut = static_cast<uint8_t>((occupant->GetRotation() + 3) & 3);
				Geometry::TryInferTunnelPieceDirectionFromSurfaceApproach(
					cellInfo,
					endpoint.networkType,
					directionOut);
				return true;
			}

			return false;
		}

		bool RescanForLookup(cISC4TrafficSimulator* trafficSimulator)
		{
			return Scan(trafficSimulator, false);
		}
	}

	bool Scan(cISC4TrafficSimulator* trafficSimulator, bool refreshPathInfo)
	{
		const Raw::TunnelMap* const map = TrafficSimTunnels::GetMap(trafficSimulator);
		if (!TrafficSimTunnels::IsUsableMap(map))
		{
			return false;
		}

		uint32_t visitedNodes = 0;
		for (Raw::TunnelMapNode** bucket = map->start; bucket != map->end && visitedNodes < Raw::kMaxTunnelMapNodes; ++bucket)
		{
			for (Raw::TunnelMapNode* node = *bucket; node && visitedNodes < Raw::kMaxTunnelMapNodes; node = node->next)
			{
				++visitedNodes;
				Endpoint first;
				first.x = static_cast<uint8_t>(node->key >> 8);
				first.z = static_cast<uint8_t>(node->key & 0xFF);
				Endpoint second;
				second.x = static_cast<uint8_t>(node->value[0] & 0xFF);
				second.z = static_cast<uint8_t>((node->value[0] >> 8) & 0xFF);

				// The registry holds both directions; process each pair once, from
				// its lower-keyed end.
				const uint16_t firstKey = TrafficSimTunnels::PackedCellKey(first.x, first.z);
				const uint16_t secondKey = TrafficSimTunnels::PackedCellKey(second.x, second.z);
				if (node->key != firstKey || firstKey > secondKey)
				{
					continue;
				}

				uint8_t firstDirection = 0xFF;
				uint8_t secondDirection = 0xFF;
				cRZAutoRefCount<cIGZUnknown> firstTunnel;
				cRZAutoRefCount<cIGZUnknown> secondTunnel;
				if (!TryGetTunnelAtEndpoint(first, firstDirection, firstTunnel)
					|| !TryGetTunnelAtEndpoint(second, secondDirection, secondTunnel))
				{
					continue;
				}

				const uint8_t firstVectorDirection = Geometry::InferTunnelPieceDirection(first, second);
				const uint8_t secondVectorDirection = Geometry::InferTunnelPieceDirection(second, first);
				const bool requiresCustomPathStitch =
					firstDirection != firstVectorDirection
						|| secondDirection != secondVectorDirection;
				const bool requiresAvenuePathKeyResolution =
					Geometry::IsTwoTileNetwork(first.networkType)
						|| Geometry::IsTwoTileNetwork(second.networkType);
				if (!requiresCustomPathStitch && !requiresAvenuePathKeyResolution)
				{
					continue;
				}

				if (requiresCustomPathStitch)
				{
					RouteEdgeFixes::Register(
						first,
						second,
						Geometry::TunnelPieceDirectionToPathDirection(firstDirection),
						Geometry::TunnelPieceDirectionToPathDirection(secondDirection));
				}
				if (refreshPathInfo)
				{
					const uint8_t firstPathDirection =
						Geometry::TunnelPieceDirectionToPathDirection(firstDirection);
					const uint8_t secondPathDirection =
						Geometry::TunnelPieceDirectionToPathDirection(secondDirection);
					const uint16_t firstPeerLookup = requiresAvenuePathKeyResolution
						? PathStitcher::kAutomaticPeerPathLookup
						: Geometry::PortalExitPathKeyLowWord(secondPathDirection);
					const uint16_t secondPeerLookup = requiresAvenuePathKeyResolution
						? PathStitcher::kAutomaticPeerPathLookup
						: Geometry::PortalExitPathKeyLowWord(firstPathDirection);
					PathStitcher::RefreshTunnelPathInfo(
						firstTunnel,
						secondTunnel,
						firstPathDirection,
						firstPeerLookup,
						secondPathDirection);
					PathStitcher::RefreshTunnelPathInfo(
						secondTunnel,
						firstTunnel,
						secondPathDirection,
						secondPeerLookup,
						firstPathDirection);
				}
			}
		}

		return true;
	}

	void InstallRescanCallback()
	{
		RouteEdgeFixes::SetRescanForLookup(&RescanForLookup);
	}
}
