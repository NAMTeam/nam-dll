#include "PortalPlacer.h"

#include "Debug.h"
#include "GameAccess.h"
#include "GameAddresses.h"
#include "Logger.h"
#include "NetworkStubs.h"
#include "PathMapView.h"
#include "PathStitcher.h"
#include "PieceInserter.h"
#include "PortalFootprint.h"
#include "PortalGeometry.h"
#include "RouteEdgeFixes.h"
#include "TerrainPinning.h"
#include "TrafficSimTunnels.h"
#include "cISC4City.h"
#include "cISC4NetworkManager.h"
#include "cISC4NetworkOccupant.h"
#include "cISC4TrafficSimulator.h"
#include "cIGZUnknown.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace TunnelPortal::PortalPlacer
{
	namespace
	{
		namespace Game = TunnelPortal::Game;
		namespace Geometry = TunnelPortal::Geometry;
		namespace PathMapView = TunnelPortal::PathMapView;
		namespace PathStitcher = TunnelPortal::PathStitcher;
		namespace TerrainPinning = TunnelPortal::TerrainPinning;
		namespace Debug = TunnelPortal::Debug;

		// Chooses how to pair the two lanes of a two-tile portal. Prefers path
		// semantics (a one-way lane must join an exit half to an entry half);
		// falls back to nearest-geometry when both pairings look the same.
		bool ShouldReverseTwoTilePairing(
			const std::array<PortalCell, 2>& firstCells,
			const std::array<PortalCell, 2>& secondCells,
			uint8_t firstDirection,
			uint8_t secondDirection)
		{
			const auto distanceSquared = [](const Endpoint& a, const Endpoint& b)
			{
				const int64_t dx = static_cast<int64_t>(b.x) - static_cast<int64_t>(a.x);
				const int64_t dz = static_cast<int64_t>(b.z) - static_cast<int64_t>(a.z);
				return dx * dx + dz * dz;
			};
			const int64_t directPairingDistance =
				distanceSquared(firstCells[0].endpoint, secondCells[0].endpoint)
				+ distanceSquared(firstCells[1].endpoint, secondCells[1].endpoint);
			const int64_t reversePairingDistance =
				distanceSquared(firstCells[1].endpoint, secondCells[0].endpoint)
				+ distanceSquared(firstCells[0].endpoint, secondCells[1].endpoint);

			const std::array<uint32_t, 2> firstFacingExitPathCounts = {
				PathMapView::CountPathsExitingToward(firstCells[0].tunnel, firstDirection),
				PathMapView::CountPathsExitingToward(firstCells[1].tunnel, firstDirection),
			};
			const std::array<uint32_t, 2> secondFacingExitPathCounts = {
				PathMapView::CountPathsExitingToward(secondCells[0].tunnel, secondDirection),
				PathMapView::CountPathsExitingToward(secondCells[1].tunnel, secondDirection),
			};
			const auto pairingScore = [&] (bool reverse)
			{
				uint32_t score = 0;
				for (size_t i = 0; i < 2; ++i)
				{
					const size_t firstIndex = reverse ? 1 - i : i;
					const bool firstHasFacingExitPath = firstFacingExitPathCounts[firstIndex] != 0;
					const bool secondHasFacingExitPath = secondFacingExitPathCounts[i] != 0;
					if (firstHasFacingExitPath != secondHasFacingExitPath)
					{
						++score;
					}
				}
				return score;
			};
			const uint32_t directPathScore = pairingScore(false);
			const uint32_t reversePathScore = pairingScore(true);
			const bool pathSemanticsDistinguishPairing = directPathScore != reversePathScore;

			bool reverseFirstPortalPairing;
			if (pathSemanticsDistinguishPairing)
			{
				// A one-way Avenue lane must join one path half that exits toward
				// its mouth to one half that enters from its mouth. This
				// distinguishes perpendicular and same-facing mouths, where
				// geometric distance alone can tie or pick the wrong carriageway.
				reverseFirstPortalPairing = reversePathScore > directPathScore;
			}
			else
			{
				// Preserve native LIFO on an otherwise unresolved tie.
				reverseFirstPortalPairing = reversePairingDistance <= directPairingDistance;
			}

			Debug::LogPairingDecision(
				reverseFirstPortalPairing,
				pathSemanticsDistinguishPairing,
				directPathScore,
				reversePathScore,
				firstFacingExitPathCounts,
				secondFacingExitPathCounts,
				directPairingDistance,
				reversePairingDistance);
			return reverseFirstPortalPairing;
		}

		bool PlacePortalPairInternal(
			const Endpoint& first,
			const Endpoint& second,
			const Styles::Style& style)
		{
			Logger& logger = Logger::GetInstance();

			Debug::LogPlacementAttempt(first, second, style);

			if (first.networkType != second.networkType)
			{
				logger.WriteLine(LogLevel::Error, "Tunnel portal endpoints must be on the same network type.");
				return false;
			}
			if (style.networkType != first.networkType
				|| style.tileCount != (Geometry::IsTwoTileNetwork(first.networkType) ? 2 : 1))
			{
				logger.WriteLine(
					LogLevel::Error,
					"TunnelPortalTool: selected facade style is incompatible with the portal network or width.");
				return false;
			}

			cISC4City* city = GameAccess::GetCity();
			cISC4NetworkManager* networkManager = city ? city->GetNetworkManager() : nullptr;
			cSC4NetworkTool* tool = GameAccess::GetNetworkTool(networkManager, first.networkType);

			if (!tool)
			{
				logger.WriteLine(LogLevel::Error, "Could not acquire the network tool for tunnel portal placement.");
				return false;
			}

			cSC4NetworkCellInfo* firstCell = PieceInserter::GetEndpointCell(tool, first);
			cSC4NetworkCellInfo* secondCell = PieceInserter::GetEndpointCell(tool, second);

			if (!firstCell || !secondCell)
			{
				logger.WriteLine(LogLevel::Error, "Tunnel portal placement requires both endpoints to be existing compatible network tiles.");
				return false;
			}

			uint8_t firstDirection = Geometry::InferTunnelPieceDirection(first, second);
			uint8_t secondDirection = Geometry::InferTunnelPieceDirection(second, first);
			std::array<PortalCell, 2> firstPortalCells{};
			std::array<PortalCell, 2> secondPortalCells{};
			size_t portalCellCount = 1;

			{
				PieceInserter::PlacementStateScope placementState(tool);
				Geometry::TryInferTunnelPieceDirectionFromSurfaceApproach(
					firstCell,
					first.networkType,
					firstDirection);
				Geometry::TryInferTunnelPieceDirectionFromSurfaceApproach(
					secondCell,
					second.networkType,
					secondDirection);

				const size_t firstPortalCellCount = Footprint::BuildCellList(
					tool,
					"first",
					first,
					firstCell,
					firstDirection,
					firstPortalCells);
				const size_t secondPortalCellCount = Footprint::BuildCellList(
					tool,
					"second",
					second,
					secondCell,
					secondDirection,
					secondPortalCells);

				if (firstPortalCellCount == 0 || secondPortalCellCount == 0
					|| firstPortalCellCount != secondPortalCellCount)
				{
					logger.WriteLineFormatted(
						LogLevel::Error,
						"TunnelPortalTool: portal endpoints did not resolve one compatible shape, first tile count=%u second tile count=%u.",
						static_cast<uint32_t>(firstPortalCellCount),
						static_cast<uint32_t>(secondPortalCellCount));
					return false;
				}
				portalCellCount = firstPortalCellCount;

				for (size_t i = 0; i < portalCellCount; ++i)
				{
					PieceInserter::PrepareEndpointCell(firstPortalCells[i].cellInfo);
					firstPortalCells[i].occupant = PieceInserter::InsertWithStyle(
						tool,
						firstDirection,
						firstPortalCells[i].sequenceIndex,
						firstPortalCells[i].cellInfo,
						style);
				}
				for (size_t i = 0; i < portalCellCount; ++i)
				{
					PieceInserter::PrepareEndpointCell(secondPortalCells[i].cellInfo);
					secondPortalCells[i].occupant = PieceInserter::InsertWithStyle(
						tool,
						secondDirection,
						secondPortalCells[i].sequenceIndex,
						secondPortalCells[i].cellInfo,
						style);
				}
			}

			for (size_t i = 0; i < portalCellCount; ++i)
			{
				if (!firstPortalCells[i].occupant || !secondPortalCells[i].occupant)
				{
					logger.WriteLineFormatted(
						LogLevel::Error,
						"Tunnel portal placement did not create two tunnel occupants for lane %u.",
						static_cast<uint32_t>(i));
					return false;
				}
			}

			for (size_t i = 0; i < portalCellCount; ++i)
			{
				if (!PieceInserter::QueryTunnelOccupant(firstPortalCells[i].occupant, firstPortalCells[i].tunnel)
					|| !PieceInserter::QueryTunnelOccupant(secondPortalCells[i].occupant, secondPortalCells[i].tunnel))
				{
					logger.WriteLineFormatted(
						LogLevel::Error,
						"TunnelPortalTool: created occupants are not tunnel occupants for lane %u, first=%p second=%p firstTunnel=%p secondTunnel=%p.",
						static_cast<uint32_t>(i),
						firstPortalCells[i].occupant,
						secondPortalCells[i].occupant,
						static_cast<cIGZUnknown*>(firstPortalCells[i].tunnel),
						static_cast<cIGZUnknown*>(secondPortalCells[i].tunnel));
					return false;
				}
			}

			const bool reverseFirstPortalPairing = portalCellCount == 2
				&& ShouldReverseTwoTilePairing(
					firstPortalCells,
					secondPortalCells,
					firstDirection,
					secondDirection);

			for (size_t i = 0; i < portalCellCount; ++i)
			{
				const size_t firstPortalCellIndex = reverseFirstPortalPairing
					? portalCellCount - 1 - i
					: i;
				PortalCell& firstPortalCell = firstPortalCells[firstPortalCellIndex];
				PortalCell& secondPortalCell = secondPortalCells[i];

				if (portalCellCount > 1)
				{
					Debug::LogLanePair(
						i,
						reverseFirstPortalPairing,
						firstPortalCell,
						secondPortalCell,
						first.networkType,
						firstDirection,
						secondDirection);
					Debug::TracePathMap("first lane before stitch", firstPortalCell.tunnel);
					Debug::TracePathMap("second lane before stitch", secondPortalCell.tunnel);
				}

				Game::SetOtherEndOccupant(firstPortalCell.tunnel, secondPortalCell.occupant);
				Game::SetOtherEndOccupant(secondPortalCell.tunnel, firstPortalCell.occupant);

				const uint8_t pairedFirstVectorDirection =
					Geometry::InferTunnelPieceDirection(firstPortalCell.endpoint, secondPortalCell.endpoint);
				const uint8_t pairedSecondVectorDirection =
					Geometry::InferTunnelPieceDirection(secondPortalCell.endpoint, firstPortalCell.endpoint);
				const bool requiresCustomPathStitch =
					firstDirection != pairedFirstVectorDirection
						|| secondDirection != pairedSecondVectorDirection;
				const bool requiresTwoTilePathKeyResolution = portalCellCount > 1;

				const uint8_t firstPathDirection = Geometry::TunnelPieceDirectionToPathDirection(firstDirection);
				const uint8_t secondPathDirection = Geometry::TunnelPieceDirectionToPathDirection(secondDirection);

				if (requiresCustomPathStitch)
				{
					RouteEdgeFixes::Register(
						firstPortalCell.endpoint,
						secondPortalCell.endpoint,
						firstPathDirection,
						secondPathDirection);
				}

				bool laneStitched;
				if (requiresCustomPathStitch || requiresTwoTilePathKeyResolution)
				{
					const uint16_t firstPeerLookup = requiresTwoTilePathKeyResolution
						? PathStitcher::kAutomaticPeerPathLookup
						: Geometry::PortalExitPathKeyLowWord(secondPathDirection);
					const uint16_t secondPeerLookup = requiresTwoTilePathKeyResolution
						? PathStitcher::kAutomaticPeerPathLookup
						: Geometry::PortalExitPathKeyLowWord(firstPathDirection);
					laneStitched = PathStitcher::RefreshTunnelPathInfo(
						firstPortalCell.tunnel,
						secondPortalCell.tunnel,
						firstPathDirection,
						firstPeerLookup,
						secondPathDirection);
					laneStitched = PathStitcher::RefreshTunnelPathInfo(
						secondPortalCell.tunnel,
						firstPortalCell.tunnel,
						secondPathDirection,
						secondPeerLookup,
						firstPathDirection) && laneStitched;
				}
				else
				{
					// A native-compatible single-tile pair can use each full path
					// key unchanged.
					laneStitched = PathStitcher::RefreshTunnelPathInfo(firstPortalCell.tunnel, secondPortalCell.tunnel);
					laneStitched = PathStitcher::RefreshTunnelPathInfo(secondPortalCell.tunnel, firstPortalCell.tunnel)
						&& laneStitched;
				}
				if (!laneStitched)
				{
					// The occupants are already committed to the city and there is no
					// rollback path, so continue and register/notify. Surface the
					// failure: this lane has no usable tunnel path map and traffic
					// will not route across it.
					logger.WriteLineFormatted(
						LogLevel::Error,
						"Tunnel portal lane %u failed to stitch a path map at (%u,%u)-(%u,%u); portal is committed but not routable.",
						static_cast<uint32_t>(i),
						firstPortalCell.endpoint.x,
						firstPortalCell.endpoint.z,
						secondPortalCell.endpoint.x,
						secondPortalCell.endpoint.z);
				}
				if (portalCellCount > 1)
				{
					Debug::TracePathMap("first lane after stitch", firstPortalCell.tunnel);
					Debug::TracePathMap("second lane after stitch", secondPortalCell.tunnel);
				}
				PieceInserter::MarkOccupantUsable(firstPortalCell.occupant, "first");
				PieceInserter::MarkOccupantUsable(secondPortalCell.occupant, "second");
				TrafficSimTunnels::NotifyLinkedTunnels(
					firstPortalCell.endpoint,
					secondPortalCell.endpoint,
					firstPortalCell.tunnel,
					secondPortalCell.tunnel);
			}

			// Both ends are registered with the traffic simulator by now, so this
			// picks up the pair just placed along with everything already there.
			TerrainPinning::MarkCommittedPortalsImmovable();

			logger.WriteLineFormatted(
				LogLevel::Info,
				"Placed experimental %s tunnel portal pair at (%u,%u) and (%u,%u).",
				Geometry::NetworkTypeName(first.networkType),
				first.x,
				first.z,
				second.x,
				second.z);

			return true;
		}
	}

	const char* NetworkTypeName(cISC4NetworkOccupant::eNetworkType type)
	{
		return Geometry::NetworkTypeName(type);
	}

	bool TryFindNetworkAtTile(
		uint32_t x,
		uint32_t z,
		cISC4NetworkOccupant::eNetworkType& networkTypeOut)
	{
		return GameAccess::FindNetworkAtTile(x, z, networkTypeOut);
	}

	uint8_t ExpectedPortalTileCount(cISC4NetworkOccupant::eNetworkType networkType)
	{
		return Geometry::IsTwoTileNetwork(networkType) ? 2 : 1;
	}

	bool PlacePortalPair(
		const Endpoint& first,
		const Endpoint& second,
		const Styles::Style& style)
	{
		return PlacePortalPairInternal(first, second, style);
	}
}
