// Banding rules of the neighbor connection return-tile search: which city-edge
// tile a carriageway pairs with. These cases pin down the behaviour for the
// carriageway layouts raised in review of the neighbor connection patch.
#include "doctest/doctest.h"
#include "NeighborConnectionBanding.h"

#include <string>

namespace
{
	using namespace NeighborConnectionBanding;

	// Builds a strip of city-edge tiles. "o" and "i" are one-way carriageways,
	// "b" a bidirectional piece, "-" a tile with no eligible neighbor connection
	// (empty, or a different NC such as light rail). The tile at originIndex is
	// the origin tile at offset 0; offsets grow towards the side the return
	// search looks at. Tiles outside the listed ones are valid edge tiles with
	// nothing on them.
	EdgeScan MakeScan(const std::string& tiles, const size_t originIndex)
	{
		EdgeScan scan{};
		for (int32_t offset = -static_cast<int32_t>(kMaxSearchDistance);
			offset <= static_cast<int32_t>(kMaxSearchDistance);
			++offset)
		{
			EdgeTile& tile = scan[static_cast<size_t>(offset + static_cast<int32_t>(kMaxSearchDistance))];
			tile.offset = offset;
			tile.valid = std::abs(offset) <= 20;
		}

		for (size_t index = 0; index < tiles.size(); ++index)
		{
			EdgeTile& tile = scan[static_cast<size_t>(
				static_cast<int32_t>(index - originIndex) + static_cast<int32_t>(kMaxSearchDistance))];
			tile.isEligibleNc = tiles[index] != '-';
			tile.direction = tiles[index] == 'o' ? PathDir::Outbound
				: tiles[index] == 'i' ? PathDir::Inbound
				: tiles[index] == 'b' ? PathDir::Bidi
				: PathDir::None;
		}
		return scan;
	}

	// Offset of the tile that pairs with the origin tile; 0 means no pair, since
	// the patch then leaves the tile unchanged.
	int32_t PairOffset(const std::string& tiles, const size_t originIndex = 0,
		const SearchLimits& limits = {}, const bool allowBidiSource = false, const bool allowBidiTarget = false)
	{
		const EdgeScan scan = MakeScan(tiles, originIndex);
		int32_t resultOffset = 0;
		return TryFindPairedReturnOffset(scan, limits, allowBidiSource, allowBidiTarget, resultOffset) ? resultOffset : 0;
	}
}

TEST_CASE("only one-way traffic has an opposite direction to pair with")
{
	CHECK(GetOppositeDirection(PathDir::Inbound) == PathDir::Outbound);
	CHECK(GetOppositeDirection(PathDir::Outbound) == PathDir::Inbound);
	CHECK(GetOppositeDirection(PathDir::Bidi) == PathDir::None);
	CHECK(GetOppositeDirection(PathDir::None) == PathDir::None);

	// A bidirectional source tile is only accepted for OWR, which is disabled.
	CHECK(PairOffset("boi") == 0);
}

TEST_CASE("adjacent carriageways of the same direction form one band")
{
	// RHW-4 outbound, RHW-4 outbound, then RHW-8 inbound. The outer source tile
	// mirrors onto the outer target tile ...
	CHECK(PairOffset("ooii", 0) == 3);

	// ... and the source tile next to the target side onto the near target tile.
	CHECK(PairOffset("ooii", 1) == 1);
}

TEST_CASE("a one-tile source band pairs with the near tile of a wider target band")
{
	// RHW-8 outbound next to two RHW-4 inbound tiles: a single source lane can
	// only use the carriageway closest to it.
	CHECK(PairOffset("oii") == 1);
}

TEST_CASE("several source tiles can share one narrow target carriageway")
{
	// Seen from the RHW-4 side, both inbound tiles pair with the RHW-8 tile.
	CHECK(PairOffset("iio", 0) == 2);
	CHECK(PairOffset("iio", 1) == 1);

	// Two outbound tiles sharing one inbound tile.
	CHECK(PairOffset("ooi", 0) == 2);
	CHECK(PairOffset("ooi", 1) == 1);
}

TEST_CASE("unequal band widths scale the mirrored lane index")
{
	// Three outbound tiles sharing two inbound tiles: the outer source tile
	// reaches the outer target tile, the inner two share the near one.
	CHECK(PairOffset("oooii", 0) == 4);
	CHECK(PairOffset("oooii", 1) == 2);
	CHECK(PairOffset("oooii", 2) == 1);
}

TEST_CASE("a tile of the opposite direction ends the source band")
{
	// RHW-4 inbound, RHW-4 outbound, RHW-8 inbound: the outbound tile pairs with
	// the RHW-8 tile ...
	CHECK(PairOffset("ioi", 1) == 1);

	// The search only looks at one side of the origin tile, so only one of the
	// two inbound tiles is used as a return tile; the one behind the origin tile
	// is never reached.
	CHECK(PairOffset("ioi", 0) == 1);
	CHECK(PairOffset("ioi", 2) == 0);
}

TEST_CASE("a tile without an eligible neighbor connection is a gap, not a lane")
{
	// Light rail NC between two RHW-4 NCs. With the default grouping gap of two
	// tiles both RHW-4 tiles stay one band, and the light rail tile is never
	// picked as a return tile.
	CHECK(PairOffset("o-oi", 0) == 3);
	CHECK(PairOffset("o-oi", 2) == 1);

	// With no gap tolerance the band stops at the light rail tile, but the origin
	// tile still pairs with the same return tile.
	CHECK(PairOffset("o-oi", 2, SearchLimits{4, 0}) == 1);
}

TEST_CASE("the search distance limits how far along the edge it looks")
{
	// One empty tile between the source band and the return carriageway.
	CHECK(PairOffset("oo-i", 1) == 2);

	// Too far away to reach within the configured distance.
	CHECK(PairOffset("ooooo-i", 1, SearchLimits{3, 2}) == 0);
}
