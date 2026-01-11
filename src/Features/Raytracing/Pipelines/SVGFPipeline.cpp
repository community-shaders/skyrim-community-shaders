#include "SVGFPipeline.h"
#include "State.h"

void SVGFPipeline::CompileShaders()
{
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\Denoiser\\SVGF\\TemporalCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		temporalCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\Denoiser\\SVGF\\VarianceCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		varianceCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\Denoiser\\SVGF\\SpatialCS.hlsl", { { "DX11", "" } }, "cs_5_0")); rawPtr)
		spatialDiffuseCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\Denoiser\\SVGF\\SpatialCS.hlsl", { { "DX11", "" }, { "SSRT_SPECULAR", "" } }, "cs_5_0")); rawPtr)
		spatialSpecularCS.attach(rawPtr);
}

void SVGFPipeline::SetupResources()
{
	frameData = eastl::make_unique<SVGF>();

	auto cbDesc = ConstantBufferDesc<SVGF>();
	frameBuffer = eastl::make_unique<ConstantBuffer>(cbDesc);

	CompileShaders();
}

void SVGFPipeline::SetupTextureResources(uint2 size)
{
	// RGBA16
	{
		temporalTexture = CreateTexture2D(size, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

		for (uint i = 0; i < HISTORY_TEXTURES; i++)
			historyTexture[i] = CreateTexture2D(size, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE);

		varianceTexture = CreateTexture2D(size, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
	}

	// RG11B10
	{
		momentsTexture = CreateTexture2D(size, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

		for (uint i = 0; i < HISTORY_TEXTURES; i++)
			historyMomentsTexture[i] = CreateTexture2D(size, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_SHADER_RESOURCE);
	}

	// RG32
	depthLinearTexture = CreateTexture2D(size, DXGI_FORMAT_R32G32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

	historyDepthTexture = CreateTexture2D(size, DXGI_FORMAT_R32G32_FLOAT, D3D11_BIND_SHADER_RESOURCE);

	// RGBA16 SNORM
	historyNormalsTexture = CreateTexture2D(size, DXGI_FORMAT_R16G16B16A16_SNORM, D3D11_BIND_SHADER_RESOURCE);

}

void SVGFPipeline::Denoise(ID3D11DeviceContext4* context, uint2 renderSize, Settings settings, WrappedResource* normalRoughness, WrappedResource* color, const bool diffuse) const
{
	const uint historyIndex = diffuse ? 0 : 1;

	const uint2 dispatchCount = { DivideRoundUp(renderSize.x, 8u), DivideRoundUp(renderSize.y, 8u) };

	frameData->Alpha = 1.0f / static_cast<float>(settings.AlphaFrames);
	frameData->MomentsAlpha = 1.0f / static_cast<float>(settings.MomentsAlphaFrames);
	frameData->AtrousIterations = settings.AtrousIterations;

	frameData->ColorPhi = settings.ColorPhi;
	frameData->NormalPhi = settings.NormalPhi;
	frameData->DepthPhi = settings.DepthPhi / Util::Units::GAME_UNIT_TO_M;

	frameData->DepthThreshold = settings.DepthThreshold / Util::Units::GAME_UNIT_TO_M;
	frameData->NormalThreshold = std::cosf(static_cast<float>(settings.NormalThreshold));
	frameData->HistoryThreshold = settings.HistoryThreshold;

	auto eye = Util::GetCameraData(0);
	float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));

	frameData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

	frameBuffer->Update(frameData.get(), sizeof(SVGF));

	auto cb = frameBuffer->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	cb = globals::state->sharedDataCB->CB();
	context->CSSetConstantBuffers(5, 1, &cb);

	cb = *globals::game::perFrame.get();
	context->CSSetConstantBuffers(12, 1, &cb);

	std::array<ID3D11ShaderResourceView*, 8> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 3> uavs = { nullptr };

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	auto renderer = globals::game::renderer;
	auto motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	// temporal filter
	srvs.at(0) = historyTexture[historyIndex]->srv.get();
	srvs.at(1) = motion.SRV;
	srvs.at(2) = normalRoughness->srv;
	srvs.at(3) = color->srv;
	srvs.at(4) = historyMomentsTexture[historyIndex]->srv.get();
	srvs.at(5) = historyDepthTexture->srv.get();
	srvs.at(6) = historyNormalsTexture->srv.get();
	srvs.at(7) = depth.depthSRV;

	uavs.at(0) = temporalTexture->uav.get();
	uavs.at(1) = momentsTexture->uav.get();
	uavs.at(2) = depthLinearTexture->uav.get();

	context->CSSetShaderResources(0, 7, srvs.data());
	context->CSSetUnorderedAccessViews(0, 3, uavs.data(), nullptr);
	context->CSSetShader(temporalCS.get(), nullptr, 0);

	context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);
	resetViews();

	context->CopyResource(historyMomentsTexture[historyIndex]->resource.get(), momentsTexture->resource.get());

	// variance filter
	if (settings.Variance) {
		srvs.at(0) = historyTexture[historyIndex]->srv.get();
		srvs.at(1) = momentsTexture->srv.get();
		srvs.at(2) = normalRoughness->srv;
		srvs.at(3) = temporalTexture->srv.get();
		srvs.at(4) = depthLinearTexture->srv.get();

		uavs.at(0) = varianceTexture->uav.get();

		context->CSSetShaderResources(0, 5, srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
		context->CSSetShader(varianceCS.get(), nullptr, 0);

		context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);
		resetViews();
	} else {
		context->CopyResource(varianceTexture->resource.get(), temporalTexture->resource.get());
	}

	// spatial filter
	if (settings.Spatial) {
		for (uint i = 0; i < settings.AtrousIterations; ++i) {
			frameData->AtrousIterations = 1 << i;
			frameBuffer->Update(frameData.get(), sizeof(SVGF));

			cb = frameBuffer->CB();
			context->CSSetConstantBuffers(1, 1, &cb);

			srvs.at(0) = (i % 2 == 0) ? varianceTexture->srv.get() : color->srv;
			srvs.at(2) = normalRoughness->srv;
			srvs.at(4) = depthLinearTexture->srv.get();

			uavs.at(0) = (i % 2 == 0) ? color->uav : varianceTexture->uav.get();

			context->CSSetShaderResources(0, 5, srvs.data());
			context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
			context->CSSetShader(diffuse ? spatialDiffuseCS.get() : spatialSpecularCS.get(), nullptr, 0);

			context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

			resetViews();
		}

		if (settings.AtrousIterations % 2 == 0) {
			context->CopyResource(color->resource11, varianceTexture->resource.get());
		}
	} else {
		context->CopyResource(color->resource11, varianceTexture->resource.get());
	}

	context->CopyResource(historyDepthTexture->resource.get(), depthLinearTexture->resource.get());
	//context->CopyResource(historyDepthTexture->resource.get(), depth.texture);
	context->CopyResource(historyNormalsTexture->resource.get(), normalRoughness->resource11);
	context->CopyResource(historyTexture[historyIndex]->resource.get(), color->resource11);

	context->CSSetShader(nullptr, nullptr, 0);
}