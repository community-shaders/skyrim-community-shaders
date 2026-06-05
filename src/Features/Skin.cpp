#include "Skin.h"
#include <DirectXTex.h>

#include "Deferred.h"
#include "Globals.h"
#include "Hooks.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"

#include "DynamicWetness_PublicAPI.h"
#include "I18n/I18n.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Skin::Settings,
	EnableSkin,
	SkinMainRoughness,
	SkinSecondRoughness,
	SkinSpecularTexMultiplier,
	SecondarySpecularStrength,
	F0,
	BaseColorMultiplier,
	PhysicalMainRoughnessMultiplier,
	PhysicalSecondRoughnessMultiplier,
	PhysicalSpecularStrength,
	ExtraEdgeRoughness,
	EnableSkinDetail,
	SkinDetailStrength,
	SkinDetailTiling,
	BodyTilingMultiplier,
	ExtraSkinWetness,
	WetFadeTime,
	StartSweat,
	FullSweat,
	WetParams,
	Translucency,
	sssWidth,
	UseSSS,
	FuzzStrength,
	FuzzRoughness,
	FuzzF0,
	UseDynamicWetness);

void Skin::DrawSettings()
{
	ImGui::Checkbox(T("feature.skin.enable_advanced_skin", "Enable Advanced Skin"), &settings.EnableSkin);

	ImGui::Text("%s", T("feature.skin.advanced_skin_shader_using_dual_specular_lobes", "Advanced Skin Shader using dual specular lobes."));

	ImGui::Spacing();
	ImGui::SliderFloat(T("feature.skin.primary_roughness", "Primary Roughness"), &settings.SkinMainRoughness, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.controls_microscopic_roughness_of_stratum_corneum_layer", "Controls microscopic roughness of stratum corneum layer"));
	}

	ImGui::SliderFloat(T("feature.skin.secondary_roughness", "Secondary Roughness"), &settings.SkinSecondRoughness, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.smoothness_of_epidermal_cell_layer_reflections", "Smoothness of epidermal cell layer reflections"));
		ImGui::BulletText(T("feature.skin.should_be_30_50_lower_than_primary", "Should be 30-50%% lower than Primary"));
	}

	ImGui::SliderFloat(T("feature.skin.specular_texture_multiplier", "Specular Texture Multiplier"), &settings.SkinSpecularTexMultiplier, 0.0f, 10.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.multiplier_for_specular_map", "Multiplier for specular map"));
		ImGui::BulletText("%s", T("feature.skin.a_multiplier_for_the_vanilla_specular_map_applied", "A multiplier for the vanilla specular map, applied to the first layer's roughness"));
	}

	ImGui::SliderFloat(T("feature.skin.secondary_specular_strength", "Secondary Specular Strength"), &settings.SecondarySpecularStrength, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.intensity_of_secondary_specular_highlights", "Intensity of secondary specular highlights"));
	}

	ImGui::SliderFloat(T("feature.skin.fresnel_f0", "Fresnel F0"), &settings.F0, 0.0f, 0.1f, "%.4f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.fresnel_reflectance", "Fresnel reflectance"));
	}

	ImGui::SliderFloat(T("feature.skin.base_color_multiplier", "Base Color Multiplier"), &settings.BaseColorMultiplier, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.multiplier_for_the_base_color_texture", "Multiplier for the base color texture"));
	}

	ImGui::Spacing();
	ImGui::Text("%s", T("feature.skin.options_for_additional_roughness_and_specular_maps", "Options for additional roughness and specular maps."));

	ImGui::SliderFloat(T("feature.skin.physical_main_roughness_multiplier", "Physical Main Roughness Multiplier"), &settings.PhysicalMainRoughnessMultiplier, 0.0f, 2.0f, "%.2f");
	ImGui::SliderFloat(T("feature.skin.physical_second_roughness_multiplier", "Physical Second Roughness Multiplier"), &settings.PhysicalSecondRoughnessMultiplier, 0.0f, 2.0f, "%.2f");
	ImGui::SliderFloat(T("feature.skin.physical_specular_multiplier", "Physical Specular Multiplier"), &settings.PhysicalSpecularStrength, 0.0f, 2.0f, "%.2f");

	ImGui::Spacing();

	ImGui::SliderFloat(T("feature.skin.extra_edge_roughness", "Extra Edge Roughness"), &settings.ExtraEdgeRoughness, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.extra_roughness_at_the_edges_of_the_skin", "Extra roughness at the edges of the skin, to approximate peach fuzz on the face."));
	}

	ImGui::SliderFloat(T("feature.skin.fuzz_strength", "Fuzz Strength"), &settings.FuzzStrength, 0.0f, 2.0f, "%.2f");

	ImGui::SliderFloat(T("feature.skin.fuzz_roughness", "Fuzz Roughness"), &settings.FuzzRoughness, 0.1f, 1.0f, "%.2f");

	ImGui::SliderFloat(T("feature.skin.fuzz_f0", "Fuzz F0"), &settings.FuzzF0, 0.0f, 0.5f, "%.4f");

	ImGui::Spacing();

	ImGui::Checkbox(T("feature.skin.enable_sss_transmission", "Enable SSS Transmission"), &settings.UseSSS);

	ImGui::SliderFloat(T("feature.skin.translucency", "Translucency"), &settings.Translucency, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.translucency_of_the_sss_transmittance_effect", "Translucency of the SSS Transmittance effect"));
	}

	ImGui::SliderFloat(T("feature.skin.sss_width", "SSS Width"), &settings.sssWidth, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.width_of_the_sss_transmittance_effect", "Width of the SSS Transmittance effect"));
	}

	ImGui::Spacing();

	ImGui::SliderFloat(T("feature.skin.extra_skin_wetness", "Extra Skin Wetness"), &settings.ExtraSkinWetness, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.adds_a_constant_layer_of_wetness_to_all", "Adds a constant layer of wetness to all skin, making it look slightly damp or sweaty at all times, even when not in water or exerting effort."));
	}

	ImGui::SliderFloat(T("feature.skin.wetness_fade_out_time", "Wetness Fade Out Time"), &settings.WetFadeTime, 0.0f, 50.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.how_many_seconds_it_takes_for_skin_to", "How many seconds it takes for skin to fully dry after leaving water. Higher values mean wetness lingers longer."));
	}

	if (isDynamicWetnessAvailable) {
		ImGui::Text("%s", T("feature.skin.dynamic_wetness_detected", "Dynamic Wetness detected."));
		ImGui::Checkbox(T("feature.skin.use_dynamic_wetness", "Use Dynamic Wetness"), &settings.UseDynamicWetness);
	} else {
		settings.UseDynamicWetness = false;
	}

	if (!settings.UseDynamicWetness) {
		ImGui::SliderFloat(T("feature.skin.stamina_threshold_for_sweat", "Stamina Threshold for Sweat"), &settings.StartSweat, 0.0f, 1.0f, "%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T("feature.skin.the_character_starts_sweating_when_their_stamina_drops", "The character starts sweating when their stamina drops below this percentage. For example, 0.75 means sweat appears below 75%% stamina."));
		}
		ImGui::SliderFloat(T("feature.skin.full_sweat_threshold", "Full Sweat Threshold"), &settings.FullSweat, 0.0f, 1.0f, "%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T("feature.skin.the_character_reaches_maximum_sweat_when_stamina_drops", "The character reaches maximum sweat when stamina drops below this percentage. For example, 0.15 means full sweat below 15%% stamina."));
		}
	}

	ImGui::SliderFloat(T("feature.skin.wetness_perlin_noise_scale", "Wetness Perlin Noise Scale"), &settings.WetParams.x, 0.0f, 1024.0f, "%1.f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.controls_the_size_of_the_wet_dry_pattern", "Controls the size of the wet/dry pattern on skin. Higher values create a finer, more detailed pattern; lower values produce larger, broader wet patches."));
	}
	ImGui::SliderFloat(T("feature.skin.wetness_perlin_noise_lacunarity", "Wetness Perlin Noise Lacunarity"), &settings.WetParams.y, 0.0f, 2.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.controls_how_much_fine_detail_is_added_to", "Controls how much fine detail is added to the wetness pattern. Higher values add more small-scale variation on top of the base pattern."));
	}
	ImGui::SliderFloat(T("feature.skin.wetness_perlin_noise_persistence", "Wetness Perlin Noise Persistence"), &settings.WetParams.z, 0.0f, 20.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.controls_the_overall_contrast_and_roughness_of_the", "Controls the overall contrast and roughness of the wetness pattern. Higher values make the pattern more pronounced and varied."));
	}
	ImGui::SliderFloat(T("feature.skin.wetness_normal_scale", "Wetness Normal Scale"), &settings.WetParams.w, 0.0f, 20.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.controls_how_bumpy_wet_skin_appears_higher_values", "Controls how bumpy wet skin appears. Higher values create more visible surface ripples and distortion on wet areas."));
	}

	ImGui::Spacing();

	ImGui::Checkbox(T("feature.skin.enable_skin_detail", "Enable Skin Detail"), &settings.EnableSkinDetail);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.enable_skin_detail_texture", "Enable skin detail texture"));
	}

	ImGui::SliderFloat(T("feature.skin.skin_detail_strength", "Skin Detail Strength"), &settings.SkinDetailStrength, -2.0f, 2.0f);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.strength_of_skin_detail_texture", "Strength of skin detail texture"));
	}

	ImGui::SliderFloat(T("feature.skin.skin_detail_tiling", "Skin Detail Tiling"), &settings.SkinDetailTiling, 1.0f, 50.0f, "%1.f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.the_more_tiling_the_more_detailed_the_skin", "The more tiling, the more detailed the skin will be"));
	}

	ImGui::SliderFloat(T("feature.skin.body_tiling_multiplier", "Body Tiling Multiplier"), &settings.BodyTilingMultiplier, 0.5f, 5.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("feature.skin.multiply_the_tiling_for_the_body_to_match", "Multiply the tiling for the body to match the face"));
	}

	if (ImGui::Button(T("feature.skin.reload_skin_detail_texture", "Reload Skin Detail Texture"))) {
		ReloadSkinDetail();
	}

	BUFFER_VIEWER_NODE(texSkinDetail, 1.0f)
}

void Skin::LoadSkinDetailTexture()
{
	auto device = globals::d3d::device;

	DirectX::ScratchImage image;
	try {
		std::filesystem::path path{ "Data\\Shaders\\Skin\\skin_detail_n.dds" };
		DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
	} catch (const DX::com_exception& e) {
		logger::error("{}", e.what());
		return;
	}

	ID3D11Resource* pResource = nullptr;
	try {
		DX::ThrowIfFailed(CreateTexture(device,
			image.GetImages(), image.GetImageCount(),
			image.GetMetadata(), &pResource));
	} catch (const DX::com_exception& e) {
		logger::error("{}", e.what());
		return;
	}

	texSkinDetail = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texSkinDetail->desc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = static_cast<UINT>(image.GetMetadata().mipLevels) }
	};
	texSkinDetail->CreateSRV(srvDesc);
}

void Skin::SetupResources()
{
	logger::debug("Loading skin detail texture...");
	LoadSkinDetailTexture();

	PerGeometryCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<PerGeometryData>());

	// Check for Dynamic Wetness availability
	isDynamicWetnessAvailable = SWE::API::Init();
}

void Skin::ReloadSkinDetail()
{
	logger::debug("Reloading skin detail texture...");
	LoadSkinDetailTexture();
}

void Skin::Prepass()
{
	auto context = globals::d3d::context;

	if (texSkinDetail) {
		ID3D11ShaderResourceView* srv = texSkinDetail->srv.get();
		context->PSSetShaderResources(72, 1, &srv);
	}
}

struct SKIN_BSLightingShader_SetupMaterial
{
	static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		func(shader, material);

		auto& skin = globals::features::skin;
		if (skin.loaded) {
			skin.BSLightingShader_SetupMaterial(material);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Skin::PostPostLoad()
{
	logger::info("[Advanced Skin] Hooking BSLightingShader::SetupMaterial");
	stl::write_vfunc<0x4, SKIN_BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);
	Hooks::Install();
}

Skin::SkinData Skin::GetCommonBufferData()
{
	SkinData data{};
	data.skinParams = float4(settings.SkinMainRoughness, settings.SkinSecondRoughness, settings.SkinSpecularTexMultiplier, float(settings.EnableSkin));
	data.skinParams2 = float4(settings.SecondarySpecularStrength, settings.ExtraSkinWetness, settings.F0, settings.BaseColorMultiplier);
	data.skinDetailParams = float4(settings.SkinDetailTiling, settings.BodyTilingMultiplier, settings.SkinDetailStrength, float(settings.EnableSkinDetail && settings.EnableSkin));
	data.sssParams = float4(settings.Translucency, settings.sssWidth, 0.0f, float(settings.UseSSS));
	data.fuzzParams = float4(settings.FuzzStrength, settings.FuzzRoughness, settings.FuzzF0, settings.ExtraEdgeRoughness);
	data.physicalParams = float4(settings.PhysicalMainRoughnessMultiplier, settings.PhysicalSecondRoughnessMultiplier, settings.PhysicalSpecularStrength, 0.0f);
	data.wetParams = settings.WetParams;
	return data;
}

void Skin::LoadSettings(json& o_json)
{
	settings = o_json;
}

void Skin::SaveSettings(json& o_json)
{
	o_json = settings;
}

void Skin::RestoreDefaultSettings()
{
	settings = {};
}

// By PO3
// https://github.com/powerof3/Splashes-of-Skyrim/blob/master/src/Manager.cpp
float Skin::GetWaterHeight(const RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos)
{
	float waterHeight = -RE::NI_INFINITY;

	if (const auto waterManager = RE::TESWaterSystem::GetSingleton()) {
		waterHeight = a_ref->GetWaterHeight();

		if (waterHeight != -RE::NI_INFINITY) {
			return waterHeight;
		}

		const auto get_nearest_water_object_height = [&]() {
			for (const auto& waterObject : waterManager->waterObjects) {
				if (waterObject) {
					for (const auto& bound : waterObject->multiBounds) {
						if (bound) {
							if (auto size{ bound->size }; size.z <= 10.0f) {  //avoid sloped water
								auto center{ bound->center };
								const auto boundMin = center - size;
								const auto boundMax = center + size;
								if (!(a_pos.x < boundMin.x || a_pos.x > boundMax.x || a_pos.y < boundMin.y || a_pos.y > boundMax.y)) {
									return center.z;
								}
							}
						}
					}
				}
			}

			return -RE::NI_INFINITY;
		};

		waterHeight = get_nearest_water_object_height();
	}

	return waterHeight;
}

float4 Skin::GetWetness(RE::BSGeometry* geometry)
{
	float4 wetness = float4(0.0f, 0.0f, 0.0f, 0.0f);
	if (auto userData = geometry->GetUserData())
		if (auto actor = userData->As<RE::Character>()) {
			const float positionZ = actor->GetPositionZ();
			wetness.z = positionZ;
			if (settings.UseDynamicWetness && isDynamicWetnessAvailable) {
				float dynamicWetness = SWE::API::GetFinalWetness(actor);
				wetness.x = dynamicWetness;
			} else {
				const float stamina = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
				const float permanentStamina = actor->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kStamina);
				const float temporaryStamina = actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kStamina);
				const float maxStamina = std::max(permanentStamina + temporaryStamina, 1.0f);
				const float staminaPercentage = actor->IsDead() ? 1.0f : (stamina / maxStamina);
				const float sweatRange = settings.StartSweat - settings.FullSweat;
				wetness.x = (std::abs(sweatRange) < 1e-5f)             ? 0.0f :
				            (staminaPercentage >= settings.StartSweat) ? 0.0f :
				            (staminaPercentage <= settings.FullSweat)  ? 1.0f :
				                                                         (settings.StartSweat - staminaPercentage) / sweatRange;
			}
			if (actor->IsInWater()) {
				wetness.y = 2.0f;
				float waterHeight = -RE::NI_INFINITY;
				const uint32_t formID = actor->AsReference()->formID;
				const uint currentFrame = globals::state->frameCount;
				auto cacheIt = waterHeightCache.find(formID);
				if (cacheIt != waterHeightCache.end() && cacheIt->second.frameCount == currentFrame) {
					waterHeight = cacheIt->second.waterHeight;
				} else {
					waterHeight = GetWaterHeight(actor->AsReference(), actor->GetPosition());
					waterHeightCache[formID] = { currentFrame, waterHeight };
				}
				wetness.w = std::max(0.0f, waterHeight - positionZ);
			} else {
				wetness.y = 0.0f;
				wetness.w = 0.0f;
			}

			const uint32_t actorFormID = actor->AsReference()->formID;

			// Prevent unbounded growth: clear stale entries periodically
			if (actorWetnessMap.size() > 1024) {
				actorWetnessMap.clear();
			}

			auto it = actorWetnessMap.find(actorFormID);
			if (it != actorWetnessMap.end()) {
				auto& cached = it->second;

				const float fadeTime = std::max(settings.WetFadeTime, 0.001f);
				if (cached.x < wetness.x) {
					cached.x = wetness.x;
				} else if (cached.x > wetness.x) {
					cached.x -= *globals::game::deltaTime / fadeTime;
					cached.x = std::max(cached.x, 0.0f);
					wetness.x = cached.x;
				}

				if (cached.y < wetness.y) {
					cached.y = wetness.y;
					if (cached.w < wetness.w) {
						cached.w = wetness.w;
					} else {
						wetness.w = cached.w;
					}
				} else if (cached.y > wetness.y) {
					cached.y -= *globals::game::deltaTime / fadeTime;
					cached.y = std::max(cached.y, 0.0f);
					wetness.y = cached.y;
					if (wetness.y == 0.0f) {
						wetness.w = 0.0f;
						cached.w = 0.0f;
					} else if (cached.w < wetness.w) {
						cached.w = wetness.w;
					} else {
						wetness.w = cached.w;
					}
				} else if (cached.w < wetness.w) {
					cached.w = wetness.w;
				} else {
					wetness.w = cached.w;
				}
			} else {
				actorWetnessMap.emplace(actorFormID, wetness);
			}
		}
	return wetness;
}

struct SkinExtendedRendererState
{
	uint32_t PSResourceModifiedBits = 0;
	std::array<ID3D11ShaderResourceView*, 2> PSTexture;

	void SetExtraSkinPSTexture(RE::BSGraphics::Texture* newTexture, RE::BSGraphics::Texture* newTexture2)
	{
		{
			PSTexture = {
				newTexture ? newTexture->resourceView : nullptr,
				newTexture2 ? newTexture2->resourceView : nullptr
			};
			PSResourceModifiedBits = 1;
		}
	}

	SkinExtendedRendererState()
	{
		PSTexture.fill(nullptr);
	}
} skinExtendedRendererState;

void Skin::SetupExtraTexture(RE::BSLightingShaderMaterialBase const* material, RE::BSTextureSet* inTextureSet, uint32_t i_hashKey)
{
	if (!inTextureSet || material->normalTexture == nullptr) {
		logger::error("[Advanced Skin] SetupExtraTexture : Texture set is null for material: {}", i_hashKey);
		return;
	}

	uint32_t hashKey = 0;
	hashKey = material->hashKey;
	if (hashKey == 0 || hashKey != i_hashKey) {
		logger::error("[Advanced Skin] SetupExtraTexture : Invalid hash key for material: {}", i_hashKey);
		return;
	}

	const char extraTextureName[] = "_rfaos.dds";
	const char wetnessTextureName[] = "_wet.dds";
	const char* workingNormalPath = nullptr;
	const char* workingSpecularPath = nullptr;
	auto workingMaterial = static_cast<const RE::BSLightingShaderMaterialBase*>(material);
	auto hasSpecular = workingMaterial->specularBackLightingTexture != nullptr;

	auto graphicsState = globals::game::graphicsState;
	const auto& stateData = graphicsState->GetRuntimeData();

	if (hasSpecular) {
		if (auto specularPath = inTextureSet->GetTexturePath(RE::BSTextureSet::Texture::kSpecular)) {
			workingSpecularPath = specularPath;
		}
	}
	if (auto normalPath = inTextureSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal)) {
		workingNormalPath = normalPath;
	} else {
		logger::error("[Advanced Skin] SetupExtraTexture : No specular or normal texture found in texture set from material: {}", hashKey);
		auto& workingExtraPtr = skinExtraTextures.try_emplace(hashKey).first->second;
		workingExtraPtr.rfaosTexture = stateData.defaultTextureBlack;
		workingExtraPtr.wetnessTexture = stateData.defaultTextureBlack;
		workingExtraPtr.extraTexturePath = "";
		workingExtraPtr.wetnessTexturePath = "";
		workingExtraPtr.hasExtraTexture = false;
		workingExtraPtr.hasWetnessTexture = false;
		return;
	}

	const char* foundPath = nullptr;
	std::string extraTexturePath = "";
	std::string wetnessTexturePath = "";

	auto findIgnoreCase = [](std::string_view str, std::string_view pattern) -> size_t {
		auto it = std::search(str.begin(), str.end(), pattern.begin(), pattern.end(),
			[](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); });
		return it == str.end() ? std::string_view::npos : std::distance(str.begin(), it);
	};

	auto tryReplaceSuffix = [&](const char* basePath, std::string_view suffix) -> bool {
		auto pos = findIgnoreCase(basePath, suffix);
		if (pos == std::string_view::npos)
			return false;
		extraTexturePath = std::string(basePath);
		wetnessTexturePath = std::string(basePath);
		extraTexturePath.replace(pos, suffix.size(), extraTextureName);
		wetnessTexturePath.replace(pos, suffix.size(), wetnessTextureName);
		foundPath = basePath;
		return true;
	};

	if (hasSpecular && workingSpecularPath) {
		tryReplaceSuffix(workingSpecularPath, "_s.dds");
	}

	if (!foundPath && workingNormalPath) {
		if (!tryReplaceSuffix(workingNormalPath, "_n.dds")) {
			if (!tryReplaceSuffix(workingNormalPath, "_msn.dds")) {
				tryReplaceSuffix(workingNormalPath, ".dds");
			}
		}
	}

	logger::debug("[Advanced Skin] SetupExtraTexture : Extra texture path: {} for {}", extraTexturePath, foundPath ? foundPath : "(none)");
	logger::debug("[Advanced Skin] SetupExtraTexture : Wetness texture path: {} for {}", wetnessTexturePath, foundPath ? foundPath : "(none)");

	auto& workingExtraPtr = skinExtraTextures.try_emplace(hashKey).first->second;
	workingExtraPtr.rfaosTexture = stateData.defaultTextureWhite;
	workingExtraPtr.wetnessTexture = stateData.defaultTextureWhite;
	workingExtraPtr.extraTexturePath = extraTexturePath;
	workingExtraPtr.wetnessTexturePath = wetnessTexturePath;

	inTextureSet->SetTexturePath(RE::BSTextureSet::Texture::kEnvironment, workingExtraPtr.extraTexturePath.c_str());
	inTextureSet->SetTexturePath(RE::BSTextureSet::Texture::kMultilayer, workingExtraPtr.wetnessTexturePath.c_str());
	inTextureSet->SetTexture(RE::BSTextureSet::Texture::kEnvironment, workingExtraPtr.rfaosTexture);
	inTextureSet->SetTexture(RE::BSTextureSet::Texture::kMultilayer, workingExtraPtr.wetnessTexture);

	workingExtraPtr.hasExtraTexture = workingExtraPtr.rfaosTexture != nullptr && !workingExtraPtr.extraTexturePath.empty() && workingExtraPtr.rfaosTexture != stateData.defaultTextureBlack;
	workingExtraPtr.hasWetnessTexture = workingExtraPtr.wetnessTexture != nullptr && !workingExtraPtr.wetnessTexturePath.empty() && workingExtraPtr.wetnessTexture != stateData.defaultTextureBlack;

	if (workingExtraPtr.hasExtraTexture || workingExtraPtr.hasWetnessTexture) {
		logger::debug("[Advanced Skin] SetupExtraTexture : Extra texture set with hash key: {}", hashKey);
	} else {
		logger::debug("[Advanced Skin] SetupExtraTexture : Failed to set extra texture for material: {}", hashKey);
	}
}

void Skin::BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material)
{
	auto materialFeature = material->GetFeature();
	if (materialFeature != RE::BSShaderMaterial::Feature::kFaceGen &&
		materialFeature != RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
		return;
	}

	auto materialTextureSet = material->textureSet.get();

	uint32_t hashKey = 0;
	hashKey = material->hashKey;
	if (hashKey == 0) {
		logger::error("[Advanced Skin] BSLightingShader_SetupMaterial : Invalid hash key for material: {}", static_cast<int>(materialFeature));
		return;
	}

	if (!skinExtraTextures.contains(hashKey)) {
		// logger::debug("[Advanced Skin] BSLightingShader_SetupMaterial : Setting up extra texture for material: {}", static_cast<int>(materialFeature));
		globals::features::skin.SetupExtraTexture(material, materialTextureSet, hashKey);
	}

	auto graphicsState = globals::game::graphicsState;
	const auto& workingExtraPtr = skinExtraTextures[hashKey];

	if (workingExtraPtr.hasExtraTexture || workingExtraPtr.hasWetnessTexture) {
		skinExtendedRendererState.SetExtraSkinPSTexture(workingExtraPtr.rfaosTexture->rendererTexture, workingExtraPtr.wetnessTexture->rendererTexture);
	} else {
		skinExtendedRendererState.SetExtraSkinPSTexture(graphicsState->GetRuntimeData().defaultTextureBlack->rendererTexture, graphicsState->GetRuntimeData().defaultTextureBlack->rendererTexture);
	}
}

void Skin::BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass)
{
	auto context = globals::d3d::context;

	if (settings.EnableSkin) {
		auto geometry = a_pass->geometry;
		float4 wetness = GetWetness(geometry);

		if (currentWetness != wetness) {
			currentWetness = wetness;
			PerGeometryData perGeometryData{};
			perGeometryData.skinPerGeometry = wetness;
			PerGeometryCB->Update(perGeometryData);
		}

		ID3D11Buffer* buffer = { PerGeometryCB->CB() };
		context->PSSetConstantBuffers(7, 1, &buffer);
	}
}

void Skin::SetShaderResources(ID3D11DeviceContext* a_context)
{
	if (skinExtendedRendererState.PSResourceModifiedBits != 0) {
		a_context->PSSetShaderResources(71, 1, &skinExtendedRendererState.PSTexture.at(0));
		a_context->PSSetShaderResources(74, 1, &skinExtendedRendererState.PSTexture.at(1));
	}
	skinExtendedRendererState.PSResourceModifiedBits = 0;
}

void Skin::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	auto& skin = globals::features::skin;
	skin.BSLightingShader_SetupGeometry(Pass);
	return func(This, Pass, RenderFlags);
}
