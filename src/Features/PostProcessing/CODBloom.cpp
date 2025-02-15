#include "CODBloom.h"

#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CODBloom::Settings,
	Threshold,
	UpsampleRadius,
	BlendFactor,
	MipBlendFactor)

void CODBloom::DrawSettings()
{
	ImGui::SliderFloat("Threshold", &settings.Threshold, -6.f, 21.f, "%+.2f EV");
	ImGui::SliderFloat("Upsampling Radius", &settings.UpsampleRadius, 1.f, 5.f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("A greater radius makes the bloom slightly blurrier.");

	ImGui::SliderFloat("Mix", &settings.BlendFactor, 0.f, 1.f, "%.2f");

	ImGui::Separator();

	static int mipLevel = 1;
	ImGui::SliderInt("Mip Level", &mipLevel, 1, (int)settings.MipBlendFactor.size() + 1, "%d", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("The greater the level, the blurrier the part it controls");
	ImGui::Indent();
	{
		ImGui::SliderFloat("Intensity", &settings.MipBlendFactor[mipLevel - 1], 0.f, 1.f, "%.2f");
	}
	ImGui::Unindent();

	if (ImGui::CollapsingHeader("Debug")) {
		static int mip = 0;
		ImGui::SliderInt("Debug Mip Level", &mip, 0, (int)s_BloomMips - 1, "%d", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);

		ImGui::BulletText("texBloom");
		ImGui::Image(texBloomMipSRVs[mip].get(), { texBloom->desc.Width * .2f, texBloom->desc.Height * .2f });
	}
}

void CODBloom::RestoreDefaultSettings()
{
	settings = {};
}

void CODBloom::LoadSettings(json& o_json)
{
	settings = o_json;
}

void CODBloom::SaveSettings(json& o_json)
{
	o_json = settings;
}

void CODBloom::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		bloomCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<BloomCB>());
	}

	logger::debug("Creating 2D textures...");
	{
		// texAdapt for adaptation
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = s_BloomMips;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texBloom = std::make_unique<Texture2D>(texDesc);
		texBloom->CreateSRV(srvDesc);

		// SRV for each mip
		for (uint i = 0; i < s_BloomMips; i++) {
			D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc = {
				.Format = texDesc.Format,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MostDetailedMip = i, .MipLevels = 1 }
			};
			DX::ThrowIfFailed(device->CreateShaderResourceView(texBloom->resource.get(), &mipSrvDesc, texBloomMipSRVs[i].put()));
		}

		// UAV for each mip
		for (uint i = 0; i < s_BloomMips; i++) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
				.Format = texDesc.Format,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = i }
			};
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texBloom->resource.get(), &mipUavDesc, texBloomMipUAVs[i].put()));
		}
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
			.AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
			.AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};

		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, colorSampler.put()));
	}

	CompileComputeShaders();
}

void CODBloom::ClearShaderCache()
{
	auto const shaderPtrs = std::array{
		&thresholdCS, &downsampleCS, &downsampleFirstMipCS, &upsampleCS, &compositeCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void CODBloom::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry = "main";
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &thresholdCS, "bloom.cs.hlsl", {}, "CS_Threshold" },
			{ &downsampleCS, "bloom.cs.hlsl", {}, "CS_Downsample" },
			{ &downsampleFirstMipCS, "bloom.cs.hlsl", { { "FIRST_MIP", "" } }, "CS_Downsample" },
			{ &upsampleCS, "bloom.cs.hlsl", {}, "CS_Upsample" },
			{ &compositeCS, "bloom.cs.hlsl", {}, "CS_Composite" }
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\CODBloom") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
}

void CODBloom::Draw(TextureInfo& inout_tex)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("COD Bloom");

	// update cb
	BloomCB cbData = {
		.Threshold = exp2(settings.Threshold) * .125f,
		.UpsampleRadius = settings.UpsampleRadius,
		.UpsampleMult = 1.f,
		.CurrentMipMult = 1.f
	};
	bloomCB->Update(cbData);

	//////////////////////////////////////////////////////////////////////////////

	std::array<ID3D11ShaderResourceView*, 2> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 1> samplers = { colorSampler.get() };
	auto cb = bloomCB->CB();

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// Threshold
	{
		srvs.at(0) = inout_tex.srv;
		uavs.at(0) = texBloomMipUAVs[0].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(thresholdCS.get(), nullptr, 0);
		context->Dispatch(((texBloom->desc.Width - 1) >> 5) + 1, ((texBloom->desc.Height - 1) >> 5) + 1, 1);
	}

	// Downsample
	context->CSSetShader(downsampleFirstMipCS.get(), nullptr, 0);
	for (int i = 0; i < s_BloomMips - 1; i++) {
		resetViews();

		srvs.at(1) = texBloomMipSRVs[i].get();
		uavs.at(0) = texBloomMipUAVs[i + 1].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		if (i == 1)
			context->CSSetShader(downsampleCS.get(), nullptr, 0);

		uint mipWidth = texBloom->desc.Width >> (i + 1);
		uint mipHeight = texBloom->desc.Height >> (i + 1);
		context->Dispatch(((mipWidth - 1) >> 5) + 1, ((mipHeight - 1) >> 5) + 1, 1);
	}

	// upsample
	context->CSSetShader(upsampleCS.get(), nullptr, 0);
	for (int i = s_BloomMips - 2; i >= 1; i--) {
		resetViews();

		cbData.UpsampleMult = 1.f;
		if (i == s_BloomMips - 2)
			cbData.UpsampleMult = settings.MipBlendFactor[i];
		cbData.CurrentMipMult = settings.MipBlendFactor[i - 1];
		bloomCB->Update(cbData);

		srvs.at(1) = texBloomMipSRVs[i + 1].get();
		uavs.at(0) = texBloomMipUAVs[i].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		uint mipWidth = texBloom->desc.Width >> i;
		uint mipHeight = texBloom->desc.Height >> i;
		context->Dispatch(((mipWidth - 1) >> 5) + 1, ((mipHeight - 1) >> 5) + 1, 1);
	}

	// composite
	{
		resetViews();

		cbData.UpsampleMult = settings.BlendFactor;
		bloomCB->Update(cbData);

		context->CSSetShader(compositeCS.get(), nullptr, 0);
		srvs.at(0) = inout_tex.srv;
		srvs.at(1) = texBloomMipSRVs[1].get();
		uavs.at(0) = texBloomMipUAVs[0].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->Dispatch(((texBloom->desc.Width - 1) >> 5) + 1, ((texBloom->desc.Height - 1) >> 5) + 1, 1);
	}

	// cleanup
	resetViews();

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);

	// return
	inout_tex = { texBloom->resource.get(), texBloomMipSRVs[0].get() };

	state->EndPerfEvent();
}
