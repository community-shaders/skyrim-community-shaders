#include "TreeLODWind.h"

#include <imgui.h>

#include "../I18n/I18n.h"

#define I18N_KEY_PREFIX "feature.tree_lod_wind."

void TreeLODWind::DrawSettings()
{
	ImGui::TextWrapped("%s", T(TKEY("settings_info"),
		"This feature provides compatibility with the Windswept Tree LOD SKSE plugin, which makes the distant DynDOLOD billboard trees sway in the wind. The plugin's own settings control the sway. This feature is only active when the Windswept Tree LOD plugin is installed."));
}

void TreeLODWind::PostPostLoad()
{
	// Checked here because every SKSE plugin has loaded by now and the shader disk cache has not
	// been validated yet, so installing or removing the plugin invalidates the cache through
	// regular feature validation.
	const auto companionModule = GetModuleHandleW(L"K747_LodWind.dll");
	if (!loaded)
		return;

	if (companionModule == nullptr) {
		// Most users never install the plugin, so it is disabled without a failure message and kept out of the menu.
		loaded = false;
		logger::info("[Tree LOD Wind] Windswept Tree LOD plugin not detected, compatibility disabled");
		return;
	}

	// The b13 cbuffer layout and the t120 buffer are a contract with the plugin, so only a build that reports
	// this interface version enables the shader changes.
	using InterfaceVersionFn = std::uint32_t (*)();
	const auto interfaceVersionFn = reinterpret_cast<InterfaceVersionFn>(GetProcAddress(companionModule, "WindsweptTreeLOD_GetInterfaceVersion"));
	const std::uint32_t pluginVersion = interfaceVersionFn ? interfaceVersionFn() : 0;
	if (pluginVersion != kInterfaceVersion) {
		loaded = false;
		const auto requiredVersion = kInterfaceVersion;
		failedLoadedMessage = std::vformat(
			T(TKEY("incompatible_plugin"), "This Windswept Tree LOD version is not compatible (interface {}, required {}), compatibility is disabled. Update Windswept Tree LOD."),
			std::make_format_args(pluginVersion, requiredVersion));
		logger::warn("[Tree LOD Wind] {}", failedLoadedMessage);
		return;
	}

	logger::info("[Tree LOD Wind] Windswept Tree LOD plugin detected (interface {}), compatibility enabled", pluginVersion);
}

#undef I18N_KEY_PREFIX
