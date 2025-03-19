#include "HDR.h"

#include "PCH.h"

#include "Buffer.h"
#include "State.h"
#include "Util.h"

#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <imgui.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDR::Settings,
	displayPeakBrightness,
	gameBrightness,
	uiBrightness);

void HDR::DrawSettings()
{
	ImGui::Checkbox("HDR Enabled", &settings.enabled);

	if (ImGui::Button("Reset HDR Settings", { -1, 0 })) {
		settings.displayPeakBrightness = 400;
		settings.gameBrightness = 200;
		settings.uiBrightness = 200;
	}

	ImGui::SliderInt("Display Peak Brightness (nits)", (int*)&settings.displayPeakBrightness, 400, 10000);
	ImGui::SliderInt("Game Brightness (nits)", (int*)&settings.gameBrightness, 100, 500);
	ImGui::SliderInt("UI Brightness (nits)", (int*)&settings.uiBrightness, 100, 500);

	UpdateHDRData();
}

void HDR::SaveSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	o_json = settings;
}

void HDR::LoadSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	settings = o_json;
}

void HDR::RestoreDefaultSettings()
{
	settings = {};
}

float4 HDR::GetHDRData() const
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
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.RTV->GetDesc(&rtvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;

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
}

void HDR::DestroyResources() const
{
	hdrTexture->srv = nullptr;
	hdrTexture->uav = nullptr;
	hdrTexture->rtv = nullptr;
	hdrTexture->resource = nullptr;
	delete hdrTexture;

	outputTexture->srv = nullptr;
	outputTexture->uav = nullptr;
	outputTexture->rtv = nullptr;
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
