#include "VolumetricLighting.h"

#include "ShaderCache.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VolumetricLighting::Settings,
	EnabledVL);

void VolumetricLighting::DrawSettings()
{
	if (ImGui::TreeNodeEx("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (globals::game::isVR) {
			if (ImGui::Checkbox("Enable Volumetric Lighting in VR", reinterpret_cast<bool*>(&settings.EnabledVL))) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Enable Volumetric Lighting in VR");
				}
				SetBooleanSettings(hiddenVRSettings, GetName(), settings.EnabledVL);
			}
			if (settings.EnabledVL) {
				RenderImGuiSettingsTree(VLSettings, "Skyrim Settings");
			}
		}
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

void VolumetricLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	if (globals::game::isVR) {
		Util::LoadGameSettings(VLSettings);
	}
}

void VolumetricLighting::SaveSettings(json& o_json)
{
	o_json = settings;
	if (globals::game::isVR) {
		Util::SaveGameSettings(VLSettings);
	}
}

void VolumetricLighting::RestoreDefaultSettings()
{
	settings = {};
	if (globals::game::isVR) {
		Util::ResetGameSettingsToDefaults(VLSettings);
		Util::ResetGameSettingsToDefaults(hiddenVRSettings);
	}
}

void VolumetricLighting::DataLoaded()
{
	auto shaderCache = globals::shaderCache;
	const static auto address = REL::Offset{ 0x1ec6b88 }.address();
	bool& bDepthBufferCulling = *reinterpret_cast<bool*>(address);

	if (REL::Module::IsVR() && bDepthBufferCulling && shaderCache->IsDiskCache()) {
		// clear cache to fix bug caused by bDepthBufferCulling
		logger::info("Force clearing cache due to bDepthBufferCulling");
		shaderCache->Clear();
	}
}

void VolumetricLighting::PostPostLoad()
{
	if (REL::Module::IsVR()) {
		if (settings.EnabledVL)
			EnableBooleanSettings(hiddenVRSettings, GetName());
		auto address = REL::RelocationID(100475, 0).address() + 0x45b;  // AE not needed, VR only hook
		logger::info("[{}] Hooking CopyResource at {:x}", GetName(), address);
		REL::safe_fill(address, REL::NOP, 7);
		stl::write_thunk_call<CopyResource>(address);
	}
}

void VolumetricLighting::Reset()
{
}