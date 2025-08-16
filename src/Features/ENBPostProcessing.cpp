#include "PCH.h"
#include "ENBPostProcessing.h"
#include "State.h"
#include "Util.h"
#include "Feature.h"

void ENBPostProcessing::SaveSettings(json& o_json)
{
	o_json["Enabled"] = settings.Enabled;
	o_json["EffectPath"] = settings.EffectPath;
	o_json["SelectedTechnique"] = settings.SelectedTechnique;
	
	// Save variable values
	if (!settings.FloatVariables.empty()) {
		o_json["FloatVariables"] = settings.FloatVariables;
	}
	if (!settings.IntVariables.empty()) {
		o_json["IntVariables"] = settings.IntVariables;
	}
	if (!settings.BoolVariables.empty()) {
		o_json["BoolVariables"] = settings.BoolVariables;
	}
}

void ENBPostProcessing::LoadSettings(json& o_json)
{
	if (o_json["Enabled"].is_boolean())
		settings.Enabled = o_json["Enabled"];
	
	if (o_json["EffectPath"].is_string())
		settings.EffectPath = o_json["EffectPath"];
	
	if (o_json["SelectedTechnique"].is_string())
		settings.SelectedTechnique = o_json["SelectedTechnique"];
	
	// Load variable values
	if (o_json.contains("FloatVariables") && o_json["FloatVariables"].is_object()) {
		settings.FloatVariables = o_json["FloatVariables"].get<std::unordered_map<std::string, float>>();
	}
	if (o_json.contains("IntVariables") && o_json["IntVariables"].is_object()) {
		settings.IntVariables = o_json["IntVariables"].get<std::unordered_map<std::string, int>>();
	}
	if (o_json.contains("BoolVariables") && o_json["BoolVariables"].is_object()) {
		settings.BoolVariables = o_json["BoolVariables"].get<std::unordered_map<std::string, bool>>();
	}
}

void ENBPostProcessing::RestoreDefaultSettings()
{
	settings.Enabled = false;
	settings.EffectPath = "";
	settings.SelectedTechnique = "";
	settings.FloatVariables.clear();
	settings.IntVariables.clear();
	settings.BoolVariables.clear();
}

void ENBPostProcessing::DrawSettings()
{
	ImGui::Checkbox("Enable ENB Post Processing", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enables ENBSeries-compatible post-processing effects using DirectX 11 Effect (.fx) files.");
	}

	ImGui::Spacing();

	// Effect file selection
	static char effectPathBuffer[256];
	if (effectPathBuffer[0] == '\0') {
		strcpy_s(effectPathBuffer, sizeof(effectPathBuffer), settings.EffectPath.c_str());
	}
	
	if (ImGui::InputText("Effect File Path", effectPathBuffer, sizeof(effectPathBuffer))) {
		settings.EffectPath = effectPathBuffer;
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse##EffectPath")) {
		auto enbPath = Util::PathHelpers::GetRootRealPath() / "enbseries";
		if (!std::filesystem::exists(enbPath)) {
			std::filesystem::create_directories(enbPath);
		}
		Util::FileHelpers::OpenFolderInExplorer(enbPath);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Path to the ENB effect (.fx) file to load. Relative to game directory.");
	}

	ImGui::Spacing();

	// Reload button
	if (ImGui::Button("Reload Effect")) {
		if (!settings.EffectPath.empty()) {
			effect11.LoadFXFile(settings.EffectPath);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Load Default")) {
		settings.EffectPath = "enbseries/enbeffect.fx";
		effect11.LoadFXFile(settings.EffectPath);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Reload the current effect file or load the default ENB effect. Use the General tab for preset management.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Effect-specific settings (only if enabled and effect is loaded)
	if (settings.Enabled && !settings.EffectPath.empty()) {
		effect11.RenderImGui();
	}
}

void ENBPostProcessing::SetupResources()
{
	effect11.Initialize();
	effect11.LoadFXFile("enbseries/enbeffect.fx");
}

void ENBPostProcessing::Reset()
{
	// Reset effect state if needed
}

struct Main_HDRTonemapBlendCinematic_Render
{
	static void thunk(RE::ImageSpaceManager* a1, RE::ImageSpaceEffect* a2, uint32_t a3, uint32_t a4, RE::ImageSpaceShaderParam* a5)
	{
		// Check if ENB Post Processing is enabled
		if (globals::features::enbPostProcessing.settings.Enabled && 
		    globals::features::enbPostProcessing.GetEffect11().IsEffectLoaded()) {
			auto renderer = globals::game::renderer;	
			
			auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

			auto& imageSpaceTempCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY];
			auto& imageSpaceTempCopy2 = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2];

			globals::features::enbPostProcessing.GetEffect11().Execute(main, imageSpaceTempCopy, imageSpaceTempCopy2);

			globals::d3d::context->CopyResource(imageSpaceTempCopy2.texture, imageSpaceTempCopy.texture);
		} else {
			// If ENB Post Processing is disabled, call the original function
			func(a1, a2, a3, a4, a5);
		}
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

void ENBPostProcessing::PostPostLoad()
{
	stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x1EA, 0x178));
	if (REL::Module::IsSE())
		stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x230, 0x178));
}
