#include "Debug.h"

#include "GameAddresses.h"
#include "Logger.h"
#include "NetworkStubs.h"
#include "PathMapView.h"
#include "RawLayouts.h"
#include "cISC4NetworkOccupant.h"
#include "cIGZUnknown.h"

#include <cstdint>

namespace TunnelPortal::Debug
{
	namespace
	{
		namespace Game = TunnelPortal::Game;
		namespace Geometry = TunnelPortal::Geometry;
		namespace PathMapView = TunnelPortal::PathMapView;
	}

	void TracePathMap(const char* label, cIGZUnknown* tunnel)
	{
		if (!tunnel)
		{
			return;
		}

		void* const pathInfo = Game::GetTunnelPathInfo(tunnel);
		const Raw::PathMap* const map = PathMapView::GetPathMap(pathInfo);
		Logger& logger = Logger::GetInstance();
		if (!PathMapView::IsUsable(map))
		{
			logger.WriteLineFormatted(
				LogLevel::Debug,
				"TunnelPortalTool: %s path map unavailable pathInfo=%p.",
				label,
				pathInfo);
			return;
		}

		uint32_t pathIndex = 0;
		for (Raw::PathMapNode** bucket = map->start;
			bucket != map->end && pathIndex < 24;
			++bucket)
		{
			for (Raw::PathMapNode* node = *bucket; node && pathIndex < 24; node = node->next)
			{
				const Raw::PathPoint* const first = PathMapView::FirstPoint(node);
				const Raw::PathPoint* const last = PathMapView::LastPoint(node);
				logger.WriteLineFormatted(
					LogLevel::Debug,
					"TunnelPortalTool: %s path[%u] key=0x%08X keyType=%u pathType=%u unique=%u entry=%u exit=%u points=%u first=(%.2f,%.2f,%.2f) last=(%.2f,%.2f,%.2f).",
					label,
					pathIndex,
					node->key,
					(node->key >> 28) & 0x0F,
					(node->key >> 24) & 0x0F,
					(node->key >> 16) & 0xFF,
					(node->key >> 8) & 0xFF,
					node->key & 0xFF,
					PathMapView::CountPoints(node),
					first ? first->x : 0.0f,
					first ? first->y : 0.0f,
					first ? first->z : 0.0f,
					last ? last->x : 0.0f,
					last ? last->y : 0.0f,
					last ? last->z : 0.0f);
				++pathIndex;
			}
		}

		if (pathIndex == 0)
		{
			logger.WriteLineFormatted(
				LogLevel::Debug,
				"TunnelPortalTool: %s path map is empty pathInfo=%p.",
				label,
				pathInfo);
		}
	}

	void LogPlacementAttempt(
		const Endpoint& first,
		const Endpoint& second,
		const Styles::Style& style)
	{
		Logger::GetInstance().WriteLineFormatted(
			LogLevel::Debug,
			"TunnelPortalTool: attempting to place %s portal pair at (%u,%u) -> (%u,%u), facade style=\"%s\".",
			Geometry::NetworkTypeName(first.networkType),
			first.x, first.z,
			second.x, second.z,
			style.name.c_str());
	}

	void LogPairingDecision(
		bool reverse,
		bool fromPathSemantics,
		uint32_t directPathScore,
		uint32_t reversePathScore,
		const std::array<uint32_t, 2>& firstFacingExitPathCounts,
		const std::array<uint32_t, 2>& secondFacingExitPathCounts,
		int64_t directPairingDistance,
		int64_t reversePairingDistance)
	{
		Logger::GetInstance().WriteLineFormatted(
			LogLevel::Debug,
			"TunnelPortalTool: two-tile pairing selected=%s source=%s pathScores direct=%u reverse=%u facingExitPaths first=%u/%u second=%u/%u distances direct=%lld reverse=%lld.",
			reverse ? "reverse" : "direct",
			fromPathSemantics ? "paths" : "geometry",
			directPathScore,
			reversePathScore,
			firstFacingExitPathCounts[0],
			firstFacingExitPathCounts[1],
			secondFacingExitPathCounts[0],
			secondFacingExitPathCounts[1],
			directPairingDistance,
			reversePairingDistance);
	}

	void LogLanePair(
		size_t laneIndex,
		bool reverse,
		const PortalCell& firstCell,
		const PortalCell& secondCell,
		cISC4NetworkOccupant::eNetworkType networkType,
		uint8_t firstDirection,
		uint8_t secondDirection)
	{
		const uint32_t networkIndex = static_cast<uint32_t>(networkType);
		Logger::GetInstance().WriteLineFormatted(
			LogLevel::Debug,
			"TunnelPortalTool: lane pair %u mode=%s first=(%u,%u) seq=%u piece=0x%08X rf=0x%02X edges=0x%08X second=(%u,%u) seq=%u piece=0x%08X rf=0x%02X edges=0x%08X portalDirections=%u/%u.",
			static_cast<uint32_t>(laneIndex),
			reverse ? "reverse" : "direct",
			firstCell.endpoint.x,
			firstCell.endpoint.z,
			static_cast<uint32_t>(firstCell.sequenceIndex),
			firstCell.occupant->PieceId(),
			static_cast<uint32_t>(firstCell.occupant->GetRotationAndFlip()),
			firstCell.cellInfo->edgesPerNetwork[networkIndex],
			secondCell.endpoint.x,
			secondCell.endpoint.z,
			static_cast<uint32_t>(secondCell.sequenceIndex),
			secondCell.occupant->PieceId(),
			static_cast<uint32_t>(secondCell.occupant->GetRotationAndFlip()),
			secondCell.cellInfo->edgesPerNetwork[networkIndex],
			static_cast<uint32_t>(firstDirection),
			static_cast<uint32_t>(secondDirection));
	}

	void LogPathRefresh(
		uint8_t selfLookupPathDirection,
		uint16_t peerPathKeyLowWord,
		uint32_t exactKeyCount,
		uint32_t remappedUniqueKeyCount,
		uint32_t unresolvedKeyCount,
		const std::array<uint32_t, 4>& unresolvedOriginalKeys)
	{
		Logger::GetInstance().WriteLineFormatted(
			LogLevel::Debug,
			"TunnelPortalTool: path refresh selfDirection=%u peerLowWord=0x%04X peer lookups exact=%u remappedUnique=%u unresolved=%u unresolvedKeys=0x%08X/0x%08X/0x%08X/0x%08X.",
			static_cast<uint32_t>(selfLookupPathDirection),
			static_cast<uint32_t>(peerPathKeyLowWord),
			exactKeyCount,
			remappedUniqueKeyCount,
			unresolvedKeyCount,
			unresolvedOriginalKeys[0],
			unresolvedOriginalKeys[1],
			unresolvedOriginalKeys[2],
			unresolvedOriginalKeys[3]);
	}
}
