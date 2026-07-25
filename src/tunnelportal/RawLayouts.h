#pragma once

#include <cstddef>
#include <cstdint>

// Mirrors of game structures the tunnel portal tool has to walk but that have
// no declaration in the SDK headers. Field names are ours; sizes and offsets
// are fixed by the game, so the static_asserts below are load-bearing.
//
// Layouts only, no behaviour. See GameAddresses.h for where these live.
namespace TunnelPortal::Raw
{
	// --- cSC4TrafficSimulator tunnel registry -------------------------------
	//
	// The simulator tracks tunnels twice: a hash map keyed by portal cell, and a
	// doubly linked list. The two use different payload layouts.

	// Hash map entry. key packs the portal cell as (x << 8) | z; value[0] packs
	// the peer cell in its low half and a connection mask in its high half:
	//   value[0] bits  0..7  peer x
	//   value[0] bits  8..15 peer z
	//   value[0] bits 16..31 connection mask
	struct TunnelMapNode
	{
		TunnelMapNode* next;
		uint16_t key;
		uint16_t padding;
		uint32_t value[11];
	};

	// std::hash_map-style bucket array. start/end delimit the bucket range.
	struct TunnelMap
	{
		uint32_t reserved;
		TunnelMapNode** start;
		TunnelMapNode** end;
		TunnelMapNode** capacity;
		uint32_t size;
	};

	// Linked list entry. Same payload size as TunnelMapNode but packed
	// differently: both cells sit in the first four bytes and the connection
	// mask at byte 12.
	//   value bytes  0..3  from x, from z, to x, to z
	//   value bytes 12..13 connection mask
	struct TunnelListNode
	{
		TunnelListNode* next;
		TunnelListNode* previous;
		uint32_t value[11];
	};

	static_assert(sizeof(TunnelMapNode) == 0x34);
	static_assert(sizeof(TunnelMap) == 0x14);
	static_assert(offsetof(TunnelListNode, value) == 0x08);

	// Traversal caps. The registry is reached through raw pointers, so a
	// corrupt or unexpected layout must not turn into an endless walk.
	constexpr size_t kMaxTunnelMapBuckets = 4096;
	constexpr size_t kMaxTunnelMapNodes = 16384;

	// --- cSC4PathInfo path map ----------------------------------------------
	//
	// Maps a path key to a vector of points. Key layout, from
	// cISC4PathInfo::ExtractFullPathKey:
	//   bits 31..28 key type
	//   bits 27..24 path type
	//   bits 23..16 uniqueness index
	//   bits 15..8  entry direction
	//   bits  7..0  exit direction
	// Directions use kNextX/kNextZ numbering: 0=west, 1=north, 2=east, 3=south.

	struct PathVector
	{
		uint8_t* start;
		uint8_t* end;
		uint8_t* capacity;
	};

	struct PathMapNode
	{
		PathMapNode* next;
		uint32_t key;
		PathVector path;
	};

	struct PathMap
	{
		uint32_t reserved;
		PathMapNode** start;
		PathMapNode** end;
		PathMapNode** capacity;
	};

	struct PathPoint
	{
		float x;
		float y;
		float z;
	};

	static_assert(sizeof(PathVector) == 0x0c);
	static_assert(sizeof(PathMapNode) == 0x14);
	static_assert(sizeof(PathMap) == 0x10);
	static_assert(sizeof(PathPoint) == 0x0c);

	constexpr size_t kMaxPathMapBuckets = 4096;
	constexpr size_t kMaxPathMapNodes = 16384;
}
