#include "Params.h"

#include "../../State.h"
#include "../../Util.h"
#include "../DlssEnhancerFeature.h"
#include "../DLSSperf.h"
#include "../Upscaling.h"

namespace DlssEnhancer
{
	VRDlssParams VRDlssParams::Resolve(
		ID3D11Resource* upscalingTexture,
		ID3D11Resource* depth,
		ID3D11Resource* reactive,
		ID3D11Resource* transparency,
		ID3D11Resource* mvec)
	{
		VRDlssParams p{};

		// ── Dimensions ──
		auto screenSize = globals::state->screenSize;

		// When DLSSperf hook is active, screenSize is polluted (= RenderRes).
		// DLSS output dimensions must use real HMD DisplayRes.
		auto& dlssPerf = globals::features::dlssPerf;
		auto displaySize = dlssPerf.IsHookActive()
			                   ? dlssPerf.GetDisplayScreenSize()
			                   : screenSize;

		auto renderSize = Util::ConvertToDynamic(screenSize);
		p.renderW      = (uint32_t)renderSize.x;
		p.renderH      = (uint32_t)renderSize.y;
		p.eyeWidthIn   = (uint32_t)(renderSize.x / 2);
		p.eyeHeightIn  = (uint32_t)renderSize.y;
		p.eyeWidthOut  = (uint32_t)(displaySize.x / 2);
		p.eyeHeightOut = (uint32_t)displaySize.y;

		// ── Textures ──
		// colorSrc = kMAIN (input, always correct)
		// colorDst = where DLSS output goes: testTexture when DLSSperf active, otherwise kMAIN
		p.colorSrc = upscalingTexture;
		p.colorDst = upscalingTexture;
		p.colorDstUAV = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].UAV;
		if (dlssPerf.IsHookActive()) {
			if (auto* testTex = dlssPerf.GetTestTexture()) {
				p.colorDst = testTex;
				p.colorDstUAV = dlssPerf.GetTestTextureUAV();
			}
		}

		p.depthTexture    = depth;
		p.reactiveMask    = reactive;
		p.transparencyMask = transparency;
		p.motionVectors   = mvec;

		// ── Mode & subrect ──
		auto& enhancer = globals::features::dlssEnhancer;
		p.mode     = enhancer.GetDlssMode();
		p.leftUV   = enhancer.subrectController.GetLeftEyeUV();
		p.rightUV  = enhancer.subrectController.GetRightEyeUV();
		p.isFullEye = (p.leftUV.w >= 0.999f && p.leftUV.h >= 0.999f);

		// ── Jitter ──
		// ConfigureUpscaling already computes correct DLSS jitter for both
		// normal and DLSSperf cases (using real render→display ratio).
		auto& upscaling = globals::features::upscaling;
		p.jitterX = upscaling.jitter.x;
		p.jitterY = upscaling.jitter.y;

		return p;
	}
}
