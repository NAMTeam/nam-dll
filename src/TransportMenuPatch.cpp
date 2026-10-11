#include "TransportMenuPatch.h"
#include "Patching.h"

namespace
{
	// network menu groups:
	// 0x4000 road menu
	// 0x4001 rail menu
	// 0x4002 subway menu
	// 0x4003 power utility menu
	// 0x4004 water utility menu
	// 0x4005 highway menu
	constexpr uint32_t networkGroupSeaport = 0x4006;
	constexpr uint32_t networkGroupAirport = 0x4007;

	constexpr uint32_t DoTransportMenu_Seaport_Inject   = 0x7f3d3b;
	constexpr uint32_t DoTransportMenu_Seaport_Continue = 0x7f3d43;
	constexpr uint32_t DoTransportMenu_Airport_Inject   = 0x7f3d1e;
	constexpr uint32_t DoTransportMenu_Airport_Continue = 0x7f3d26;

	void NAKED_FUN Hook_DoTransportMenu_Seaport(void)
	{
		__asm {
			mov ecx, networkGroupSeaport;
			lea edi, [esi + 0x29c];
			push DoTransportMenu_Seaport_Continue;
			ret;
		}
	}

	void NAKED_FUN Hook_DoTransportMenu_Airport(void)
	{
		__asm {
			mov ecx, networkGroupAirport;
			lea edi, [esi + 0x294];
			push DoTransportMenu_Airport_Continue;
			ret;
		}
	}
}

void TransportMenuPatch::Install()
{
	Patching::InstallHook(DoTransportMenu_Seaport_Inject, Hook_DoTransportMenu_Seaport);
	Patching::InstallHook(DoTransportMenu_Airport_Inject, Hook_DoTransportMenu_Airport);
}
