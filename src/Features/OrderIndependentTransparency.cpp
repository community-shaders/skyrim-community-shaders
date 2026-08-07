#include "OrderIndependentTransparency.h"
#include "../I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Upscaling.h"
#include "Features/TerrainBlending.h"
#include "Features/HDRDisplay.h"
#include "Deferred.h"

#define I18N_KEY_PREFIX "feature.oit."

NLOHMANN_JSON_SERIALIZE_ENUM(OrderIndependentTransparency::Method,
	{ 
		{ OrderIndependentTransparency::Method::OIT_DISABLED, "Disabled" },
		{ OrderIndependentTransparency::Method::OIT_VISUALIZE, "Visualize" },
		{ OrderIndependentTransparency::Method::OIT_AT, "AT" },
		{ OrderIndependentTransparency::Method::OIT_BLENDED, "Blended" },
		{ OrderIndependentTransparency::Method::OIT_RVO, "RVO" }
	}
)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OrderIndependentTransparency::Settings,
	Method,
	BufferSize,
	MaxLayers,
	AlphaThreshold,
	DepthThreshold,
	DistanceThreshold,
	CaptureMultiplicativeLayer,
	OverrideRenderTargets,
	WriteDepth,
	WriteDepthThreshold,
	WBOITAdditiveAlphaScale,
	WBOITMinProjectedDistance,
	WBOITWeightMin,
	WBOITWeightMax,
	SSRAlphaScale
)

std::span<const D3D_SHADER_MACRO> OrderIndependentTransparency::GetShaderDefines() const
{
	return { &shaderDefines[0], 1UZ + (settings.Method == Method::OIT_RVO) };
}

static constexpr const char OIT_METHOD_DEFINES[][2] = { "0", "1", "1", "3", "2" };

bool OrderIndependentTransparency::UpdateShaderDefines()
{
	D3D_SHADER_MACRO defines[2] = { 0 };
	defines[0].Name = "OIT";
	defines[0].Definition = OIT_METHOD_DEFINES[(int)settings.Method];
	std::array<char, 4> definesBuffer;
	definesBuffer.fill(0);
	if (settings.Method == Method::OIT_RVO)
	{
		defines[1].Name = "OIT_NODE_COUNT";
		defines[1].Definition = definesBuffer.data();

		// convert MaxLayers to string in shaderDefineBuffer
		for (char& c : definesBuffer) c = 0;
		std::to_chars(definesBuffer.data(), definesBuffer.data() + definesBuffer.size(), GetNodeCount());
	} else {
		defines[1] = shaderDefines[1];  // keep the previous value, so we can detect change
	}

	auto SV = [](const char* str) { return str ? std::string_view(str) : std::string_view{}; };

	if (SV(shaderDefines[0].Definition) != SV(defines[0].Definition) || SV(shaderDefines[1].Definition) != SV(defines[1].Definition))
	{
		shaderDefineBuffer = definesBuffer;
		if (defines[1].Definition == definesBuffer.data())
			defines[1].Definition = shaderDefineBuffer.data();
		shaderDefines[0] = defines[0];
		shaderDefines[1] = defines[1];

		auto new_defines = GetShaderDefines();
		auto define_str = new_defines | std::views::transform([](const D3D_SHADER_MACRO& def) {
			if (def.Name && def.Definition)
				return std::string(def.Name) + "=" + std::string(def.Definition) + " ";
			else if (def.Name)
				return std::string(def.Name);
			else
				return std::string{};
		}) | std::views::join_with(',') | std::ranges::to<std::string>();
		logger::info("[OIT] Shader defines updated [{}]: {}", new_defines.size(), define_str);
		return true;
	}
	else
	{
		logger::info("[OIT] Shader defines unchanged: OIT={}->{} OIT_NODE_COUNT={}->{}", SV(shaderDefines[0].Definition), SV(defines[0].Definition), SV(shaderDefines[1].Definition), SV(defines[1].Definition));
	}
	return false;
}

bool OrderIndependentTransparency::HasShaderDefine(RE::BSShader::Type /*shaderType*/)
{
	return false;
}

struct Main_RenderWorld_RenderTransparency
{
	static void thunk(RE::BSShaderAccumulator* accumulator, uint32_t renderFlags);
	static inline REL::Relocation<decltype(thunk)> func;
};

template <RE::BSShader::Type ShaderType>
struct BSShader_SetupGeometry
{
	static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
	{
		globals::features::orderIndependentTransparency.SetupGeometry(shader, pass, renderFlags);
		func(shader, pass, renderFlags);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

template <RE::BSShader::Type ShaderType>
struct BSShader_RestoreGeometry
{
	static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
	{
		func(shader, pass, renderFlags);
		globals::features::orderIndependentTransparency.RestoreGeometry(shader, pass, renderFlags);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

struct Renderer_Flush_OMSetRenderTargets
{
	static void thunk(ID3D11DeviceContext* a_self, UINT a_numViews, ID3D11RenderTargetView** a_ppRenderTargetViews, ID3D11DepthStencilView* a_depthStencilView)
	{
		auto& oit = globals::features::orderIndependentTransparency;
		if (!oit.inAlphaPass)
			a_self->OMSetRenderTargets(a_numViews, a_ppRenderTargetViews, a_depthStencilView);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct Renderer_Flush
{
	static void thunk(RE::BSGraphics::Renderer* renderer, uint8_t flags)
	{
		// Prevent render target change during the alpha pass
		// Effect shader forced RT change in sub_1414EA1B0 + 0x43
		// And called renderer flush in sub_1414EA1B0 + 0x5E
		// So we have to hook in this level
		auto& oit = globals::features::orderIndependentTransparency;
		oit.PreSetStateDirty();
		func(renderer, flags);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

template <RE::BSShader::Type ShaderType> constexpr REL::VariantID VTABLE_BSShader;
template<> constexpr REL::VariantID VTABLE_BSShader<RE::BSShader::Type::Lighting> = RE::VTABLE_BSLightingShader[0];
template<> constexpr REL::VariantID VTABLE_BSShader<RE::BSShader::Type::Effect> = RE::VTABLE_BSEffectShader[0];
template<> constexpr REL::VariantID VTABLE_BSShader<RE::BSShader::Type::Particle> = RE::VTABLE_BSParticleShader[0];
template <RE::BSShader::Type ShaderType>
void HookSetupGeometry()
{
	stl::write_vfunc<0x6, BSShader_SetupGeometry<ShaderType>>(VTABLE_BSShader<ShaderType>);
	stl::write_vfunc<0x7, BSShader_RestoreGeometry<ShaderType>>(VTABLE_BSShader<ShaderType>);
}

void OrderIndependentTransparency::PostPostLoad()
{
	logger::info("[OIT] Hooking BSShader_SetupGeometry");
	HookSetupGeometry<RE::BSShader::Type::Lighting>();
	HookSetupGeometry<RE::BSShader::Type::Effect>();
	HookSetupGeometry<RE::BSShader::Type::Particle>();

	logger::info("[OIT] Hooking Main_RenderWorld_RenderTransparency");
	stl::detour_thunk<Main_RenderWorld_RenderTransparency>(REL::RelocationID(99940, 106585));

	logger::info("[OIT] Hooking Renderer_Flush");
	// Need RE for SSE/VR
	if (REL::Module::IsAE())
	{
		stl::detour_thunk<Renderer_Flush>(REL::RelocationID(77247, 77247));
	}
}

void OrderIndependentTransparency::DataLoaded()
{
	UpdateShaderDefines();
}

void OrderIndependentTransparency::DrawSettings()
{
	struct DirtyFlags
	{
		bool PixelBuffer = false;
		bool ConstantBuffer = false;
		bool CompositionShader = false;
		bool ShaderDefines = false;

		~DirtyFlags()
		{
			auto& oit = globals::features::orderIndependentTransparency;
			if (ConstantBuffer) {
				oit.UpdateShaderConstantBuffer();
			}
			if (ShaderDefines)
			{
				if (oit.UpdateShaderDefines()) {
					logger::info("[OIT] Shader defines changed, clearing shader cache.");
					globals::shaderCache->Clear();
					if (oit.settings.Method == Method::OIT_RVO && !oit.colorBuffer.has_value())
					{
						PixelBuffer = true;
					}
				}
			}
			if (PixelBuffer || ShaderDefines) {
				oit.SetupPixelBuffers();
			}
			if (CompositionShader) {
				oit.ClearShaderCache();
			}
		}
	} dirtied;

	struct EnableScope
	{
		bool Disabled;
		EnableScope(bool EnableCondition) : Disabled(!EnableCondition)
		{
			if (Disabled) ImGui::BeginDisabled();
		}
		~EnableScope() 
		{
			if (Disabled) ImGui::EndDisabled();
		}
	};

	if (ImGui::TreeNodeEx(T(TKEY("method"), "Method"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::RadioButton(T(TKEY("method_disabled"), "Disabled"), (int*)&settings.Method, Method::OIT_DISABLED);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("method_disabled_tooltip"), "Disable OIT without unloading the feature."));
		}
		dirtied.ShaderDefines |= ImGui::RadioButton(T(TKEY("method_visualize"), "Visualize (Performance Tuning)"), (int*)&settings.Method, Method::OIT_VISUALIZE);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("method_visualize_tooltip"),
				"Visualizes the number of transparent layers blended per pixel.\n"
				"Use this to tune performance settings.\n"
				"Layer counts progress from blue through cyan, green, yellow, orange, red, purple, and white."));
		}
		dirtied.ShaderDefines |= ImGui::RadioButton(T(TKEY("method_blended"), "Fast Approximation (Fog, Hair, Grass, ...)"), (int*)&settings.Method, Method::OIT_BLENDED);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("method_blended_tooltip"),
				"Uses weighted, blended OIT.\n"
				"Fast, but approximate: dense or highly opaque layers can appear overbright.\n"
				"Best for volumetric fog, hair, and grass with many overlapping layers.\n"
				"Has a negligible to minimal performance cost."));
		}
		dirtied.ShaderDefines |= ImGui::RadioButton(T(TKEY("method_adaptive"), "Balanced (Characters, Clothing, ...)"), (int*)&settings.Method, Method::OIT_AT);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("method_adaptive_tooltip"),
				"Uses adaptive transparency.\n"
				"Accurate up to the configured maximum layer count; additional layers use an error approximation.\n"
				"Fast, but may flicker or show artifacts beyond the layer limit.\n"
				"Typically uses about 10% more GPU time and is memory-bound."));
		}
		dirtied.ShaderDefines |= ImGui::RadioButton(T(TKEY("method_stable"), "Stable (Slow)"), (int*)&settings.Method, Method::OIT_RVO);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("method_stable_tooltip"),
				"Uses MLAB (Multi-Layer Alpha Blend), Intel's Adaptive Order-Independent Transparency (AOIT); requires DirectX 11.3 or newer.\n"
				"Unreal Engine's default OIT method for its predictability.\n"
				"Uses Rasterizer Ordered Views to avoid flickering with bounded memory.\n"
				"Has a major performance cost compared with the other methods.\n"
				"Very expensive when Max Layers is greater than 4."));
		}
		ImGui::TreePop();
	}
	ImGui::Spacing();
	if (settings.Method == Method::OIT_BLENDED && ImGui::TreeNodeEx(T(TKEY("wboit_parameters"), "Tuning"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("%s", T(TKEY("wboit_advanced_tuning"), "For advanced tuning, edit WBOITWeight.hlsli directly."));
		dirtied.ConstantBuffer |= ImGui::SliderFloat(T(TKEY("wboit_additive_alpha_scale"), "Additive Alpha Scale"), &settings.WBOITAdditiveAlphaScale, 0.f, 1.f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("wboit_additive_alpha_scale_tooltip"), "Adjust this when additive Lighting or Particle effects have sharp visible boundaries."));
		}
		dirtied.ConstantBuffer |= ImGui::SliderFloat(T(TKEY("wboit_min_projected_distance"), "Minimum Projected Distance"), &settings.WBOITMinProjectedDistance, 0.f, 1.f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("wboit_min_projected_distance_tooltip"), "Increase this when rain or snow particles appear overbright."));
		}
		dirtied.ConstantBuffer |= ImGui::SliderFloat(T(TKEY("wboit_min_final_weight"), "Minimum Final Weight"), &settings.WBOITWeightMin, 0.f, settings.WBOITWeightMax, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("wboit_final_weight_tooltip"),
				"Controls the final WBOIT weight range.\n"
				"A wider range improves close-layer accuracy, favoring clothing.\n"
				"A narrower range reduces overbright distant layers, favoring fog."));
		}
		dirtied.ConstantBuffer |= ImGui::SliderFloat(T(TKEY("wboit_max_final_weight"), "Maximum Final Weight"), &settings.WBOITWeightMax, settings.WBOITWeightMin, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("wboit_final_weight_tooltip"),
				"Controls the final WBOIT weight range.\n"
				"A wider range improves close-layer accuracy, favoring clothing.\n"
				"A narrower range reduces overbright distant layers, favoring fog."));
		}
		ImGui::TreePop();
	}
	ImGui::Spacing();
	if (ImGui::TreeNodeEx(T(TKEY("compatibility"), "Compatibility"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox(T(TKEY("multiplicative_blend_support"), "Multiplicative Blend Support"), &settings.CaptureMultiplicativeLayer))
		{
			featureCB.Flags = settings.CaptureMultiplicativeLayer ? 1 : 0;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("multiplicative_blend_support_tooltip"),
				"Captures grayscale multiplicative blend layers in OIT.\n"
				"Colored multiplicative blend layers are not supported."));
		}
		ImGui::Checkbox(T(TKEY("enforce_render_target"), "Enforce Render Targets"), &settings.OverrideRenderTargets);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enforce_render_target_tooltip"),
				"Forces transparent render targets on every draw call.\n"
				"Has a CPU cost; enable only if transparent meshes disappear.\n"
				"Currently affects AE only."));
		}
		dirtied.CompositionShader |= ImGui::Checkbox(T(TKEY("write_depth"), "Write Depth"), &settings.WriteDepth);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("write_depth_tooltip"),
				"Allows OIT composition to write depth for meshes with the Write Depth flag.\n"
				"Requires pixel shader resolve.\n"
				"Applies to all Lighting shader materials, excluding Effect shaders."));
		}
		dirtied.ConstantBuffer |= ImGui::SliderFloat(T(TKEY("ssr_alpha_scale"), "SSR Alpha Scale"), &settings.SSRAlphaScale, 0.f, 1.f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("ssr_alpha_scale_tooltip"),
				"Scales the alpha value used for screen space reflections (SSR).\n"
				"Adjusting this can affect the visibility and intensity of reflections.\n"
				"Use Ctrl+Click to input value beyond 1.0\n"));
		}
		ImGui::TreePop();
	}
	ImGui::Spacing();
	if (ImGui::TreeNodeEx(T(TKEY("performance"), "Performance"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(T(TKEY("pass_time"), "Pass Time      = %6.2f ms"), passTime);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("pass_time_tooltip"), "CPU time spent in the engine's transparent pass."));
		}
		ImGui::Text(T(TKEY("composite_time"), "Composite Time = %6.2f ms"), compositeTime);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("composite_time_tooltip"), "CPU time spent issuing the OIT composition draw."));
		}
		{
			EnableScope _(settings.Method == Method::OIT_AT);
			dirtied.PixelBuffer |= ImGui::SliderInt(T(TKEY("pixel_buffer_size"), "Pixel Buffer Size"), (int*)&settings.BufferSize, 2, 16);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("pixel_buffer_size_tooltip"),
					"Multiplier of the back-buffer size reserved for transparent layers.\n"
					"Buffer overflow can cause flickering.\n"
					"Adjust for your system's available performance and memory."));
			}
		}
		{
			EnableScope _(settings.Method == Method::OIT_AT || settings.Method == Method::OIT_RVO);
			int MaxLayers = settings.MaxLayers;
			ImGui::SliderInt(T(TKEY("max_layers"), "Maximum Layers"), &MaxLayers, 4, 32, "%d", ImGuiSliderFlags_AlwaysClamp);
			MaxLayers = ((MaxLayers + 2) / 4) * 4; // Round to multiple of 4
			if (MaxLayers != (int)settings.MaxLayers) {
				settings.MaxLayers = MaxLayers;
				dirtied.ShaderDefines = true;
				dirtied.CompositionShader = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("max_layers_tooltip"),
					"Maximum number of layers blended during OIT composition.\n"
					"Layers beyond this limit can introduce artifacts.\n"
					"Increasing this value can reduce performance."));
			}
		}
		dirtied.ConstantBuffer |= ImGui::SliderFloat(T(TKEY("alpha_cutoff"), "Alpha Cutoff"), &settings.AlphaThreshold, 0.0f, 0.1f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("alpha_cutoff_tooltip"),
				"Pixels below this alpha value are discarded.\n"
				"Increasing it can slightly improve performance but may introduce visible artifacts.\n"
				"For example, values above 0.01 can fade rain particles."));
		}
		dirtied.ConstantBuffer |= ImGui::SliderFloat(T(TKEY("depth_cutoff"), "Depth Cutoff"), &settings.DepthThreshold, 0.9f, 1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("depth_cutoff_tooltip"),
				"Screen-space depth threshold used to avoid z-fighting from 32-bit floating-point precision limits.\n"
				"Decrease it if overlapping transparent surfaces z-fight.\n"
				"Values that are too low can make surfaces disappear."));
		}
		{
			EnableScope _{ settings.Method != Method::OIT_BLENDED };
			ImGui::SliderFloat(T(TKEY("distance_threshold"), "Distance Threshold"), &settings.DistanceThreshold, 0.f, Settings::InfDistanceThreshold, "%.0f units");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("distance_threshold_tooltip"),
					"Starts capturing OIT at this camera distance to exclude distant, complex volumetric fog.\n"
					"Decrease it if distant fog reduces performance or flickers.\n"
					"Use Visualize mode to tune the threshold.\n"
					"1 unit = 1.428 cm or 0.5625 in."));
			}
		}
		ImGui::TreePop();
	}
}

#undef I18N_KEY_PREFIX

void OrderIndependentTransparency::UpdateShaderConstantBuffer()
{
	featureCB.AlphaThreshold = settings.AlphaThreshold;
	featureCB.DepthThreshold = settings.DepthThreshold;
	featureCB.Flags = settings.CaptureMultiplicativeLayer ? 1 : 0;
	featureCB.WBOITAdditiveAlphaScale = settings.WBOITAdditiveAlphaScale;
	featureCB.WBOITMinProjectedDistance = settings.WBOITMinProjectedDistance;
	featureCB.WBOITWeightMin = settings.WBOITWeightMin;
	featureCB.WBOITWeightMax = settings.WBOITWeightMax;
	featureCB.SSRAlphaScale = settings.SSRAlphaScale;
}

void OrderIndependentTransparency::OnSettingLoaded()
{
	UpdateShaderConstantBuffer();
}

void OrderIndependentTransparency::LoadSettings(json& o_json)
{
	settings = o_json;
	OnSettingLoaded();
}

void OrderIndependentTransparency::SaveSettings(json& o_json)
{
	o_json = settings;
}

void OrderIndependentTransparency::RestoreDefaultSettings()
{
	settings = {};
	OnSettingLoaded();
	if (UpdateShaderDefines()) {
		logger::info("[OIT] Shader defines changed, clearing shader cache.");
		globals::shaderCache->Clear();
	}
	SetupPixelBuffers();
	ClearShaderCache();
}

void SetupRenderTarget(RE::RENDER_TARGET target, D3D11_TEXTURE2D_DESC texDesc, D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc, D3D11_RENDER_TARGET_VIEW_DESC rtvDesc, D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc, DXGI_FORMAT format, uint bindFlags);

// Create texture with both SRV and RTV
static void CreateTextureSR(std::optional<Texture2D>& texture, std::string_view name, const D3D11_TEXTURE2D_DESC& baseDesc, DXGI_FORMAT format)
{
	auto texDesc = baseDesc;
	texDesc.Format = format;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	texture.emplace(texDesc);
	texture->resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.data());
	texture->CreateSRV(CD3D11_SHADER_RESOURCE_VIEW_DESC(D3D11_SRV_DIMENSION_TEXTURE2D, format));
	texture->CreateRTV(CD3D11_RENDER_TARGET_VIEW_DESC(D3D11_RTV_DIMENSION_TEXTURE2D, format));
}

void OrderIndependentTransparency::SetupResources()
{
	auto disable = [this] { DisableForResourceFailure(); };
	auto* renderer = globals::game::renderer;
	auto* device = globals::d3d::device;
	// When you want to align with the main texture format
	auto& mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC mainDesc;
	mainTex.texture->GetDesc(&mainDesc);
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		mainTex.SRV->GetDesc(&srvDesc);
		mainTex.RTV->GetDesc(&rtvDesc);
		mainTex.UAV->GetDesc(&uavDesc);

		SetupRenderTarget(RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA, mainDesc, srvDesc, rtvDesc, uavDesc, mainDesc.Format, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
	}

	logger::info("[OIT] Setting up Order Independent Transparency resources...");
	{
		auto texDesc = mainDesc;
		texDesc.Format = DXGI_FORMAT_R32_UINT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		try {
			headerBuffer.emplace(texDesc);
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create oit header texture @{}x{}: {}", mainDesc.Width, mainDesc.Height, e.what());
			disable();
			return;
		}
		headerBuffer->resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)sizeof("OIT Header") - 1, "OIT Header");

		CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(
			D3D11_SRV_DIMENSION_TEXTURE2D,
			texDesc.Format
		);
		CD3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc(
			D3D11_UAV_DIMENSION_TEXTURE2D,
			texDesc.Format);
		try {
			headerBuffer->CreateSRV(srvDesc);
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create fragment list head SRV: {}", e.what());
			disable();
			return;
		}
		try {
			headerBuffer->CreateUAV(uavDesc);
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create fragment list head UVA: {}", e.what());
			disable();
			return;
		}
	}

	{
		UINT numElem = mainDesc.Width * mainDesc.Height;
		if (!SetupPixelBuffers(numElem)) {
			return;
		}
	}

	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		depthStencilDesc.StencilEnable = false;
		try {
			DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, depthStencilState.put()));
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create depth stencil state {}", e.what());
			disable();
			return;
		}
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		try {
			DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, resolveDepthStencilState.put()));
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create depth stencil state {}", e.what());
			disable();
			return;
		}
		try {
			CreateTextureSR(accumalationBuffer, "OIT Front Accumalation", mainDesc, DXGI_FORMAT_R16G16B16A16_FLOAT);
			CreateTextureSR(accumalationWaterBuffer, "OIT Accumalation", mainDesc, DXGI_FORMAT_R16G16B16A16_FLOAT);
			CreateTextureSR(revealageBuffer, "OIT Revealage", mainDesc, DXGI_FORMAT_R16G16B16A16_FLOAT);
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create weighted blended OIT render targets: {}", e.what());
			disable();
			return;
		}

		D3D11_BLEND_DESC wboitBlendDesc{};
		wboitBlendDesc.AlphaToCoverageEnable = false;
		wboitBlendDesc.IndependentBlendEnable = true;
		for (auto slot : { 3, 4 }) {
			auto& rt = wboitBlendDesc.RenderTarget[slot];
			rt.BlendEnable = true;
			rt.SrcBlend = D3D11_BLEND_ONE;
			rt.DestBlend = D3D11_BLEND_ONE;
			rt.BlendOp = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlendAlpha = D3D11_BLEND_ONE;
			rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		{
			auto& rt = wboitBlendDesc.RenderTarget[5];
			rt.BlendEnable = true;
			rt.SrcBlend = D3D11_BLEND_ZERO;
			rt.DestBlend = D3D11_BLEND_SRC_COLOR;
			rt.BlendOp = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlendAlpha = D3D11_BLEND_ONE;
			rt.BlendOpAlpha = D3D11_BLEND_OP_MIN;
			rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_ALPHA;
		}
		try {
			DX::ThrowIfFailed(device->CreateBlendState(&wboitBlendDesc, wboitBlendState.put()));
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create weighted blended OIT blend state {}", e.what());
			disable();
			return;
		}

		D3D11_BLEND_DESC blendDesc{};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		try {
			DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, resolveBlendState.put()));
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create blend state {}", e.what());
			disable();
			return;
		}
	}

	{
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto& postWaterCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_WATER_COPY];

		try {
			D3D11_TEXTURE2D_DESC texDesc;
			mainDepth.texture->GetDesc(&texDesc);
			DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, NULL, &postWaterCopy.texture));

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			mainDepth.depthSRV->GetDesc(&srvDesc);
			DX::ThrowIfFailed(device->CreateShaderResourceView(postWaterCopy.texture, &srvDesc, &postWaterCopy.depthSRV));

			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
			mainDepth.views[0]->GetDesc(&dsvDesc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(postWaterCopy.texture, &dsvDesc, &postWaterCopy.views[0]));
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create post-water depth copy: {}", e.what());
			disable();
			return;
		}
	}

	CompileShaders();
	logger::info("[OIT] Order Independent Transparency resources setup complete.");
}

void OrderIndependentTransparency::ClearShaderCache()
{
	psAT.detach();
	psVisualize.detach();
	psBlend.detach();
	psROV.detach();
	CompileShaders();
}

uint OrderIndependentTransparency::GetNodeCount() const
{
	uint nodes = std::clamp<uint>(settings.MaxLayers, 1, 32);
	return nodes % 4 == 0 ? nodes : nodes - nodes % 4 + 4;
}

void OrderIndependentTransparency::CompileShaders()
{
	// OIT_NODE_COUNT = MaxLayers
	uint nodes = GetNodeCount();
	char nodesStr[4] = { 0 };
	std::to_chars(nodesStr, nodesStr + 4, nodes);
	const char* writeDepthDefine = settings.WriteDepth ? "1" : "0";

	logger::info("[OIT] Compiling Order Independent Transparency shaders, OIT_NODE_COUNT={}, OIT_WRITE_DEPTH={}...", nodesStr, writeDepthDefine);

	if (/*settings.Method == OIT_VISUALIZE && */!psVisualize) {
		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\OITResolve.ps.hlsl", { { "OIT_DEBUG", "1" } }, "ps_5_0"))) {
			psVisualize.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency debug pixel shader.");
		}
	}
	if (/*settings.Method == OIT_AT && */!psAT) {
		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\OITResolve.ps.hlsl", { { "OIT_AT", "1" }, { "OIT_NODE_COUNT", nodesStr }, { "OIT_WRITE_DEPTH", writeDepthDefine } }, "ps_5_0"))) {
			psAT.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency resolve pixel shader.");
		}
	}
	if (/*settings.Method == OIT_BLENDED && */!psBlend) {
		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\OITResolve.ps.hlsl", { { "OIT_BLENDED", "1" }, { "OIT_WRITE_DEPTH", writeDepthDefine } }, "ps_5_0"))) {
			psBlend.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency blend pixel shader.");
		}
	}
	if (/*settings.Method == OIT_RVO && */!psROV) {
		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\OITResolve.ps.hlsl", { { "OIT_ROV", "1" }, { "OIT_NODE_COUNT", nodesStr }, { "OIT_WRITE_DEPTH", writeDepthDefine } }, "ps_5_0"))) {
			psROV.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency ROV resolve pixel shader.");
		}
	}
}

void OrderIndependentTransparency::DisableForResourceFailure()
{
	headerBuffer.reset();
	nodesBuffer.reset();
	accumalationBuffer.reset();
	accumalationWaterBuffer.reset();
	revealageBuffer.reset();
	colorBuffer.reset();
	depthBuffer.reset();
	psVisualize = nullptr;
	psAT = nullptr;
	psBlend = nullptr;
	psROV = nullptr;
	depthStencilState = nullptr;
	wboitBlendState = nullptr;
	resolveBlendState = nullptr;
	resolveDepthStencilState = nullptr;
	rtvs.fill(nullptr);
	uavs.fill(nullptr);
	dsv = nullptr;
	inAlphaPass = false;

	settings.Method = Method::OIT_DISABLED;
	if (UpdateShaderDefines()) {
		globals::shaderCache->Clear();
	}
	logger::error("[OIT] Disabled after resource setup failure.");
}

bool OrderIndependentTransparency::SetupPixelBuffers()
{
	auto* renderer = globals::game::renderer;
	auto& mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC mainDesc;
	mainTex.texture->GetDesc(&mainDesc);
	return SetupPixelBuffers(mainDesc.Width * mainDesc.Height);
}

static bool CreateStructBuffer(std::optional<Buffer>& buffer, std::string_view name, uint elements, uint size, bool counter = false)
{
	CD3D11_BUFFER_DESC bufferDesc(
		elements * size,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		D3D11_USAGE_DEFAULT,
		0,
		D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
		size);
	try {
		buffer.emplace(bufferDesc);
	} catch (const DX::com_exception& e) {
		logger::error("Failed to create {} buffer: {}", name, e.what());
		return false;
	}
	buffer->resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.data());

	CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(
		D3D11_SRV_DIMENSION_BUFFER,
		DXGI_FORMAT_UNKNOWN,
		0, elements);

	try {
		buffer->CreateSRV(srvDesc);
	} catch (const DX::com_exception& e) {
		logger::error("Failed to create {} SRV: {}", name, e.what());
		buffer.reset();
		return false;
	}
	if (!buffer->srv) {
		logger::error("Failed to create {} SRV", name);
		buffer.reset();
		return false;
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = elements;
	uavDesc.Buffer.Flags = counter ? D3D11_BUFFER_UAV_FLAG_COUNTER : 0;
	try {
		buffer->CreateUAV(uavDesc);
	} catch (const DX::com_exception& e) {
		logger::error("Failed to create {} UAV: {}", name, e.what());
		buffer.reset();
		return false;
	}
	if (!buffer->uav)
	{
		logger::error("Failed to create {} UAV", name);
		buffer.reset();
		return false;
	}
	return true;
}

bool OrderIndependentTransparency::SetupPixelBuffers(uint numElem)
{
	logger::info("[OIT] Requested Setup Pixel Buffers {}", numElem);
	if (settings.Method == Method::OIT_RVO)
	{
		uint NodeCount = GetNodeCount();
		uint colorBufferStride = sizeof(uint32_t) * 2 * NodeCount;
		uint depthBufferStride = sizeof(float) * 4 * NodeCount;
		uint colorBufferBytes = colorBufferStride * numElem;
		if (!colorBuffer.has_value() || colorBuffer->desc.ByteWidth != colorBufferBytes || colorBuffer->desc.StructureByteStride != colorBufferStride)
		{
			logger::info("[OIT] Creating AOIT Buffers {} * {}", numElem, NodeCount);
			if (!CreateStructBuffer(colorBuffer, "OIT Color", numElem, colorBufferStride) ||
				!CreateStructBuffer(depthBuffer, "OIT Depth", numElem, depthBufferStride)) {
				DisableForResourceFailure();
				return false;
			}
		}
	}
	else if (settings.Method == Method::OIT_AT || settings.Method == Method::OIT_VISUALIZE)
	{
		uint BufferSize = settings.BufferSize * numElem;
		if (!nodesBuffer.has_value() || nodesBuffer->desc.ByteWidth != BufferSize * sizeof(FragmentListNode))
		{
			logger::info("[OIT] Creating Fragment List Buffers {} * {}", numElem, settings.BufferSize);
			featureCB.MaxListNodes = BufferSize;
			if (!CreateStructBuffer(nodesBuffer, "OIT Nodes", BufferSize, sizeof(FragmentListNode), true)) {
				DisableForResourceFailure();
				return false;
			}
		}
	}
	return true;
}

enum AlphaBlendMode : uint32_t
{
	kAlpha = 1,              //  src.rgb * src.a + dst.rgb * (1 - src.a)
	kAdditive = 2,           //  src.rgb * src.a + dst.rgb *  1
	kMultiplicativeAlpha = 3,//                    dst.rgb * (src.rgb + 1 - src.a)
	kMultiplicative = 4,     //                    dst.rgb *  src.rgb
};

void OrderIndependentTransparency::PreSetStateDirty()
{
	if (!inAlphaPass /*|| !closeEnough*/) {
		return;
	}
	globals::game::stateUpdateFlags->set(false, RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	globals::game::stateUpdateFlags->set(false, RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);
	if (settings.Method == Method::OIT_BLENDED)
		globals::game::stateUpdateFlags->set(false, RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

	// Force depth to test only (not write)
	auto shadowState = globals::game::shadowState;
	auto& depthStencil = shadowState->GetRuntimeData().depthStencil;
	auto& depthStencilDepthMode = shadowState->GetRuntimeData().depthStencilDepthMode;

	depthStencil = 0;
	depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTest;
}

void OrderIndependentTransparency::PreDrawHack()
{
	using enum State::ExtraFeatureDescriptors;
	static constexpr uint OITAddtiveDescriptor = std::to_underlying(OITAdditive);
	static constexpr uint OITMultiplicativeDescriptor = std::to_underlying(OITMultiplicative);
	static constexpr uint OITMultiplicativeAlphaDescriptor = std::to_underlying(OITMultiplicative) | std::to_underlying(OITAdditive);
	static constexpr uint OITDepthWriteDescriptor = std::to_underlying(OITDepthWrite);
	static constexpr uint OITDisabledDescriptor = std::to_underlying(OITDisabled);

	auto& descriptor = globals::state->permutationData.ExtraFeatureDescriptor;
	descriptor &= ~(OITAddtiveDescriptor | OITMultiplicativeDescriptor | OITDepthWriteDescriptor | OITDisabledDescriptor);

	if (!inAlphaPass/* || !closeEnough*/) {
		return;
	}

	if (!closeEnough)
	{
		descriptor |= OITDisabledDescriptor;
	}

	// Distance gating is evaluated in SetupGeometry(), which runs after this hook.
	// Disabling OIT here makes translucent objects bypass capture and show up as overlays in water refraction.

	auto shadowState = globals::game::shadowState;
	auto& alphaBlendMode = shadowState->GetRuntimeData().alphaBlendMode;
	auto& depthStencilDepthMode = shadowState->GetRuntimeData().depthStencilDepthMode;

	if (alphaBlendMode > 4) [[unlikely]] {
		winrt::com_ptr<ID3D11BlendState> blend = nullptr;
		globals::d3d::context->OMGetBlendState(blend.put(), nullptr, nullptr);
		D3D11_BLEND_DESC desc;
		blend->GetDesc(&desc);

		auto dest = desc.RenderTarget[0].DestBlend;
		auto src = desc.RenderTarget[0].SrcBlend;
		auto op = desc.RenderTarget[0].BlendOp;
		
		logger::warn("unknown alpha blend mode {}, DestBlend {} , SrcBlend {}, Op {}", alphaBlendMode, magic_enum::enum_name(dest), magic_enum::enum_name(src), magic_enum::enum_name(op));
	}

	// Blend mode descriptors
	if (alphaBlendMode == AlphaBlendMode::kAdditive)
		descriptor |= OITAddtiveDescriptor;
	else if (alphaBlendMode == AlphaBlendMode::kMultiplicative)
		descriptor |= OITMultiplicativeDescriptor;
	else if (alphaBlendMode == AlphaBlendMode::kMultiplicativeAlpha)
		descriptor |= OITMultiplicativeAlphaDescriptor;

	// Setup depth write descriptor, depth write will be skipped in capture pass
	using enum RE::BSGraphics::DepthStencilDepthMode;
	if (depthStencilDepthMode == kWrite || depthStencilDepthMode == kTestWrite || drawWriteDepth)
	{
		descriptor |= OITDepthWriteDescriptor;
	}

	if (settings.Method == Method::OIT_BLENDED)
		globals::d3d::context->OMSetBlendState(wboitBlendState.get(), nullptr, 0xffffffff);

	if (settings.OverrideRenderTargets || !REL::Module::IsAE())
	{
		if (settings.Method == Method::OIT_BLENDED)
			globals::d3d::context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), dsv);
		else
			globals::d3d::context->OMSetRenderTargetsAndUnorderedAccessViews(3, rtvs.data(), dsv, 3, 2 + (settings.Method == OIT_RVO ? 1 : 0), uavs.data(), nullptr);
	}
}

void OrderIndependentTransparency::SetupGeometry(RE::BSShader*, RE::BSRenderPass* pass, uint32_t)
{
	if (!inAlphaPass)
		return;

	if (pass->shaderProperty && pass->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite)) 
	{
		drawWriteDepth = true;
	}

	// Distance threshold gating evaluation
	if (closeEnough)
	{
		return;
	}

	// This is how the engine sort the objects, projected distance in view direction
	const RE::NiBound& geometryBound = pass->geometry->worldBound;
	auto position = geometryBound.center;
	auto viewPos = cameraWorldInverse * position;

	// using bounding box extent can be good idea for regular objects
	// but large volumetric fog can have a very large bounding box and break this hard
	// float rdistance = distance - geometryBound.radius;
	if (viewPos.y < settings.DistanceThreshold) {
		closeEnough = true;
		logger::debug("Close enough for OIT");
	}
}

void OrderIndependentTransparency::RestoreGeometry(RE::BSShader* , RE::BSRenderPass* , uint32_t )
{
	drawWriteDepth = false;
}

void OrderIndependentTransparency::BeginAlphaGroup()
{
	EndWater();

	cameraPos = Util::GetEyePosition();
	auto cameraWorld = RE::PlayerCamera::GetSingleton()->cameraRoot->world;
	cameraWorldInverse = cameraWorld.Invert();
	inAlphaPass = true;
	calls = 0;
	// don't need distance gating for blended OIT, whos cost does not scale with layers in the pixel
	closeEnough = settings.Method == Method::OIT_BLENDED ||
	              settings.DistanceThreshold + 1.f >= Settings::InfDistanceThreshold;

	logger::debug("Beginning OIT alpha group camera pos = {}, camera world position = {}", cameraPos, cameraWorld.translate);

	static constexpr bool resetUAVCounter = true;

	auto* renderer = globals::game::renderer;
	ID3D11DeviceContext* context = globals::d3d::context;
	ID3D11UnorderedAccessView* headerUAV = headerBuffer->uav.get();

	// Initialize the first node offset RW UAV with a NULL offset (end of the list)
	static constexpr UINT clearValuesHead[4] = {
		0x0UL,
		0x0UL,
		0x0UL,
		0x0UL
	};

	if (settings.WriteDepth)
	{
		// We need main depth (depth after water) in composition
		// Copying it so that we can read from MainCopy and write write to Main (depth cannot be UAV)
		auto& main = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto& mainCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_WATER_COPY];
		context->CopyResource(mainCopy.texture, main.texture);
	}

	context->ClearUnorderedAccessViewUint(headerUAV, clearValuesHead);

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& TAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
	auto& alphaOnly = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA];
	// Need to capture pre-water depth
	auto& preWaterDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	dsv = preWaterDepth.readOnlyViews[0];
	static constexpr UINT uavcounters[2] = { 1, 1 };

	if (settings.Method == OIT_BLENDED)
	{
		static constexpr float clearAccum[4] = { 0, 0, 0, 0 };
		static constexpr float clearRevealage[4] = { 1, 1, 0, 1 };
		rtvs = { main.RTV, TAAMask.RTV, alphaOnly.RTV, accumalationBuffer->rtv.get(), accumalationWaterBuffer->rtv.get(), revealageBuffer->rtv.get() };
		context->ClearRenderTargetView(rtvs[3], clearAccum);
		context->ClearRenderTargetView(rtvs[4], clearAccum);
		context->ClearRenderTargetView(rtvs[5], clearRevealage);
		context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), dsv);

		using globals::features::terrainBlending;
		ID3D11ShaderResourceView* waterDepthSrv = terrainBlending.loaded ? terrainBlending.depthSRVBackup : renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
		context->PSSetShaderResources(120, 1, &waterDepthSrv);
		context->OMSetBlendState(wboitBlendState.get(), nullptr, 0xffffffff);
	}
	else if (settings.Method == OIT_RVO)
	{
		rtvs = { main.RTV, TAAMask.RTV, alphaOnly.RTV };
		uavs = { headerBuffer->uav.get(), colorBuffer->uav.get(), depthBuffer->uav.get() };
		context->OMSetRenderTargetsAndUnorderedAccessViews(3, rtvs.data(), dsv, 3, 3, uavs.data(), nullptr);
	}
	else
	{
		if (headerBuffer->uav == nullptr || nodesBuffer->uav == nullptr) 
		{
			logger::error("Failed to get UAVs for OIT capture pass.");
		}

		rtvs = { main.RTV, TAAMask.RTV, alphaOnly.RTV };
		uavs = { headerBuffer->uav.get(), nodesBuffer->uav.get(), nullptr };
		context->OMSetRenderTargetsAndUnorderedAccessViews(3, rtvs.data(), dsv, 3, 2, uavs.data(), uavcounters);
	}

	context->OMSetDepthStencilState(depthStencilState.get(), 0xFF);
}

template <typename ShaderType, typename ViewType, size_t N>
struct ScopedShaderResource
{
	template <typename... TArgs>
	ScopedShaderResource(ShaderType* , ViewType* (&_views)[N], TArgs&&... _args)
	{
		if constexpr (std::is_same_v<ShaderType, ID3D11PixelShader>)
		{
			if constexpr (std::is_same_v<ViewType, ID3D11ShaderResourceView>)
			{
				globals::d3d::context->PSSetShaderResources(_args..., N, _views);
			} 
			else if constexpr (std::is_same_v<ViewType, ID3D11RenderTargetView>) 
			{
				globals::d3d::context->OMSetRenderTargets(N, _views, _args...);
			}
		}
	}

	~ScopedShaderResource()
	{
		ViewType* views[N] = { 0 };
		if constexpr (std::is_same_v<ShaderType, ID3D11PixelShader>) {
			if constexpr (std::is_same_v<ViewType, ID3D11ShaderResourceView>) {
				globals::d3d::context->PSSetShaderResources(0, N, views);
			} else if constexpr (std::is_same_v<ViewType, ID3D11RenderTargetView>) {
				globals::d3d::context->OMSetRenderTargets(N, views, nullptr);
			}
		}
	}
};

template <typename ShaderType, typename ViewType, size_t N, typename... TArgs>
ScopedShaderResource(ShaderType*, ViewType* (&)[N], TArgs&&...) -> ScopedShaderResource<ShaderType, ViewType, N>;

void OrderIndependentTransparency::EndAlphaGroup()
{
	inAlphaPass = false;
	closeEnough = false;

	logger::debug("End OIT alpha group.");

	TracyD3D11Zone(globals::state->tracyCtx, "OIT Composite");
	ID3D11DeviceContext* context = globals::d3d::context;
	auto* renderer = globals::game::renderer;

	// First unbind resources from UAV in collection phase
	{
		if (settings.Method == OIT_BLENDED) {
			ID3D11RenderTargetView* _rtvs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			context->OMSetRenderTargets(6, _rtvs, nullptr);
		} else {
			ID3D11RenderTargetView* _rtvs[3] = { nullptr, nullptr, nullptr };
			ID3D11UnorderedAccessView* _uavs[3] = { nullptr, nullptr, nullptr };
			context->OMSetRenderTargetsAndUnorderedAccessViews(3, _rtvs, nullptr, 3, 3, _uavs, nullptr);
		}
	}
	{
		ID3D11ShaderResourceView* _srv = nullptr;
		context->PSSetShaderResources(120, 1, &_srv);
	}

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& alpha = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA];
	auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& mainDepthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_WATER_COPY];

	using globals::features::terrainBlending;
	ID3D11ShaderResourceView* waterDepthSrv = mainDepth.depthSRV;
	// If we need to write depth in composition pass, we need to use the copied depth
	if (settings.WriteDepth) waterDepthSrv =  mainDepthCopy.depthSRV;
	// Water depth is rendered at kMain after water pass
	// But mainDepth.depthSRV was REDIRECTED by terrain blending to its own copy (for UAV access)
	// At this point, we need to access the actual main depth as SRV
	else if (terrainBlending.loaded) waterDepthSrv = terrainBlending.depthSRVBackup;

	ID3D11PixelShader* shader = nullptr;
	switch (settings.Method) {
	case Method::OIT_AT:
		shader = psAT.get();
		break;
	case Method::OIT_BLENDED:
		shader = psBlend.get();
		break;
	case Method::OIT_VISUALIZE:
		shader = psVisualize.get();
		break;
	case Method::OIT_RVO:
		shader = psROV.get();
		break;
	}

	// Set up viewport for fullscreen rendering
	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = screenSize.x;
	viewport.Height = screenSize.y;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Set up Input Assembler for fullscreen triangle
	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set up vertex shader
	context->VSSetShader(globals::features::upscaling.GetUpscaleVS(), nullptr, 0);

	// Set up rasterizer and blend states
	context->RSSetState(globals::features::upscaling.upscaleRasterizerState.get());
	context->OMSetBlendState(resolveBlendState.get(), nullptr, 0xffffffff);
	context->OMSetDepthStencilState(resolveDepthStencilState.get(), 1);

	// Set up pixel shader resources
	ID3D11RenderTargetView* _rtvs[2] = { main.RTV, alpha.RTV };
	ID3D11DepthStencilView* _dsv = nullptr;
	if (settings.WriteDepth) {
		_dsv = mainDepth.views[0];
	}
	ScopedShaderResource rtvGuard(shader, _rtvs, _dsv);

	context->PSSetShader(shader, nullptr, 0);

	if (settings.Method == Method::OIT_BLENDED)
	{
		ID3D11ShaderResourceView* srvs[4] = {
			accumalationBuffer->srv.get(),
			accumalationWaterBuffer->srv.get(),
			revealageBuffer->srv.get(),
			waterDepthSrv
		};
		ScopedShaderResource srvGuard(shader, srvs, 0);
		context->Draw(3, 0);
	}
	else
	{
		ID3D11ShaderResourceView* srvs[4] = { waterDepthSrv, headerBuffer->srv.get(), nullptr, nullptr };
		if (settings.Method == OIT_RVO)
		{
			srvs[2] = colorBuffer->srv.get();
			srvs[3] = depthBuffer->srv.get();
		}
		else
		{
			srvs[2] = nodesBuffer->srv.get();
		}
		ScopedShaderResource srvGuard(shader, srvs, 0);
		context->Draw(3, 0);
	}

	context->PSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

void OrderIndependentTransparency::BeginWater()
{
	if (settings.Method == Method::OIT_DISABLED)
		return;
	auto* renderer = globals::game::renderer;
	ID3D11DeviceContext* context = globals::d3d::context;
	auto& alpha = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA];
	ID3D11ShaderResourceView* srv[] = { alpha.SRV };
	context->PSSetShaderResources(66, ARRAYSIZE(srv), srv);
}

void OrderIndependentTransparency::EndWater()
{
	ID3D11DeviceContext* context = globals::d3d::context;
	ID3D11ShaderResourceView* srv[] = { nullptr };
	context->PSSetShaderResources(66, ARRAYSIZE(srv), srv);
}

void Main_RenderWorld_RenderTransparency::thunk(RE::BSShaderAccumulator* accumulator, uint32_t flags)
{
	using namespace std::chrono;
	using clock = high_resolution_clock;
	auto& oit = globals::features::orderIndependentTransparency;
	auto& hdr = globals::features::hdrDisplay;

	hdr.EnableCustomBlending(true);

	bool began = false;
	auto passBegin = clock::now();
	if (globals::shaderCache->IsEnabled() && globals::state->inWorld && oit.loaded && oit.settings.Method != OrderIndependentTransparency::Method::OIT_DISABLED)
	{
		began = true;
		// Override the pixel shaders & OM state for OIT
		oit.BeginAlphaGroup();
	}

	// Render the alpha blending geometries
	{
		TracyD3D11Zone(globals::state->tracyCtx, "Transparency");
		(*func)(accumulator, flags);
	}
	auto passEnd = clock::now();
	oit.passTime = duration_cast<duration<float, std::milli>>(passEnd - passBegin).count();

	if (began)
	{
		// OIT resolve and blend in image space
		oit.EndAlphaGroup();
		oit.compositeTime = duration_cast<duration<float, std::milli>>(clock::now() - passEnd).count();
	} else {
		oit.compositeTime = 0;
	}
	hdr.EnableCustomBlending(false);
}
