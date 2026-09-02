#include "CODBloom.h"

#include "Features/PostProcessing.h"
#include "I18n/I18n.h"
#include "RasterPass.h"
#include "ShaderCache.h"
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
	ImGui::SliderFloat(T("feature.post_processing.codbloom.threshold", "Threshold"), &settings.Threshold, -7.f, 23.f, "%+.2f EV100");
	ImGui::SliderFloat(T("feature.post_processing.codbloom.upsampling_radius", "Upsampling Radius"), &settings.UpsampleRadius, 1.f, 5.f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.codbloom.a_greater_radius_makes_the_bloom_slightly_blurrier", "A greater radius makes the bloom slightly blurrier."));

	ImGui::SliderFloat(T("feature.post_processing.codbloom.mix", "Mix"), &settings.BlendFactor, 0.f, 1.f, "%.2f");

	ImGui::Separator();

	static int mipLevel = 1;
	ImGui::SliderInt(T("feature.post_processing.codbloom.mip_level", "Mip Level"), &mipLevel, 1, (int)settings.MipBlendFactor.size() + 1, "%d", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.codbloom.the_greater_the_level_the_blurrier_the_part", "The greater the level, the blurrier the part it controls"));
	ImGui::Indent();
	{
		ImGui::SliderFloat(T("feature.post_processing.codbloom.intensity", "Intensity"), &settings.MipBlendFactor[mipLevel - 1], 0.f, 1.f, "%.2f");
	}
	ImGui::Unindent();

	if (ImGui::CollapsingHeader(T("feature.post_processing.codbloom.debug", "Debug"))) {
		static int mip = 0;
		ImGui::SliderInt(T("feature.post_processing.codbloom.debug_mip_level", "Debug Mip Level"), &mip, 0, (int)s_BloomMips - 1, "%d", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);

		ImGui::BulletText(T("feature.post_processing.codbloom.texbloom", "texBloom"));
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
		// texBloom for bloom mip chain
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = s_BloomMips;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
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

		// RTV for each mip
		for (uint i = 0; i < s_BloomMips; i++) {
			D3D11_RENDER_TARGET_VIEW_DESC mipRtvDesc = {
				.Format = texDesc.Format,
				.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = i }
			};
			DX::ThrowIfFailed(device->CreateRenderTargetView(texBloom->resource.get(), &mipRtvDesc, texBloomMipRTVs[i].put()));
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

	logger::debug("Creating blend states...");
	{
		// Upsample accumulate: out = PS_Out * 1 + dst * CurrentMipMult, with the
		// PS already scaling its output by UpsampleMult. Blend factor carries
		// CurrentMipMult (alpha factor 0 keeps the written alpha at 1).
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_BLEND_FACTOR;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_BLEND_FACTOR;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, upsampleBlendState.put()));
	}

	CompileRasterShaders();
}

void CODBloom::ClearShaderCache()
{
	BumpShaderGeneration();
	auto const shaderPtrs = std::array{
		&thresholdPS, &downsamplePS, &downsampleFirstMipPS, &upsamplePS, &compositePS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/CODBloom");
	CompileRasterShaders();
}

void CODBloom::CompileRasterShaders()
{
	const std::vector<PixelShaderCompileInfo> shaderInfos = {
		{ &thresholdPS, "bloom.ps.hlsl", {}, "PS_Threshold" },
		{ &downsamplePS, "bloom.ps.hlsl", {}, "PS_Downsample" },
		{ &downsampleFirstMipPS, "bloom.ps.hlsl", { { "FIRST_MIP", "" } }, "PS_Downsample" },
		{ &upsamplePS, "bloom.ps.hlsl", {}, "PS_Upsample" },
		{ &compositePS, "bloom.ps.hlsl", {}, "PS_Composite" }
	};

	CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\CODBloom", {}, shaderInfos);
}

void CODBloom::Draw(TextureInfo& inout_tex)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	if (!AllShadersReady({ &thresholdPS, &downsampleFirstMipPS, &downsamplePS, &upsamplePS }))
		return;
	if (!owner || !owner->GetFullscreenVS())
		return;

	state->BeginPerfEvent("COD Bloom");

	// update cb
	BloomCB cbData = {
		.Threshold = exp2(settings.Threshold - 3.0f),
		.UpsampleRadius = settings.UpsampleRadius,
		.UpsampleMult = 1.f,
		.CurrentMipMult = 1.f
	};
	bloomCB->Update(cbData);

	//////////////////////////////////////////////////////////////////////////////

	std::array<ID3D11ShaderResourceView*, 2> srvs = { nullptr };
	auto cb = bloomCB->CB();
	ID3D11SamplerState* sampler = colorSampler.get();
	auto* vs = owner->GetFullscreenVS();

	// One raster scope for the whole mip chain: state save/restore once, then
	// per-pass target/viewport/shader/blend switches. Targets are set before
	// binding the input SRV so a mip never sits bound as SRV and RTV at once.
	PostProcessingRaster::RasterPass pass(context);
	context->PSSetConstantBuffers(1, 1, &cb);
	context->PSSetSamplers(0, 1, &sampler);

	// Threshold
	{
		globals::profiler->BeginPass("PostProcessing::CODBloom::Threshold");
		pass.SetTargets({ texBloomMipRTVs[0].get() }, (float)texBloom->desc.Width, (float)texBloom->desc.Height);
		srvs.at(0) = inout_tex.srv;
		context->PSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		pass.SetShaders(vs, thresholdPS.get());
		pass.Draw();
		globals::profiler->EndPass();
	}

	// Downsample
	globals::profiler->BeginPass("PostProcessing::CODBloom::Downsample");
	pass.SetShaders(vs, downsampleFirstMipPS.get());
	for (int i = 0; i < s_BloomMips - 1; i++) {
		if (i == 1)
			pass.SetShaders(vs, downsamplePS.get());

		uint mipWidth = texBloom->desc.Width >> (i + 1);
		uint mipHeight = texBloom->desc.Height >> (i + 1);
		pass.SetTargets({ texBloomMipRTVs[i + 1].get() }, (float)mipWidth, (float)mipHeight);

		srvs.fill(nullptr);
		srvs.at(1) = texBloomMipSRVs[i].get();
		context->PSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		pass.Draw();
	}
	globals::profiler->EndPass();

	// upsample (ROP blend accumulates into the destination mip)
	globals::profiler->BeginPass("PostProcessing::CODBloom::Upsample");
	pass.SetShaders(vs, upsamplePS.get());
	for (int i = s_BloomMips - 2; i >= 1; i--) {
		cbData.UpsampleMult = 1.f;
		if (i == s_BloomMips - 2)
			cbData.UpsampleMult = settings.MipBlendFactor[i];
		cbData.CurrentMipMult = settings.MipBlendFactor[i - 1];
		bloomCB->Update(cbData);

		float blendFactor[4] = { cbData.CurrentMipMult, cbData.CurrentMipMult, cbData.CurrentMipMult, 0.f };
		pass.SetBlendState(upsampleBlendState.get(), blendFactor);

		uint mipWidth = texBloom->desc.Width >> i;
		uint mipHeight = texBloom->desc.Height >> i;
		pass.SetTargets({ texBloomMipRTVs[i].get() }, (float)mipWidth, (float)mipHeight);

		srvs.fill(nullptr);
		srvs.at(1) = texBloomMipSRVs[i + 1].get();
		context->PSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		pass.Draw();
	}

	// upsample final mip to mip 0 with blend factor applied (CurrentMipMult=0 to discard threshold data in mip 0)
	{
		cbData.UpsampleMult = settings.BlendFactor;
		cbData.CurrentMipMult = 0.f;
		bloomCB->Update(cbData);

		float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
		pass.SetBlendState(upsampleBlendState.get(), blendFactor);

		pass.SetTargets({ texBloomMipRTVs[0].get() }, (float)texBloom->desc.Width, (float)texBloom->desc.Height);

		srvs.fill(nullptr);
		srvs.at(1) = texBloomMipSRVs[1].get();
		context->PSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		pass.Draw();
	}
	globals::profiler->EndPass();

	// cleanup (RasterPass destructor restores the pipeline state)
	srvs.fill(nullptr);
	context->PSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	cb = nullptr;
	context->PSSetConstantBuffers(1, 1, &cb);
	sampler = nullptr;
	context->PSSetSamplers(0, 1, &sampler);

	// return
	inout_tex = { texBloom->resource.get(), texBloomMipSRVs[0].get() };

	state->EndPerfEvent();
}
