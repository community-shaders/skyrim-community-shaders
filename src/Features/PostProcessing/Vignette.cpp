#include "Vignette.h"

#include "Features/PostProcessing.h"
#include "I18n/I18n.h"
#include "RasterPass.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Vignette::Settings,
	FocalLength,
	Power)

void Vignette::DrawSettings()
{
	ImGui::SliderFloat(T("feature.post_processing.vignette.focal_length", "Focal Length"), &settings.FocalLength, 0.1f, 2.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.vignette.the_focal_length_of_the_lens_relative_to", "The focal length of the lens, relative to image width."));

	ImGui::SliderFloat(T("feature.post_processing.vignette.anamorphic_squeeze", "Anamorphic Squeeze"), &settings.Anamorphism, 0.1f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.vignette.how_flat_the_vignette_looks_simulating_anamorphic_lens", "How flat the vignette looks, simulating anamorphic lens."));

	ImGui::SliderFloat(T("feature.post_processing.vignette.power", "Power"), &settings.Power, 0.f, 4.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			T("feature.post_processing.vignette.the_natural_vignetting_of_a_camera_follows_the",
				"The natural vignetting of a camera follows the fourth law, where the vignette is proportional to the fourth power of the incident angle. "
				"The actual power in a camera is usually lower due to designed compensation."));
}

void Vignette::RestoreDefaultSettings()
{
	settings = {};
}

void Vignette::LoadSettings(json& o_json)
{
	settings = o_json;
}

void Vignette::SaveSettings(json& o_json)
{
	o_json = settings;
}

void Vignette::SetupResources()
{
	auto renderer = globals::game::renderer;

	logger::debug("Creating buffers...");
	{
		vignetteCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<VignetteCB>());
	}

	logger::debug("Creating 2D textures...");
	{
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateRTV(rtvDesc);
	}

	CompileRasterShaders();
}

void Vignette::ClearShaderCache()
{
	BumpShaderGeneration();
	const auto shaderPtrs = std::array{
		&vignettePS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/Vignette");
	CompileRasterShaders();
}

void Vignette::CompileRasterShaders()
{
	const std::vector<PixelShaderCompileInfo> shaderInfos = {
		{ &vignettePS, "vignette.ps.hlsl" },
	};

	CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\Vignette", {}, shaderInfos);
}

void Vignette::Draw(TextureInfo& inout_tex)
{
	if (!owner || !owner->GetFullscreenVS())
		return;
	if (!AllShadersReady({ &vignettePS }))
		return;

	globals::profiler->BeginPass("PostProcessing::Vignette");
	auto context = globals::d3d::context;

	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	res = Util::ConvertToDynamic(res);
	VignetteCB data = {
		.settings = settings,
		.AspectRatio = res.y / res.x / settings.Anamorphism,
		.RcpDynRes = float2(1.f) / res
	};
	vignetteCB->Update(data);

	{
		PostProcessingRaster::RasterPass pass(context);

		ID3D11ShaderResourceView* srv = inout_tex.srv;
		ID3D11Buffer* cb = vignetteCB->CB();

		context->PSSetConstantBuffers(1, 1, &cb);
		context->PSSetShaderResources(0, 1, &srv);
		pass.SetTargets({ texOutput->rtv.get() }, res.x, res.y);
		pass.SetShaders(owner->GetFullscreenVS(), vignettePS.get());
		pass.Draw();

		// clean up
		srv = nullptr;
		cb = nullptr;
		context->PSSetShaderResources(0, 1, &srv);
		context->PSSetConstantBuffers(1, 1, &cb);
	}

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	globals::profiler->EndPass();
}
