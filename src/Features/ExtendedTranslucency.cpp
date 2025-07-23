#include "ExtendedTranslucency.h"

#include "../ShaderCache.h"
#include "../State.h"
#include "../Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExtendedTranslucency::MaterialParams,
	AlphaMode,
	AlphaReduction,
	AlphaSoftness,
	AlphaStrength);

const RE::BSFixedString ExtendedTranslucency::NiExtraDataName_AnisotropicAlphaMaterial = "AnisotropicAlphaMaterial";

ExtendedTranslucency* ExtendedTranslucency::GetSingleton()
{
	static ExtendedTranslucency singleton;
	return &singleton;
}

void ExtendedTranslucency::BSLightingShader_SetupGeometry(RE::BSRenderPass* pass)
{
	globals::state->permutationData.ExtraFeatureDescriptor &= ~(ExtraFeatureDescriptorMask << ExtraFeatureDescriptorShift);
	// TODO: PERFORMANCE: Caching the feature descriptor in map<RE::BSGeometry*, uint> if this get more complex
	auto& unknownProperty = pass->geometry->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kProperty];
	static const REL::Relocation<const RE::NiRTTI*> NiAlphaPropertyRTTI{ RE::NiAlphaProperty::Ni_RTTI };
	auto alphaProperty = unknownProperty && unknownProperty->GetRTTI() == NiAlphaPropertyRTTI.get() ? static_cast<RE::NiAlphaProperty*>(unknownProperty.get()) : nullptr;
	// Check alpha property exists and blending is enabled
	if (alphaProperty && alphaProperty->GetAlphaBlending()) {
		if (auto* data = pass->geometry->GetExtraData(NiExtraDataName_AnisotropicAlphaMaterial)) {
		i f (data->GetRTTI() == globals::rtti::NiIntegerExtraDataRTTI.get()) {
				uint32_t material = static_cast<uint32_t>(static_cast<RE::NiIntegerExtraData*>(data)->value) & ExtraFeatureDescriptorMask;
				if (material == MaterialModel::Disabled) {
					// MaterialModel::Disabled (0) is the flag when this extra does not exist
					// And it will let the effect use default settings instead of force disable it
					// Ensure this is disabled by using the ForceDisabled flag
					material = MaterialModel::ForceDisabled;
				}
				globals::state->permutationData.ExtraFeatureDescriptor |= (material << ExtraFeatureDescriptorShift);

				// TODO: Per-material settings from Nif
				// Mods supporting this feature should adjust their alpha value in texture already
				// And the texture should be adjusted based on full strength param
			}
		}
	} else {
		globals::state->permutationData.ExtraFeatureDescriptor |= ((MaterialModel::ForceDisabled) << ExtraFeatureDescriptorShift);
	}
}

struct ExtendedTranslucency::Hooks
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			ExtendedTranslucency::BSLightingShader_SetupGeometry(Pass);
			func(This, Pass, RenderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		logger::info("[ExtendedTranslucency] Installed hooks - BSLightingShader_SetupGeometry");
	}
};

void ExtendedTranslucency::PostPostLoad()
{
	Hooks::Install();
}

void ExtendedTranslucency::DrawSettings()
{
	if (ImGui::TreeNodeEx("Translucent Material", ImGuiTreeNodeFlags_DefaultOpen)) {
		static const char* AlphaModeNames[4] = {
			"Disabled",
			"Rim Light",
			"Isotropic Fabric",
			"Anisotropic Fabric"
		};

		bool changed = false;
		if (ImGui::Combo("Default Material Model", (int*)&settings.AlphaMode, AlphaModeNames, 4)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Anisotropic transluency will make the surface more opaque when you view it parallel to the surface.\n"
				"  - Disabled: No anisotropic transluency\n"
				"  - Rim Light: Naive rim light effect\n"
				"  - Isotropic Fabric: Imaginary fabric weaved from threads in one direction, respect normal map.\n"
				"  - Anisotropic Fabric: Common fabric weaved from tangent and birnormal direction, ignores normal map.\n");
		}

		if (ImGui::SliderFloat("Transparency Increase", &settings.AlphaReduction, 0.f, 1.f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Transluent material will make the material more opaque on average, which could be different from the intent, reduce the alpha to counter this effect and increase the dynamic range of the output.");
		}

		if (ImGui::SliderFloat("Softness", &settings.AlphaSoftness, 0.0f, 1.0f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Control the softness of the alpha increase, increase the softness reduce the increased amount of alpha.");
		}

		if (ImGui::SliderFloat("Blend Weight", &settings.AlphaStrength, 0.0f, 1.0f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Control the blend weight of the effect applied to the final result.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

void ExtendedTranslucency::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ExtendedTranslucency::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ExtendedTranslucency::RestoreDefaultSettings()
{
	settings = {};
}

std::pair<std::string, std::vector<std::string>> ExtendedTranslucency::GetFeatureSummary()
{
	return {
		"Extended Translucency provides realistic rendering of thin fabric and other translucent materials.\n"
		"This feature supports multiple material models for different types of translucent surfaces.",
		{ "Multiple translucency material models (rim light, isotropic/anisotropic fabric)",
			"Realistic fabric translucency with directional light transmission",
			"Per-material override support via NIF extra data",
			"Configurable transparency and softness controls",
			"Performance-optimized translucency calculations" }
	};
}
