#pragma once
#include <cstdint>

enum RotFlip : uint8_t { R0F0 = 0, R1F0 = 1, R2F0 = 2, R3F0 = 3, R0F1 = 0x80, R1F1 = 0x81, R2F1 = 0x82, R3F1 = 0x83 };

static constexpr RotFlip rotate(RotFlip x, uint32_t rotation)
{
	return static_cast<RotFlip>((x + (0x1 | (x >> 6)) * (rotation & 0x3)) & 0x83);
}

static constexpr RotFlip operator*(RotFlip x, RotFlip y)
{
	return static_cast<RotFlip>(rotate(x, y) ^ (y & 0x80));
}

static constexpr RotFlip rotate180(RotFlip rf)
{
	return static_cast<RotFlip>(rf ^ 0x2);
}

static constexpr RotFlip flipVertically(RotFlip rf)
{
	return static_cast<RotFlip>(rf ^ 0x82);
}

static constexpr RotFlip flipHorizontally(RotFlip rf)
{
	return static_cast<RotFlip>(rf ^ 0x80);
}

struct Tile
{
	uint32_t id;
	RotFlip rf;
};
static_assert(sizeof(Tile) == 0x8);

struct cSC4NetworkTileConflictRule
{
	Tile _1;
	Tile _2;
	Tile _3;
	Tile _4;
};
static_assert(sizeof(cSC4NetworkTileConflictRule) == 32);
