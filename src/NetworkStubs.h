#pragma once
#include <cstdint>
#include "cISC4NetworkOccupant.h"
#include "SC4Vector.h"
#include "SC4HashMap.h"
#include "SC4Point.h"
#include "RotFlip.h"

// The RESERVED macro provides names for reserved or unknown struct fields
#define _CONCATNAM(x,y) x ## y
#define _EXPANDNAM(x,y) _CONCATNAM(x,y)
#define RESERVED _EXPANDNAM(reserved,__LINE__)

const int32_t kNextX[] = {-1, 0, 1, 0};
const int32_t kNextZ[] = {0, -1, 0, 1};

class cSC4NetworkCellInfo
{
	public:
		uint32_t x;
		uint32_t z;
		uint32_t networkTypeFlags;
		cISC4NetworkOccupant* networkOccupant;
		uint8_t RESERVED[0x8];
		uint32_t edgesPerNetwork[13];
		uint32_t edgeFlagsCombined;  // bitwise-or in case of 2 networks
		uint8_t RESERVED[0x03];
		bool isImmovable;
		uint8_t RESERVED[2];
		bool isNetworkLot;
		uint8_t RESERVED[168-4-0x43-16];
		int32_t vertices[4];  // NW, SW, SE, NE (see CellCorner): vertex indices in contiguous (n+1)×(n+1) array where n is city size
		int32_t idxInCellsBuffer;
};
static_assert(offsetof(cSC4NetworkCellInfo, edgesPerNetwork) == 0x18);
static_assert(offsetof(cSC4NetworkCellInfo, isImmovable) == 0x53);
static_assert(offsetof(cSC4NetworkCellInfo, isNetworkLot) == 0x56);
static_assert(offsetof(cSC4NetworkCellInfo, idxInCellsBuffer) == 0xb8);
static_assert(sizeof(cSC4NetworkCellInfo) == 0xbc);

class cSC4NetworkWorldCache
{
	public:
		typedef cSC4NetworkCellInfo* (__thiscall* pfn_GetCell)(cSC4NetworkWorldCache* pThis, uint32_t xz);
		inline cSC4NetworkCellInfo* GetCell(uint32_t xz) {
			return reinterpret_cast<pfn_GetCell>(0x647a20)(this, xz);
		}

		uint8_t RESERVED[40];
};
static_assert(sizeof(cSC4NetworkWorldCache) == 0x28);

class cSC4NetworkTypeInfo
{
	public:
		uint8_t RESERVED[0xf0];
		SC4Vector<uint32_t> pylonSupportIDs;
		uint8_t RESERVED[0x4];
		float slopeOrth;
		float slopeDiag;
		float smoothnessOrth;
		float smoothnessDiag;
		float sideSlope;
};
static_assert(offsetof(cSC4NetworkTypeInfo, slopeOrth) == 0x100);
static_assert(sizeof(cSC4NetworkTypeInfo) == 0x114);

class cSC4VertexHtConstraintSatisfier
{
	public:
		typedef void (__thiscall* pfn_InsertSlopeConstraint)(cSC4VertexHtConstraintSatisfier* pThis, int32_t vertex1, int32_t vertex2, float slope);
		static inline pfn_InsertSlopeConstraint InsertSlopeConstraint = reinterpret_cast<pfn_InsertSlopeConstraint>(0x65ff60);

		uint8_t RESERVED[0x64];
};
static_assert(sizeof(cSC4VertexHtConstraintSatisfier) == 0x64);

class cSC4NetworkTool
{
	public:
		struct tCrossSection
		{
			int32_t vertex1;
			int32_t vertex2;
		};
		static_assert(sizeof(tCrossSection) == 0x8);

		struct tDraggedStep
		{
			uint8_t RESERVED[4];
			uint32_t draggedCellsIndex;
			uint32_t stepType;  // 0: regular/non-immovable, 1: bridge, 2: tunnel, 3: reserved
		};
		static_assert(sizeof(tDraggedStep) == 0xc);

		struct tSolvedCell
		{
			uint32_t id;
			RotFlip rf;
			uint32_t xz;
		};
		static_assert(sizeof(tSolvedCell) == 0xc);
		static_assert(offsetof(tSolvedCell, xz) == 0x8);

		struct tIntersection
		{
			uint32_t hid;
			SC4Point<int32_t> origin;
			uint8_t RESERVED[8];
			uint32_t step;
			uint8_t RESERVED[4];
			SC4Vector<int32_t> cells;
		};
		static_assert(sizeof(tIntersection) == 0x28);

		static inline cSC4NetworkTypeInfo* const sNetworkTypeInfo = (reinterpret_cast<cSC4NetworkTypeInfo*>(0xb452c8));

		typedef cISC4NetworkOccupant::eNetworkType (*pfn_GetFirstNetworkTypeFromFlags)(uint32_t networkTypeFlags);
		static inline pfn_GetFirstNetworkTypeFromFlags GetFirstNetworkTypeFromFlags = reinterpret_cast<pfn_GetFirstNetworkTypeFromFlags>(0x623e60);

		typedef void (__thiscall* pfn_InsertEqualityConstraint)(cSC4NetworkTool* pThis, int32_t vertex1, int32_t vertex2);
		inline void InsertEqualityConstraint(int32_t vertex1, int32_t vertex2) {
			return reinterpret_cast<pfn_InsertEqualityConstraint>(0x636000)(this, vertex1, vertex2);
		}

		typedef bool (__thiscall* pfn_GetCrossSections)(cSC4NetworkTool* pThis, cSC4NetworkCellInfo const &cellInfo, cISC4NetworkOccupant::eNetworkType networkType, int32_t direction,
				cSC4NetworkTool::tCrossSection &mid, cSC4NetworkTool::tCrossSection &start, cSC4NetworkTool::tCrossSection &end, bool &isDiag);
		inline bool GetCrossSections(cSC4NetworkCellInfo const &cellInfo, cISC4NetworkOccupant::eNetworkType networkType, int32_t direction,
				cSC4NetworkTool::tCrossSection &mid, cSC4NetworkTool::tCrossSection &start, cSC4NetworkTool::tCrossSection &end, bool &isDiag) {
			return reinterpret_cast<pfn_GetCrossSections>(0x636360)(this, cellInfo, networkType, direction, mid, start, end, isDiag);
		}

		void InsertSlopeConstraint(int32_t vertex1, int32_t vertex2, float slope) {
			if (vertex1 >= 0 && vertex2 >= 0 && vertex1 != vertex2) {
				cSC4VertexHtConstraintSatisfier::InsertSlopeConstraint(&this->vertexHtConstraintSatisfier1, vertex1, vertex2, slope);
			}
		}

		typedef void (__thiscall* pfn_InsertSmoothnessConstraint)(cSC4NetworkTool* pThis, int32_t vertex1, int32_t vertex2, int32_t vertex3, float smoothness);
		inline void InsertSmoothnessConstraint(int32_t vertex1, int32_t vertex2, int32_t vertex3, float smoothness) {
			return reinterpret_cast<pfn_InsertSmoothnessConstraint>(0x636030)(this, vertex1, vertex2, vertex3, smoothness);
		}

		typedef cSC4NetworkCellInfo* (__thiscall* pfn_GetCellInfo)(cSC4NetworkTool* pThis, uint32_t xz);
		inline cSC4NetworkCellInfo* GetCellInfo(uint32_t xz) {
			return reinterpret_cast<pfn_GetCellInfo>(0x633220)(this, xz);
		}

		void* vtable;
		intptr_t RESERVED[6];
		cSC4NetworkWorldCache networkWorldCache;
		uint8_t RESERVED[4];
		intptr_t occupantManager;
		uint8_t RESERVED[4];
		bool placeById;
		uint8_t RESERVED[3];
		SC4Vector<tDraggedStep> draggedSteps;
		SC4Vector<SC4Point<uint32_t>> draggedCells;
		uint8_t RESERVED[0x98 - 0x6c];
		SC4Vector<tSolvedCell> solvedCells;
		uint8_t RESERVED[0xcc - 0xa4];
		SC4Vector<tIntersection> highwayIntersections;
		uint8_t RESERVED[0x124 - 0xd8];
		cSC4VertexHtConstraintSatisfier vertexHtConstraintSatisfier1;
		cSC4VertexHtConstraintSatisfier vertexHtConstraintSatisfier2;
		uint8_t RESERVED[0x240 - 0x1ec];
		uint32_t numCellsX;
		uint32_t numCellsZ;
		uint8_t RESERVED[0x270 - 0x248];
};
static_assert(offsetof(cSC4NetworkTool, networkWorldCache) == 0x1c);
static_assert(offsetof(cSC4NetworkTool, draggedSteps) == 0x54);
static_assert(offsetof(cSC4NetworkTool, solvedCells) == 0x98);
static_assert(offsetof(cSC4NetworkTool, vertexHtConstraintSatisfier1) == 0x124);
static_assert(sizeof(cSC4NetworkTool) == 0x270);

namespace nSC4Networks
{
	struct cIntPiece
	{
		uint32_t pieceId;
		uint8_t rot;
		bool flip;
		uint8_t RESERVED[2];
		SC4Point<float> offset;
	};
	static_assert(sizeof(cIntPiece) == 0x10);

	struct cIntCheckCell
	{
		SC4Point<int32_t> cell;
		char letter;
	};
	static_assert(sizeof(cIntCheckCell) == 0xc);

	struct CheckType
	{
		uint8_t networks[2];  // 0xff or eNetworkType
		uint8_t RESERVED[2];
		uint32_t edges1;
		uint32_t mask1;
		uint32_t edges2;
		uint32_t mask2;
		bool isMultiOk;
		bool isStatic;  // i.e. prebuilt, otherwise `check` (or `optional`)
		bool isOptional;
		uint8_t RESERVED;
	};
	static_assert(sizeof(CheckType) == 0x18);

	struct cTileDef
	{
		uint32_t iidOffset;
		uint8_t rotFlip;
		bool isUnnamed;
		uint8_t RESERVED[2];
	};

	// RUL0 highway intersection entry
	struct cIntRule
	{
		uint32_t hid;
		SC4Vector<cIntPiece> pieces;  // usually size=1 (offset and orientation of preview model)
		SC4Point<int32_t> stepOffsets;
		SC4Point<int32_t> min;
		SC4Point<int32_t> max;
		SC4Vector<SC4Point<int32_t>> unnamedStaticCells;
		SC4Vector<SC4Point<int32_t>> RESERVED;
		SC4Point<int32_t> replacementIntersection;
		bool hasReplacementIntersection;
		uint8_t RESERVED[3];
		SC4Vector<SC4Point<int32_t>> staticCells;
		SC4Vector<cIntCheckCell> checkCells;
		uint8_t RESERVED[0x68 - 0x64];
		SC4HashMap<uint8_t, CheckType> checkTypes;
		uint8_t RESERVED[0x9c - 0x78];
		SC4Vector<uint8_t> constraints;  // for slopes of static cells
		uint32_t constraintsGridWidth;
		SC4Point<int32_t> constraintsOrigin;
		uint8_t RESERVED[0xb8 - 0xb4];
		typedef uint8_t AutoTileIndex;
		SC4Vector<AutoTileIndex> autoTileIndices;  // z*width+x -> 0 or z*0x10+x+1
		uint32_t autoTileGridWidth;
		uint32_t autoTileBase;
		uint32_t autoPathBase;
		uint8_t RESERVED[0xd4 - 0xd0];
		SC4HashMap<AutoTileIndex, cTileDef> tileDefs;
		uint32_t networkFlags;
		uint8_t oneWayDir;
		bool autoPlace;
		uint8_t RESERVED[2];
		uint32_t hidRotPrev;
		uint32_t hidRotNext;
		uint32_t hidTabPrev;
		uint32_t hidTabNext;
	};
	static_assert(offsetof(cIntRule, staticCells) == 0x4c);
	static_assert(sizeof(cIntRule) == 0xfc);
}
