#pragma once

#include "RawLayouts.h"

#include <cstdint>

class cIGZUnknown;

// Read-only helpers over a cISC4PathInfo path map. All raw-pointer walks are
// bounds-checked against the caps in RawLayouts.h, so a corrupt map yields
// empty results instead of a crash.
namespace TunnelPortal::PathMapView
{
	// Path map embedded in a cISC4PathInfo, or null when pathInfo is null.
	const Raw::PathMap* GetPathMap(void* pathInfo);

	// True when a map has a sane bucket range and can be walked.
	bool IsUsable(const Raw::PathMap* map);

	// True when a node holds at least one path point.
	bool HasPoints(const Raw::PathMapNode* node);

	// Number of points in a node, or 0 if the vector length is not a whole
	// multiple of PathPoint.
	uint32_t CountPoints(const Raw::PathMapNode* node);

	// First / last point of a node, or null when it has none.
	const Raw::PathPoint* FirstPoint(const Raw::PathMapNode* node);
	const Raw::PathPoint* LastPoint(const Raw::PathMapNode* node);

	// Node whose key exactly matches, or null. Uses the map's own hashing.
	const Raw::PathMapNode* FindKey(const Raw::PathMap* map, uint32_t key);

	// Counts paths on a tunnel occupant whose exit direction points out of the
	// portal mouth (piece rotation -> path exit direction). Used to tell one
	// carriageway of a two-tile portal from the other.
	uint32_t CountPathsExitingToward(cIGZUnknown* tunnel, uint8_t tunnelPieceDirection);
}
