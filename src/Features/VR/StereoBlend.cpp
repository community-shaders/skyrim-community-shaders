#include "Features/VR.h"

#include "Deferred.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "State.h"
#include "Utils/D3D.h"

void VR::ClearShaderCache()
{
	stereoBlendCS = nullptr;
	stereoBlendDebugBackCheckCS = nullptr;
	stereoBlendDebugBlendWeightCS = nullptr;
	stereoBlendDebugEdgeDetectionCS = nullptr;
	stereoBlendOverwriteCS = nullptr;
	stereoOpt.ClearShaderCache();
}

bool VR::AnyScreenSpaceEffectLoaded()
{
	return globals::features::screenSpaceGI.loaded ||
	       globals::features::dynamicCubemaps.loaded ||
	       globals::features::screenSpaceShadows.loaded;
}

void VR::DrawStereoBlend()
{
	bool vrStereoOptActive = IsStereoOptimizationCullingReady();

	if (!REL::Module::IsVR() || !stereoBlendCopyTex || !stereoBlendCB)
		return;

	if (!vrStereoOptActive && (!settings.EnableStereoBlend || !stereoBlendCS))
		return;

	if (!vrStereoOptActive && !AnyScreenSpaceEffectLoaded() && !globals::state->IsDeveloperMode())
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VR Stereo Blend");

	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("VR Stereo Blend");

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto* depthSRV = Util::GetCurrentSceneDepthSRV();

	// Copy main color to read-only texture to avoid read/write race between eyes
	context->CopyResource(stereoBlendCopyTex->resource.get(), main.texture);

	auto dispatchCount = Util::GetScreenDispatchCount(true);
	float2 resolution = Util::ConvertToDynamic(globals::state->screenSize);

	StereoBlendCB cbData{};
	cbData.FrameDim[0] = resolution.x;
	cbData.FrameDim[1] = resolution.y;
	cbData.RcpFrameDim[0] = 1.0f / resolution.x;
	cbData.RcpFrameDim[1] = 1.0f / resolution.y;
	cbData.DepthSigma = settings.StereoBlendDepthSigma;
	cbData.MaxBlendFactor = settings.StereoBlendMaxFactor;
	cbData.ColorDiffThreshold = settings.StereoBlendColorThreshold;

	// Pass debug edge tint from VRStereoOptimizations settings
	if (vrStereoOptActive && globals::features::vr.stereoOpt.settings.debugVisualization)
		cbData.DebugEdgeTint = 0.3f;
	else
		cbData.DebugEdgeTint = 0.0f;

	// Debug mode: 0=normal, 1=depth map diagnostic, 2=full blend depth visualizer
	if (vrStereoOptActive && globals::features::vr.stereoOpt.settings.debugDepthMap)
		cbData.DebugMode = 1u;
	else if (vrStereoOptActive && globals::features::vr.stereoOpt.settings.debugFullBlendDepth)
		cbData.DebugMode = 2u;
	else if (vrStereoOptActive && globals::features::vr.stereoOpt.settings.debugPOMDepth)
		cbData.DebugMode = 3u;
	else
		cbData.DebugMode = 0u;

	cbData.FullBlendDistance = vrStereoOptActive ? globals::features::vr.stereoOpt.settings.fullBlendDistance : 0.0f;
	cbData.POMDepthScale = vrStereoOptActive ? globals::features::vr.stereoOpt.settings.pomDepthScale : 1.0f;

	stereoBlendCB->Update(cbData);
	auto cbPtr = stereoBlendCB->CB();

	auto& motionVectors = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	bool isOverwriteMode = vrStereoOptActive;

	ID3D11ComputeShader* activeCS = stereoBlendCS.get();
	if (vrStereoOptActive) {
		activeCS = stereoBlendOverwriteCS.get();
	} else {
		int effectiveMode = settings.StereoBlendDebugMode;
		if (effectiveMode == 1 && stereoBlendDebugBackCheckCS)
			activeCS = stereoBlendDebugBackCheckCS.get();
		else if (effectiveMode == 2 && stereoBlendDebugBlendWeightCS)
			activeCS = stereoBlendDebugBlendWeightCS.get();
		else if (effectiveMode == 3 && stereoBlendDebugEdgeDetectionCS)
			activeCS = stereoBlendDebugEdgeDetectionCS.get();
	}

	// Save and unbind DSV to avoid SRV/DSV conflict on depth buffer in overwrite mode
	ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* savedDSV = nullptr;
	if (isOverwriteMode) {
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, nullptr);
		for (auto& rtv : savedRTVs) {
			if (rtv)
				rtv->Release();
		}
	}

	ID3D11ShaderResourceView* srvs[2]{ stereoBlendCopyTex->srv.get(), depthSRV };
	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShaderResources(0, 2, srvs);

	if (isOverwriteMode) {
		ID3D11ShaderResourceView* modeSRV = globals::features::vr.stereoOpt.GetModeTextureSRV();
		context->CSSetShaderResources(2, 1, &modeSRV);

		// Bind REFLECTANCE SRV for POM depth offset (stored in .w by Lighting pass)
		auto& reflectanceRT = renderer->GetRuntimeData().renderTargets[REFLECTANCE];
		context->CSSetShaderResources(3, 1, &reflectanceRT.SRV);

		ID3D11UnorderedAccessView* uavs[2]{ main.UAV, motionVectors.UAV };
		context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
	} else {
		ID3D11UnorderedAccessView* uavs[1]{ main.UAV };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	}

	// Bind linear sampler for hardware bilinear color sampling in overwrite mode
	if (isOverwriteMode) {
		if (!stereoBlendLinearSampler) {
			D3D11_SAMPLER_DESC sampDesc = {};
			sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			globals::d3d::device->CreateSamplerState(&sampDesc, stereoBlendLinearSampler.put());
		}
		ID3D11SamplerState* samplers[] = { stereoBlendLinearSampler.get() };
		context->CSSetSamplers(0, 1, samplers);
	}

	context->CSSetShader(activeCS, nullptr, 0);
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

	// Cleanup
	ID3D11ShaderResourceView* nullSRVs[4] = {};
	context->CSSetShaderResources(0, isOverwriteMode ? 4 : 2, nullSRVs);
	ID3D11UnorderedAccessView* nullUAVs[2] = {};
	context->CSSetUnorderedAccessViews(0, isOverwriteMode ? 2 : 1, nullUAVs, nullptr);
	ID3D11Buffer* nullCB = nullptr;
	context->CSSetConstantBuffers(1, 1, &nullCB);
	if (isOverwriteMode) {
		ID3D11SamplerState* nullSampler[] = { nullptr };
		context->CSSetSamplers(0, 1, nullSampler);
	}
	context->CSSetShader(nullptr, nullptr, 0);

	// Restore DSV after CS dispatch in overwrite mode
	if (isOverwriteMode && savedDSV) {
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, nullptr);
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
		for (auto& rtv : savedRTVs) {
			if (rtv)
				rtv->Release();
		}
		savedDSV->Release();
	}

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}
