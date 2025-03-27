#include "HDR.h"

#include "PCH.h"

#include "Buffer.h"
#include "State.h"
#include "Upscaling.h"
#include "Util.h"

#include <dxgi1_4.h>
#include <imgui.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDR::Settings,
	displayPeakBrightness,
	gameBrightness,
	uiBrightness,
	enableHDR);

void HDR::DrawSettings()
{
	ImGui::Text("Toggling this setting requires a restart to work correctly!");
	ImGui::Checkbox("HDR Enabled", &enabledSaveLater);

	if (settings.enableHDR != enabledSaveLater) {
		ImGui::TextColored({ 1, 0, 0, 1 }, "Warning: This setting will only apply after saving and restarting!");
	}

	if (ImGui::Button("Reset HDR Settings", { -1, 0 })) {
		settings.displayPeakBrightness = 1000;
		settings.gameBrightness = 400;
		settings.uiBrightness = 400;
	}

	ImGui::SliderInt("Display Peak Brightness (nits)", (int*)&settings.displayPeakBrightness, 400, 10000);
	ImGui::SliderInt("Game Brightness (nits)", (int*)&settings.gameBrightness, 100, 500);

	ImGui::BeginDisabled();
	ImGui::Text("Setting UI brightness is currently not supported.");
	ImGui::SliderInt("UI Brightness (nits)", (int*)&settings.uiBrightness, 100, 500);
	ImGui::EndDisabled();

	UpdateHDRData();
}

void HDR::SaveSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	auto settingsCopy = settings;
	settingsCopy.enableHDR = enabledSaveLater;
	o_json = settingsCopy;
}

void HDR::LoadSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	settings = o_json;
	enabledSaveLater = settings.enableHDR;
}

void HDR::RestoreDefaultSettings()
{
	settings = {};
}

float4 HDR::GetHDRData() const
{
	float4 data;
	data.x = static_cast<float>(settings.enableHDR);
	data.y = static_cast<float>(settings.displayPeakBrightness);
	data.z = static_cast<float>(settings.gameBrightness);
	data.w = static_cast<float>(settings.uiBrightness);
	return data;
}

void HDR::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	hdrTexture = new Texture2D(texDesc);
	hdrTexture->CreateSRV(srvDesc);
	hdrTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	outputTexture = new Texture2D(texDesc);
	outputTexture->CreateSRV(srvDesc);
	outputTexture->CreateUAV(uavDesc);

	hdrDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDataCB>());
	UpdateHDRData();
}

void HDR::ApplyHDR()
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& swapChainBuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	ID3D11Resource* swapChainBufferResource;
	swapChainBuffer.SRV->GetResource(&swapChainBufferResource);

	auto dispatchCount = Util::GetScreenDispatchCount(false);

	state->BeginPerfEvent("HDR");

	{
		{
			ID3D11ShaderResourceView* views[1] = { hdrTexture->srv.get() };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { outputTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			ID3D11Buffer* cbs[1]{ hdrDataCB->CB() };
			context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

			context->CSSetShader(GetHDROutputCS(), nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		ID3D11Buffer* cbs[1]{ nullptr };
		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);
	}

	state->EndPerfEvent();

	context->CopyResource(swapChainBufferResource, outputTexture->resource.get());
}

void HDR::DestroyResources() const
{
	hdrTexture->srv = nullptr;
	hdrTexture->uav = nullptr;
	hdrTexture->resource = nullptr;
	delete hdrTexture;

	outputTexture->srv = nullptr;
	outputTexture->uav = nullptr;
	outputTexture->resource = nullptr;
	delete outputTexture;
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

void HDR::UpdateHDRData() const
{
	HDRDataCB data = { GetHDRData() };
	hdrDataCB->Update(data);
}
