// Exercises the cell geometry the transit access patch uses to pick candidate
// lots. Everything here is pure, so it runs without SimCity 4.
#include "doctest/doctest.h"
#include "TransitAccessGeometry.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace TransitAccessGeometry;

namespace
{
	constexpr int32_t kCityCells = 64;

	SC4Rect<int32_t> Rect(int32_t left, int32_t top, int32_t right, int32_t bottom)
	{
		return SC4Rect<int32_t>(left, top, right, bottom);
	}

	std::vector<std::pair<int32_t, int32_t>> NeighborCells(
		const SC4Rect<int32_t>& rect,
		int32_t cellCountX = kCityCells,
		int32_t cellCountZ = kCityCells)
	{
		std::vector<std::pair<int32_t, int32_t>> cells;
		ForEachOrthogonalNeighborCell(
			rect,
			cellCountX,
			cellCountZ,
			[&cells](int32_t x, int32_t z) { cells.emplace_back(x, z); });
		return cells;
	}

	// Models what the patch does in the game: walk the source lot's perimeter and
	// keep whatever lot occupies one of those cells.
	bool CandidateIsReachable(
		const SC4Rect<int32_t>& source,
		const SC4Rect<int32_t>& candidate,
		int32_t cellCountX = kCityCells,
		int32_t cellCountZ = kCityCells)
	{
		return AnyOrthogonalNeighborCell(
			source,
			cellCountX,
			cellCountZ,
			[&candidate](int32_t x, int32_t z) { return RectContains(candidate, x, z); });
	}

	bool Contains(const std::vector<std::pair<int32_t, int32_t>>& cells, int32_t x, int32_t z)
	{
		return std::find(cells.begin(), cells.end(), std::make_pair(x, z)) != cells.end();
	}
}

TEST_CASE("the scan covers exactly the four sides of the lot")
{
	const SC4Rect<int32_t> source = Rect(10, 10, 11, 12);
	const std::vector<std::pair<int32_t, int32_t>> cells = NeighborCells(source);

	// 2 wide x 3 tall: 2 cells north, 2 south, 3 west, 3 east.
	CHECK(cells.size() == 10);

	SUBCASE("every orthogonal neighbour is visited")
	{
		CHECK(Contains(cells, 10, 9));
		CHECK(Contains(cells, 11, 9));
		CHECK(Contains(cells, 10, 13));
		CHECK(Contains(cells, 11, 13));
		CHECK(Contains(cells, 9, 10));
		CHECK(Contains(cells, 9, 11));
		CHECK(Contains(cells, 9, 12));
		CHECK(Contains(cells, 12, 10));
		CHECK(Contains(cells, 12, 11));
		CHECK(Contains(cells, 12, 12));
	}

	SUBCASE("diagonal corners are never visited")
	{
		CHECK_FALSE(Contains(cells, 9, 9));
		CHECK_FALSE(Contains(cells, 12, 9));
		CHECK_FALSE(Contains(cells, 9, 13));
		CHECK_FALSE(Contains(cells, 12, 13));
	}

	SUBCASE("cells inside the lot are never visited")
	{
		for (const auto& cell : cells)
		{
			CHECK_FALSE(RectContains(source, cell.first, cell.second));
		}
	}
}

TEST_CASE("a lot sharing a side is accepted")
{
	const SC4Rect<int32_t> source = Rect(10, 10, 11, 12);

	CHECK(CandidateIsReachable(source, Rect(12, 10, 13, 12)));  // east
	CHECK(CandidateIsReachable(source, Rect(8, 10, 9, 12)));    // west
	CHECK(CandidateIsReachable(source, Rect(10, 7, 11, 9)));    // north
	CHECK(CandidateIsReachable(source, Rect(10, 13, 11, 15)));  // south

	// A single overlapping cell along the shared side is still a shared side.
	CHECK(CandidateIsReachable(source, Rect(12, 12, 14, 14)));
}

TEST_CASE("a lot touching only a diagonal corner is rejected")
{
	const SC4Rect<int32_t> source = Rect(10, 10, 11, 12);

	CHECK_FALSE(CandidateIsReachable(source, Rect(12, 13, 13, 14)));  // south-east
	CHECK_FALSE(CandidateIsReachable(source, Rect(8, 8, 9, 9)));      // north-west
	CHECK_FALSE(CandidateIsReachable(source, Rect(12, 8, 13, 9)));    // north-east
	CHECK_FALSE(CandidateIsReachable(source, Rect(8, 13, 9, 14)));    // south-west
}

TEST_CASE("a lot one cell away is rejected")
{
	const SC4Rect<int32_t> source = Rect(10, 10, 11, 12);

	CHECK_FALSE(CandidateIsReachable(source, Rect(13, 10, 14, 12)));  // one empty column
	CHECK_FALSE(CandidateIsReachable(source, Rect(7, 10, 8, 12)));
	CHECK_FALSE(CandidateIsReachable(source, Rect(10, 6, 11, 8)));    // one empty row
	CHECK_FALSE(CandidateIsReachable(source, Rect(10, 14, 11, 16)));
}

// A candidate that spans several perimeter cells is found through each of them;
// the patch deduplicates by lot pointer rather than by cell.
TEST_CASE("a multi cell candidate is reached from every one of its cells")
{
	const SC4Rect<int32_t> source = Rect(10, 10, 11, 12);
	const SC4Rect<int32_t> candidate = Rect(12, 10, 13, 12);

	int matches = 0;
	for (const auto& cell : NeighborCells(source))
	{
		if (RectContains(candidate, cell.first, cell.second))
		{
			++matches;
		}
	}

	CHECK(matches == 3);
}

// The scan must stay inside the city instead of asking the game about negative
// or out of range cells.
TEST_CASE("the scan is clamped to the city on all four borders")
{
	const std::vector<SC4Rect<int32_t>> edgeLots{
		Rect(0, 5, 1, 6),                            // west edge
		Rect(kCityCells - 2, 5, kCityCells - 1, 6),  // east edge
		Rect(5, 0, 6, 1),                            // north edge
		Rect(5, kCityCells - 2, 6, kCityCells - 1),  // south edge
		Rect(0, 0, 1, 1),                            // north-west corner
		Rect(kCityCells - 2, kCityCells - 2, kCityCells - 1, kCityCells - 1)
	};

	for (const SC4Rect<int32_t>& lot : edgeLots)
	{
		const std::vector<std::pair<int32_t, int32_t>> cells = NeighborCells(lot);
		CHECK_FALSE(cells.empty());
		for (const auto& cell : cells)
		{
			CHECK(cell.first >= 0);
			CHECK(cell.first < kCityCells);
			CHECK(cell.second >= 0);
			CHECK(cell.second < kCityCells);
		}
	}

	// A lot filling the whole city has no neighbours at all.
	CHECK(NeighborCells(Rect(0, 0, kCityCells - 1, kCityCells - 1)).empty());
}

TEST_CASE("an invalid rectangle or city size produces no cells")
{
	CHECK(NeighborCells(Rect(11, 10, 10, 12)).empty());  // inverted on x
	CHECK(NeighborCells(Rect(10, 12, 11, 10)).empty());  // inverted on z
	CHECK(NeighborCells(Rect(10, 10, 11, 12), 0, 0).empty());
	CHECK(NeighborCells(Rect(10, 10, 11, 12), -1, -1).empty());
}

TEST_CASE("the rectangle helpers match the checks the game makes")
{
	SUBCASE("IsValidRect")
	{
		CHECK(IsValidRect(Rect(3, 4, 3, 4)));
		CHECK_FALSE(IsValidRect(Rect(4, 4, 3, 4)));
	}

	SUBCASE("RectsEqual")
	{
		CHECK(RectsEqual(Rect(1, 2, 3, 4), Rect(1, 2, 3, 4)));
		CHECK_FALSE(RectsEqual(Rect(1, 2, 3, 4), Rect(1, 2, 3, 5)));
	}

	SUBCASE("RectContains")
	{
		CHECK(RectContains(Rect(1, 2, 3, 4), 1, 2));
		CHECK(RectContains(Rect(1, 2, 3, 4), 3, 4));
		CHECK_FALSE(RectContains(Rect(1, 2, 3, 4), 0, 2));
		CHECK_FALSE(RectContains(Rect(1, 2, 3, 4), 3, 5));
	}

	SUBCASE("RectIsInsideCity")
	{
		CHECK(RectIsInsideCity(Rect(0, 0, kCityCells - 1, kCityCells - 1), kCityCells, kCityCells));
		CHECK_FALSE(RectIsInsideCity(Rect(-1, 0, 2, 2), kCityCells, kCityCells));
		CHECK_FALSE(RectIsInsideCity(Rect(0, -1, 2, 2), kCityCells, kCityCells));
		CHECK_FALSE(RectIsInsideCity(Rect(0, 0, kCityCells, 2), kCityCells, kCityCells));
		CHECK_FALSE(RectIsInsideCity(Rect(0, 0, 2, kCityCells), kCityCells, kCityCells));
		CHECK_FALSE(RectIsInsideCity(Rect(4, 0, 3, 2), kCityCells, kCityCells));
	}
}

// Stopping at the first match is what makes the road-like network lookup cheap
// for a lot that is already connected.
TEST_CASE("the scan stops at the first match")
{
	int visited = 0;
	const bool found = AnyOrthogonalNeighborCell(
		Rect(10, 10, 11, 12),
		kCityCells,
		kCityCells,
		[&visited](int32_t, int32_t)
		{
			++visited;
			return true;
		});

	CHECK(found);
	CHECK(visited == 1);
}
