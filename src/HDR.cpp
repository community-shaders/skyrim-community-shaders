#include "C:/Users/micro/source/repos/skyrim-community-shaders/build/ALL/CMakeFiles/CommunityShaders.dir/Debug/cmake_pch.hxx"
#include "HDR.h"

void HDR::DrawSettings()
{
	ImGui::Checkbox("HDR Enabled", &settings.enabled);

	if (ImGui::Button("Reset HDR Settings", { -1, 0 })) {
		settings.displayPeakBrightness = 400;
		settings.gameBrightness = 200;
		settings.uiBrightness = 200;
	}

	ImGui::SliderInt("Display Peak Brightness (nits)", &settings.displayPeakBrightness, 400, 10000);
	ImGui::SliderInt("Game Brightness (nits)", &settings.gameBrightness, 100, 400);
	ImGui::SliderInt("UI Brightness (nits)", &settings.uiBrightness, 100, 400);
}

float4 HDR::GetHDRData()
{
	float4 data;
	data.x = static_cast<float>(settings.enabled);
	data.y = static_cast<float>(settings.displayPeakBrightness);
	data.z = static_cast<float>(settings.gameBrightness);
	data.w = static_cast<float>(settings.uiBrightness);
	return data;
}

void HDR::SetupResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.RTV->GetDesc(&rtvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	uiTexture = new Texture2D(texDesc);
	uiTexture->CreateSRV(srvDesc);
	uiTexture->CreateRTV(rtvDesc);
	uiTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	hdrTexture = new Texture2D(texDesc);
	hdrTexture->CreateSRV(srvDesc);
	hdrTexture->CreateRTV(rtvDesc);
	hdrTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	srvDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	outputTexture = new Texture2D(texDesc);
	outputTexture->CreateSRV(srvDesc);
	outputTexture->CreateRTV(rtvDesc);
	outputTexture->CreateUAV(uavDesc);

	hdrDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDataCB>());
}

void HDR::ClearShaderCache()
{
	if (hdrOutputCS) {
		hdrOutputCS->Release();
		hdrOutputCS = nullptr;
	}
}

ID3D11ComputeShader* HDR::GetHDROutputCS()
{
	if (!hdrOutputCS) {
		logger::debug("Compiling HDROutputCS.hlsl");
		hdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDROutputCS.hlsl", {}, "cs_5_0"));
	}
	return hdrOutputCS;
}

void HDR::HDROutput()
{
	static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto context = globals::d3d::context;
	static auto& swapChain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	context->OMSetRenderTargets(0, nullptr, nullptr); // Unbind all bound render targets

	{
		HDRDataCB data = { GetHDRData() };
		hdrDataCB->Update(data);
	}

	{
		ID3D11ShaderResourceView* srvs[2]{ hdrTexture->srv.get(), uiTexture->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[1]{ outputTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbs[1]{ hdrDataCB->CB() };
		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		context->CSSetShader(GetHDROutputCS(), nullptr, 0);

		auto dispatchCount = Util::GetScreenDispatchCount(false);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		srvs[0] = nullptr;
		srvs[1] = nullptr;
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		uavs[0] = nullptr;
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		cbs[0] = nullptr;
		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);
	}

	// Copy fake swapchain into real one
	context->CopyResource(swapChainResource, outputTexture->resource.get());

	// Reset UI buffer
	float clearColor[4] = { 0, 0, 0, 0 };
	context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);

	// Set render target for compatibility
	context->OMSetRenderTargets(1, &swapChain.RTV, nullptr);
}
