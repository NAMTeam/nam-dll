#include "NetworkSlopes.h"
#include "Patching.h"
#include "NetworkStubs.h"
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <bit>
#include "RotFlip.h"

#define NW_MASK(n) (1 << cISC4NetworkOccupant::eNetworkType::n)
constexpr uint32_t allNetworksMask = 0x1fff;  // 13 networks

struct IntersectionFlags {
	uint32_t networkTypeFlags;
	uint32_t edgeFlags1;
	uint32_t edgeFlags2;
	bool operator==(const IntersectionFlags&) const = default;
};
template<> struct std::hash<IntersectionFlags>
{
	std::size_t operator()(const IntersectionFlags &s) const noexcept
	{
		std::size_t h = 5381;
		h = ((h << 5) + h) + std::hash<uint32_t>{}(s.networkTypeFlags);
		h = ((h << 5) + h) + std::hash<uint32_t>{}(s.edgeFlags1);
		h = ((h << 5) + h) + std::hash<uint32_t>{}(s.edgeFlags2);
		return h;
	}
};

namespace
{
	enum CellCorner : uint8_t { NW = 0, SW = 1, SE = 2, NE = 3 };

	// apply rotation/flip to vertex corners 0:NW, 1:SW, 2:SE, 3:NE
	constexpr CellCorner rotateCorner(CellCorner corner, RotFlip rf) {
		return static_cast<CellCorner>(((corner - (rf & 0x3)) & 0x3) ^ (rf >> 6 | rf >> 7));
	}

	enum CellSide : uint8_t { West = 0, North = 1, East = 2, South = 3 };

	constexpr CellSide rotateSide(CellSide side, RotFlip rf) {
		uint8_t x = ((uint8_t) side + (uint8_t) rf) & 0x3;
		return static_cast<CellSide>(isFlipped(rf)
				? (x * 3 + 2) & 0x3  // switch 0 and 2, keep 1 and 3
				: x);
	}

	struct OnslopeSpec {
		CellSide groundSide;
		float height;
		bool firstIsMain;
	};

	constexpr float roadHtL1 = 7.5, roadHtL2 = 15.0;
	constexpr float railHtL1 = 7.5, railHtL2 = 15.5;

	// the flags must have the same order as in RUL1 (so must not be swapped)
	const std::unordered_map<IntersectionFlags, OnslopeSpec> onslopePiecesPartial = {
		// viaducts
		{{NW_MASK(Road)      | NW_MASK(DirtRoad),   0x00040004, 0x00020001}, {West, roadHtL1,  true}},  // L1 Road OST
		{{NW_MASK(Road)      | NW_MASK(DirtRoad),   0x00040004, 0x00020003}, {West, roadHtL2,  true}},  // L2 Road OST
		{{NW_MASK(Rail)      | NW_MASK(OneWayRoad), 0x00040000, 0x00020002}, {West, roadHtL1, false}},  // L1 OWR OST
		{{NW_MASK(LightRail) | NW_MASK(OneWayRoad), 0x00040000, 0x00020002}, {West, roadHtL2, false}},  // L2 OWR OST
		{{NW_MASK(Avenue)    | NW_MASK(DirtRoad),   0x04040004, 0x00020001}, {West, roadHtL1,  true}},  // L1 Avenue OST
		{{NW_MASK(Avenue)    | NW_MASK(DirtRoad),   0x00040404, 0x00020003}, {West, roadHtL1,  true}},  // L1 Avenue OST flipped
		{{NW_MASK(Avenue)    | NW_MASK(DirtRoad),   0x04040004, 0x00010002}, {West, roadHtL2,  true}},  // L2 Avenue OST
		{{NW_MASK(Avenue)    | NW_MASK(DirtRoad),   0x00040404, 0x00030002}, {West, roadHtL2,  true}},  // L2 Avenue OST flipped
		// RHW
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040000, 0x00020002}, {West, roadHtL1, false}},  // L1 RHW2 OST
		{{NW_MASK(Monorail)  | NW_MASK(DirtRoad),   0x00040000, 0x00020002}, {West, roadHtL2, false}},  // L2 RHW2 OST
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00000004, 0x00000401}, {West, roadHtL1, false}},  // L1 RHW2 OST diag lower
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00000004, 0x04000003}, {West, roadHtL1, false}},  // L1 RHW2 OST diag lower flipped
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x04040400, 0x04010000}, {West, roadHtL1, false}},  // L1 RHW2 OST diag upper
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x04040400, 0x00030400}, {West, roadHtL1, false}},  // L1 RHW2 OST diag upper flipped
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040000, 0x00000401}, {West, roadHtL2, false}},  // L2 RHW2 OST diag lower
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040000, 0x04000003}, {West, roadHtL2, false}},  // L2 RHW2 OST diag lower flipped
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040004, 0x04010000}, {West, roadHtL2, false}},  // L2 RHW2 OST diag upper
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040004, 0x00030400}, {West, roadHtL2, false}},  // L2 RHW2 OST diag upper flipped
		// Rail
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040404, 0x00020002}, {West, railHtL1,  true}},  // L1 Rail OST
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040402, 0x00020000}, {West, railHtL2,  true}},  // L2 Rail OST
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x04040400, 0x03010000}, {West, railHtL1,  true}},  // L1 Rail OST diag upper
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x04040400, 0x00030100}, {West, railHtL1,  true}},  // L1 Rail OST diag upper flipped
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00000304, 0x03010000}, {West, railHtL1,  true}},  // L1 Rail OST diag lower
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x01000004, 0x00030100}, {West, railHtL1,  true}},  // L1 Rail OST diag lower flipped
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x03040004, 0x00010000}, {West, railHtL2,  true}},  // L2 Rail OST diag upper
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040104, 0x00030000}, {West, railHtL2,  true}},  // L2 Rail OST diag upper flipped
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x00040301, 0x03010000}, {West, railHtL2,  true}},  // L2 Rail OST diag lower
		{{NW_MASK(Rail)      | NW_MASK(DirtRoad),   0x01040003, 0x00030100}, {West, railHtL2,  true}},  // L2 Rail OST diag lower flipped
		// HRW
		{{NW_MASK(Rail)      | NW_MASK(Monorail),   0x04020402, 0x02040304}, {West, railHtL1,  true}},  // L1 HRW OST (orth and diag)
		{{NW_MASK(Rail)      | NW_MASK(Monorail),   0x04020402, 0x01040204}, {West, railHtL2,  true}},  // L2 HRW OST (orth and diag)
	};

	std::unordered_map<IntersectionFlags, OnslopeSpec> initOnslopePiecesWithRotations() {
		std::unordered_map<IntersectionFlags, OnslopeSpec> onslopePieces = {};
		for (auto const& [key, value] : onslopePiecesPartial) {
			onslopePieces[key] = value;
			onslopePieces[{key.networkTypeFlags, std::rotl(key.edgeFlags1,  8), std::rotl(key.edgeFlags2,  8)}] = {rotateSide(value.groundSide, R1F0), value.height, value.firstIsMain};
			onslopePieces[{key.networkTypeFlags, std::rotl(key.edgeFlags1, 16), std::rotl(key.edgeFlags2, 16)}] = {rotateSide(value.groundSide, R2F0), value.height, value.firstIsMain};
			onslopePieces[{key.networkTypeFlags, std::rotl(key.edgeFlags1, 24), std::rotl(key.edgeFlags2, 24)}] = {rotateSide(value.groundSide, R3F0), value.height, value.firstIsMain};
		}
		return onslopePieces;
	}
	const std::unordered_map<IntersectionFlags, OnslopeSpec> onslopePieces = initOnslopePiecesWithRotations();

	enum CurveType : uint8_t { Curve45Diag, Curve45Orth, Curve45Kink, Diagonal, Curve45DoubleKink };

	struct CurveSpec {
		CurveType curveType;
		RotFlip rf;
	};

	constexpr uint32_t flagsFromOctal(uint8_t w, uint8_t n, uint8_t e, uint8_t s) {
		return ((uint32_t) s) << 24 | ((uint32_t) e) << 16 | ((uint32_t) n) << 8 | ((uint32_t) w);
	}

	const std::unordered_map<uint32_t, CurveSpec> curvePiecesPartial = {
		{flagsFromOctal(00, 00, 01, 013), {Curve45Diag, R0F0}},
		{flagsFromOctal(03, 00, 00, 011), {Curve45Diag, R0F1}},
		{flagsFromOctal(00, 02, 00, 011), {Curve45Orth, R0F0}},
		{flagsFromOctal(00, 02, 00, 013), {Curve45Orth, R0F1}},
		{flagsFromOctal(00, 00, 02, 013), {Curve45Kink, R0F0}},
		{flagsFromOctal(02, 00, 00, 011), {Curve45Kink, R0F1}},
		{flagsFromOctal(00, 00, 011, 013), {Curve45DoubleKink, R0F0}},
		{flagsFromOctal(013, 00, 00, 011), {Curve45DoubleKink, R0F1}},
		{flagsFromOctal(00, 00, 01, 03), {Diagonal, R0F0}},
	};

	std::unordered_map<uint32_t, CurveSpec> initCurvePiecesWithRotations() {
		std::unordered_map<uint32_t, CurveSpec> curvePieces = {};
		for (auto const& [key, value] : curvePiecesPartial) {
			curvePieces[key] = value;
			curvePieces[std::rotl(key,  8)] = {value.curveType, rotate(value.rf, 1)};
			curvePieces[std::rotl(key, 16)] = {value.curveType, rotate(value.rf, 2)};
			curvePieces[std::rotl(key, 24)] = {value.curveType, rotate(value.rf, 3)};
		}
		return curvePieces;
	}
	const std::unordered_map<uint32_t, CurveSpec> curvePieces = initCurvePiecesWithRotations();

	uint32_t countConnections(uint32_t edgeFlags, uint8_t mask) {
		uint32_t m = mask & 0xff;
		return
			((edgeFlags & m << 24) != 0) +
			((edgeFlags & m << 16) != 0) +
			((edgeFlags & m <<  8) != 0) +
			((edgeFlags & m      ) != 0);
	}

	bool isMultiType(const cSC4NetworkCellInfo &cellInfo) {
		return (cellInfo.networkTypeFlags & cellInfo.networkTypeFlags - 1 & allNetworksMask) != 0;
	}

	bool isPureDiag(uint32_t edgeFlags) {
		return edgeFlags == 0x03010000 || edgeFlags == 0x00030100 || edgeFlags == 0x00000301 || edgeFlags == 0x01000003;
	}

	bool isPureOrth(uint32_t edgeFlags) {
		return (edgeFlags & ~0x00040004) == 0x02000200 || (edgeFlags & ~0x04000400) == 0x00020002;
	}

	// estimate whether this likely is an orthogonal (or diagonal) falsie with respect to first network
	bool isFalsieFirst(uint32_t edgeFlags1, uint32_t edgeFlags2, bool diag) {
		if (diag ? isPureDiag(edgeFlags1) : isPureOrth(edgeFlags1)) {
			auto numConns2 = countConnections(edgeFlags2, 0xff);
			if (numConns2 >= 3 || numConns2 == 2 && !isPureOrth(edgeFlags2) && !isPureDiag(edgeFlags2)) {
				return true;
			}
		}
		return false;
	}

	constexpr uint32_t mkCellXZ(const uint32_t &x, const uint32_t &z) {
		return z * 0x10000 + x;
	}

	// determine the xz cell coordinate of the cell containing the cross section vertices
	bool diagCrossSectionToCellXZ(const cSC4NetworkTool::tCrossSection &cs, const uint32_t &numCellsX, const uint32_t &numCellsZ, uint32_t &xz) {
		auto totalVerts = (numCellsX + 1) * (numCellsZ + 1);
		if (cs.vertex1 < 0 || cs.vertex1 >= totalVerts || cs.vertex2 < 0 || cs.vertex2 >= totalVerts) {
			return false;
		} else {
			auto div1 = std::div(cs.vertex1, numCellsX + 1);
			auto div2 = std::div(cs.vertex2, numCellsX + 1);
			xz = mkCellXZ(std::min(div1.rem, div2.rem), std::min(div1.quot, div2.quot));
			return true;
		}
	}

	void insertSlopeAndSmoothnessConstraints(cSC4NetworkTool* networkTool, cSC4NetworkCellInfo &cellInfo)
	{
		// cell is not immovable and not null
		auto networkType = cSC4NetworkTool::GetFirstNetworkTypeFromFlags(cellInfo.networkTypeFlags);
		uint32_t numBasicConnsCombined = countConnections(cellInfo.edgeFlagsCombined, 0x03);
		bool isMulti = isMultiType(cellInfo);

		// check if cell is onslope or falsie
		bool isFalsie = false;
		const OnslopeSpec* onslopeSpec;
		if (isMulti && !cellInfo.isNetworkLot) {
			auto networkType2 = cSC4NetworkTool::GetFirstNetworkTypeFromFlags(cellInfo.networkTypeFlags & cellInfo.networkTypeFlags - 1);
			auto edgeFlags1 = cellInfo.edgesPerNetwork[networkType];
			auto edgeFlags2 = cellInfo.edgesPerNetwork[networkType2];
			IntersectionFlags key = {.networkTypeFlags = cellInfo.networkTypeFlags & allNetworksMask, .edgeFlags1 = edgeFlags1, .edgeFlags2 = edgeFlags2};
			if (auto search = onslopePieces.find(key); search != onslopePieces.end()) {
				onslopeSpec = &search->second;
				if (!onslopeSpec->firstIsMain) {
					std::swap(networkType, networkType2);
					std::swap(edgeFlags1, edgeFlags2);
				}
			} else if (isFalsieFirst(edgeFlags1, edgeFlags2, false)) {  // orth
				isFalsie = true;
			} else if (isFalsieFirst(edgeFlags2, edgeFlags1, false)) {  // orth
				isFalsie = true;
				std::swap(networkType, networkType2);
				std::swap(edgeFlags1, edgeFlags2);
			} else if (isFalsieFirst(edgeFlags1, edgeFlags2, true)) {  // diag
				isFalsie = true;
			} else if (isFalsieFirst(edgeFlags2, edgeFlags1, true)) {  // diag
				isFalsie = true;
				std::swap(networkType, networkType2);
				std::swap(edgeFlags1, edgeFlags2);
			} else {
				// neither falsie nor onslope
			}
		}

		const CurveSpec* curveSpec;
		if (!isMulti && !cellInfo.isNetworkLot && (cellInfo.networkTypeFlags & (NW_MASK(LightRail) | NW_MASK(Monorail))) == 0) {
			// For now, avoid sloped curves for Lightrail/Monorail, as otherwise support pillars could sometimes stick through the track
			// as they are not perfectly perpendicular to the gradient of the S3D polygons. Consider revisiting this when there are Lightrail WRCs.
			if (auto search = curvePieces.find(cellInfo.edgeFlagsCombined); search != curvePieces.end()) {
				curveSpec = &search->second;
			}
		}

		if (onslopeSpec != nullptr)
		{
			// larger slope tolerance for onslope pieces
			auto slope = cSC4NetworkTool::sNetworkTypeInfo[networkType].slopeOrth + onslopeSpec->height;
			if (onslopeSpec->groundSide == West || onslopeSpec->groundSide == East) {  // x-axis
				networkTool->InsertEqualityConstraint(cellInfo.vertices[NW], cellInfo.vertices[SW]);
				networkTool->InsertEqualityConstraint(cellInfo.vertices[NE], cellInfo.vertices[SE]);
				networkTool->InsertSlopeConstraint(cellInfo.vertices[SW], cellInfo.vertices[SE], slope);
			} else {  // z-axis
				networkTool->InsertEqualityConstraint(cellInfo.vertices[SE], cellInfo.vertices[SW]);
				networkTool->InsertEqualityConstraint(cellInfo.vertices[NE], cellInfo.vertices[NW]);
				networkTool->InsertSlopeConstraint(cellInfo.vertices[SE], cellInfo.vertices[NE], slope);
			}
		}
		else if (curveSpec != nullptr)
		{
			// slope tolerance for 45 degree curves
			auto getAdjacentCell = [&networkTool, &cellInfo](CellSide dir) {
				uint32_t x = kNextX[dir] + cellInfo.x;
				uint32_t z = kNextZ[dir] + cellInfo.z;
				cSC4NetworkCellInfo* result;
				if (x < networkTool->numCellsX && z < networkTool->numCellsZ) {
					result = networkTool->GetCellInfo(mkCellXZ(x, z));
				}
				return result;
			};
			auto &&rf = curveSpec->rf;
			auto &&vNW = cellInfo.vertices[rotateCorner(NW, rf)];
			auto &&vSW = cellInfo.vertices[rotateCorner(SW, rf)];
			auto &&vSE = cellInfo.vertices[rotateCorner(SE, rf)];
			auto &&vNE = cellInfo.vertices[rotateCorner(NE, rf)];
			auto &&ti = cSC4NetworkTool::sNetworkTypeInfo[networkType];
			switch (curveSpec->curveType) {
				case Diagonal:  // rewriting constraints for pure diagonals so that smoothness constraints stay within the same cell
				case Curve45DoubleKink:
					networkTool->InsertEqualityConstraint(vNW, vSE);
					networkTool->InsertSlopeConstraint(vNW, vNE, ti.slopeDiag);
					networkTool->InsertSlopeConstraint(vNW, vSW, ti.slopeDiag);
					networkTool->InsertSmoothnessConstraint(vNE, vNW, vSW, ti.smoothnessDiag);
					break;
				case Curve45Diag:
					// For better slope conformance, we skip equality constraints for this cell, so all vertices can have different heights. Instead, we add more slope constraints.
					networkTool->InsertSlopeConstraint(vNE, vNW, ti.slopeDiag / 2);
					networkTool->InsertSlopeConstraint(vNW, vSW, ti.slopeDiag / 2);
					networkTool->InsertSlopeConstraint(vNE, vSE, ti.slopeDiag);  // same slope as on adjacent diagonal cell
					networkTool->InsertSlopeConstraint(vSE, vSW, ti.slopeDiag);
					networkTool->InsertSmoothnessConstraint(vNE, vNW, vSW, ti.smoothnessDiag);
					// Adding smoothness constraints involving the neighboring cells frequently leads to red drags for this cell, so we don't do that.
					break;
				case Curve45Orth:
					networkTool->InsertEqualityConstraint(vNW, vNE);
					networkTool->InsertEqualityConstraint(vSW, vSE);
					networkTool->InsertSlopeConstraint(vNW, vSW, ti.slopeOrth);
					{
						auto &&adjCellOrth = getAdjacentCell(rotateSide(North, rf));
						if (adjCellOrth != nullptr && !adjCellOrth->isNetworkLot) {
							networkTool->InsertSmoothnessConstraint(vSW, vNW, adjCellOrth->vertices[rotateCorner(NW, rf)], ti.smoothnessOrth);
						}
						auto &&adjCellBlend = getAdjacentCell(rotateSide(South, rf));
						if (adjCellBlend != nullptr && !adjCellBlend->isNetworkLot) {
							networkTool->InsertSmoothnessConstraint(  // for outside curve
									isFlipped(rf) ? vNE : vNW,
									isFlipped(rf) ? vSE : vSW,
									adjCellBlend->vertices[rotateCorner(isFlipped(rf) ? SE : SW, rf)],
									ti.smoothnessOrth);
						}
					}
					break;
				case Curve45Kink:
					networkTool->InsertEqualityConstraint(vNE, vSE);
					networkTool->InsertSlopeConstraint(vNE, vNW, ti.slopeDiag / 2);
					networkTool->InsertSlopeConstraint(vNW, vSW, ti.slopeDiag / 2);
					networkTool->InsertSmoothnessConstraint(vNE, vNW, vSW, ti.smoothnessDiag);
					{
						auto &&adjCellOrth = getAdjacentCell(rotateSide(East, rf));
						if (adjCellOrth != nullptr && !adjCellOrth->isNetworkLot) {
							networkTool->InsertSmoothnessConstraint(vSW, vSE, adjCellOrth->vertices[rotateCorner(SE, rf)], ti.smoothnessOrth);
						}
					}
					break;
				default:
					break;
			}
		}
		else if (!isFalsie && (
			(numBasicConnsCombined == 0 || numBasicConnsCombined > 2)
			&& (isMulti || cellInfo.edgeFlagsCombined != 0x3010301 && cellInfo.edgeFlagsCombined != 0x1030103)  // intersections, regardless of number of networks
			|| !cSC4NetworkTool::sNetworkTypeInfo[networkType].pylonSupportIDs.empty()
			&& (cellInfo.edgeFlagsCombined & 0xf8f8f8f8) != 0 && numBasicConnsCombined == 2  // special higher-flag Lightrail/Monorail pieces
			))
		{
			// flattening of intersections
			uint32_t networkLotOffset = cellInfo.isNetworkLot != false ? 100000 : 0;
			networkTool->InsertEqualityConstraint(cellInfo.vertices[NW] + networkLotOffset, cellInfo.vertices[SW] + networkLotOffset);
			networkTool->InsertEqualityConstraint(cellInfo.vertices[SW] + networkLotOffset, cellInfo.vertices[SE] + networkLotOffset);
			networkTool->InsertEqualityConstraint(cellInfo.vertices[SE] + networkLotOffset, cellInfo.vertices[NE] + networkLotOffset);
		}
		else
		{
			// slope and smoothness of straight network tiles (taking into account adjacent onslope pieces)
			std::unordered_map<uint32_t, bool> cellIsOnslopeCache = {{mkCellXZ(cellInfo.x, cellInfo.z), onslopeSpec != nullptr}};
			auto cellIsOnslope = [&networkTool, &cellIsOnslopeCache](uint32_t xz) {
				if (auto search = cellIsOnslopeCache.find(xz); search != cellIsOnslopeCache.end()) {
					return search->second;
				} else {
					auto adjCellInfo = networkTool->GetCellInfo(xz);
					bool result = false;
					if (adjCellInfo != nullptr && !adjCellInfo->isNetworkLot && isMultiType(*adjCellInfo)) {
						auto adjNetwork1 = cSC4NetworkTool::GetFirstNetworkTypeFromFlags(adjCellInfo->networkTypeFlags);
						auto adjNetwork2 = cSC4NetworkTool::GetFirstNetworkTypeFromFlags(adjCellInfo->networkTypeFlags & adjCellInfo->networkTypeFlags - 1);
						result = onslopePieces.contains({
								.networkTypeFlags = adjCellInfo->networkTypeFlags & allNetworksMask,
								.edgeFlags1 = adjCellInfo->edgesPerNetwork[adjNetwork1],
								.edgeFlags2 = adjCellInfo->edgesPerNetwork[adjNetwork2]});
					}
					cellIsOnslopeCache[xz] = result;
					return result;
				}
			};

			cSC4NetworkTool::tCrossSection csA, csB, csC;
			bool isDiag;
			for (int32_t dir = 0; dir < 4; dir++) {
				if (networkTool->GetCrossSections(cellInfo, networkType, dir, csA, csB, csC, isDiag)) {
					auto slope = isDiag ? cSC4NetworkTool::sNetworkTypeInfo[networkType].slopeDiag : cSC4NetworkTool::sNetworkTypeInfo[networkType].slopeOrth;
					auto smoothness = isDiag ? cSC4NetworkTool::sNetworkTypeInfo[networkType].smoothnessDiag : cSC4NetworkTool::sNetworkTypeInfo[networkType].smoothnessOrth;
					networkTool->InsertEqualityConstraint(csA.vertex1, csA.vertex2);

					// check if adjacent piece is an OST in which case some constraints must be skipped
					uint32_t xzB, xzC;
					bool adjIsOnslopeB = false, adjIsOnslopeC = false;
					// in orthogonal case, construct a diagonal cross section to determine a unique cell
					if (diagCrossSectionToCellXZ(isDiag ? csB : (cSC4NetworkTool::tCrossSection {csA.vertex1, csB.vertex2}), networkTool->numCellsX, networkTool->numCellsZ, xzB)) {
						adjIsOnslopeB = cellIsOnslope(xzB);
					}
					if (diagCrossSectionToCellXZ(isDiag ? csC : (cSC4NetworkTool::tCrossSection {csA.vertex1, csC.vertex2}), networkTool->numCellsX, networkTool->numCellsZ, xzC)) {
						adjIsOnslopeC = cellIsOnslope(xzC);
					}

					if (!adjIsOnslopeB) {
						networkTool->InsertEqualityConstraint(csB.vertex1, csB.vertex2);
						networkTool->InsertSlopeConstraint(csB.vertex1, csA.vertex1, slope);
					}
					if (!adjIsOnslopeC) {
						networkTool->InsertEqualityConstraint(csC.vertex1, csC.vertex2);
						networkTool->InsertSlopeConstraint(csA.vertex1, csC.vertex1, slope);
					}
					if (!adjIsOnslopeB && !adjIsOnslopeC) {
						networkTool->InsertSmoothnessConstraint(csB.vertex1, csA.vertex1, csC.vertex1, smoothness);
					}
				}
			}
		}
	}

	constexpr uint32_t InsertSlopeAndSmoothnessConstraintsForCell_InjectPoint = 0x6366c5;
	constexpr uint32_t InsertSlopeAndSmoothnessConstraintsForCell_ReturnJump = 0x636907;

	void NAKED_FUN Hook_InsertSlopeAndSmoothnessConstraintsForCell(void)
	{
		__asm {
			push eax;  // store
			push ecx;  // store
			push edx;  // store
			push ebx;  // cellInfo
			push ebp;  // networkTool
			call insertSlopeAndSmoothnessConstraints;  // (cdecl)
			add esp, 0x8;
			pop edx;  // restore
			pop ecx;  // restore
			pop eax;  // restore
			push InsertSlopeAndSmoothnessConstraintsForCell_ReturnJump;
			ret;
		}
	}

}

void NetworkSlopes::Install()
{
	Patching::InstallHook(InsertSlopeAndSmoothnessConstraintsForCell_InjectPoint, Hook_InsertSlopeAndSmoothnessConstraintsForCell);
}
