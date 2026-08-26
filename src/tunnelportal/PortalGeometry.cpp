#include "PortalGeometry.h"

#include "NetworkStubs.h"

#include <array>
#include <cstdlib>

namespace TunnelPortal::Geometry
{
	namespace
	{
		// Surface-approach edge flag per piece rotation (index = rotation).
		constexpr std::array<uint32_t, 4> kSurfaceApproachEdge = {
			0x02000000, // north-facing portal, approached from the south
			0x00000002, // east-facing portal,  approached from the west
			0x00000200, // south-facing portal, approached from the north
			0x00020000, // west-facing portal,  approached from the east
		};
	}

	const char* NetworkTypeName(cISC4NetworkOccupant::eNetworkType type)
	{
		switch (type)
		{
		case cISC4NetworkOccupant::Road: return "Road";
		case cISC4NetworkOccupant::Rail: return "Rail";
		case cISC4NetworkOccupant::Highway: return "Highway";
		case cISC4NetworkOccupant::Street: return "Street";
		case cISC4NetworkOccupant::Avenue: return "Avenue";
		case cISC4NetworkOccupant::LightRail: return "Light rail";
		case cISC4NetworkOccupant::Monorail: return "Monorail";
		case cISC4NetworkOccupant::OneWayRoad: return "One-way road";
		case cISC4NetworkOccupant::DirtRoad: return "Dirt road";
		case cISC4NetworkOccupant::GroundHighway: return "Ground highway";
		default: return "Network";
		}
	}

	bool IsTwoTileNetwork(cISC4NetworkOccupant::eNetworkType networkType)
	{
		switch (networkType)
		{
		case cISC4NetworkOccupant::Avenue:
		case cISC4NetworkOccupant::Highway:
		case cISC4NetworkOccupant::GroundHighway:
			return true;
		default:
			return false;
		}
	}

	uint8_t InferTunnelPieceDirection(const Endpoint& from, const Endpoint& to)
	{
		const int32_t dx = static_cast<int32_t>(to.x) - static_cast<int32_t>(from.x);
		const int32_t dz = static_cast<int32_t>(to.z) - static_cast<int32_t>(from.z);

		if (std::abs(dx) >= std::abs(dz))
		{
			return dx >= 0 ? 1 : 3;
		}

		return dz >= 0 ? 2 : 0;
	}

	uint8_t TunnelPieceDirectionToPathDirection(uint8_t tunnelPieceDirection)
	{
		static constexpr std::array<uint8_t, 4> kPathDirection = { 1, 2, 3, 0 };
		return kPathDirection[tunnelPieceDirection & 3];
	}

	uint16_t PortalExitPathKeyLowWord(uint8_t pathDirection)
	{
		const uint8_t direction = pathDirection & 3;
		return static_cast<uint16_t>((direction << 8) | (direction ^ 2));
	}

	uint32_t SurfaceApproachEdge(uint8_t tunnelPieceDirection)
	{
		return kSurfaceApproachEdge[tunnelPieceDirection & 3];
	}

	int32_t CrossAxisCoordinate(const Endpoint& endpoint, uint8_t tunnelPieceDirection)
	{
		return (tunnelPieceDirection & 1) != 0
			? static_cast<int32_t>(endpoint.z)
			: static_cast<int32_t>(endpoint.x);
	}

	bool UseAscendingTwoTileSequence(const uint8_t tunnelPieceDirection)
	{
		return tunnelPieceDirection == 1 || tunnelPieceDirection == 2;
	}

	bool TryInferTunnelPieceDirectionFromSurfaceApproach(
		const cSC4NetworkCellInfo* cellInfo,
		cISC4NetworkOccupant::eNetworkType networkType,
		uint8_t& directionOut)
	{
		if (!cellInfo)
		{
			return false;
		}

		const uint32_t edgeFlags = cellInfo->edgesPerNetwork[static_cast<uint32_t>(networkType)];
		uint8_t matchedDirection = 0;
		uint32_t matchCount = 0;

		for (uint8_t direction = 0; direction < kSurfaceApproachEdge.size(); ++direction)
		{
			if ((edgeFlags & kSurfaceApproachEdge[direction]) != 0)
			{
				matchedDirection = direction;
				++matchCount;
			}
		}

		if (matchCount == 1)
		{
			directionOut = matchedDirection;
			return true;
		}

		return false;
	}
}
