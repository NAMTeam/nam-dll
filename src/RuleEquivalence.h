#pragma once
#include "cSC4NetworkTileConflictRule.h"
#include <functional>

struct RuleEquivalenceHash
{
	std::size_t operator()(const cSC4NetworkTileConflictRule& rule) const noexcept;
};

struct RuleEquivalence
{
	bool operator()(const cSC4NetworkTileConflictRule& p, const cSC4NetworkTileConflictRule& q) const noexcept;
};
