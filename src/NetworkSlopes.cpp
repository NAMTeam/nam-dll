#include "NetworkSlopes.h"
#include "Patching.h"
#include "Logger.h"
#include "NetworkStubs.h"
#include <algorithm>

namespace
{
	uint32_t countConnections(uint32_t edgeFlags, uint8_t mask) {
		uint32_t m = mask & 0xff;
		return
			((edgeFlags & m << 24) != 0) +
			((edgeFlags & m << 16) != 0) +
			((edgeFlags & m <<  8) != 0) +
			((edgeFlags & m      ) != 0);
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

	void insertSlopeAndSmoothnessConstraints(cSC4NetworkTool* networkTool, cSC4NetworkCellInfo &cellInfo)
	{
		// cell is not immovable and not null
		auto networkType = cSC4NetworkTool::GetFirstNetworkTypeFromFlags(cellInfo.networkTypeFlags);
		uint32_t numBasicConnsCombined = countConnections(cellInfo.edgeFlagsCombined, 0x03);
		bool isMultiType = (cellInfo.networkTypeFlags & cellInfo.networkTypeFlags - 1) != 0;

		bool isFalsie = false;
		if (isMultiType && !cellInfo.isNetworkLot) {
			auto networkType2 = cSC4NetworkTool::GetFirstNetworkTypeFromFlags(cellInfo.networkTypeFlags & cellInfo.networkTypeFlags - 1);
			auto edgeFlags1 = cellInfo.edgesPerNetwork[networkType];
			auto edgeFlags2 = cellInfo.edgesPerNetwork[networkType2];
			if (isFalsieFirst(edgeFlags1, edgeFlags2, false)) {  // orth
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
				// not a falsie
			}
		}

		if (!isFalsie && (
			(numBasicConnsCombined == 0 || numBasicConnsCombined > 2)
			&& (isMultiType || cellInfo.edgeFlagsCombined != 0x3010301 && cellInfo.edgeFlagsCombined != 0x1030103)  // intersections, regardless of number of networks
			|| !cSC4NetworkTool::sNetworkTypeInfo[networkType].pylonSupportIDs.empty()
			&& (cellInfo.edgeFlagsCombined & 0xf8f8f8f8) != 0 && numBasicConnsCombined == 2  // special higher-flag Lightrail/Monorail pieces
			))
		{
			uint32_t networkLotOffset = cellInfo.isNetworkLot != false ? 100000 : 0;
			networkTool->InsertEqualityConstraint(cellInfo.vertexNW + networkLotOffset, cellInfo.vertexSW + networkLotOffset);
			networkTool->InsertEqualityConstraint(cellInfo.vertexSW + networkLotOffset, cellInfo.vertexSE + networkLotOffset);
			networkTool->InsertEqualityConstraint(cellInfo.vertexSE + networkLotOffset, cellInfo.vertexNE + networkLotOffset);
		}
		else
		{
			cSC4NetworkTool::tCrossSection csA, csB, csC;
			bool isDiag;
			for (int32_t dir = 0; dir < 4; dir++) {
				if (networkTool->GetCrossSections(cellInfo, networkType, dir, csA, csB, csC, isDiag)) {
					auto slope = isDiag ? cSC4NetworkTool::sNetworkTypeInfo[networkType].slopeDiag : cSC4NetworkTool::sNetworkTypeInfo[networkType].slopeOrth;
					auto smoothness = isDiag ? cSC4NetworkTool::sNetworkTypeInfo[networkType].smoothnessDiag : cSC4NetworkTool::sNetworkTypeInfo[networkType].smoothnessOrth;
					networkTool->InsertEqualityConstraint(csA.vertex1, csA.vertex2);
					networkTool->InsertEqualityConstraint(csB.vertex1, csB.vertex2);
					networkTool->InsertEqualityConstraint(csC.vertex1, csC.vertex2);
					networkTool->InsertSlopeConstraint(csB.vertex1, csA.vertex1, slope);
					networkTool->InsertSlopeConstraint(csA.vertex1, csC.vertex1, slope);
					networkTool->InsertSmoothnessConstraint(csB.vertex1, csA.vertex1, csC.vertex1, smoothness);
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
