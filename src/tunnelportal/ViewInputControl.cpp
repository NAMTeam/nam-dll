#include "TunnelPortalTool.h"

#include "PortalPlacer.h"
#include "Style.h"

#include "Logger.h"
#include "cGZMessage.h"
#include "cGZPersistResourceKey.h"
#include "cIGZBuffer.h"
#include "cIGZPersistResourceManager.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "cIGZWin.h"
#include "cIGZWinBtn.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cRZBaseWinProc.h"
#include "cSC4BaseViewInputControl.h"
#include "GZServPtrs.h"
#include "SC4UI.h"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
	constexpr uint32_t kTunnelPortalViewInputControlID = 0x4A7B6E31;
	constexpr uint32_t kPrimaryCursorTextID = kTunnelPortalViewInputControlID + 1;
	constexpr uint32_t kSecondaryCursorTextID = kTunnelPortalViewInputControlID + 2;
	constexpr uint32_t kStyleSelectorUIInstance = 0x4A7B6E41;
	// Use the vanilla bridge-entry template directly. The UI script service
	// creates this resource successfully as a repeated child, but rejects an
	// otherwise byte-identical copy under a custom instance.
	constexpr uint32_t kStyleSelectorEntryUIInstance = 0xEBD0D36D;
	constexpr uint32_t kUIScriptType = 0x00000000;
	constexpr uint32_t kUIScriptGroup = 0x96A006B0;
	constexpr uint32_t kStyleSelectorWindowCLSID = 0x0C525B9E;
	// Windows cSC4NetworkToolUI::AddBridgeEntry passes the template root ID
	// here (0xA0000), not the GZWinGen class ID suggested by the Mac symbols.
	constexpr uint32_t kStyleEntryWindowCLSID = 0x000A0000;
	constexpr uint32_t kStyleListID = 0x1000;
	constexpr uint32_t kStyleListContentID = 0x1001;
	constexpr uint32_t kAcceptButtonID = 0x2000;
	constexpr uint32_t kCancelButtonID = 0x2001;
	constexpr uint32_t kSelectedStyleTextID = 0x3002;
	constexpr uint32_t kStyleEntryTemplateButtonID = 0xA0002;
	constexpr uint32_t kStyleEntryButtonBaseID = 0xA1000;
	constexpr uint32_t kButtonActivatedMessage = 0x287259F6;
	constexpr uint32_t kPNGType = 0x856DDBAC;
	constexpr uint32_t kPNGResourceIID = 0x06F1568E;

	// The PNG resource factory returns this interface for type 0x856DDBAC.
	// cSC4NetworkToolUI's bridge-entry path calls its first method to obtain
	// the decoded buffer before passing that buffer to cIGZWinBtn::SetStyle.
	class cIGZPNGResource : public cIGZUnknown
	{
	public:
		virtual cIGZBuffer* AsIGZBuffer() = 0;
	};

	using TunnelPortal::PortalPlacer::Endpoint;
	using TunnelPortal::PortalPlacer::ExpectedPortalTileCount;
	using TunnelPortal::PortalPlacer::NetworkTypeName;
	using TunnelPortal::PortalPlacer::PlacePortalPair;
	using TunnelPortal::PortalPlacer::TryFindNetworkAtTile;

	cISC4City* GetCity()
	{
		cISC4AppPtr app;
		return app ? app->GetCity() : nullptr;
	}

	class TunnelPortalViewInputControl final
		: public cSC4BaseViewInputControl
		, public cRZBaseWinProc
	{
	public:
		TunnelPortalViewInputControl()
			: cSC4BaseViewInputControl(kTunnelPortalViewInputControlID)
		{
		}

		// This control implements two COM-style interfaces.  cSC4BaseViewInputControl
		// and cRZBaseWinProc each have their own reference-counted unknown base, so
		// inheriting their default AddRef/Release implementations would leave the
		// window-proc interface on a different lifetime than the view-control
		// interface.  The game retains/releases the window proc while dispatching
		// the selector; make both interfaces share the view-control lifetime.
		bool QueryInterface(uint32_t riid, void** ppvObj) override
		{
			if (!ppvObj)
			{
				return false;
			}

			if (riid == GZIID_cIGZWinProc)
			{
				*ppvObj = static_cast<cIGZWinProc*>(this);
				AddRef();
				return true;
			}

			return cSC4BaseViewInputControl::QueryInterface(riid, ppvObj);
		}

		uint32_t AddRef() override
		{
			return cSC4BaseViewInputControl::AddRef();
		}

		uint32_t Release() override
		{
			return cSC4BaseViewInputControl::Release();
		}

		bool Init() override
		{
			const bool result = cSC4BaseViewInputControl::Init();
			if (result)
			{
				Logger::GetInstance().WriteLine(LogLevel::Debug, "TunnelPortalTool: activated, waiting for first endpoint.");
				ShowPrompt("Select first portal endpoint", "Left click a road/network tile. Esc or right click cancels.");
			}

			return result;
		}

		bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override
		{
			if (vkCode == VK_ESCAPE)
			{
				Logger::GetInstance().WriteLine(LogLevel::Debug, "TunnelPortalTool: cancelled via Escape.");
				ClearFeedback();
				EndInput();
				return true;
			}

			return cSC4BaseViewInputControl::OnKeyDown(vkCode, modifiers);
		}

		bool OnMouseDownR(int32_t x, int32_t z, uint32_t modifiers) override
		{
			Logger::GetInstance().WriteLine(LogLevel::Debug, "TunnelPortalTool: cancelled via right click.");
			ClearFeedback();
			EndInput();
			return true;
		}

		bool OnMouseDownL(int32_t screenX, int32_t screenZ, uint32_t modifiers) override
		{
			if (!IsOnTop())
			{
				return false;
			}
			if (styleSelectorWindow)
			{
				return true;
			}

			Endpoint endpoint;
			if (!PickEndpoint(screenX, screenZ, endpoint))
			{
				Logger::GetInstance().WriteLineFormatted(
					LogLevel::Debug,
					"TunnelPortalTool: no compatible network at screen (%d,%d).",
					screenX, screenZ);
				ShowPrompt("No compatible network tile", "Pick an existing surface network tile.");
				return true;
			}

			if (!hasFirstEndpoint)
			{
				firstEndpoint = endpoint;
				hasFirstEndpoint = true;

				Logger::GetInstance().WriteLineFormatted(
					LogLevel::Debug,
					"TunnelPortalTool: first endpoint set - %s at (%u,%u).",
					NetworkTypeName(endpoint.networkType),
					endpoint.x,
					endpoint.z);

				// Keep the cursor-text ABI on the simple literal path.  The SDK's
				// cRZBaseString::Sprintf implementation is not safe to pass through
				// the game's cIGZString interface in this callback context.
				ShowPrompt("First portal endpoint", "Select second endpoint on the same network.");
				return true;
			}

			if (endpoint.x == firstEndpoint.x && endpoint.z == firstEndpoint.z)
			{
				Logger::GetInstance().WriteLineFormatted(
					LogLevel::Debug,
					"TunnelPortalTool: second endpoint (%u,%u) is the same tile as the first - rejected.",
					endpoint.x,
					endpoint.z);
				ShowPrompt("Invalid second endpoint", "Select a different network tile.");
				return true;
			}

			if (endpoint.networkType != firstEndpoint.networkType)
			{
				Logger::GetInstance().WriteLineFormatted(
					LogLevel::Debug,
					"TunnelPortalTool: network mismatch - first is %s, second is %s at (%u,%u).",
					NetworkTypeName(firstEndpoint.networkType),
					NetworkTypeName(endpoint.networkType),
					endpoint.x,
					endpoint.z);
				ShowPrompt("Network mismatch", "Select the second endpoint on the same network type.");
				return true;
			}

			secondEndpoint = endpoint;
			if (!ShowStyleSelector())
			{
				ShowPrompt(
					"Tunnel portal style selector failed",
					"See NAM.log for details.");
			}

			return true;
		}

		void Deactivate() override
		{
			CloseStyleSelector();
			ClearFeedback();
			cSC4BaseViewInputControl::Deactivate();
		}

		bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override
		{
			if (message.dwMessageType == 3
				&& message.dwData1 == kButtonActivatedMessage)
			{
				HandleStyleSelectorButton(message.dwData2);
				return true;
			}
			return false;
		}

		bool DoWinMsg(
			cIGZWin* window,
			uint32_t messageID,
			uint32_t data1,
			uint32_t data2,
			uint32_t data3) override
		{
			if (messageID == 3 && data1 == kButtonActivatedMessage)
			{
				HandleStyleSelectorButton(data2);
				return true;
			}
			return false;
		}

	private:
		bool ShowStyleSelector()
		{
			styles = TunnelPortal::Styles::LoadCompatibleStyles(
				firstEndpoint.networkType,
				ExpectedPortalTileCount(firstEndpoint.networkType));
			if (styles.empty())
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Error,
					"TunnelPortalTool: no compatible portal styles are available.");
				return false;
			}
			selectedStyleIndex = 0;
			iconBuffers.clear();

			const cGZPersistResourceKey selectorKey(
				kUIScriptType,
				kUIScriptGroup,
				kStyleSelectorUIInstance);
			cIGZWin* const createdSelector = SC4UI::CreateLegacyWindowFromScript(
				selectorKey,
				view3D ? view3D->AsIGZWin() : nullptr,
				kStyleSelectorWindowCLSID,
				static_cast<cIGZWinProc*>(this));
			styleSelectorWindow = cRZAutoRefCount<cIGZWin>(createdSelector);
			if (!styleSelectorWindow)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Error,
					"TunnelPortalTool: could not create the portal style selector UI. Is NAM_TunnelPortalStyles.dat installed?");
				return false;
			}

			cIGZWin* const root = styleSelectorWindow;
			if (!root)
			{
				CloseStyleSelector();
				return false;
			}
			// Do not call CenterWindowInRect here.  The retail cIGZWin vtable has
			// differed between game builds, and this slot currently triggers the
			// game's stack-check breakpoint.  The script supplies a valid default
			// position; the selector remains functional without recentering.
			for (const uint32_t hiddenControlID : {
				0x3000u, // bridge-height slider
				0x3003u, // bridge cost
				0x3004u, // bridge-height label
				0x4000u, // ferry clearance text
				0x4001u, // ferry clearance panel
			})
			{
				cIGZWin* const control =
					root->GetChildWindowFromID(hiddenControlID);
				if (control)
				{
					control->HideWindow();
				}
			}

			cIGZWin* const list = root->GetChildWindowFromIDRecursive(kStyleListID);
			cIGZWin* const content =
				list ? list->GetChildWindowFromIDRecursive(kStyleListContentID) : nullptr;
			if (!list || !content)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Error,
					"TunnelPortalTool: portal style selector UI is missing its style list controls.");
				CloseStyleSelector();
				return false;
			}

			const cGZPersistResourceKey entryKey(
				kUIScriptType,
				kUIScriptGroup,
				kStyleSelectorEntryUIInstance);
			for (size_t i = 0; i < styles.size(); ++i)
			{
				cRZAutoRefCount<cIGZWin> entry;
				if (!SC4UI::CreateWindowFromScript(
					entryKey,
					content,
					kStyleEntryWindowCLSID,
					GZIID_cIGZWin,
					entry.AsPPVoid())
					|| !entry)
				{
					Logger::GetInstance().WriteLineFormatted(
						LogLevel::Error,
						"TunnelPortalTool: failed to create style entry %u (%s).",
						static_cast<uint32_t>(i),
						styles[i].name.c_str());
					continue;
				}

				constexpr int32_t kEntryWidth = 89;
				constexpr int32_t kEntryHeight = 58;
				constexpr int32_t kColumns = 4;
				entry->GZWinMoveTo(
					static_cast<int32_t>(i % kColumns) * kEntryWidth,
					static_cast<int32_t>(i / kColumns) * kEntryHeight);

				cRZAutoRefCount<cIGZWinBtn> button;
				if (!entry->GetChildAsRecursive(
					kStyleEntryTemplateButtonID,
					GZIID_cIGZWinBtn,
					button.AsPPVoid())
					|| !button)
				{
					Logger::GetInstance().WriteLineFormatted(
						LogLevel::Error,
						"TunnelPortalTool: style entry %u is missing button 0x%08X.",
						static_cast<uint32_t>(i),
						kStyleEntryTemplateButtonID);
					continue;
				}

				button->SetID(kStyleEntryButtonBaseID + static_cast<uint32_t>(i));
				button->AsIGZWin()->SetNotificationTarget(root);
				button->SetBtnFlag(cIGZWinBtn::BtnFlagShowCaption, true);
				cRZBaseString caption(styles[i].name.c_str());
				button->SetCaption(caption);
				button->SetChecked(i == selectedStyleIndex);
				TrySetStyleIcon(button, styles[i]);
			}

			const int32_t rows =
				static_cast<int32_t>((styles.size() + 3) / 4);
			// The bridge selector keeps the scrolling content at least as tall
			// as its viewport. Shrinking it to one 58-pixel row exposes the
			// transparent 3D view behind the list.
			content->SetH(std::max(std::max(rows, 1) * 58, list->GetH()));
			list->InvalidateSelfAndParents();
			UpdateSelectedStyle();
			ShowPrompt(
				"Select tunnel portal style",
				"Choose a facade, then click Accept.");
			return true;
		}

		void TrySetStyleIcon(
			cIGZWinBtn* button,
			const TunnelPortal::Styles::Style& style)
		{
			if (!button || style.iconGroup == 0 || style.iconInstance == 0)
			{
				return;
			}

			cIGZPersistResourceManagerPtr resourceManager;
			if (!resourceManager)
			{
				return;
			}

			cGZPersistResourceKey iconKey(
				kPNGType,
				style.iconGroup,
				style.iconInstance);
			cRZAutoRefCount<cIGZPNGResource> pngResource;
			const bool loaded = resourceManager->GetResource(
				iconKey,
				kPNGResourceIID,
				pngResource.AsPPVoid(),
				cGZBufferColorType::AutoFromScreenFormat,
				nullptr);
			if (loaded && pngResource)
			{
				cIGZBuffer* const decodedBuffer = pngResource->AsIGZBuffer();
				if (!decodedBuffer)
				{
					Logger::GetInstance().WriteLineFormatted(
						LogLevel::Error,
						"TunnelPortalTool: PNG resource %08X-%08X-%08X returned no decoded buffer.",
						kPNGType,
						style.iconGroup,
						style.iconInstance);
					return;
				}

				cRZAutoRefCount<cIGZBuffer> buffer(
					decodedBuffer,
					cRZAutoRefCount<cIGZBuffer>::kAddRef);
				const int32_t width = buffer->Width();
				const int32_t height = buffer->Height();

				// Match cSC4NetworkToolUI::AddBridgeEntry: retain the button's
				// script-defined toggle style and install a prebuilt four-frame
				// state strip.
				const cIGZWinBtn::Style buttonStyle = button->GetStyle();
				const bool styleSet =
					button->SetStyle(buttonStyle, buffer, nullptr);
				Logger::GetInstance().WriteLineFormatted(
					styleSet ? LogLevel::Debug : LogLevel::Error,
					"TunnelPortalTool: style icon %s, key=%08X-%08X-%08X, buffer=%dx%d, buttonStyle=%d.",
					styleSet ? "installed" : "SetStyle failed",
					kPNGType,
					style.iconGroup,
					style.iconInstance,
					width,
					height,
					static_cast<int32_t>(buttonStyle));
				if (!styleSet)
				{
					return;
				}
				// The four-frame PNG already contains the style name. Keep the
				// caption only for entries such as Default that have no icon.
				button->SetBtnFlag(cIGZWinBtn::BtnFlagShowCaption, false);
				iconBuffers.push_back(std::move(buffer));
			}
			else
			{
				Logger::GetInstance().WriteLineFormatted(
					LogLevel::Error,
					"TunnelPortalTool: PNG resource manager failed to load style icon %08X-%08X-%08X (loaded=%d, resource=%d).",
					kPNGType,
					style.iconGroup,
					style.iconInstance,
					loaded ? 1 : 0,
					pngResource ? 1 : 0);
			}
		}

		void HandleStyleSelectorButton(uint32_t buttonID)
		{
			if (!styleSelectorWindow)
			{
				return;
			}

			if (buttonID == kAcceptButtonID)
			{
				if (selectedStyleIndex >= styles.size())
				{
					return;
				}
				const TunnelPortal::Styles::Style selectedStyle =
					styles[selectedStyleIndex];
				CloseStyleSelector();

				const bool placed =
					PlacePortalPair(firstEndpoint, secondEndpoint, selectedStyle);
				ShowPrompt(
					placed ? "Tunnel portals linked" : "Tunnel portal placement failed",
					placed ? "Portal pair committed." : "See NAM.log for details.");
				if (placed)
				{
					EndInput();
				}
				return;
			}

			if (buttonID == kCancelButtonID)
			{
				CloseStyleSelector();
				ClearFeedback();
				EndInput();
				return;
			}

			const uint32_t index = buttonID - kStyleEntryButtonBaseID;
			if (buttonID >= kStyleEntryButtonBaseID && index < styles.size())
			{
				selectedStyleIndex = index;
				UpdateSelectedStyle();
			}
		}

		void UpdateSelectedStyle()
		{
			if (!styleSelectorWindow || selectedStyleIndex >= styles.size())
			{
				return;
			}

			cIGZWin* const root = styleSelectorWindow;
			for (size_t i = 0; i < styles.size(); ++i)
			{
				cRZAutoRefCount<cIGZWinBtn> button;
				if (root->GetChildAsRecursive(
					kStyleEntryButtonBaseID + static_cast<uint32_t>(i),
					GZIID_cIGZWinBtn,
					button.AsPPVoid())
					&& button)
				{
					button->SetChecked(i == selectedStyleIndex);
				}
			}

			cIGZWin* const selectedText =
				root->GetChildWindowFromIDRecursive(kSelectedStyleTextID);
			if (selectedText)
			{
				cRZBaseString caption(styles[selectedStyleIndex].name.c_str());
				selectedText->SetCaption(caption);
			}
		}

		void CloseStyleSelector()
		{
			if (!styleSelectorWindow)
			{
				return;
			}

			cIGZWin* const window = styleSelectorWindow;
			if (window)
			{
				cIGZWin* const parent = window->GetParentWin();
				if (parent)
				{
					parent->ChildDelete(window);
				}
			}

			styleSelectorWindow.Reset();
			iconBuffers.clear();
			styles.clear();
		}

		bool PickEndpoint(int32_t screenX, int32_t screenZ, Endpoint& endpoint)
		{
			if (!view3D)
			{
				return false;
			}

			float worldCoords[3] = { 0.0f, 0.0f, 0.0f };
			if (!view3D->PickTerrain(screenX, screenZ, worldCoords, view3D->GetTerrainQueryEnabled()))
			{
				return false;
			}

			cISC4City* city = GetCity();
			if (!city)
			{
				return false;
			}

			const uint32_t maxX = city->CellCountX();
			const uint32_t maxZ = city->CellCountZ();

			if (maxX == 0 || maxZ == 0)
			{
				return false;
			}

			endpoint.x = std::min(static_cast<uint32_t>(std::max(worldCoords[0], 0.0f) / 16.0f), maxX - 1);
			endpoint.z = std::min(static_cast<uint32_t>(std::max(worldCoords[2], 0.0f) / 16.0f), maxZ - 1);

			return TryFindNetworkAtTile(endpoint.x, endpoint.z, endpoint.networkType);
		}

		void ShowPrompt(const char* titleText, const char* detailText)
		{
			if (!view3D)
			{
				return;
			}

			cRZBaseString title(titleText);
			cRZBaseString detail(detailText);
			view3D->SetCursorText(kPrimaryCursorTextID, 0, &detail, &title, 0);

			cRZBaseString hint("Right click or Esc: cancel");
			cRZBaseString mode("NAM tunnel portal tool");
			view3D->SetCursorText(kSecondaryCursorTextID, 0, &hint, &mode, 0);
		}

		void ClearFeedback()
		{
			if (view3D)
			{
				view3D->ClearCursorText(kPrimaryCursorTextID);
				view3D->ClearCursorText(kSecondaryCursorTextID);
			}
		}

		Endpoint firstEndpoint;
		Endpoint secondEndpoint;
		bool hasFirstEndpoint = false;
		uint32_t selectedStyleIndex = 0;
		std::vector<TunnelPortal::Styles::Style> styles;
		cRZAutoRefCount<cIGZWin> styleSelectorWindow;
		std::vector<cRZAutoRefCount<cIGZBuffer>> iconBuffers;
	};
}

bool TunnelPortalTool::Activate(cISC4View3DWin* view3D)
{
	Logger::GetInstance().WriteLineFormatted(
		LogLevel::Debug,
		"TunnelPortalTool: activation requested, view3D=%p.",
		view3D);

	if (!view3D)
	{
		return false;
	}

	static cRZAutoRefCount<cISC4ViewInputControl> sActiveControl;

	TunnelPortalViewInputControl* control = new TunnelPortalViewInputControl();
	static_cast<cISC4ViewInputControl*>(control)->AddRef();
	sActiveControl = static_cast<cISC4ViewInputControl*>(control);
	static_cast<cISC4ViewInputControl*>(control)->Release();

	const bool activated = view3D->SetCurrentViewInputControl(
		control,
		cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl);

	if (!activated)
	{
		Logger::GetInstance().WriteLine(LogLevel::Error, "TunnelPortalTool: failed to set view input control.");
		sActiveControl.Reset();
	}
	else
	{
		Logger::GetInstance().WriteLine(LogLevel::Debug, "TunnelPortalTool: view input control installed.");
	}

	return activated;
}
