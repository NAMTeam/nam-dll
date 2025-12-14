// This file provides a hash and equivalence for RUL2 override rules that takes
// into account the 4 symmetries under which two override rules are the same.
// This only considers the two tiles to the left of the equality sign.
// The implementation follows
// https://github.com/memo33/metarules/blob/386eadf20213f6f6171f1610c59fe773e175121e/src/main/scala/meta/EquivRule.scala
#include "RuleEquivalence.h"
#include <array>

namespace
{
	constexpr std::array<std::array<std::size_t, 8>, 8> rfHashLookup = {{
		{ 0,  1,  2,  3,  4,  5,  6,  7},
		{ 8,  9,  3, 11,  7, 13, 14, 15},
		{16, 17,  0,  8,  6, 21, 22, 14},
		{17, 25,  1,  9,  5, 29, 21, 13},
		{22, 21,  6, 14,  0, 17, 16,  8},
		{14, 13,  7, 15,  3,  9,  8, 11},
		{ 6,  5,  4,  7,  2,  1,  0,  3},
		{21, 29,  5, 13,  1, 25, 17,  9}
	}};

	constexpr std::array<std::array<bool, 8>, 8> swappedLookup = {{
		{false, false, false, false, false, false, false, false},
		{false, false, true , false, true , false, false, false},
		{false, false, true , true , true , false, false, true },
		{true , false, true , true , true , false, true , true },
		{false, false, true , true , true , false, false, true },
		{false, false, true , false, true , false, false, false},
		{false, false, false, false, false, false, false, false},
		{true , false, true , true , true , false, true , true }
	}};

	constexpr std::array<uint32_t, 32> equivClassSize = {
		4, 4, 2, 4, 2, 4, 4, 4, 4, 4, 0, 2, 0, 4, 4, 2, 2, 4, 0, 0, 0, 4, 2, 0, 0, 2, 0, 0, 0, 2, 0, 0
	};

	// maps RotFlip to range 0..7 for use in lookup tables (swapping R1F1 and R3F1 to match metarules ordinals)
	constexpr uint8_t rotFlipOrdinal(RotFlip rf)
	{
		return ((rf & 3) ^ ((rf << 1) & (rf >> 6))) | (rf >> 5);  // rotation = bit 0 and 1, flip = shifted from bit 7 to 2
	}
	static_assert(rotFlipOrdinal(R0F0) == 0);
	static_assert(rotFlipOrdinal(R1F0) == 1);
	static_assert(rotFlipOrdinal(R2F0) == 2);
	static_assert(rotFlipOrdinal(R3F0) == 3);
	static_assert(rotFlipOrdinal(R0F1) == 4);
	static_assert(rotFlipOrdinal(R1F1) == 7);
	static_assert(rotFlipOrdinal(R2F1) == 6);
	static_assert(rotFlipOrdinal(R3F1) == 5);

	constexpr std::size_t rfHash(const cSC4NetworkTileConflictRule& rule)
	{
		return rfHashLookup[rotFlipOrdinal(rule._1.rf)][rotFlipOrdinal(rule._2.rf)];
	}

	constexpr bool swapped(const cSC4NetworkTileConflictRule& rule)
	{
		return swappedLookup[rotFlipOrdinal(rule._1.rf)][rotFlipOrdinal(rule._2.rf)];
	}

	constexpr bool isWeird(std::size_t rfh)
	{
		return equivClassSize[rfh] != 4;
	}
}

std::size_t RuleEquivalenceHash::operator()(const cSC4NetworkTileConflictRule& rule) const noexcept
{
	const std::size_t rfh = rfHash(rule);  // bounds: 0 <= rfh < 32
	const uint32_t a = rule._1.id;
	const uint32_t b = rule._2.id;
	constexpr std::size_t prime = 66403;  // most ID information lies in bits 7-23 (17 bits), so prime should have at least 32-17 = 15 bits so as to shift much of the information around
	if (swapped(rule) || (isWeird(rfh) && b < a)) {
		return ((prime + std::hash<std::uint32_t>{}(b)) * prime + std::hash<std::uint32_t>{}(a)) * prime + rfh;
	} else {
		return ((prime + std::hash<std::uint32_t>{}(a)) * prime + std::hash<std::uint32_t>{}(b)) * prime + rfh;
	}
}

bool RuleEquivalence::operator()(const cSC4NetworkTileConflictRule& p, const cSC4NetworkTileConflictRule& q) const noexcept
{
	const std::size_t rfh = rfHash(p);
	if (rfh != rfHash(q)) {
		return false;
	} else if (isWeird(rfh)) {  // the weird equiv classes have only 2 RotFlips (rather than 4) which is why this additional case necessary
		return (p._1.id == q._1.id && p._2.id == q._2.id) || (p._1.id == q._2.id && p._2.id == q._1.id);
	} else if (swapped(p) == swapped(q)) {
		return p._1.id == q._1.id && p._2.id == q._2.id;
	} else {
		return p._1.id == q._2.id && p._2.id == q._1.id;
	}
}
