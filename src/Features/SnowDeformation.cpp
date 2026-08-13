#include "SnowDeformation.h"

#include "Globals.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowDeformation::Settings,
	EnableSnowDeformation)

void SnowDeformation::SetupResources()
{
	perFrame = new ConstantBuffer(ConstantBufferDesc<PerFrame>(), "SnowDeformation::PerFrame");

	D3D11_TEXTURE2D_DESC texDesc = {
		.Width = kTextureDim,
		.Height = kTextureDim,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R16_FLOAT,
		.SampleDesc = { .Count = 1 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
	};

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MipSlice = 0 }
	};

	for (uint i = 0; i < 2; i++) {
		deformationTextures[i] = new Texture2D(texDesc, i == 0 ? "SnowDeformation::DeformationMap0" : "SnowDeformation::DeformationMap1");
		deformationTextures[i]->CreateSRV(srvDesc);
		deformationTextures[i]->CreateUAV(uavDesc);
	}
}

void SnowDeformation::LoadSettings(json& o_json)
{
	settings = o_json;
}

void SnowDeformation::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SnowDeformation::RestoreDefaultSettings()
{
	settings = {};
	clearRequested = true;
}
