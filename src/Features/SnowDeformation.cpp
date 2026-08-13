#include "SnowDeformation.h"

#include "Globals.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowDeformation::Settings,
	EnableSnowDeformation,
	StampRadius,
	RefillTime)

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

void SnowDeformation::UpdateWindowOrigin()
{
	// Snap to whole texels so scrolling never resamples the map. Use the
	// cached FrameBuffer camera position: it is exactly what the lighting
	// pixel shader sees as CameraPosAdjust, so map and terrain agree.
	auto eyePosFB = globals::game::frameBufferCached.GetCameraPosAdjust();
	float2 desiredOrigin = {
		std::floor((eyePosFB.x - kWorldSize * 0.5f) / kTexelSize) * kTexelSize,
		std::floor((eyePosFB.y - kWorldSize * 0.5f) / kTexelSize) * kTexelSize
	};

	pendingScrollDelta.x += (int)std::lround((desiredOrigin.x - windowOrigin.x) / kTexelSize);
	pendingScrollDelta.y += (int)std::lround((desiredOrigin.y - windowOrigin.y) / kTexelSize);
	windowOrigin = desiredOrigin;
}

void SnowDeformation::Prepass()
{
	if (!settings.EnableSnowDeformation)
		return;

	auto ui = globals::game::ui;
	if (ui && ui->GameIsPaused())
		return;

	auto context = globals::d3d::context;

	UpdateWindowOrigin();

	PerFrame perFrameData{};
	perFrameData.ScrollDelta = pendingScrollDelta;
	pendingScrollDelta = { 0, 0 };

	perFrameData.WindowOrigin = windowOrigin;
	perFrameData.TexelSize = kTexelSize;

	float deltaTime = *globals::game::deltaTime;
	perFrameData.RefillAmount = settings.RefillTime > 0.0f ? deltaTime / settings.RefillTime : 0.0f;

	perFrameData.ClearMap = clearRequested;
	clearRequested = false;

	GatherStamps(perFrameData);

	perFrame->Update(perFrameData);

	uint previousTexture = currentTexture;
	currentTexture = 1 - currentTexture;

	{
		ID3D11Buffer* buffers[1] = { perFrame->CB() };
		context->CSSetConstantBuffers(0, 1, buffers);

		ID3D11ShaderResourceView* srvs[] = { deformationTextures[previousTexture]->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { deformationTextures[currentTexture]->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(GetDeformationUpdateCS(), nullptr, 0);
		globals::profiler->BeginPass("SnowDeformation::DeformationUpdate");
		context->Dispatch(kTextureDim / 8, kTextureDim / 8, 1);
		globals::profiler->EndPass();
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* nullBuffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &nullBuffer);

	ID3D11ShaderResourceView* nullSrvs[1] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSrvs);

	ID3D11UnorderedAccessView* nullUavs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
}

ID3D11ComputeShader* SnowDeformation::GetDeformationUpdateCS()
{
	if (!deformationUpdateCS) {
		logger::debug("Compiling DeformationUpdateCS");
		deformationUpdateCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\DeformationUpdateCS.hlsl", {}, "cs_5_0"));
	}
	return deformationUpdateCS;
}

void SnowDeformation::ClearShaderCache()
{
	if (deformationUpdateCS)
		deformationUpdateCS->Release();
	deformationUpdateCS = nullptr;
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
