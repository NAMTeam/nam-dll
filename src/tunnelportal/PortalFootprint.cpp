#include "PortalFootprint.h"

#include "Logger.h"
#include "NetworkStubs.h"
#include "PieceInserter.h"
#include "cISC4NetworkOccupant.h"

#include <algorithm>

namespace TunnelPortal::Footprint
{
	namespace
	{
		// Same network piece, per the game's own structure test.
		bool IsSameNetworkStructure(cSC4NetworkCellInfo* first, cSC4NetworkCellInfo* second)
		{
			if (!first || !second || !first->networkOccupant || !second->networkOccupant)
			{
				return false;
			}

			return first->networkOccupant == second->networkOccupant
				|| first->networkOccupant->IsPartOfTheSameStructure(second->networkOccupant);
		}

		// True when `second` is the companion tile of a two-tile portal rooted at
		// `first`, for the given facing.
		bool IsCompatibleCompanionCell(
			cSC4NetworkCellInfo* first,
			cSC4NetworkCellInfo* second,
			cISC4NetworkOccupant::eNetworkType networkType,
			uint8_t tunnelPieceDirection)
		{
			if (!first || !second)
			{
				return false;
			}

			if (IsSameNetworkStructure(first, second))
			{
				return true;
			}

			// Ordinary Avenue carriageways are separate occupants, so
			// IsPartOfTheSameStructure is false even though the two cells form one
			// network piece. Identify that relationship from the solved topology:
			// both halves must face the same portal direction and expose reciprocal
			// cross-axis connections to each other.
			const uint32_t firstEdges = first->edgesPerNetwork[static_cast<uint32_t>(networkType)];
			const uint32_t secondEdges = second->edgesPerNetwork[static_cast<uint32_t>(networkType)];
			const uint32_t surfaceApproachEdge =
				Geometry::SurfaceApproachEdge(tunnelPieceDirection);
			if ((firstEdges & surfaceApproachEdge) == 0
				|| (secondEdges & surfaceApproachEdge) == 0)
			{
				return false;
			}

			const int32_t dx = static_cast<int32_t>(second->x) - static_cast<int32_t>(first->x);
			const int32_t dz = static_cast<int32_t>(second->z) - static_cast<int32_t>(first->z);
			uint8_t firstToSecondDirection = 0xFF;
			if (dx == -1 && dz == 0)
			{
				firstToSecondDirection = 0; // west
			}
			else if (dx == 0 && dz == -1)
			{
				firstToSecondDirection = 1; // north
			}
			else if (dx == 1 && dz == 0)
			{
				firstToSecondDirection = 2; // east
			}
			else if (dx == 0 && dz == 1)
			{
				firstToSecondDirection = 3; // south
			}
			else
			{
				return false;
			}

			const uint8_t secondToFirstDirection = firstToSecondDirection ^ 2;
			const uint8_t firstCrossEdge =
				static_cast<uint8_t>((firstEdges >> (firstToSecondDirection * 8)) & 0xFF);
			const uint8_t secondCrossEdge =
				static_cast<uint8_t>((secondEdges >> (secondToFirstDirection * 8)) & 0xFF);
			return firstCrossEdge != 0 && secondCrossEdge != 0;
		}
	}

	size_t BuildCellList(
		cSC4NetworkTool* tool,
		const char* label,
		const Endpoint& endpoint,
		cSC4NetworkCellInfo* primaryCell,
		uint8_t tunnelPieceDirection,
		std::array<PortalCell, 2>& cells)
	{
		cells[0] = {};
		cells[1] = {};
		cells[0].endpoint = endpoint;
		cells[0].cellInfo = primaryCell;

		size_t count = 1;
		if (!Geometry::IsTwoTileNetwork(endpoint.networkType) || !primaryCell)
		{
			cells[0].sequenceIndex = 0;
			return count;
		}

		Logger& logger = Logger::GetInstance();
		const int32_t offsets[2] = { -1, 1 };
		size_t companionCount = 0;
		for (const int32_t offset : offsets)
		{
			Endpoint candidate = endpoint;
			if ((tunnelPieceDirection & 1) != 0)
			{
				const int32_t z = static_cast<int32_t>(endpoint.z) + offset;
				if (z < 0)
				{
					continue;
				}
				candidate.z = static_cast<uint32_t>(z);
			}
			else
			{
				const int32_t x = static_cast<int32_t>(endpoint.x) + offset;
				if (x < 0)
				{
					continue;
				}
				candidate.x = static_cast<uint32_t>(x);
			}

			cSC4NetworkCellInfo* const candidateCell = PieceInserter::GetEndpointCell(tool, candidate);
			if (IsCompatibleCompanionCell(
				primaryCell,
				candidateCell,
				endpoint.networkType,
				tunnelPieceDirection))
			{
				++companionCount;
				if (companionCount == 1)
				{
					cells[1].endpoint = candidate;
					cells[1].cellInfo = candidateCell;
					count = 2;
				}
			}
		}

		if (companionCount != 1)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"TunnelPortalTool: %s %s endpoint at (%u,%u) resolved %u same-structure companion tiles; exactly one is required.",
				label,
				Geometry::NetworkTypeName(endpoint.networkType),
				endpoint.x,
				endpoint.z,
				static_cast<uint32_t>(companionCount));
			return 0;
		}

		if (count == 2)
		{
			const bool ascending = Geometry::UseAscendingTwoTileSequence(tunnelPieceDirection);
			const int32_t firstCrossAxis = Geometry::CrossAxisCoordinate(cells[0].endpoint, tunnelPieceDirection);
			const int32_t secondCrossAxis = Geometry::CrossAxisCoordinate(cells[1].endpoint, tunnelPieceDirection);
			if ((ascending && secondCrossAxis < firstCrossAxis)
				|| (!ascending && firstCrossAxis < secondCrossAxis))
			{
				std::swap(cells[0], cells[1]);
			}
		}

		for (size_t i = 0; i < count; ++i)
		{
			cells[i].sequenceIndex = static_cast<uint8_t>(i);
		}

		return count;
	}
}
