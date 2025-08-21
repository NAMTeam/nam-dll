#pragma once
#include <cstdint>
#include "RotFlip.h"

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
