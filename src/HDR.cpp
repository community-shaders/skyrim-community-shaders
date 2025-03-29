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
	useDXTonemapping,
	dxOperator,
	dxTransferFunction,
	dxExposure,
	dxPaperWhite);

void HDR::DrawSettings()
{
	const char* operators[] = {
		"None (Pass-through)",
		"Saturate (Clamp [0,1])",
		"Reinhard (x/(1+x))",
		"ACES Filmic"
	};
	const char* transferFunctions[] = {
		"Linear",
		"sRGB"
	};
	const char* rotationFunctions[] = {
		"Rec.709/Rec.2020",
		"Rec.709/DCI-P3-D65",
		"DCI-P3-D65/Rec.2020"
	};

	ImGui::Text("Toggling this setting requires a restart to work correctly!");
	ImGui::Checkbox("HDR Enabled", &enabledSaveLater);

	if (settings.enableHDR != enabledSaveLater) {
		ImGui::TextColored({ 1, 0, 0, 1 }, "Warning: This setting will only apply after saving and restarting!");
	}

	if (ImGui::Button("Reset HDR Settings", { -1, 0 })) {
		settings.useDXTonemapping = false;
		settings.dxOperator = 3;
		settings.dxTransferFunction = 1;
		settings.dxColorRotation = 0;
		settings.dxExposure = 0.5f;
		settings.dxPaperWhite = 1000;
		settings.displayPeakBrightness = 1000;
		settings.gameBrightness = 400;
		settings.uiBrightness = 400;
	}

	ImGui::Checkbox("Use DirectXTK Tonemapping", &settings.useDXTonemapping);
	if (settings.useDXTonemapping) {
		ImGui::Text("Recommended Defaults: ACES Filmic operator, sRGB transfer function, and 0.5f exposure.");
		ImGui::SliderInt("Operator", reinterpret_cast<int*>(&settings.dxOperator), 0, 3, std::format("{}", operators[settings.dxOperator]).c_str());

		ImGui::SliderInt("Transfer Function", reinterpret_cast<int*>(&settings.dxTransferFunction), 0, 1, std::format("{}", transferFunctions[settings.dxTransferFunction]).c_str());
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Linear:\n"
				"Pass-Through.\n"
				"\n"
				"sRGB:\n"
				"Rec.709 and approximate sRGB display curve.");
		}

		ImGui::SliderInt("Color Rotation", reinterpret_cast<int*>(&settings.dxColorRotation), 0, 2, std::format("{}", rotationFunctions[settings.dxColorRotation]).c_str());
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Rec.709/Rec.2020:\n"
				"Rec.709 color primaries into Rec.2020\n"
				"\n"
				"Rec.709/DCI-P3-D65:\n"
				"Rec.709 color primaries into DCI-P3-D65\n"
				"\n"
				"DCI-P3-D65/Rec.2020:\n"
				"DCI-P3-D65 color primaries into Rec.2020");
		}

		ImGui::SliderFloat("Exposure", &settings.dxExposure, 0.001, 2);
		ImGui::SliderInt("Paper White (nits)", reinterpret_cast<int*>(&settings.dxPaperWhite), 400, 10000);
	} else {
		ImGui::SliderInt("Display Peak Brightness (nits)", reinterpret_cast<int*>(&settings.displayPeakBrightness), 400, 10000);
		ImGui::SliderInt("Game Brightness (nits)", reinterpret_cast<int*>(&settings.gameBrightness), 100, 500);

		ImGui::BeginDisabled();
		ImGui::Text("Setting UI brightness is currently not supported.");
		ImGui::SliderInt("UI Brightness (nits)", reinterpret_cast<int*>(&settings.uiBrightness), 100, 500);
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
	auto device = globals::d3d::device;

	m_toneMap = std::make_unique<DirectX::ToneMapPostProcess>(device);
	m_toneMap->SetOperator(DirectX::ToneMapPostProcess::None);
	m_toneMap->SetTransferFunction(DirectX::ToneMapPostProcess::SRGB);

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
	hdrDxDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDxDataCB>());

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

	state->BeginPerfEvent("HDR");

	{
		{
			auto dispatchCount = Util::GetScreenDispatchCount(false);

			ID3D11ShaderResourceView* views[1] = { hdrTexture->srv.get() };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { outputTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			ID3D11Buffer* cbs[1]{ nullptr };
			if (settings.useDXTonemapping) {
				cbs[0] = hdrDxDataCB->CB();
			} else {
				cbs[0] = hdrDataCB->CB();
			}
			context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

			if (settings.useDXTonemapping) {
				context->CSSetShader(GetDxHDROutputCS(), nullptr, 0);
			} else {
				context->CSSetShader(GetHDROutputCS(), nullptr, 0);
			}

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

	if (hdrDxOutputCS) {
		hdrDxOutputCS->Release();
		hdrDxOutputCS = nullptr;
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

ID3D11ComputeShader* HDR::GetDxHDROutputCS()
{
	if (!hdrDxOutputCS) {
		logger::debug("Compiling DxHDROutputCS.hlsl");
		hdrDxOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DxHDROutputCS.hlsl", {}, "cs_5_0"));
	}
	return hdrDxOutputCS;
}

void HDR::UpdateHDRData() const
{
	if (settings.useDXTonemapping) {
		HDRDxDataCB data = {};
		data.parameters = DirectX::XMVectorSet(settings.dxExposure, static_cast<float>(settings.dxPaperWhite), static_cast<float>(static_cast<int>(settings.dxTransferFunction) * 4 + static_cast<int>(settings.dxOperator)), 0.f);
		switch (settings.dxColorRotation) {
		default:
		case 0:
			memcpy(data.colorRotation, c_from709to2020, sizeof(c_from709to2020));
			break;
		case 1:
			memcpy(data.colorRotation, c_from709toP3D65, sizeof(c_from709toP3D65));
			break;
		case 2:
			memcpy(data.colorRotation, c_fromP3D65to2020, sizeof(c_fromP3D65to2020));
			break;
		}
		hdrDxDataCB->Update(data);
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
