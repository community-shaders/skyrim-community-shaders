#include "LinearLighting.h"

#include "../I18n/I18n.h"
#include "State.h"
#include "Util.h"

#include "Effects11.h"
#include "Globals.h"
#include "InverseSquareLighting/Common.h"
#include "PostProcessing.h"
#include "ShaderCache.h"
#include "Utils/ColorSpace.h"
#include "Utils/Game.h"

#define I18N_KEY_PREFIX "feature.linear_lighting."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LinearLighting::Settings,
	enableLinearLighting,
	enableACEScg,
	vanillaDiffuseColorMult,
	directionalLightMult,
	pointLightMult,
	ambientMult,
	emitColorMult,
	glowmapMult,
	effectLightingMult,
	membraneEffectMult,
	bloodEffectMult,
	projectedEffectMult,
	deferredEffectMult,
	otherEffectMult)

void LinearLighting::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enable_linear_lighting"), "Enable Linear Lighting"), (bool*)&settings.enableLinearLighting);
	ImGui::Checkbox(T(TKEY("enable_acescg"), "Enable ACEScg Wide Gamut"), (bool*)&settings.enableACEScg);
	ImGui::TextDisabled("%s", T(TKEY("startup_settings"), "Linear Lighting and working color space settings require a restart."));
	if (globals::features::effects11.IsActive())
		ImGui::TextDisabled("Effects 11 overrides Linear Lighting while UseEffect is enabled.");

	if (ImGui::BeginTabBar("##LinearLightingTabs", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem(T(TKEY("tab_general"), "General"))) {
			ImGui::SeparatorText(T(TKEY("multipliers"), "Multipliers"));
			ImGui::SliderFloat(T(TKEY("directional_light_multiplier"), "Directional Light Multiplier"), &settings.directionalLightMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("ambient_multiplier"), "Ambient Multiplier"), &settings.ambientMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("glowmap_multiplier"), "Glowmap Multiplier"), &settings.glowmapMult, 0.0f, 10.0f, "%.2f");

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("tab_advanced"), "Advanced"))) {
			ImGui::SeparatorText(T(TKEY("multipliers"), "Multipliers"));
			ImGui::SliderFloat(T(TKEY("vanilla_diffuse_color_multiplier"), "Vanilla Diffuse Color Multiplier"), &settings.vanillaDiffuseColorMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("emissive_color_multiplier"), "Emissive Color Multiplier"), &settings.emitColorMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("point_light_multiplier"), "Point Light Multiplier"), &settings.pointLightMult, 0.0f, 10.0f, "%.2f");

			if (ImGui::TreeNodeEx(T(TKEY("effects"), "Effects"), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat(T(TKEY("effect_lighting_multiplier"), "Effect Lighting Multiplier"), &settings.effectLightingMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("membrane_effects_multiplier"), "Membrane Effects Multiplier"), &settings.membraneEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("blood_effects_multiplier"), "Blood Effects Multiplier"), &settings.bloodEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("projected_effects_multiplier"), "Projected Effects Multiplier"), &settings.projectedEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("deferred_effects_multiplier"), "Deferred Effects Multiplier"), &settings.deferredEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("other_effects_multiplier"), "Other Effects Multiplier"), &settings.otherEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::TreePop();
			}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void LinearLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	if (o_json.contains("mode") && !o_json.contains("enableLinearLighting"))
		settings.enableLinearLighting = o_json.value("mode", 0u) == 1u;
}

void LinearLighting::SaveSettings(json& o_json)
{
	o_json = settings;
}

bool LinearLighting::IsLinearLightingActive() const
{
	return configuredLinearLighting && !globals::features::effects11.IsActive();
}

void LinearLighting::PostSetupResources()
{
	if (configuredLinearLighting && !globals::features::effects11.IsPresetEnabled() && !globals::features::postProcessing.loaded)
		stl::report_and_fail("Linear Lighting requires Post Processing for its display transform."sv);
}

void LinearLighting::ClearShaderCache()
{
	if (!globals::d3d::context || !globals::game::renderer)
		return;
	if (IsLinearLightingActive() && !globals::features::postProcessing.loaded)
		stl::report_and_fail("Linear Lighting requires Post Processing for its display transform."sv);
	globals::state->RequestHistoryReset();
	workingSunlight = nullptr;
	const float clear[4]{};
	auto& reflections = globals::game::renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];
	for (auto* rtv : reflections.cubeSideRTV)
		if (rtv)
			globals::d3d::context->ClearRenderTargetView(rtv, clear);
}

std::vector<std::pair<std::string_view, std::string_view>> LinearLighting::GetCommonShaderDefines()
{
	auto defines = GetShaderDefineOptions();
	if (IsLinearLightingActive())
		defines.emplace_back("ENABLE_LL", "");
	return defines;
}

std::vector<std::pair<std::string_view, std::string_view>> LinearLighting::GetShaderDefineOptions()
{
	std::vector<std::pair<std::string_view, std::string_view>> options;
	if (IsACEScgActive())
		options.emplace_back("ENABLE_ACESCG", "");
	return options;
}

void LinearLighting::RestoreDefaultSettings()
{
	settings = {};
}

LinearLighting::PerFrameData LinearLighting::GetCommonBufferData()
{
	auto data = PerFrameData{};
	data.vanillaDiffuseColorMult = 1.0f;
	data.directionalLightMult = 1.0f;
	data.pointLightMult = 1.0f;
	data.ambientMult = 1.0f;
	data.glowmapMult = 1.0f;
	data.effectLightingMult = 1.0f;
	data.membraneEffectMult = 1.0f;
	data.bloodEffectMult = 1.0f;
	data.projectedEffectMult = 1.0f;
	data.deferredEffectMult = 1.0f;
	data.otherEffectMult = 1.0f;

	data.isMainOrLoadingMenu = globals::state->IsMainOrLoadingMenuOpen();

	if (!loaded || !IsLinearLightingActive())
		return data;

	data.vanillaDiffuseColorMult = settings.vanillaDiffuseColorMult;
	data.directionalLightMult = RE::NI_PI * settings.directionalLightMult;
	data.pointLightMult = RE::NI_PI * settings.pointLightMult;
	data.ambientMult = settings.ambientMult;
	data.glowmapMult = settings.glowmapMult;
	data.effectLightingMult = settings.effectLightingMult;
	data.membraneEffectMult = settings.membraneEffectMult;
	data.bloodEffectMult = settings.bloodEffectMult;
	data.projectedEffectMult = settings.projectedEffectMult;
	data.deferredEffectMult = settings.deferredEffectMult;
	data.otherEffectMult = settings.otherEffectMult;
	return data;
}

void LinearLighting::ModifySharedLighting(SharedLighting& lighting)
{
	if (!IsLinearLightingActive())
		return;
	lighting.directional.color = LightColorToWorking(lighting.directionalLight);
	lighting.sun.color = SRGBToWorking(lighting.sun.color);
	for (auto* moon : { &lighting.masser, &lighting.secunda }) {
		moon->color = SRGBToWorking(moon->color);
		moon->tint = SRGBToWorking(moon->tint);
	}
}

void LinearLighting::SRGBToWorking(float* color) const
{
	if (!IsLinearLightingActive())
		return;
	Util::ColorSpace::SRGBToLinear(color);
	if (IsACEScgActive())
		Util::ColorSpace::SRGBGamutToAP1(color);
}

RE::NiColor LinearLighting::SRGBToWorking(RE::NiColor color) const
{
	SRGBToWorking(&color.red);
	return color;
}

RE::NiColor LinearLighting::LightColorToWorking(const RE::NiLight* light, bool effect) const
{
	if (!light)
		return {};
	const auto& diffuse = light->GetLightRuntimeData().diffuse;
	if (light == workingSunlight && diffuse == workingSunlightColor)
		return workingSunlightColor;

	auto color = effect ? static_cast<const RE::NiDirectionalLight*>(light)->GetDirectionalLightRuntimeData().effectColor : diffuse;
	if (!IsLinearLightingActive())
		return color;
	auto* const point = skyrim_cast<RE::NiPointLight*>(const_cast<RE::NiLight*>(light));
	const bool linear = point && ISLCommon::RuntimeLightDataExt::Get(point)->flags.any(LightLimitFix::LightFlags::Linear);
	if (!linear)
		Util::ColorSpace::SRGBToLinear(&color.red);
	if (IsACEScgActive())
		Util::ColorSpace::SRGBGamutToAP1(&color.red);
	return color;
}

void LinearLighting::SetSunlightColor(RE::NiLight* light, RE::NiColor color)
{
	if (!light)
		return;
	light->GetLightRuntimeData().diffuse = color;
	workingSunlight = light;
	workingSunlightColor = color;
}

void LinearLighting::ClearSunlightColor(const RE::NiLight* light)
{
	if (workingSunlight == light)
		workingSunlight = nullptr;
}

namespace
{
	using Group = RE::BSGraphics::ConstantGroupLevel;
	// Native SetupGeometry reuses its argument spill slots before Unmap.
	thread_local RE::BSRenderPass* geometryPass = nullptr;

	template <RE::BSShader::Type Type>
	struct SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			if constexpr (Type == RE::BSShader::Type::Lighting) {
				globals::state->permutationData.BaseTextureIsWorking = 0;
				if (pass && pass->shaderProperty && pass->shaderProperty->material) {
					const auto* material = static_cast<const RE::BSLightingShaderMaterialBase*>(pass->shaderProperty->material);
					const auto target = material->diffuseRenderTargetSourceIndex;
					globals::state->permutationData.BaseTextureIsWorking = target == RE::RENDER_TARGETS::kWATER_REFLECTIONS ||
					                                                       (!globals::state->permutationData.RenderToUI && (target == RE::RENDER_TARGETS::kMAIN || target == RE::RENDER_TARGETS::kMAIN_COPY));
				}
			}
			if constexpr (Type == RE::BSShader::Type::Effect || Type == RE::BSShader::Type::Particle) {
				constexpr auto alphaBlend = static_cast<uint>(State::ExtraShaderDescriptors::SourceAlphaBlend);
				auto& descriptor = globals::state->permutationData.ExtraShaderDescriptor;
				descriptor &= ~alphaBlend;
				if (pass && pass->geometry && pass->shaderProperty &&
					!pass->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kPremultAlpha)) {
					const auto* alpha = pass->geometry->GetGeometryRuntimeData().alphaProperty.get();
					if (alpha && alpha->GetAlphaBlending() && alpha->GetSrcBlendMode() == RE::NiAlphaProperty::AlphaFunction::kSrcAlpha) {
						if (alpha->GetDestBlendMode() == RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha ||
							alpha->GetDestBlendMode() == RE::NiAlphaProperty::AlphaFunction::kOne)
							descriptor |= alphaBlend;
					}
				}
			}
			auto* previous = std::exchange(geometryPass, pass);
			const SKSE::stl::scope_exit restore([previous]() noexcept { geometryPass = previous; });
			func(shader, pass, renderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	float* PixelConstant(Group group, uint index)
	{
		auto& ll = globals::features::linearLighting;
		if (!ll.IsLinearLightingActive() || !globals::state->customPixelShader)
			return nullptr;
		auto* shader = *globals::game::currentPixelShader;
		if (!shader)
			return nullptr;
		auto& buffer = shader->constantBuffers[static_cast<uint>(group)];
		if (!buffer.buffer || !buffer.data)
			return nullptr;
		return reinterpret_cast<float*>(buffer.data) + static_cast<uint8_t>(shader->constantTable[index]);
	}

	void WriteRGB(Group group, uint index, const RE::NiColor& color, uint element = 0)
	{
		if (auto* value = PixelConstant(group, index)) {
			value += element * 4;
			value[0] = color.red;
			value[1] = color.green;
			value[2] = color.blue;
		}
	}

	struct LightingMaterialUpload
	{
		static void thunk(ID3D11DeviceContext* context, ID3D11Resource* buffer, UINT subresource, const RE::BSLightingShaderMaterialBase* material)
		{
			auto& ll = globals::features::linearLighting;
			const auto descriptor = globals::state->currentPixelDescriptor;
			if (material && ll.IsLinearLightingActive() &&
				(descriptor & static_cast<uint>(SIE::ShaderCache::LightingShaderFlags::Specular)) &&
				!(descriptor & static_cast<uint>(SIE::ShaderCache::LightingShaderFlags::TruePbr)))
				WriteRGB(Group::PerMaterial, 25, ll.SRGBToWorking(material->specularColor) * material->specularColorScale);
			context->Unmap(buffer, subresource);
		}
	};

	struct EffectMaterialUpload
	{
		static void thunk(ID3D11DeviceContext* context, ID3D11Resource* buffer, UINT subresource, const RE::BSEffectShaderMaterial* material)
		{
			auto& ll = globals::features::linearLighting;
			if (material && ll.IsLinearLightingActive() &&
				!(globals::state->currentPixelDescriptor & (static_cast<uint>(SIE::ShaderCache::EffectShaderFlags::GrayscaleToColor) |
															   static_cast<uint>(SIE::ShaderCache::EffectShaderFlags::Blood)))) {
				const auto& c = material->baseColor;
				WriteRGB(Group::PerMaterial, 15, ll.SRGBToWorking({ c.red, c.green, c.blue }) * material->baseColorScale);
			}
			context->Unmap(buffer, subresource);
		}
	};

	struct LightingGeometryUpload
	{
		static void thunk(ID3D11DeviceContext* context, ID3D11Resource* buffer)
		{
			auto* pass = geometryPass;
			auto& ll = globals::features::linearLighting;
			if (pass && ll.IsLinearLightingActive()) {
				const auto* property = static_cast<const RE::BSLightingShaderProperty*>(pass->shaderProperty);
				if (property && property->emissiveColor)
					WriteRGB(Group::PerGeometry, 8, ll.SRGBToWorking(*property->emissiveColor) * (property->emissiveMult * ll.settings.emitColorMult));
				if (pass->sceneLights && pass->numLights && pass->sceneLights[0] && pass->sceneLights[0]->light) {
					const auto* light = pass->sceneLights[0]->light.get();
					const auto& data = light->GetLightRuntimeData();
					const float scale = RE::ImageSpaceManager::GetSingleton()->GetRuntimeData().data.baseData.hdr.sunlightScale;
					WriteRGB(Group::PerGeometry, 4, ll.LightColorToWorking(light) * (data.fade * scale));
				}
				if (pass->sceneLights && !globals::features::lightLimitFix.loaded) {
					for (uint i = 1; i < std::min<uint>(pass->numLights, 8); ++i) {
						const auto* bsLight = pass->sceneLights[i];
						if (!bsLight || !bsLight->light)
							continue;
						const auto* light = bsLight->light.get();
						const auto& data = light->GetLightRuntimeData();
						WriteRGB(Group::PerGeometry, 2, ll.LightColorToWorking(light) * (data.fade * bsLight->lodDimmer), i - 1);
					}
				}
			}
			context->Unmap(buffer, 0);
		}
	};

	struct EffectGeometryUpload
	{
		static void thunk(ID3D11DeviceContext* context, ID3D11Resource* buffer)
		{
			auto* pass = geometryPass;
			auto& ll = globals::features::linearLighting;
			if (pass && pass->shaderProperty && ll.IsLinearLightingActive()) {
				auto* property = pass->shaderProperty;
				const auto descriptor = globals::state->currentPixelDescriptor;
				const bool membrane = descriptor & static_cast<uint>(SIE::ShaderCache::EffectShaderFlags::Membrane);
				const bool grayscale = descriptor & static_cast<uint>(SIE::ShaderCache::EffectShaderFlags::GrayscaleToColor);
				if (membrane && property->effectData && !grayscale) {
					const auto& data = *property->effectData;
					WriteRGB(Group::PerGeometry, 0, ll.SRGBToWorking({ data.fillColor.red, data.fillColor.green, data.fillColor.blue }) * data.baseFillScale);
				} else if (!membrane && !grayscale && property->GetRTTI() == globals::rtti::BSEffectShaderPropertyRTTI.get()) {
					const auto* effect = static_cast<const RE::BSEffectShaderProperty*>(property);
					WriteRGB(Group::PerGeometry, 0, ll.SRGBToWorking(effect->emittanceColor ? *effect->emittanceColor : RE::NiColor{ 1.f, 1.f, 1.f }));
				}
				if (!membrane && (descriptor & (1u << 16)) && pass->numLights && pass->sceneLights && pass->sceneLights[0]) {
					if (const auto* light = skyrim_cast<RE::NiDirectionalLight*>(pass->sceneLights[0]->light.get())) {
						const float scale = RE::ImageSpaceManager::GetSingleton()->GetRuntimeData().data.baseData.hdr.sunlightScale;
						WriteRGB(Group::PerGeometry, 11, ll.LightColorToWorking(light, true) * (light->GetLightRuntimeData().fade * scale));
					}
					for (uint i = 1; i < std::min<uint>(pass->numLights, 5); ++i) {
						const auto* bsLight = pass->sceneLights[i];
						if (!bsLight || !bsLight->light)
							continue;
						const auto* light = bsLight->light.get();
						const auto& data = light->GetLightRuntimeData();
						const auto color = ll.LightColorToWorking(light) * (data.fade * bsLight->lodDimmer);
						if (auto* r = PixelConstant(Group::PerGeometry, 8))
							r[i - 1] = color.red;
						if (auto* g = PixelConstant(Group::PerGeometry, 9))
							g[i - 1] = color.green;
						if (auto* b = PixelConstant(Group::PerGeometry, 10))
							b[i - 1] = color.blue;
					}
				}
			}
			context->Unmap(buffer, 0);
		}
	};

	struct WaterMaterialUpload
	{
		static void thunk(ID3D11DeviceContext* context, ID3D11Resource* buffer, UINT subresource, const RE::BSWaterShaderMaterial* material)
		{
			auto& ll = globals::features::linearLighting;
			if (material && ll.IsLinearLightingActive()) {
				static REL::Relocation<const RE::NiColor*> waterLighting{ REL::RelocationID(527901, 414848) };
				const auto lighting = ll.SRGBToWorking(*waterLighting);
				const auto& deep = material->deepWaterColor;
				const auto& reflection = material->reflectionColor;
				WriteRGB(Group::PerMaterial, 1, ll.SRGBToWorking(material->shallowWaterColor) * lighting);
				WriteRGB(Group::PerMaterial, 2, ll.SRGBToWorking({ deep.red, deep.green, deep.blue }) * lighting);
				WriteRGB(Group::PerMaterial, 3, ll.SRGBToWorking({ reflection.red, reflection.green, reflection.blue }) * lighting);
			}
			context->Unmap(buffer, subresource);
		}
	};

	struct WaterGeometryUpload
	{
		static void thunk(ID3D11DeviceContext* context, ID3D11Resource* buffer)
		{
			auto* pass = geometryPass;
			auto& ll = globals::features::linearLighting;
			const uint lightCount = (globals::state->currentPixelDescriptor >> 11) & 0xF;
			if (pass && pass->shaderProperty && pass->sceneLights && ll.IsLinearLightingActive() && lightCount < 8) {
				const auto* material = static_cast<const RE::BSWaterShaderMaterial*>(pass->shaderProperty->material);
				for (uint i = 0; material && i < lightCount && i + 1 < pass->numLights; ++i) {
					const auto* bsLight = pass->sceneLights[i + 1];
					if (!bsLight || !bsLight->light || !bsLight->affectWater)
						continue;
					const auto* light = bsLight->light.get();
					const auto& data = light->GetLightRuntimeData();
					const float lightScale = *reinterpret_cast<const float*>(reinterpret_cast<const std::byte*>(material) + 0xAC);
					WriteRGB(Group::PerGeometry, 18, ll.LightColorToWorking(light) * (data.fade * bsLight->lodDimmer * lightScale), i);
				}
			}
			context->Unmap(buffer, 0);
		}
	};

	struct SetDirectionalAmbientColors
	{
		static void thunk(Effects11::DirectionalAmbientColors& DirectionalAmbientColors, RE::NiColor* AmbientSpecularTint, float AmbientSpecularFresnel)
		{
			auto& linearLighting = globals::features::linearLighting;
			if (linearLighting.IsLinearLightingActive()) {
				Effects11::DirectionalAmbientColors converted = DirectionalAmbientColors;
				for (auto& axis : converted.directionalAmbientColors) {
					for (auto& color : axis)
						color = linearLighting.SRGBToWorking(color);
				}

				RE::NiColor convertedSpecularTint{};
				auto* specularTint = AmbientSpecularTint;
				if (AmbientSpecularTint) {
					convertedSpecularTint = linearLighting.SRGBToWorking(*AmbientSpecularTint);
					specularTint = &convertedSpecularTint;
				}

				func(converted, specularTint, AmbientSpecularFresnel);
				return;
			}
			func(DirectionalAmbientColors, AmbientSpecularTint, AmbientSpecularFresnel);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct RenderMenuScene
	{
		static DWORD thunk(RE::UI3DSceneManager* manager, int scheme, RE::NiCamera* camera, bool arg4)
		{
			auto& renderToUI = globals::state->permutationData.RenderToUI;
			const uint previous = renderToUI;
			renderToUI = 1;
			const auto result = func(manager, scheme, camera, arg4);
			renderToUI = previous;
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

}

namespace ImageSpaceColorManagement
{
	constexpr std::size_t VOLUMETRIC_LIGHTING_COLOR = 0;
	constexpr std::size_t FOG_NEAR_COLOR = 4;
	constexpr std::size_t FOG_FAR_COLOR = 8;

	template <std::size_t... ColorOffsets>
	class ScopedInputColors
	{
	public:
		explicit ScopedInputColors(RE::ImageSpaceEffectParam* param)
		{
			shaderParam = skyrim_cast<RE::ImageSpaceShaderParam*>(param);
			if (!globals::features::linearLighting.IsLinearLightingActive() || !globals::shaderCache->IsEnabled() ||
				!globals::state->enablePShaders || !globals::state->ShaderEnabled(RE::BSShader::Type::ImageSpace) ||
				!shaderParam || !shaderParam->pixelConstantGroup ||
				!((ColorOffsets + 3 <= shaderParam->pixelConstantGroupSize) && ...))
				return;

			constexpr std::array offsets{ ColorOffsets... };
			for (std::size_t index = 0; index < offsets.size(); ++index) {
				auto* color = shaderParam->pixelConstantGroup + offsets[index];
				std::copy_n(color, 3, originalColors[index].data());
				auto& linearLighting = globals::features::linearLighting;
				linearLighting.SRGBToWorking(color);
			}
			active = true;
		}

		~ScopedInputColors()
		{
			if (!active)
				return;

			constexpr std::array offsets{ ColorOffsets... };
			for (std::size_t index = 0; index < offsets.size(); ++index)
				std::copy_n(originalColors[index].data(), 3, shaderParam->pixelConstantGroup + offsets[index]);
		}

	private:
		RE::ImageSpaceShaderParam* shaderParam = nullptr;
		std::array<std::array<float, 3>, sizeof...(ColorOffsets)> originalColors{};
		bool active = false;
	};

	template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType, std::size_t... ColorOffsets>
	struct BSImagespaceShader_Render
	{
		static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
		{
			const ScopedInputColors<ColorOffsets...> inputColors(param);
			func(imageSpaceShader, shape, param);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void LinearLighting::Load()
{
	configuredLinearLighting = loaded && settings.enableLinearLighting;
	configuredACEScg = configuredLinearLighting && settings.enableACEScg;
	if (!configuredLinearLighting)
		return;

	struct UploadCall : Xbyak::CodeGenerator
	{
		UploadCall(uintptr_t target, bool waterMaterial = false)
		{
			if (waterMaterial && REL::Module::IsAE())
				mov(r9, r15);
			else
				mov(r9, rbx);
			xor_(r8d, r8d);
			mov(rax, target);
			jmp(rax);
		}
	};
	static UploadCall lightingMaterial(reinterpret_cast<uintptr_t>(LightingMaterialUpload::thunk));
	static UploadCall effectMaterial(reinterpret_cast<uintptr_t>(EffectMaterialUpload::thunk));
	static UploadCall waterMaterial(reinterpret_cast<uintptr_t>(WaterMaterialUpload::thunk), true);
	const std::array calls{
		std::tuple{ "Lighting material"sv, REL::RelocationID(100563, 107298).address() + REL::Relocate(0xACD, 0xC65), lightingMaterial.getCode() },
		std::tuple{ "Effect material"sv, REL::RelocationID(100744, 107525).address() + REL::Relocate(0x3E2, 0x3E3), effectMaterial.getCode() },
		std::tuple{ "Lighting geometry"sv, REL::RelocationID(100565, 107300).address() + REL::Relocate(0xC1E, 0x12F0), reinterpret_cast<const uint8_t*>(LightingGeometryUpload::thunk) },
		std::tuple{ "Effect geometry"sv, REL::RelocationID(100746, 107527).address() + REL::Relocate(0xF16, 0xE6F), reinterpret_cast<const uint8_t*>(EffectGeometryUpload::thunk) },
		std::tuple{ "Water material"sv, REL::RelocationID(100602, 107363).address() + REL::Relocate(0x5FA, 0x626), waterMaterial.getCode() }
	};
	const auto verify = [](std::string_view name, uintptr_t address, const auto& expected) {
		const auto* actual = reinterpret_cast<const uint8_t*>(address);
		if (std::memcmp(actual, expected.data(), expected.size()) == 0)
			return;
		auto message = fmt::format("Linear Lighting: {} does not match the verified layout.\nSkyrim {}, RVA 0x{:X}\nExpected:",
			name, Util::GetFormattedVersion(REL::Module::get().version()), address - REL::Module::get().base());
		for (const auto byte : expected)
			message += fmt::format(" {:02X}", byte);
		message += "\nActual:";
		for (std::size_t i = 0; i < expected.size(); ++i)
			message += fmt::format(" {:02X}", actual[i]);
		stl::report_and_fail(message);
	};
	constexpr std::array<uint8_t, 6> unmapCall{ 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x78 };
	for (const auto& [name, address, code] : calls)
		verify(name, address, unmapCall);
	const auto waterGeometryAddress = REL::RelocationID(100604, 107365).address() + REL::Relocate(0x52F, 0x61F);
	constexpr std::array<uint8_t, 6> waterUnmapCall{ 0x45, 0x33, 0xC0, 0xFF, 0x50, 0x78 };
	verify("Water geometry", waterGeometryAddress, waterUnmapCall);
	const auto menuAddress = REL::RelocationID(51855, 52727).address();
	constexpr std::array<uint8_t, 16> menuEntry{ 0x48, 0x8B, 0xC4, 0x44, 0x88, 0x48, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56 };
	verify("Menu renderer", menuAddress, menuEntry);
	RenderMenuScene::func = menuAddress;
	if (DetourTransactionBegin() != NO_ERROR)
		stl::report_and_fail("Linear Lighting: could not begin menu hook installation."sv);
	if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
		DetourAttach(reinterpret_cast<PVOID*>(&RenderMenuScene::func), reinterpret_cast<PVOID>(RenderMenuScene::thunk)) != NO_ERROR) {
		DetourTransactionAbort();
		stl::report_and_fail("Linear Lighting: could not attach the menu hook."sv);
	}
	if (DetourTransactionCommit() != NO_ERROR)
		stl::report_and_fail("Linear Lighting: could not commit the menu hook."sv);
	stl::write_vfunc<0x1,
		ImageSpaceColorManagement::BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCompositeFog,
			ImageSpaceColorManagement::FOG_NEAR_COLOR,
			ImageSpaceColorManagement::FOG_FAR_COLOR>>(
		RE::VTABLE_BSImagespaceShaderISSAOCompositeFog[3]);
	stl::write_vfunc<0x1,
		ImageSpaceColorManagement::BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCompositeSAOFog,
			ImageSpaceColorManagement::FOG_NEAR_COLOR,
			ImageSpaceColorManagement::FOG_FAR_COLOR>>(
		RE::VTABLE_BSImagespaceShaderISSAOCompositeSAOFog[3]);
	stl::write_vfunc<0x1,
		ImageSpaceColorManagement::BSImagespaceShader_Render<RE::ImageSpaceManager::ISCompositeVolumetricLighting,
			ImageSpaceColorManagement::VOLUMETRIC_LIGHTING_COLOR>>(
		RE::VTABLE_BSImagespaceShaderISCompositeVolumetricLighting[3]);
	stl::write_vfunc<0x1,
		ImageSpaceColorManagement::BSImagespaceShader_Render<RE::ImageSpaceManager::ISCompositeLensFlareVolumetricLighting,
			ImageSpaceColorManagement::VOLUMETRIC_LIGHTING_COLOR>>(
		RE::VTABLE_BSImagespaceShaderISCompositeLensFlareVolumetricLighting[3]);
	stl::detour_thunk<SetDirectionalAmbientColors>(REL::RelocationID(98989, 105643));
	stl::write_vfunc<0x6, SetupGeometry<RE::BSShader::Type::Lighting>>(RE::VTABLE_BSLightingShader[0]);
	stl::write_vfunc<0x6, SetupGeometry<RE::BSShader::Type::Effect>>(RE::VTABLE_BSEffectShader[0]);
	stl::write_vfunc<0x6, SetupGeometry<RE::BSShader::Type::Particle>>(RE::VTABLE_BSParticleShader[0]);
	stl::write_vfunc<0x6, SetupGeometry<RE::BSShader::Type::Water>>(RE::VTABLE_BSWaterShader[0]);
	SKSE::GetTrampoline().write_call<6>(waterGeometryAddress, reinterpret_cast<uintptr_t>(WaterGeometryUpload::thunk));
	for (const auto& [name, address, code] : calls)
		SKSE::GetTrampoline().write_call<6>(address, reinterpret_cast<uintptr_t>(code));
}

#undef I18N_KEY_PREFIX
