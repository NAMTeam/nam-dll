#pragma once

#include "SC4Rect.h"

#include <algorithm>
#include <cstdint>

// Cell geometry used by the transit access patch. Nothing in here touches the
// game, so the adjacency rules can be unit tested without running SimCity 4.
namespace TransitAccessGeometry
{
	inline bool IsValidRect(const SC4Rect<int32_t>& rect)
	{
		return rect.topLeftX <= rect.bottomRightX && rect.topLeftY <= rect.bottomRightY;
	}

	inline bool RectsEqual(const SC4Rect<int32_t>& a, const SC4Rect<int32_t>& b)
	{
		return a.topLeftX == b.topLeftX
			&& a.topLeftY == b.topLeftY
			&& a.bottomRightX == b.bottomRightX
			&& a.bottomRightY == b.bottomRightY;
	}

	inline bool RectContains(const SC4Rect<int32_t>& rect, int32_t x, int32_t z)
	{
		return x >= rect.topLeftX && x <= rect.bottomRightX
			&& z >= rect.topLeftY && z <= rect.bottomRightY;
	}

	inline bool RectIsInsideCity(const SC4Rect<int32_t>& rect, int32_t cellCountX, int32_t cellCountZ)
	{
		return IsValidRect(rect)
			&& rect.topLeftX >= 0
			&& rect.topLeftY >= 0
			&& rect.bottomRightX < cellCountX
			&& rect.bottomRightY < cellCountZ;
	}

	// Visits the cells directly outside the rectangle's four sides, clamped to a
	// city of cellCountX by cellCountZ cells.
	template <typename Predicate>
	bool AnyOrthogonalNeighborCell(
		const SC4Rect<int32_t>& rect,
        const int32_t cellCountX,
        const int32_t cellCountZ,
		Predicate&& predicate)
	{
		if (!IsValidRect(rect) || cellCountX <= 0 || cellCountZ <= 0)
		{
			return false;
		}

		const int32_t firstX = std::max(rect.topLeftX, 0);
		const int32_t lastX = std::min(rect.bottomRightX, cellCountX - 1);
		const int32_t firstZ = std::max(rect.topLeftY, 0);
		const int32_t lastZ = std::min(rect.bottomRightY, cellCountZ - 1);

		const int32_t northZ = rect.topLeftY - 1;
		if (northZ >= 0)
		{
			for (int32_t x = firstX; x <= lastX; ++x)
			{
				if (predicate(x, northZ))
				{
					return true;
				}
			}
		}

		const int32_t southZ = rect.bottomRightY + 1;
		if (southZ < cellCountZ)
		{
			for (int32_t x = firstX; x <= lastX; ++x)
			{
				if (predicate(x, southZ))
				{
					return true;
				}
			}
		}

		const int32_t westX = rect.topLeftX - 1;
		if (westX >= 0)
		{
			for (int32_t z = firstZ; z <= lastZ; ++z)
			{
				if (predicate(westX, z))
				{
					return true;
				}
			}
		}

		const int32_t eastX = rect.bottomRightX + 1;
		if (eastX < cellCountX)
		{
			for (int32_t z = firstZ; z <= lastZ; ++z)
			{
				if (predicate(eastX, z))
				{
					return true;
				}
			}
		}

		return false;
	}

	template <typename Visitor>
	void ForEachOrthogonalNeighborCell(
		const SC4Rect<int32_t>& rect,
		int32_t cellCountX,
		int32_t cellCountZ,
		Visitor&& visit)
	{
		AnyOrthogonalNeighborCell(
			rect,
			cellCountX,
			cellCountZ,
			[&visit](int32_t x, int32_t z)
			{
				visit(x, z);
				return false;
			});
	}
}
