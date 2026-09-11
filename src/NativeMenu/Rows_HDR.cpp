#include "NativeMenu/NativeMenu.h"

#include "Features/HDRDisplay.h"
#include "Globals.h"
#include "I18n/I18n.h"

#include <mutex>

#define I18N_KEY_PREFIX "native_menu.hdr."

namespace
{
	HDRDisplay& HDR() { return globals::features::hdrDisplay; }

	bool __stdcall IsHDRToggleEnabled()
	{
		std::lock_guard<std::mutex> lock(HDR().settingsMutex);
		return HDRDisplay::isHDRMonitor || HDR().settings.enableHDR;
	}

	float __stdcall GetHDREnabled()
	{
		std::lock_guard<std::mutex> lock(HDR().settingsMutex);
		return HDR().settings.enableHDR ? 1.0f : 0.0f;
	}

	void __stdcall SetHDREnabled(float v)
	{
		const bool enable = v != 0.0f;
		std::lock_guard<std::mutex> lock(HDR().settingsMutex);
		if (HDR().settings.enableHDR == enable)
			return;
		HDR().settings.enableHDR = enable;
		HDR().UpdateHDRData();
		HDR().UpdateSwapChainColorSpace();
	}
}

namespace NativeMenu
{
	std::vector<Row> HDRRows()
	{
		if (!globals::features::hdrDisplay.loaded)
			return {};

		return {
			Checkbox(T(TKEY("enable"), "Enable HDR"), &GetHDREnabled, &SetHDREnabled,
				HDRDisplay::Settings{}.enableHDR ? 1.0f : 0.0f,
				T(TKEY("enable_desc"),
					"Real HDR output for HDR displays. Greyed out until an HDR-capable, HDR-enabled display is detected; "
					"use the full settings menu's Advanced override to force it otherwise."),
				&IsHDRToggleEnabled),
		};
	}
}

#undef I18N_KEY_PREFIX
