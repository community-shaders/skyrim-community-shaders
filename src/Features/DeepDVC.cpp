#include "DeepDVC.h"

#include "../Globals.h"
#include "Upscaling.h"

#include "RE/B/BSShaderRenderTargets.h"
#include "RE/R/Renderer.h"

// NVIDIA Deep Learning Video Clarity (DeepDVC)
// AI-based post-process filter that significantly enhances color vibrance and
// combats overexposure. Works well alongside traditional post-processing
// (tested with jiayev/pp branch: greatly improves dark-area detail without
// blowing out the overall image).
// Run after tone mapping and before film grain when film grain exists.
//
// Currently VR-only due to lack of an SE/AE test client; flat-screen
// adaptation should be straightforward.

bool DeepDVC::IsSupported() const
{
	auto& streamline = globals::features::upscaling.streamline;
	return globals::game::isVR && streamline.featureDeepDVC;
}

bool DeepDVC::IsInMenu() const
{
	return IsSupported();
}

void DeepDVC::RestoreDefaultSettings()
{
	settings = Settings();
}

void DeepDVC::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.mode = std::clamp(settings.mode, 0u, 1u);
	settings.intensity = std::clamp(settings.intensity, 0.0f, 1.0f);
	settings.saturationBoost = std::clamp(settings.saturationBoost, 0.0f, 1.0f);
}

void DeepDVC::SaveSettings(json& o_json)
{
	o_json = settings;
}

void DeepDVC::DrawSettings()
{
	if (ImGui::TreeNodeEx("DeepDVC Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		const char* modes[] = { "Off", "On" };
		ImGui::SliderInt("Mode", (int*)&settings.mode, 0, 1, modes[settings.mode]);

		if (settings.mode == 1) {
			if (missingInput) {
				ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "Input render target not found.");
			}

			ImGui::SliderFloat("Intensity", &settings.intensity, 0.0f, 1.0f, "%.2f");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Controls how strong or subtle the filter effect will be on an image.");
			}

			ImGui::SliderFloat("Saturation Boost", &settings.saturationBoost, 0.0f, 1.0f, "%.2f");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("This feature provides RTX Dynamic Vibrance Control, using AI to enhance digital vibrance and adjust color saturation adaptively based on game content, making them more vibrant and eye-catching.");
			}
		}

		ImGui::TreePop();
	}
}

void DeepDVC::Evaluate()
{
	if (!IsSupported())
		return;

	auto& upscaling = globals::features::upscaling;
	if (!upscaling.streamline.initialized)
		return;

	if (settings.mode == 0) {
		missingInput = false;
		return;
	}

	auto context = globals::d3d::context;
	auto device = globals::d3d::device;
	auto viewport = upscaling.streamline.viewport;

	if (!context || !device)
		return;

	if (!upscaling.streamline.EnsureFrameToken())
		return;

	auto* frameToken = upscaling.streamline.frameToken;
	if (!frameToken)
		return;

	ID3D11Texture2D* targetBuffer = nullptr;

	if (auto renderer = globals::game::renderer) {
		auto& rt = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY];
		targetBuffer = rt.texture;
	}

	if (!targetBuffer) {
		missingInput = true;
		return;
	}

	missingInput = false;

	D3D11_TEXTURE2D_DESC desc;
	targetBuffer->GetDesc(&desc);

	sl::Extent extent{};
	extent.width = desc.Width;
	extent.height = desc.Height;

	if (proxyBuffer) {
		D3D11_TEXTURE2D_DESC proxyDesc;
		proxyBuffer->GetDesc(&proxyDesc);
		if (proxyDesc.Width != desc.Width || proxyDesc.Height != desc.Height || proxyDesc.Format != desc.Format) {
			proxyBuffer->Release();
			proxyBuffer = nullptr;
		}
	}

	if (!proxyBuffer) {
		D3D11_TEXTURE2D_DESC proxyDesc = desc;
		proxyDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		proxyDesc.Usage = D3D11_USAGE_DEFAULT;
		proxyDesc.CPUAccessFlags = 0;
		proxyDesc.MiscFlags = 0;

		HRESULT hr = device->CreateTexture2D(&proxyDesc, nullptr, &proxyBuffer);
		if (FAILED(hr)) {
			logger::error("[DeepDVC] Failed to create proxy buffer. HR = {:x}", (uint32_t)hr);
			return;
		}
		logger::info("[DeepDVC] Created UAV-compatible proxy buffer {}x{}", desc.Width, desc.Height);
	}

	if (!proxyBuffer || !targetBuffer) {
		logger::error("[DeepDVC] Invalid buffer pointers for resource copy");
		return;
	}
	context->CopyResource(proxyBuffer, targetBuffer);

	sl::Resource colorOut = { sl::ResourceType::eTex2d, proxyBuffer, nullptr, nullptr, 0 };
	sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &extent };

	sl::ResourceTag inputs[] = { colorOutTag };
	if (!upscaling.streamline.slSetTag) {
		logger::warn("[DeepDVC] slSetTag not available");
		return;
	}
	upscaling.streamline.slSetTag(viewport, inputs, _countof(inputs), context);

	sl::DeepDVCOptions options{};
	options.mode = (sl::DeepDVCMode)settings.mode;
	options.intensity = settings.intensity;
	options.saturationBoost = settings.saturationBoost;

	if (upscaling.streamline.slDeepDVCSetOptions) {
		sl::Result res = upscaling.streamline.slDeepDVCSetOptions(viewport, options);
		if (res != sl::Result::eOk) {
			logger::warn("[DeepDVC] slDeepDVCSetOptions failed: {}", magic_enum::enum_name(res));
		}
	}

	const sl::BaseStructure* inputsEval[] = { &viewport };
	if (upscaling.streamline.slEvaluateFeature) {
		sl::Result res = upscaling.streamline.slEvaluateFeature(sl::kFeatureDeepDVC, *frameToken, inputsEval, _countof(inputsEval), context);
		if (res != sl::Result::eOk) {
			static int failCount = 0;
			if (failCount < 100) {
				logger::warn("[DeepDVC] slEvaluateFeature failed: {}", magic_enum::enum_name(res));
				failCount++;
			}
		}
	}

	if (proxyBuffer && targetBuffer) {
		context->CopyResource(targetBuffer, proxyBuffer);
	}
}

// ── vtable hook: run DeepDVC after tonemap ──────────────────────

namespace
{
	template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType>
	struct TonemapHook
	{
		static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
		{
			func(imageSpaceShader, shape, param);
			globals::features::deepDVC.Evaluate();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void DeepDVC::PostPostLoad()
{
	if (!globals::game::isVR)
		return;

	stl::write_vfunc<0x1,
		TonemapHook<RE::ImageSpaceManager::ISHDRTonemapBlendCinematic>>(
		RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[3]);
	stl::write_vfunc<0x1,
		TonemapHook<RE::ImageSpaceManager::ISHDRTonemapBlendCinematicFade>>(
		RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[3]);

	logger::info("[DeepDVC] Tonemap hooks installed");
}
