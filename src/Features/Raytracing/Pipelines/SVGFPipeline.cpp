#include "SVGFPipeline.h"

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
	auto createTexture2D = [&](DXGI_FORMAT format, uint bindFlags) {
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = size.x;
		texDesc.Height = size.y;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = bindFlags;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto texture2D = eastl::make_unique<Texture2D>(texDesc);

		if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
			texture2D->CreateSRV(srvDesc);

		if (bindFlags & D3D11_BIND_UNORDERED_ACCESS)
			texture2D->CreateUAV(uavDesc);

		return texture2D;
	};

	temporalTexture = createTexture2D(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
	historyTexture = createTexture2D(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE);
	varianceTexture = createTexture2D(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

	momentsTexture = createTexture2D(DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
	historyMomentsTexture = createTexture2D(DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_SHADER_RESOURCE);

	historyNormalsTexture = createTexture2D(DXGI_FORMAT_R16G16B16A16_SNORM, D3D11_BIND_SHADER_RESOURCE);
}

void SVGFPipeline::Denoise(ID3D11DeviceContext4* context, uint2 renderSize, Settings settings, WrappedResource* normalRoughness, WrappedResource* colorResource, const bool diffuse) const
{
	const uint2 dispatchCount = { DivideRoundUp(renderSize.x, 8u), DivideRoundUp(renderSize.y, 8u) };

	frameData->InvMaxAccumulatedFrames = 1.0f / (static_cast<float>(settings.MaxAccumulatedFrames) + 1.0f);
	frameData->AtrousIterations = settings.AtrousIterations;
	frameData->ColorPhi = settings.ColorPhi;
	frameData->NormalPhi = settings.NormalPhi;

	frameBuffer->Update(frameData.get(), sizeof(SVGF));

	auto cb = frameBuffer->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	std::array<ID3D11ShaderResourceView*, 7> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { nullptr };

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	auto renderer = globals::game::renderer;
	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	// temporal filter
	uavs.at(0) = temporalTexture->uav.get();
	uavs.at(1) = momentsTexture->uav.get();
	srvs.at(0) = historyTexture->srv.get();
	srvs.at(1) = motion.SRV;
	srvs.at(2) = normalRoughness->srv;
	srvs.at(3) = colorResource->srv;
	srvs.at(4) = depth.depthSRV;
	srvs.at(5) = historyMomentsTexture->srv.get();
	srvs.at(6) = historyNormalsTexture->srv.get();

	context->CSSetShaderResources(0, 7, srvs.data());
	context->CSSetUnorderedAccessViews(0, 2, uavs.data(), nullptr);
	context->CSSetShader(temporalCS.get(), nullptr, 0);

	context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);
	resetViews();

	context->CopyResource(historyMomentsTexture->resource.get(), momentsTexture->resource.get());

	// variance filter
	uavs.at(0) = varianceTexture->uav.get();
	srvs.at(0) = historyTexture->srv.get();
	srvs.at(1) = momentsTexture->srv.get();
	srvs.at(2) = normalRoughness->srv;
	srvs.at(3) = temporalTexture->srv.get();
	srvs.at(4) = depth.depthSRV;

	context->CSSetShaderResources(0, 5, srvs.data());
	context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
	context->CSSetShader(varianceCS.get(), nullptr, 0);

	context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);
	resetViews();

	// spatial filter
	for (uint i = 0; i < settings.AtrousIterations; ++i) {
		frameData->AtrousIterations = i;
		frameBuffer->Update(frameData.get(), sizeof(SVGF));

		cb = frameBuffer->CB();
		context->CSSetConstantBuffers(1, 1, &cb);

		uavs.at(0) = (i % 2 == 0) ? colorResource->uav : varianceTexture->uav.get();

		srvs.at(0) = (i % 2 == 0) ? varianceTexture->srv.get() : colorResource->srv;
		srvs.at(2) = normalRoughness->srv;
		srvs.at(4) = depth.depthSRV;

		context->CSSetShaderResources(0, 5, srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
		context->CSSetShader(diffuse ? spatialDiffuseCS.get() : spatialSpecularCS.get(), nullptr, 0);

		context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

		resetViews();
	}

	if (settings.AtrousIterations % 2 == 0) {
		context->CopyResource(colorResource->resource11, varianceTexture->resource.get());
	}

	context->CopyResource(historyNormalsTexture->resource.get(), normalRoughness->resource11);
	context->CopyResource(historyTexture->resource.get(), colorResource->resource11);

	context->CSSetShader(nullptr, nullptr, 0);
}