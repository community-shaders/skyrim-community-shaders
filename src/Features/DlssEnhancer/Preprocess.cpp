#include "Preprocess.h"

#include "../../Deferred.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"

namespace
{
	ID3D11ComputeShader* GetEnhancerEncodeTexturesCS(Upscaling& upscaling, Upscaling::UpscaleMethod upscaleMethod)
	{
		uint methodIndex = (uint)upscaleMethod;
		if (!upscaling.encodeTexturesCS[methodIndex]) {
			std::vector<std::pair<const char*, const char*>> defines;
			defines.push_back({ "DLSS", "" });

			upscaling.encodeTexturesCS[methodIndex].attach((ID3D11ComputeShader*)Util::CompileShader(
				L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
		}

		return upscaling.encodeTexturesCS[methodIndex].get();
	}
}

namespace DlssEnhancer
{
	bool Preprocess::EncodeUpscalingTextures(Upscaling& upscaling)
	{
		auto upscaleMethod = upscaling.GetUpscaleMethod();
		if (upscaleMethod != Upscaling::UpscaleMethod::kDLSS) {
			logger::error("[DLSSENHANCER] Non-DLSS preprocess path is disabled; method={}", (int)upscaleMethod);
			return false;
		}

		auto state = globals::state;
		auto context = globals::d3d::context;
		auto renderer = globals::game::renderer;

		if (!upscaling.upscalingDataCB || !upscaling.reactiveMaskTexture || !upscaling.transparencyCompositionMaskTexture) {
			logger::error("[DLSSENHANCER] Missing preprocess resources");
			return false;
		}

		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
		auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
		auto& normals = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[2]];
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto dispatchCount = Util::GetScreenDispatchCount(true);

		state->BeginPerfEvent("DLSSENHANCER Encode Upscaling Textures");

		auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);
		Upscaling::UpscalingDataCB upscalingData{};
		upscalingData.trueSamplingDim = renderSize;
		upscaling.upscalingDataCB->Update(upscalingData);

		auto upscalingBuffer = upscaling.upscalingDataCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

		ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, motionVector.SRV, depth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3] = {
			upscaling.reactiveMaskTexture->uav.get(),
			upscaling.transparencyCompositionMaskTexture->uav.get(),
			upscaleMethod == Upscaling::UpscaleMethod::kDLSS ? upscaling.motionVectorCopyTexture->uav.get() : nullptr
		};
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* cs = GetEnhancerEncodeTexturesCS(upscaling, upscaleMethod);
		if (!cs) {
			state->EndPerfEvent();
			logger::error("[DLSSENHANCER] Failed to get encode compute shader");
			return false;
		}

		context->CSSetShader(cs, nullptr, 0);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		ID3D11ShaderResourceView* nullViews[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
		ID3D11UnorderedAccessView* nullUavs[3] = { nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUavs), nullUavs, nullptr);
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);
		context->CSSetShader(nullptr, nullptr, 0);

		state->EndPerfEvent();
		return true;
	}
}
