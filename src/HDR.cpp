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
	enableHDR,
	useAdvancedTonemapping,
	advOperator,
	advExposure,
	advMaxNits,
	advPaperWhite);

void HDR::DrawSettings()
{
	const char* operators[] = {
		"None",
		"Saturate",
		"Reinhard",
		"Reinhard-Jodie",
		"ACES Filmic",
		"Uncharted 2 Filmic"
	};

	ImGui::Text("Toggling this setting requires a restart to work correctly!");
	ImGui::Checkbox("HDR Enabled", &enabledSaveLater);

	if (settings.enableHDR != enabledSaveLater) {
		ImGui::TextColored({ 1, 0, 0, 1 }, "Warning: This setting will only apply after saving and restarting!");
	}

	if (ImGui::Button("Reset HDR Settings", { -1, 0 })) {
		settings.useAdvancedTonemapping = false;
		settings.advOperator = 0;
		settings.advExposure = 1.0f;
		settings.advPaperWhite = 1000;
		settings.advMaxNits = 10000;

		settings.displayPeakBrightness = 1000;
		settings.gameBrightness = 400;
		settings.uiBrightness = 400;
	}

	if (ImGui::Button("Reload HDR shaders", { -1, 0 })) {
		ClearShaderCache();
		GetHDROutputCS();
	}

	ImGui::Checkbox("Use Advanced Tonemapping", &settings.useAdvancedTonemapping);
	if (settings.useAdvancedTonemapping) {
		ImGui::SliderInt("Tonemap Operator", reinterpret_cast<int*>(&settings.advOperator), 0, 5, std::format("{}", operators[settings.advOperator]).c_str());

		ImGui::SliderFloat("Linear Exposure", &settings.advExposure, 0.001, 2);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Linear exposure adjusts the brightness after converting to HDR10 color from linear color.");
		}

		ImGui::SliderInt("Paper White (nits)", reinterpret_cast<int*>(&settings.advPaperWhite), 1, 10000);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Paper White sets the game's reference white brightness.");
		}

		ImGui::SliderInt("Peak Brightness (nits)", reinterpret_cast<int*>(&settings.advMaxNits), 1, 10000);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Peak Brightness defines the maximum brightness level.");
		}
	} else {
		ImGui::SliderInt("Display Peak Brightness (nits)", reinterpret_cast<int*>(&settings.displayPeakBrightness), 200, 10000);
		ImGui::SliderInt("Game Brightness (nits)", reinterpret_cast<int*>(&settings.gameBrightness), 100, 1000);

		ImGui::BeginDisabled();
		ImGui::Text("Setting UI brightness is currently not supported.");
		ImGui::SliderInt("UI Brightness (nits)", reinterpret_cast<int*>(&settings.uiBrightness), 100, 1000);
		ImGui::EndDisabled();
	}

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
	hdrAdvDataCB = new ConstantBuffer(ConstantBufferDesc<HDRAdvDataCB>());

	UpdateHDRData();
}

void HDR::ApplyHDR()
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	auto context = globals::d3d::context;

	{
		auto dispatchCount = Util::GetScreenDispatchCount(false);

		ID3D11ShaderResourceView* views[1] = { hdrTexture->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { outputTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbs[1]{ settings.useAdvancedTonemapping ? hdrAdvDataCB->CB() : cbs[0] = hdrDataCB->CB() };

		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		context->CSSetShader(GetHDROutputCS(), nullptr, 0);

		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		// Cleanup
		views[0] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		uavs[0] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		cbs[0] = { nullptr };
		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}
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

	if (hdrAdvOutputCS) {
		hdrAdvOutputCS->Release();
		hdrAdvOutputCS = nullptr;
	}
}

ID3D11ComputeShader* HDR::GetHDROutputCS()
{
	if (settings.useAdvancedTonemapping) {
		if (!hdrAdvOutputCS) {
			logger::debug("Compiling HDRAdvOutputCS.hlsl");
			hdrAdvOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRAdvOutputCS.hlsl", {}, "cs_5_0"));
		}

		return hdrAdvOutputCS;
	}

	if (!hdrOutputCS) {
		logger::debug("Compiling HDROutputCS.hlsl");
		hdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDROutputCS.hlsl", {}, "cs_5_0"));
	}
	return hdrOutputCS;
}

void HDR::UpdateHDRData() const
{
	if (settings.useAdvancedTonemapping) {
		HDRAdvDataCB data;
		data.parameters = DirectX::XMVectorSet(settings.advExposure, static_cast<float>(settings.advPaperWhite), static_cast<float>(settings.advMaxNits), static_cast<float>(settings.advOperator));

		hdrAdvDataCB->Update(data);
	} else {
		float4 parameters;
		parameters.x = static_cast<float>(settings.enableHDR);
		parameters.y = static_cast<float>(settings.displayPeakBrightness);
		parameters.z = static_cast<float>(settings.gameBrightness);
		parameters.w = static_cast<float>(settings.uiBrightness);

		HDRDataCB data = { parameters };

		hdrDataCB->Update(data);
	}
}
