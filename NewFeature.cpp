/*
* This file accompanies NewFeature.h
* Please refer to the header for more information.
*
* ProfJack
* 2025-06-28
*/

#include "NewFeature.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	NewFeature::Settings,
	ColorA,
	IdA,
	UvA)

////////////////////////////////////////////////////////////////////////////////////

void NewFeature::RestoreDefaultSettings()
{
	settings = {};
}

void NewFeature::LoadSettings(json& o_json)
{
	settings = o_json;
};

void NewFeature::SaveSettings(json& o_json)
{
	o_json = settings;
};

void NewFeature::DrawSettings()
{
	ImGui::SeparatorText("Cheese");
	ImGui::ColorEdit3("Color A", &settings.ColorA.x);
	uint step = 1;
	ImGui::InputScalarN("Id A", ImGuiDataType_U32, &settings.IdA[0], 2, &step, NULL, "%u annoying uints");
	ImGui::InputFloat2("UV A", &settings.UvA.x);
};

void NewFeature::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		cheeseCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CbData>());
	}

	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = 64,
			.Height = 64,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE |
			             D3D11_BIND_UNORDERED_ACCESS |
			             D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		// When you want to align with the main texture format
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		srvDesc.Format = uavDesc.Format = texDesc.Format;

		{
			cheeseTex = eastl::make_unique<Texture2D>(texDesc);
			cheeseTex->CreateSRV(srvDesc);
			cheeseTex->CreateUAV(uavDesc);
		}
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, cheeseSampler.put()));
	}

	CompileShaders();
}

void NewFeature::ClearShaderCache()
{
	cheeseCs = nullptr;  // This is actually optional
	CompileShaders();
}

void NewFeature::CompileShaders()
{
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\NewFeature\\nonexistent.cs.hlsl", { { "SOME_MACRO", "0" } }, "cs_5_0")); rawPtr)
		cheeseCs.attach(rawPtr);
}