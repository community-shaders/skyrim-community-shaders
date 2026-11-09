#include "Camera.h"

#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"

#include "Features/PostProcessing.h"
#include "I18n/I18n.h"
#include "RasterPass.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Camera::Settings,
	LensDistortion,
	LensFOV,
	ChromaticAberration,
	AberrationFalloff,
	BlurRadius,
	BlurStrength,
	BlurFalloff,
	GrainIntensity,
	GrainSize,
	GrainColor)

void Camera::DrawSettings()
{
	ImGui::SeparatorText(T("feature.post_processing.camera.lens", "Lens"));
	ImGui::SliderFloat(T("feature.post_processing.camera.lens_distortion", "Lens distortion"), &settings.LensDistortion, -0.5f, 0.5f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.camera.lens_distortion_tooltip", "Positive values create barrel distortion; negative values create pincushion distortion. The mapping crops inward to avoid clamped edges."));

	const auto* cam = owner ? owner->GetActivePhysicalCameraState() : nullptr;
	float lensFOV = std::clamp(cam ? cam->HorizontalFOVDeg : settings.LensFOV, 20.0f, 160.0f);
	ImGui::BeginDisabled(cam != nullptr);
	{
		const SceneWidgetInterceptor::ProxyScope proxy(&settings.LensFOV, 1.0f);
		ImGui::SliderFloat(T("feature.post_processing.camera.lens_fov", "Projection FOV"), &lensFOV, 20.0f, 160.0f, "%.1f °");
	}
	ImGui::EndDisabled();
	if (!cam && !SceneWidgetInterceptor::IsArmed())
		settings.LensFOV = lensFOV;
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.camera.lens_fov_tooltip", "Horizontal FOV used to scale lens distortion. Follows Cinematic Camera while it is active."));
	if (cam)
		ImGui::TextDisabled("%s", T("feature.post_processing.camera.physical_fov_controlled", "Projection FOV is currently controlled by Cinematic Camera."));

	ImGui::SliderFloat3(T("feature.post_processing.camera.channel_offsets", "Channel offsets (R/C, G/M, B/Y)"), &settings.ChromaticAberration.x, -5.0f, 5.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.camera.channel_offsets_tooltip", "Independent red/cyan, green/magenta and blue/yellow radial sampling offsets."));

	ImGui::SliderFloat(T("feature.post_processing.camera.aberration_falloff", "Aberration falloff"), &settings.AberrationFalloff, 1.0f, 8.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.camera.aberration_falloff_tooltip", "Higher values keep color fringing out of the center and concentrate it toward the frame edges."));

	ImGui::SeparatorText(T("feature.post_processing.camera.peripheral_softness", "Peripheral Softness"));
	ImGui::SliderFloat(T("feature.post_processing.camera.blur_radius", "Disk radius"), &settings.BlurRadius, 0.0f, 50.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.camera.blur_radius_tooltip", "Maximum lens-softness radius at 720p. It scales with output height and is independent of depth of field."));

	ImGui::SliderFloat(T("feature.post_processing.camera.blur_strength", "Softness strength"), &settings.BlurStrength, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(T("feature.post_processing.camera.blur_falloff", "Softness falloff"), &settings.BlurFalloff, 1.0f, 16.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.camera.blur_falloff_tooltip", "Higher values preserve a larger sharp center and restrict softness to the periphery."));

	ImGui::SeparatorText(T("feature.post_processing.camera.sensor", "Sensor and Film"));
	ImGui::SliderFloat(T("feature.post_processing.camera.grain_intensity", "Film grain"), &settings.GrainIntensity, 0.0f, 0.5f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.camera.grain_intensity_tooltip", "Animated luminance-dependent grain, suppressed in the deepest shadows and brightest highlights."));

	ImGui::SliderFloat(T("feature.post_processing.camera.grain_size", "Grain size"), &settings.GrainSize, 0.5f, 4.0f, "%.2f px");
	ImGui::SliderFloat(T("feature.post_processing.camera.grain_color", "Color grain"), &settings.GrainColor, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.camera.grain_color_tooltip", "Blends monochrome film grain with independent color-channel noise."));
}

void Camera::RestoreDefaultSettings()
{
	settings = {};
}

void Camera::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.LensDistortion = std::clamp(settings.LensDistortion, -0.5f, 0.5f);
	settings.LensFOV = std::clamp(settings.LensFOV, 20.0f, 160.0f);
	settings.ChromaticAberration.x = std::clamp(settings.ChromaticAberration.x, -5.0f, 5.0f);
	settings.ChromaticAberration.y = std::clamp(settings.ChromaticAberration.y, -5.0f, 5.0f);
	settings.ChromaticAberration.z = std::clamp(settings.ChromaticAberration.z, -5.0f, 5.0f);
	settings.AberrationFalloff = std::clamp(settings.AberrationFalloff, 1.0f, 8.0f);
	settings.BlurRadius = std::clamp(settings.BlurRadius, 0.0f, 50.0f);
	settings.BlurStrength = std::clamp(settings.BlurStrength, 0.0f, 1.0f);
	settings.BlurFalloff = std::clamp(settings.BlurFalloff, 1.0f, 16.0f);
	settings.GrainIntensity = std::clamp(settings.GrainIntensity, 0.0f, 0.5f);
	settings.GrainSize = std::clamp(settings.GrainSize, 0.5f, 4.0f);
	settings.GrainColor = std::clamp(settings.GrainColor, 0.0f, 1.0f);
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
		cameraCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CameraCB>(), "PostProcessing::Camera::Constants");
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

		texLens = eastl::make_unique<Texture2D>(texDesc, "PostProcessing::Camera::Lens");
		texLens->CreateSRV(srvDesc);
		texLens->CreateRTV(rtvDesc);

		texOutput = eastl::make_unique<Texture2D>(texDesc, "PostProcessing::Camera::Output");
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateRTV(rtvDesc);
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

		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, colorSampler.put()));
		Util::SetResourceName(colorSampler.get(), "PostProcessing::Camera::ColorSampler");
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
		&cameraPS,
		&finishPS
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
		{ &cameraPS, "camera.ps.hlsl" },
		{ &finishPS, "camera_finish.ps.hlsl" }
	};

	CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\Camera", {}, shaderInfos);
}

void Camera::Draw(TextureInfo& inout_tex)
{
	if (!owner || !owner->GetFullscreenVS())
		return;
	if (!AllShadersReady({ &cameraPS, &finishPS }))
		return;

	globals::profiler->BeginPass("PostProcessing::Camera");
	auto context = globals::d3d::context;
	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	res = Util::ConvertToDynamic(res);
	const float aspect = res.x / res.y;
	const float cornerRadius = sqrt(aspect * aspect * 0.25f + 0.25f);
	const auto* cam = owner ? owner->GetActivePhysicalCameraState() : nullptr;
	const float lensFOV = std::clamp(cam ? cam->HorizontalFOVDeg : settings.LensFOV, 20.0f, 160.0f);

	CameraCB data = {
		.Screen = { res.x, res.y, 1.0f / res.x, 1.0f / res.y },
		.Lens = { settings.LensDistortion, aspect, cornerRadius, lensFOV },
		.Aberration = { settings.ChromaticAberration.x, settings.ChromaticAberration.y, settings.ChromaticAberration.z, settings.AberrationFalloff },
		.Softness = { settings.BlurRadius, settings.BlurStrength, settings.BlurFalloff, 0.0f },
		.Grain = { settings.GrainSize, settings.GrainColor, settings.GrainIntensity, 0.0f }
	};

	cameraCB->Update(data);

	{
		PostProcessingRaster::RasterPass pass(context);

		ID3D11Buffer* cb = cameraCB->CB();
		ID3D11Buffer* sharedDataBuf = globals::state->sharedDataCB->CB();
		ID3D11SamplerState* sampler = colorSampler.get();
		ID3D11ShaderResourceView* srv = inout_tex.srv;

		context->PSSetConstantBuffers(1, 1, &cb);
		context->PSSetConstantBuffers(5, 1, &sharedDataBuf);
		context->PSSetSamplers(0, 1, &sampler);
		pass.SetTargets({ texLens->rtv.get() }, res.x, res.y);
		context->PSSetShaderResources(0, 1, &srv);
		pass.SetShaders(owner->GetFullscreenVS(), cameraPS.get());
		pass.Draw();

		srv = texLens->srv.get();
		pass.SetTargets({ texOutput->rtv.get() }, res.x, res.y);
		context->PSSetShaderResources(0, 1, &srv);
		pass.SetShaders(owner->GetFullscreenVS(), finishPS.get());
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
