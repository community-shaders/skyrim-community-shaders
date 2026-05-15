#include "LocalExposure.h"

#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LocalExposure::Settings,
	HighlightContrast,
	ShadowContrast,
	DetailStrength,
	BilateralSigma,
	MipBias)

void LocalExposure::DrawSettings()
{
	ImGui::SliderFloat("Highlight Contrast", &settings.HighlightContrast, 0.f, 1.5f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("How much to compress overly bright areas relative to their local neighborhood. Higher values darken local highlights more.");

	ImGui::SliderFloat("Shadow Contrast", &settings.ShadowContrast, 0.f, 1.5f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("How much to boost dark areas relative to their local neighborhood. Higher values brighten local shadows more.");

	ImGui::SliderFloat("Detail Strength", &settings.DetailStrength, 0.f, 2.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Overall intensity of the local exposure effect. 0 = disabled, 1 = full effect.");

	ImGui::SliderFloat("Bilateral Sigma", &settings.BilateralSigma, 0.5f, 5.f, "%.2f EV");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"Edge sensitivity for the bilateral upsample (in EV). Lower values produce sharper edge "
			"preservation (less halos around high-contrast edges) but may cause banding.");

	uint mipMin = 3, mipMax = s_MaxMips - 1;
	int mipVal = (int)settings.MipBias;
	ImGui::SliderInt("Blur Radius (Mip Level)", &mipVal, mipMin, mipMax);
	settings.MipBias = (uint)std::clamp(mipVal, (int)mipMin, (int)mipMax);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Which downsampled mip level to use as the 'local average'. Higher = larger spatial radius.");
}

void LocalExposure::RestoreDefaultSettings()
{
	settings = {};
}

void LocalExposure::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LocalExposure::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LocalExposure::SetupResources()
{
	auto renderer = globals::game::renderer;

	// Get screen dimensions from game render target
	auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];
	D3D11_TEXTURE2D_DESC mainDesc;
	gameTexMainCopy.texture->GetDesc(&mainDesc);

	uint fullW = mainDesc.Width;
	uint fullH = mainDesc.Height;
	uint lowW = (fullW + 3) / 4;
	uint lowH = (fullH + 3) / 4;

	// Calculate number of mips for the luminance texture
	numMips = 1;
	{
		uint w = lowW, h = lowH;
		while (w > 1 && h > 1 && numMips < s_MaxMips) {
			w = (w + 1) / 2;
			h = (h + 1) / 2;
			numMips++;
		}
	}

	// Create luminance texture (1/4 res) with mip chain
	{
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = lowW;
		texDesc.Height = lowH;
		texDesc.MipLevels = numMips;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		texLuminance = eastl::make_unique<Texture2D>(texDesc, "LocalExposure Luminance");

		// Create per-mip SRVs and UAVs
		auto device = globals::d3d::device;
		for (uint i = 0; i < numMips; i++) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = i;
			srvDesc.Texture2D.MipLevels = 1;
			DX::ThrowIfFailed(device->CreateShaderResourceView(texLuminance->resource.get(), &srvDesc, lumMipSRVs[i].put()));

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = i;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texLuminance->resource.get(), &uavDesc, lumMipUAVs[i].put()));
		}

		// Also create a full SRV for sampling with hardware mip filtering
		D3D11_SHADER_RESOURCE_VIEW_DESC fullSrvDesc = {};
		fullSrvDesc.Format = DXGI_FORMAT_R16_FLOAT;
		fullSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		fullSrvDesc.Texture2D.MostDetailedMip = 0;
		fullSrvDesc.Texture2D.MipLevels = numMips;
		texLuminance->CreateSRV(fullSrvDesc);
	}

	// Create output exposure texture (full resolution, R16F)
	{
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = fullW;
		texDesc.Height = fullH;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		texExposure = eastl::make_unique<Texture2D>(texDesc, "LocalExposure Output");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		texExposure->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		texExposure->CreateUAV(uavDesc);
	}

	// Create linear sampler
	{
		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateSamplerState(&sampDesc, linearSampler.put()));
	}

	// Create constant buffer
	localExposureCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<LocalExposureCB>());

	CompileComputeShaders();
}

void LocalExposure::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&luminanceCS, &downsampleCS, &computeExpCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void LocalExposure::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry;
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &luminanceCS, "localexposure.cs.hlsl", {}, "CSLuminance" },
		{ &downsampleCS, "localexposure.cs.hlsl", {}, "CSDownsample" },
		{ &computeExpCS, "localexposure.cs.hlsl", {}, "CSComputeExposure" },
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\LocalExposure") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
}

void LocalExposure::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	auto state = globals::state;

	state->BeginPerfEvent("Local Exposure");

	// Get dimensions
	D3D11_TEXTURE2D_DESC mainDesc;
	inout_tex.tex->GetDesc(&mainDesc);
	uint fullW = mainDesc.Width;
	uint fullH = mainDesc.Height;
	uint lowW = texLuminance->desc.Width;
	uint lowH = texLuminance->desc.Height;

	uint mipLevel = std::min(settings.MipBias, numMips - 1);

	// Update constant buffer
	LocalExposureCB cbData = {
		.HighlightContrast = settings.HighlightContrast,
		.ShadowContrast = settings.ShadowContrast,
		.DetailStrength = settings.DetailStrength,
		.BilateralSigma = settings.BilateralSigma,
		.InputWidth = fullW,
		.InputHeight = fullH,
		.LowResWidth = lowW,
		.LowResHeight = lowH,
		.MipLevel = mipLevel,
	};
	localExposureCB->Update(cbData);

	ID3D11Buffer* cb = localExposureCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	ID3D11SamplerState* sampler = linearSampler.get();
	context->CSSetSamplers(0, 1, &sampler);

	// === Pass 1: Compute log-luminance at 1/4 resolution ===
	{
		state->BeginPerfEvent("Luminance Downsample");

		ID3D11ShaderResourceView* srv = inout_tex.srv;
		ID3D11UnorderedAccessView* uav = lumMipUAVs[0].get();

		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(luminanceCS.get(), nullptr, 0);
		context->Dispatch((lowW + 7) >> 3, (lowH + 7) >> 3, 1);

		srv = nullptr;
		uav = nullptr;
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

		state->EndPerfEvent();
	}

	// === Pass 2: Build mip chain (iterative downsample) ===
	{
		state->BeginPerfEvent("Mip Chain");

		for (uint i = 1; i < numMips; i++) {
			ID3D11ShaderResourceView* srv = lumMipSRVs[i - 1].get();
			ID3D11UnorderedAccessView* uav = lumMipUAVs[i].get();

			context->CSSetShaderResources(0, 1, &srv);
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->CSSetShader(downsampleCS.get(), nullptr, 0);

			uint mipW = std::max(1u, lowW >> i);
			uint mipH = std::max(1u, lowH >> i);
			context->Dispatch((mipW + 7) >> 3, (mipH + 7) >> 3, 1);

			srv = nullptr;
			uav = nullptr;
			context->CSSetShaderResources(0, 1, &srv);
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		}

		state->EndPerfEvent();
	}

	// === Pass 3: Compute per-pixel local exposure (bilateral upsample + exposure calc) ===
	{
		state->BeginPerfEvent("Compute Exposure");

		// t0 = full res scene color (for luminance reference)
		// t1 = luminance mip chain (with all mips, for bilinear sampling)
		// u0 = output exposure map
		std::array<ID3D11ShaderResourceView*, 2> srvs = { inout_tex.srv, texLuminance->srv.get() };
		ID3D11UnorderedAccessView* uav = texExposure->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(computeExpCS.get(), nullptr, 0);
		context->Dispatch((fullW + 7) >> 3, (fullH + 7) >> 3, 1);

		srvs.fill(nullptr);
		uav = nullptr;
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

		state->EndPerfEvent();
	}

	// Cleanup
	context->CSSetShader(nullptr, nullptr, 0);
	cb = nullptr;
	context->CSSetConstantBuffers(1, 1, &cb);
	sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);

	// NOTE: We do NOT modify inout_tex. The exposure map is consumed by Composite.
	state->EndPerfEvent();
}
