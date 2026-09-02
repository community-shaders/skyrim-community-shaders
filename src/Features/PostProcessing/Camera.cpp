#include "Camera.h"

#include "Features/PostProcessing.h"
#include "I18n/I18n.h"
#include "RasterPass.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Camera::Settings,
	UseFE,
	FEFoV,
	FECrop,
	CAStrength,
	NoiseStrength,
	NoiseType)

void Camera::DrawSettings()
{
	ImGui::Checkbox(T("feature.post_processing.camera.fisheye", "Fisheye"), &settings.UseFE);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", T("feature.post_processing.camera.enable_fisheye_effect", "Enable fisheye effect."));
	}

	if (settings.UseFE) {
		ImGui::SliderFloat(T("feature.post_processing.camera.fov", "FOV"), &settings.FEFoV, 20.0f, 180.0f, "%1.0f °");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", T("feature.post_processing.camera.fov_in_degrees_set_to_in_game_fov", "FOV in degrees.\n\nSet to in-game FOV."));
		}

		ImGui::SliderFloat(T("feature.post_processing.camera.crop", "Crop"), &settings.FECrop, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", T("feature.post_processing.camera.how_much_to_crop_into_the_image", "How much to crop into the image.\n\n0 = circular, 1 = full-frame."));
		}
	}

	ImGui::SliderFloat(T("feature.post_processing.camera.ca_amount", "CA amount"), &settings.CAStrength, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", T("feature.post_processing.camera.chromatic_aberration_strength", "Chromatic aberration strength."));
	}

	ImGui::SliderFloat(T("feature.post_processing.camera.noise_amount", "Noise amount"), &settings.NoiseStrength, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", T("feature.post_processing.camera.amount_of_noise_to_apply", "Amount of noise to apply."));
	}

	ImGui::Combo(T("feature.post_processing.camera.noise_type", "Noise type"), &settings.NoiseType, "Film grain\0Color grain\0\0");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", T("feature.post_processing.camera.type_of_noise_to_apply", "Type of noise to apply."));
	}
}

void Camera::RestoreDefaultSettings()
{
	settings = {};
}

void Camera::LoadSettings(json& o_json)
{
	settings = o_json;
}

void Camera::SaveSettings(json& o_json)
{
	o_json = settings;
}

void Camera::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		cameraCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CameraCB>());
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

	logger::debug("Creating samplers...");
	{
		// Linear clamp filtering for the fisheye / chromatic aberration sampling.
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};

		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, colorSampler.put()));
	}

	logger::debug("Compiling shaders...");
	{
		CompileRasterShaders();
	}
}

void Camera::ClearShaderCache()
{
	BumpShaderGeneration();
	const auto shaderPtrs = std::array{
		&cameraPS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/Camera");
	CompileRasterShaders();
}

void Camera::CompileRasterShaders()
{
	const std::vector<PixelShaderCompileInfo> shaderInfos = {
		{ &cameraPS, "camera.ps.hlsl" }
	};

	CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\Camera", {}, shaderInfos);
}

void Camera::Draw(TextureInfo& inout_tex)
{
	if (!owner || !owner->GetFullscreenVS())
		return;
	if (!AllShadersReady({ &cameraPS }))
		return;

	globals::profiler->BeginPass("PostProcessing::Camera");
	auto context = globals::d3d::context;
	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	res = Util::ConvertToDynamic(res);

	CameraCB data = {
		.FEFoV = settings.FEFoV,
		.FECrop = settings.FECrop,
		.CAStrength = settings.CAStrength,
		.NoiseStrength = settings.NoiseStrength,
		.NoiseType = settings.NoiseType,
		.res = res,
		.UseFE = settings.UseFE
	};

	cameraCB->Update(data);

	{
		PostProcessingRaster::RasterPass pass(context);

		ID3D11ShaderResourceView* srv = inout_tex.srv;
		ID3D11Buffer* cb = cameraCB->CB();
		// Film grain animates off SharedData::FrameCount (b5).
		ID3D11Buffer* sharedDataBuf = globals::state->sharedDataCB->CB();
		ID3D11SamplerState* sampler = colorSampler.get();

		context->PSSetConstantBuffers(1, 1, &cb);
		context->PSSetConstantBuffers(5, 1, &sharedDataBuf);
		context->PSSetSamplers(0, 1, &sampler);
		context->PSSetShaderResources(0, 1, &srv);
		pass.SetTargets({ texOutput->rtv.get() }, res.x, res.y);
		pass.SetShaders(owner->GetFullscreenVS(), cameraPS.get());
		pass.Draw();

		srv = nullptr;
		cb = nullptr;
		sharedDataBuf = nullptr;
		sampler = nullptr;
		context->PSSetShaderResources(0, 1, &srv);
		context->PSSetConstantBuffers(1, 1, &cb);
		context->PSSetConstantBuffers(5, 1, &sharedDataBuf);
		context->PSSetSamplers(0, 1, &sampler);
	}

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	globals::profiler->EndPass();
}
