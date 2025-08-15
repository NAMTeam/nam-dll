#pragma once
#include <cstdint>
#include "cISC4NetworkOccupant.h"
#include "SC4Vector.h"

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
		int32_t vertexNW;  // vertex indices in contiguous (n+1)×(n+1) array where n is city size
		int32_t vertexSW;
		int32_t vertexSE;
		int32_t vertexNE;
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

		void* vtable;
		intptr_t RESERVED[6];
		cSC4NetworkWorldCache networkWorldCache;
		uint8_t RESERVED[0x124 - 28 - 10*4];
		cSC4VertexHtConstraintSatisfier vertexHtConstraintSatisfier1;
		cSC4VertexHtConstraintSatisfier vertexHtConstraintSatisfier2;
		uint8_t RESERVED[0x240 - 0x1ec];
		uint32_t numCellsX;
		uint32_t numCellsZ;
		uint8_t RESERVED[0x270 - 0x248];
};
static_assert(offsetof(cSC4NetworkTool, networkWorldCache) == 0x1c);
static_assert(offsetof(cSC4NetworkTool, vertexHtConstraintSatisfier1) == 0x124);
static_assert(sizeof(cSC4NetworkTool) == 0x270);
