#include "HorizonFix.h"

#include <imgui.h>

void HorizonFix::DrawSettings()
{
	ImGui::TextWrapped(
		"This feature provides compatibility with the Horizon Fix SKSE plugin, which extends the water far clip plane to allow water to be rendered beyond the vanilla far clip distance. With Exponential Height Fog enabled, the sky is fogged out to the far water's horizon so the two meet without a seam. This feature is only active when the Horizon Fix plugin is installed.");
}

void HorizonFix::PostPostLoad()
{
	// The shader-side far-water support is only wanted while the HorizonFix plugin is
	// actually installed; without it water keeps the vanilla far-clip look. Checked here
	// because every SKSE plugin has loaded by now and the shader disk cache has not been
	// validated yet, so installing or removing the plugin invalidates the cache through
	// regular feature validation.
	const auto companionModule = GetModuleHandleW(L"HorizonFix.dll");
	if (loaded && companionModule == nullptr) {
		loaded = false;
		failedLoadedMessage = "HorizonFix is not installed, compatibility is disabled.";
		logger::info("[Horizon Fix] HorizonFix plugin not detected, compatibility disabled");
	} else {
		logger::info("[Horizon Fix] HorizonFix plugin detected, compatibility enabled");
	}
	if (loaded) {
		// Exponential Height Fog fogs the sky out to the far water's horizon (ISSAOComposite.hlsl,
		// Sky.hlsl); the plugin reports how far that is, and 0 wherever it draws no far water.
		farWaterDistanceFn = reinterpret_cast<FarWaterDistanceFn>(GetProcAddress(companionModule, "HorizonFix_GetFarWaterDistance"));
		if (!farWaterDistanceFn)
			logger::info("[Horizon Fix] HorizonFix plugin does not export its far water distance, sky fog stays at the far plane");
	}
}

HorizonFix::Settings HorizonFix::GetCommonBufferData() const
{
	Settings data;
	if (loaded && farWaterDistanceFn)
		data.farWaterDistance = std::max(farWaterDistanceFn(), 0.0f);
	return data;
}
