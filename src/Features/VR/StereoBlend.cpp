#include "Features/VR.h"

#include "Features/DynamicCubemaps.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "State.h"

void VR::ClearShaderCache()
{
	stereoBlendCS = nullptr;
	stereoBlendDebugBackCheckCS = nullptr;
	stereoBlendDebugBlendWeightCS = nullptr;
	stereoBlendDebugEdgeDetectionCS = nullptr;
}

bool VR::AnyScreenSpaceEffectLoaded()
{
	return globals::features::screenSpaceGI.loaded ||
	       globals::features::dynamicCubemaps.loaded ||
	       globals::features::screenSpaceShadows.loaded;
}

void VR::DrawStereoBlend()
{
	if (!REL::Module::IsVR() || !settings.EnableStereoBlend || !stereoBlendCS || !stereoBlendCopyTex || !stereoBlendCB)
		return;

	if (!AnyScreenSpaceEffectLoaded() && !globals::state->IsDeveloperMode())
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

	stereoBlendCB->Update(cbData);
	auto cbPtr = stereoBlendCB->CB();

	ID3D11ShaderResourceView* srvs[2]{ stereoBlendCopyTex->srv.get(), depthSRV };
	ID3D11UnorderedAccessView* uavs[1]{ main.UAV };

	ID3D11ComputeShader* activeCS = stereoBlendCS.get();
	if (settings.StereoBlendDebugMode == 1 && stereoBlendDebugBackCheckCS)
		activeCS = stereoBlendDebugBackCheckCS.get();
	else if (settings.StereoBlendDebugMode == 2 && stereoBlendDebugBlendWeightCS)
		activeCS = stereoBlendDebugBlendWeightCS.get();
	else if (settings.StereoBlendDebugMode == 3 && stereoBlendDebugEdgeDetectionCS)
		activeCS = stereoBlendDebugEdgeDetectionCS.get();

	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	context->CSSetShader(activeCS, nullptr, 0);

	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

	// Cleanup
	srvs[0] = nullptr;
	srvs[1] = nullptr;
	uavs[0] = nullptr;
	cbPtr = nullptr;
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShader(nullptr, nullptr, 0);

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}
