#include "PathMapView.h"

#include "GameAddresses.h"
#include "PortalGeometry.h"

#include <cstddef>

namespace TunnelPortal::PathMapView
{
	const Raw::PathMap* GetPathMap(void* pathInfo)
	{
		return pathInfo
			? reinterpret_cast<const Raw::PathMap*>(
				reinterpret_cast<const uint8_t*>(pathInfo) + Game::Field::kPathInfoPathMap)
			: nullptr;
	}

	bool IsUsable(const Raw::PathMap* map)
	{
		if (!map || !map->start || !map->end || map->end < map->start)
		{
			return false;
		}

		const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
		return bucketCount > 0 && bucketCount <= Raw::kMaxPathMapBuckets;
	}

	bool HasPoints(const Raw::PathMapNode* node)
	{
		return node
			&& node->path.start
			&& node->path.end
			&& node->path.end > node->path.start;
	}

	uint32_t CountPoints(const Raw::PathMapNode* node)
	{
		if (!HasPoints(node))
		{
			return 0;
		}

		const ptrdiff_t byteCount = node->path.end - node->path.start;
		return byteCount >= static_cast<ptrdiff_t>(sizeof(Raw::PathPoint))
			&& byteCount % sizeof(Raw::PathPoint) == 0
			? static_cast<uint32_t>(byteCount / sizeof(Raw::PathPoint))
			: 0;
	}

	const Raw::PathPoint* FirstPoint(const Raw::PathMapNode* node)
	{
		return CountPoints(node) > 0
			? reinterpret_cast<const Raw::PathPoint*>(node->path.start)
			: nullptr;
	}

	const Raw::PathPoint* LastPoint(const Raw::PathMapNode* node)
	{
		if (!HasPoints(node))
		{
			return nullptr;
		}

		const ptrdiff_t byteCount = node->path.end - node->path.start;
		if (byteCount < static_cast<ptrdiff_t>(sizeof(Raw::PathPoint))
			|| byteCount % sizeof(Raw::PathPoint) != 0)
		{
			return nullptr;
		}

		return reinterpret_cast<const Raw::PathPoint*>(
			node->path.end - sizeof(Raw::PathPoint));
	}

	const Raw::PathMapNode* FindKey(const Raw::PathMap* map, uint32_t key)
	{
		if (!IsUsable(map))
		{
			return nullptr;
		}

		const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
		for (Raw::PathMapNode* node = map->start[key % bucketCount]; node; node = node->next)
		{
			if (node->key == key)
			{
				return node;
			}
		}

		return nullptr;
	}

	uint32_t CountPathsExitingToward(cIGZUnknown* tunnel, uint8_t tunnelPieceDirection)
	{
		if (!tunnel)
		{
			return 0;
		}

		const Raw::PathMap* const map = GetPathMap(Game::GetTunnelPathInfo(tunnel));
		if (!IsUsable(map))
		{
			return 0;
		}

		const uint8_t exitDirection =
			Geometry::TunnelPieceDirectionToPathDirection(tunnelPieceDirection);
		uint32_t matchingPathCount = 0;
		uint32_t visitedNodes = 0;
		for (Raw::PathMapNode** bucket = map->start;
			bucket != map->end && visitedNodes < Raw::kMaxPathMapNodes;
			++bucket)
		{
			for (Raw::PathMapNode* node = *bucket;
				node && visitedNodes < Raw::kMaxPathMapNodes;
				node = node->next)
			{
				++visitedNodes;
				if (HasPoints(node)
					&& static_cast<uint8_t>(node->key) == exitDirection)
				{
					++matchingPathCount;
				}
			}
		}

		return matchingPathCount;
	}
}
