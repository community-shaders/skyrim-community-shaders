#include "NativeMenu/NativeMenu.h"

#include "Features/HDRDisplay.h"
#include "Globals.h"
#include "I18n/I18n.h"

#include <mutex>

#define I18N_KEY_PREFIX "feature.hdr_display."

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
			Checkbox(T(TKEY("enable_hdr"), "Enable HDR"), &GetHDREnabled, &SetHDREnabled,
				HDRDisplay::Settings{}.enableHDR ? 1.0f : 0.0f,
				T(TKEY("enable_hdr_tooltip"), "Enable HDR output. Matches vanilla visuals with extended dynamic range."),
				&IsHDRToggleEnabled),
		};
	}
}

#undef I18N_KEY_PREFIX
