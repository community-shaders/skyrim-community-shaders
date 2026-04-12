#include "Postprocess.h"

#include "../../State.h"
#include "../../Globals.h"
#include "../DlssEnhancerFeature.h"
#include "../Upscaling.h"
#include "../DLSSperf.h"

#include <cmath>

namespace DlssEnhancer
{
	bool Postprocess::ApplyDlssSharpening(Upscaling& upscaling)
	{
		// Check sharpen mode — skip entirely if set to None
		auto& enhancer = globals::features::dlssEnhancer;
		if (enhancer.GetSharpenMode() == DlssEnhancerFeature::SharpenMode::kNone) {
			return true;
		}

		auto sharpnessSetting = upscaling.GetActiveSharpnessDLSS();
		if (sharpnessSetting <= 0.0f) {
			return true;
		}

		if (!upscaling.sharpenerTexture || !upscaling.sharpenerTexture->uav || !upscaling.sharpenerTexture->resource) {
			logger::error("[DLSSENHANCER] Missing sharpener resources");
			return false;
		}

		auto context = globals::d3d::context;
		auto renderer = globals::game::renderer;
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		if (!main.SRV) {
			logger::error("[DLSSENHANCER] Missing main SRV for sharpening");
			return false;
		}

		float currentSharpness = (-2.0f * sharpnessSetting) + 2.0f;
		currentSharpness = exp2(-currentSharpness);

		// DLSSperf: RCAS must operate on testTexture (3k DisplayRes) instead of kMAIN (1k).
		// Use refraTempTex as temp read source to avoid SRV↔UAV hazard on testTexture.
		auto& dlssPerf = globals::features::dlssPerf;
		if (dlssPerf.IsHookActive() && dlssPerf.GetTestTexture()) {
			auto* refraTempSRV = dlssPerf.GetRefraTempSRV();
			auto* testTextureUAV = dlssPerf.GetTestTextureUAV();
			auto* testTexture = dlssPerf.GetTestTexture();
			auto* refraTempTex = dlssPerf.GetRefraTempTex();

			if (!refraTempSRV || !testTextureUAV || !refraTempTex) {
				logger::error("[DLSSENHANCER] Missing DLSSperf resources for sharpening");
				return false;
			}

			uint32_t dispW = dlssPerf.GetDisplayEyeWidth() * 2;
			uint32_t dispH = dlssPerf.GetDisplayEyeHeight();

			context->CopyResource(refraTempTex, testTexture);
			context->OMSetRenderTargets(0, nullptr, nullptr);
			upscaling.rcas.ApplySharpen(refraTempSRV, testTextureUAV, currentSharpness, dispW, dispH);

			globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
			return true;
		}

		// Normal path: sharpen kMAIN in-place via sharpenerTexture
		ID3D11Resource* mainResource = nullptr;
		main.SRV->GetResource(&mainResource);
		if (!mainResource) {
			logger::error("[DLSSENHANCER] Failed to acquire main resource for sharpening");
			return false;
		}

		context->OMSetRenderTargets(0, nullptr, nullptr);
		upscaling.rcas.ApplySharpen(main.SRV, upscaling.sharpenerTexture->uav.get(), currentSharpness);
		context->CopyResource(mainResource, upscaling.sharpenerTexture->resource.get());
		mainResource->Release();

		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
		return true;
	}
}
