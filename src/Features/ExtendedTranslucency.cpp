#include "ExtendedTranslucency.h"

#include "../I18n/I18n.h"
#include "../ShaderCache.h"
#include "../State.h"
#include "../Util.h"

#define I18N_KEY_PREFIX "feature.extended_translucency."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExtendedTranslucency::Settings,
	AlphaMode,
	AlphaReduction,
	AlphaSoftness,
	AlphaStrength,
	SkinnedOnly);

const RE::BSFixedString ExtendedTranslucency::NiExtraDataName_AnisotropicAlphaMaterial = "AnisotropicAlphaMaterial";

void ExtendedTranslucency::BSLightingShader_SetupGeometry(RE::BSRenderPass* pass)
{
	auto SetFeatureDescriptor = [](int material) {
		auto& descriptor = globals::state->permutationData.ExtraFeatureDescriptor;
		static constexpr int mask = ExtraFeatureDescriptorMask << ExtraFeatureDescriptorShift;
		static constexpr int shift = ExtraFeatureDescriptorShift;
		descriptor = (descriptor & ~mask) | (material << shift);
	};

	// Clear the ExtraFeatureDescriptor to disable this effect on default
	SetFeatureDescriptor(MaterialModel::DescriptorDisabled);

	auto& property0 = pass->geometry->GetGeometryRuntimeData().alphaProperty;
	auto& property1 = pass->geometry->GetGeometryRuntimeData().shaderProperty;
	auto alphaProperty = property0 && property0->GetRTTI() == globals::rtti::NiAlphaPropertyRTTI.get() ? static_cast<RE::NiAlphaProperty*>(property0.get()) : nullptr;
	auto lightProperty = property1 && property1->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(property1.get()) : nullptr;

	// This effect only matters when alpha property exists and blending is enabled
	// Geometries with alpha < 1 have an implicit alpha blend property
	if (!(lightProperty && lightProperty->alpha < 0.999f) && (!alphaProperty || !alphaProperty->GetAlphaBlending())) {
		return;
	}

	const auto* data = pass->geometry->GetExtraData(NiExtraDataName_AnisotropicAlphaMaterial);
	if (!data) {
		// If there is no extra data for explicit settings, use the default material model from global user settings
		// And respect the SkinnedOnly setting
		const auto& feature = globals::features::extendedTranslucency;
		if (!feature.settings.SkinnedOnly || pass->geometry->GetGeometryRuntimeData().skinInstance != nullptr) {
			SetFeatureDescriptor(MaterialModel::DescriptorUseDefault);
		}
	} else {
		// Read explicit material model from extra data
		if (data->GetRTTI() == globals::rtti::NiIntegerExtraDataRTTI.get()) {
			uint32_t material = static_cast<uint32_t>(static_cast<const RE::NiIntegerExtraData*>(data)->value) & ExtraFeatureDescriptorMask;
			// Promote `Disabled` in settings to `DescriptorDisabled` in shader
			material = material == MaterialModel::Disabled ? MaterialModel::DescriptorDisabled : material;
			SetFeatureDescriptor(material);
		} else {
			// logging is too expensive here, treat type error as disable, should only happen for modders
		}
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
	if (ImGui::TreeNodeEx(T(TKEY("translucent_material"), "Translucent Material"), ImGuiTreeNodeFlags_DefaultOpen)) {
		const char* AlphaModeNames[] = {
			T(TKEY("alpha_mode_disabled"), "0 - Disabled"),
			T(TKEY("alpha_mode_rim_edge"), "1 - Rim Edge"),
			T(TKEY("alpha_mode_isotropic_fabric"), "2 - Isotropic Fabric, Glass, ..."),
			T(TKEY("alpha_mode_anisotropic_fabric"), "3 - Anisotropic Fabric"),
		};

		static constexpr int AlphaModeSize = static_cast<int>(std::size(AlphaModeNames));

		bool changed = false;
		if (ImGui::Combo(T(TKEY("default_material_model"), "Default Material Model"), (int*)&settings.AlphaMode, AlphaModeNames, AlphaModeSize)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("default_material_model_tooltip"),
								  "Anisotropic translucency will adjust the opacity based on your view angle to the translucent surface.\n"
								  "  - Disabled: No anisotropic translucency, flat alpha.\n"
								  "  - Rim Edge: Naive rim light effect with no physics model, the edge of the geometry is always opaque even its full transparent.\n"
								  "  - Isotropic Fabric: Imaginary fabric weaved from threads in one direction, respect normal map, also works well for layer of glass panels.\n"
								  "  - Anisotropic Fabric: Common fabric weaved from tangent and birnormal direction, ignores normal map.\n"));
		}
		if (ImGui::Checkbox(T(TKEY("skinned_mesh_only"), "Skinned Mesh Only"), &settings.SkinnedOnly)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("skinned_mesh_only_tooltip"), "Control if this effect should only apply to skinned mesh. Check this option if you are seeing undesired effects on random objects."));
		}

		if (ImGui::SliderFloat(T(TKEY("transparency_increase"), "Transparency Increase"), &settings.AlphaReduction, 0, 1.f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("transparency_increase_tooltip"), "Translucent material will make the material more opaque on average, which could be different from the intent. Reduce the alpha to counter this effect and increase the dynamic range of the output."));
		}

		if (ImGui::SliderFloat(T(TKEY("softness"), "Softness"), &settings.AlphaSoftness, 0.0f, 1.0f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("softness_tooltip"), "Control the softness of the alpha increase, increase the softness reduce the increased amount of alpha."));
		}

		if (ImGui::SliderFloat(T(TKEY("blend_weight"), "Blend Weight"), &settings.AlphaStrength, 0.0f, 1.0f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("blend_weight_tooltip"), "Control the blend weight of the effect applied to the final result."));
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

#undef I18N_KEY_PREFIX
