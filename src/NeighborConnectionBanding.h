#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// The banding rules of the neighbor connection return-tile search. The patch
// reads a strip of city-edge tiles from the traffic simulator; everything that
// decides which of those tiles is the return tile lives here, so the rules can
// be unit tested without running SimCity 4.
namespace NeighborConnectionBanding
{
	// Edge tiles are indexed by offset from the origin tile, so the strip has to
	// fit the largest search distance the settings allow.
	constexpr uint32_t kMaxSearchDistance = 64;
	constexpr size_t kScanCapacity = kMaxSearchDistance * 2 + 1;

	// How far to look along the city edge, and how many tiles without an eligible
	// neighbor connection may sit inside one carriageway band.
	struct SearchLimits
	{
		uint32_t maxSearchDistance = 8;
		uint32_t maxGroupingGap = 2;
	};

	// Traffic direction seen from the city edge for one path type.
	enum class PathDir : uint8_t
	{
		None,
		Inbound,
		Outbound,
		Bidi // short for Bidirectional
	};

	// One scanned tile on the city edge, including its NC mask and path direction.
	struct EdgeTile
	{
		int32_t x = 0;
		int32_t z = 0;
		int32_t offset = 0;
		uint16_t ncMask = 0;
		uint16_t pathMatrix = 0;
		PathDir direction = PathDir::None;
		bool valid = false;
		bool isEligibleNc = false;
	};

	using EdgeScan = std::array<EdgeTile, kScanCapacity>;

	inline const EdgeTile& GetScannedTile(const EdgeScan& scannedTiles, const int32_t offset)
	{
		return scannedTiles[static_cast<size_t>(offset + static_cast<int32_t>(kMaxSearchDistance))];
	}

	// A contiguous run of scanned tiles that belong to the same carriageway group.
	struct Band
	{
		size_t firstTile = 0;
		size_t tileCount = 0;
	};

	// The grouped source or target side used when mirroring a return tile.
	struct BandCollection
	{
		std::array<EdgeTile, kScanCapacity> tiles{};
		std::array<Band, kScanCapacity> bands{};
		size_t tileCount = 0;
		size_t bandCount = 0;

		void Add(const EdgeTile& tile)
		{
			if (tileCount >= tiles.size())
			{
				return;
			}

			const bool startsNewBand =
				tileCount == 0 || tiles[tileCount - 1].offset + 1 != tile.offset;
			if (startsNewBand)
			{
				if (bandCount >= bands.size())
				{
					return;
				}
				bands[bandCount++] = Band{tileCount, 0};
			}

			tiles[tileCount++] = tile;
			++bands[bandCount - 1].tileCount;
		}
	};


	// Only one-way traffic pairs: Bidi and None have no opposite direction.
	inline PathDir GetOppositeDirection(const PathDir dir)
	{
		switch (dir)
		{
		case PathDir::Inbound:
			return PathDir::Outbound;
		case PathDir::Outbound:
			return PathDir::Inbound;
		case PathDir::Bidi:
		case PathDir::None:
		default:
			return PathDir::None;
		}
	}

	inline bool IsOneWayDirection(const PathDir dir)
	{
		return dir == PathDir::Inbound || dir == PathDir::Outbound;
	}

	inline bool IsTargetDirection(const PathDir dir, const PathDir targetDir, const bool allowBidiFallback)
	{
		return dir == targetDir || (allowBidiFallback && dir == PathDir::Bidi);
	}

	inline bool IsSourceDirection(
		const PathDir dir,
		const PathDir sourceDir,
		const bool allowBidiSource,
		const bool bidiMustBeContiguous,
		const uint32_t gap)
	{
		if (dir == sourceDir)
		{
			return true;
		}
		return allowBidiSource && dir == PathDir::Bidi && (!bidiMustBeContiguous || gap == 0);
	}

	inline bool TryFindSourceDirectionNearOrigin(
		const EdgeScan& scannedTiles,
		const SearchLimits& limits,
		const PathDir originDir,
		const bool allowBidiSource,
		PathDir& sourceDir)
	{
		if (IsOneWayDirection(originDir))
		{
			sourceDir = originDir;
			return true;
		}
		if (!allowBidiSource || originDir != PathDir::Bidi)
		{
			return false;
		}

		uint32_t gap = 0;
		for (int32_t offset = -1; offset >= -static_cast<int32_t>(limits.maxSearchDistance); --offset)
		{
			const EdgeTile& tile = GetScannedTile(scannedTiles, offset);
			if (!tile.valid)
			{
				break;
			}
			if (!tile.isEligibleNc)
			{
				if (++gap > limits.maxGroupingGap)
				{
					break;
				}
				continue;
			}
			if (IsOneWayDirection(tile.direction))
			{
				sourceDir = tile.direction;
				return true;
			}
			if (tile.direction != PathDir::Bidi)
			{
				break;
			}
			gap = 0;
		}

		return false;
	}


	// Finds how far the source carriageway continues from the origin, allowing small gaps.
	inline int32_t FindSourceExtent(
		const EdgeScan& scannedTiles,
		const SearchLimits& limits,
		const PathDir sourceDir,
		const bool allowBidiSource,
		const int32_t inc)
	{
		int32_t extent = 0;
		uint32_t gap = 0;
		for (int32_t distance = 1;
			distance <= static_cast<int32_t>(limits.maxSearchDistance);
			++distance)
		{
			const int32_t offset = distance * inc;
			const EdgeTile& tile = GetScannedTile(scannedTiles, offset);
			if (!tile.valid)
			{
				break;
			}

			if (!tile.isEligibleNc)
			{
				if (++gap > limits.maxGroupingGap)
				{
					break;
				}
				continue;
			}

			if (!IsSourceDirection(tile.direction, sourceDir, allowBidiSource, true, gap))
			{
				break;
			}

			extent = offset;
			gap = 0;
		}
		return extent;
	}

	// Finds the first tile after the source side that can act as the return side.
	inline bool FindTargetStart(
		const EdgeScan& scannedTiles,
		const SearchLimits& limits,
		const PathDir sourceDir,
		const PathDir targetDir,
		const bool allowBidiSource,
		const bool allowBidiTarget,
		int32_t& targetStart)
	{
		uint32_t gap = 0;
		for (int32_t offset = 1;
			offset <= static_cast<int32_t>(limits.maxSearchDistance);
			++offset)
		{
			const EdgeTile& tile = GetScannedTile(scannedTiles, offset);
			if (!tile.valid)
			{
				break;
			}
			if (!tile.isEligibleNc)
			{
				++gap;
				continue;
			}
			if (IsSourceDirection(tile.direction, sourceDir, allowBidiSource, true, gap))
			{
				gap = 0;
				continue;
			}
			if (IsTargetDirection(tile.direction, targetDir, allowBidiTarget))
			{
				targetStart = offset;
				return true;
			}

			break;
		}
		return false;
	}

	inline BandCollection BuildSourceBands(
		const EdgeScan& scannedTiles,
		const PathDir sourceDir,
		const bool allowBidiSource,
		const int32_t minOffset,
		const int32_t maxOffset)
	{
		BandCollection result;
		for (int32_t offset = minOffset; offset <= maxOffset; ++offset)
		{
			const EdgeTile& tile = GetScannedTile(scannedTiles, offset);
			if (tile.valid &&
				tile.isEligibleNc &&
				IsSourceDirection(tile.direction, sourceDir, allowBidiSource, false, 0))
			{
				result.Add(tile);
			}
		}
		return result;
	}

	inline BandCollection BuildTargetBands(const EdgeScan& scannedTiles, const SearchLimits& limits, const PathDir targetDir, const bool allowBidiTarget, const int32_t targetStart)
	{
		BandCollection result;
		uint32_t gap = 0;
		for (int32_t offset = targetStart;
			offset <= static_cast<int32_t>(limits.maxSearchDistance);
			++offset)
		{
			const EdgeTile& tile = GetScannedTile(scannedTiles, offset);
			if (!tile.valid)
			{
				break;
			}

			if (!tile.isEligibleNc)
			{
				if (++gap > limits.maxGroupingGap)
				{
					break;
				}
				continue;
			}

			if (!IsTargetDirection(tile.direction, targetDir, allowBidiTarget))
			{
				break;
			}

			result.Add(tile);
			gap = 0;
		}
		return result;
	}

	inline bool FindTileInBands(const BandCollection& collection, const int32_t tileOffset, size_t& bandIndex, size_t& tileIndexInBand)
	{
		for (size_t currentBand = 0; currentBand < collection.bandCount; ++currentBand)
		{
			const Band& band = collection.bands[currentBand];
			for (size_t currentTile = 0; currentTile < band.tileCount; ++currentTile)
			{
				if (collection.tiles[band.firstTile + currentTile].offset == tileOffset)
				{
					bandIndex = currentBand;
					tileIndexInBand = currentTile;
					return true;
				}
			}
		}
		return false;
	}

	// Returns the offset of the tile that pairs with the origin tile at offset 0.
	// Source bands and lanes are counted from the target-facing side, target bands
	// and lanes from the source-facing side, and unequal widths are scaled.
	inline bool TryFindPairedReturnOffset(const EdgeScan& scannedTiles, const SearchLimits& limits,
			const bool allowBidiSource, const bool allowBidiTarget, int32_t& resultOffset)
		{
		const EdgeTile& origin = GetScannedTile(scannedTiles, 0);
		PathDir sourceDirection = PathDir::None;
		if (!TryFindSourceDirectionNearOrigin(scannedTiles, limits, origin.direction, allowBidiSource, sourceDirection))
		{
			return false;
		}

		const PathDir targetDirection = GetOppositeDirection(sourceDirection);
		if (!IsOneWayDirection(targetDirection))
		{
			return false;
		}

		int32_t targetStart = 0;
		if (!FindTargetStart(scannedTiles, limits, sourceDirection, targetDirection, allowBidiSource, allowBidiTarget, targetStart))
		{
			return false;
		}

		const auto negSourceExtent = FindSourceExtent(scannedTiles, limits, sourceDirection, allowBidiSource, -1);
		const auto posSourceExtent = FindSourceExtent(scannedTiles, limits, sourceDirection, allowBidiSource, 1);
		const auto maxOffset = std::min(posSourceExtent, targetStart - 1);
		const auto sourceBands = BuildSourceBands(scannedTiles, sourceDirection, allowBidiSource, negSourceExtent, maxOffset);
		const auto targetBands = BuildTargetBands(scannedTiles, limits, targetDirection, allowBidiTarget, targetStart);
		if (sourceBands.bandCount == 0 || targetBands.bandCount == 0)
		{
			return false;
		}

		size_t sourceBandIndex = 0;
		size_t sourceTileIndex = 0;
		if (!FindTileInBands(sourceBands, 0, sourceBandIndex, sourceTileIndex))
		{
			return false;
		}

		// Source bands and lanes are indexed from the target-facing side. Target
		// bands and lanes are already stored from the source-facing side.
		const size_t facingSourceBandIndex = sourceBands.bandCount - sourceBandIndex - 1;
		const size_t targetBandIndex = facingSourceBandIndex * targetBands.bandCount / sourceBands.bandCount;
		const Band& sourceBand = sourceBands.bands[sourceBandIndex];
		const Band& targetBand = targetBands.bands[targetBandIndex];
		const size_t facingSourceTileIndex = sourceBand.tileCount - sourceTileIndex - 1;
		const size_t targetTileIndex = facingSourceTileIndex * targetBand.tileCount / sourceBand.tileCount;
		const EdgeTile& result = targetBands.tiles[targetBand.firstTile + targetTileIndex];

		resultOffset = result.offset;
		return true;
	}
}
