#include "NetworkSlopes.h"
#include "Patching.h"
#include "Logger.h"
#include "NetworkStubs.h"

namespace
{
	void insertSlopeAndSmoothnessConstraints(cSC4NetworkTool* networkTool, cSC4NetworkCellInfo &cellInfo)
	{
		// cell is not immovable and not null
		auto networkType = cSC4NetworkTool::GetFirstNetworkTypeFromFlags(cellInfo.networkTypeFlags);
		uint32_t numBasicConns =
			((cellInfo.edgeFlagsCombined & 0x03000000) != 0) +
			((cellInfo.edgeFlagsCombined & 0x00030000) != 0) +
			((cellInfo.edgeFlagsCombined & 0x00000300) != 0) +
			((cellInfo.edgeFlagsCombined & 0x00000003) != 0);
		bool isMultiType = (cellInfo.networkTypeFlags & cellInfo.networkTypeFlags - 1) != 0;
		bool forceFlattenIntersection =
			(numBasicConns == 0 || numBasicConns > 2)
			&& (isMultiType || cellInfo.edgeFlagsCombined != 0x3010301 && cellInfo.edgeFlagsCombined != 0x1030103);
		if (forceFlattenIntersection ||
				!cSC4NetworkTool::sNetworkTypeInfo[networkType].pylonSupportIDs.empty() &&
				(cellInfo.edgeFlagsCombined & 0xf8f8f8f8) != 0 && numBasicConns == 2)  // special higher-flag Lightrail/Monorail pieces
		{
			uint32_t networkLotOffset = cellInfo.isNetworkLot != false ? 100000 : 0;
			networkTool->InsertEqualityConstraint(cellInfo.vertexNW + networkLotOffset, cellInfo.vertexSW + networkLotOffset);
			networkTool->InsertEqualityConstraint(cellInfo.vertexSW + networkLotOffset, cellInfo.vertexSE + networkLotOffset);
			networkTool->InsertEqualityConstraint(cellInfo.vertexSE + networkLotOffset, cellInfo.vertexNE + networkLotOffset);
		} else {
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
