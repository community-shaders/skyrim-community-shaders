#include "LensFlare.h"
#include "Menu.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensFlare::Settings,
	Intensity,
	ThresholdLevel,
	ThresholdRange,
	GhostStrength,
	GhostChromaShift,
	HaloStrength,
	HaloRadius,
	HaloWidth,
	HaloCompression,
	HaloChromaShift,
	GlareIntensity,
	GlareDivider,
	GlareScale,
	Tint,
	GLocalMask,
	Ghosts)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensFlare::GhostSettings,
	Color,
	Scale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensFlare::DebugSettings,
	downsampleTimes,
	upsampleTimes,
	disableThreshold,
	disableGhosts,
	disableGlare,
	disableBlur)

void LensFlare::DrawSettings()
{
	ImGui::SliderFloat("Intensity", &settings.Intensity, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Master intensity for the entire lens flare effect");

	// Threshold
	ImGui::Spacing();
	ImGui::Text("Threshold");
	ImGui::Separator();
	ImGui::SliderFloat("Threshold Level", &settings.ThresholdLevel, 0.0f, 5.0f, "%.3f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Minimum brightness for lens flare to appear");
	ImGui::SliderFloat("Threshold Range", &settings.ThresholdRange, 0.01f, 5.0f, "%.3f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Fade range for the threshold cutoff");

	// Ghost Settings
	ImGui::Spacing();
	ImGui::Text("Ghost Settings");
	ImGui::Separator();
	ImGui::SliderFloat("Ghost Strength", &settings.GhostStrength, 0.0f, 1.0f, "%.3f");
	ImGui::SliderFloat("Ghost Chroma Shift", &settings.GhostChromaShift, 0.0f, 0.1f, "%.4f");
	ImGui::Checkbox("Non-intrusive Ghosts", &settings.GLocalMask);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Only apply ghost flaring when looking directly at light sources");

	if (ImGui::TreeNode("Custom Ghost Colors & Scales")) {
		for (int i = 0; i < NUM_GHOSTS; i++) {
			ImGui::PushID(i);
			char label[32];
			snprintf(label, sizeof(label), "Ghost %d", i + 1);
			if (ImGui::TreeNode(label)) {
				ImGui::ColorEdit4("Color", settings.Ghosts[i].Color);
				ImGui::SliderFloat("Scale", &settings.Ghosts[i].Scale, -15.0f, 15.0f, "%.2f");
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (ImGui::Button("Reset Ghosts to Default")) {
			GhostSettings defaults[NUM_GHOSTS] = {
				{ { 1.0f, 0.8f, 0.4f, 1.0f }, -1.5f },
				{ { 1.0f, 1.0f, 0.6f, 1.0f }, 2.5f },
				{ { 0.8f, 0.8f, 1.0f, 1.0f }, -5.0f },
				{ { 0.5f, 1.0f, 0.4f, 1.0f }, 10.0f },
				{ { 0.5f, 0.8f, 1.0f, 1.0f }, 0.7f },
				{ { 0.9f, 1.0f, 0.8f, 1.0f }, -0.4f },
				{ { 1.0f, 0.8f, 0.4f, 1.0f }, -0.2f },
				{ { 0.9f, 0.7f, 0.7f, 1.0f }, -0.1f },
			};
			std::memcpy(settings.Ghosts, defaults, sizeof(settings.Ghosts));
		}
		ImGui::TreePop();
	}

	// Halo Settings
	ImGui::Spacing();
	ImGui::Text("Halo Settings");
	ImGui::Separator();
	ImGui::SliderFloat("Halo Strength", &settings.HaloStrength, 0.0f, 1.0f, "%.3f");
	ImGui::SliderFloat("Halo Radius", &settings.HaloRadius, 0.0f, 1.0f, "%.3f");
	ImGui::SliderFloat("Halo Width", &settings.HaloWidth, 0.0f, 1.0f, "%.3f");
	ImGui::SliderFloat("Halo Compression", &settings.HaloCompression, 0.1f, 2.0f, "%.3f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Fisheye distortion strength for the halo effect");
	ImGui::SliderFloat("Halo Chroma Shift", &settings.HaloChromaShift, 0.0f, 0.1f, "%.4f");

	// Glare Settings
	ImGui::Spacing();
	ImGui::Text("Glare (Starburst)");
	ImGui::Separator();
	ImGui::SliderFloat("Glare Intensity", &settings.GlareIntensity, 0.0f, 1.0f, "%.4f");
	ImGui::SliderFloat("Glare Divider", &settings.GlareDivider, 0.01f, 200.0f, "%.1f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Higher values normalize brighter lights; controls glare size scaling");
	ImGui::SliderFloat3("Glare Scale (per branch)", settings.GlareScale, 0.0f, 5.0f, "%.2f");

	// Tint
	ImGui::Spacing();
	ImGui::Text("Color Tint");
	ImGui::Separator();
	ImGui::ColorEdit3("Tint", settings.Tint);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Radial color gradient applied to the flare effect");

	// Debug
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Debug")) {
		ImGui::Checkbox("Disable Threshold", &debugsettings.disableThreshold);
		ImGui::Checkbox("Disable Ghosts", &debugsettings.disableGhosts);
		ImGui::Checkbox("Disable Glare", &debugsettings.disableGlare);
		ImGui::Checkbox("Disable Blur", &debugsettings.disableBlur);
		ImGui::SliderInt("Downsample Times", &debugsettings.downsampleTimes, 1, 8);
		ImGui::SliderInt("Upsample Times", &debugsettings.upsampleTimes, 1, 8);
	}
}

void LensFlare::RestoreDefaultSettings()
{
	settings = {};
}

void LensFlare::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LensFlare::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LensFlare::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("LensFlare: Creating buffers...");
	{
		lensFlareCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<LensFlareCB>());
	}

	logger::debug("LensFlare: Creating 2D textures...");
	{
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

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

		auto createTex = [&](eastl::unique_ptr<Texture2D>& tex) {
			tex = eastl::make_unique<Texture2D>(texDesc);
			tex->CreateSRV(srvDesc);
			tex->CreateUAV(uavDesc);
		};

		createTex(texFlare);
		createTex(texThreshold);
		createTex(texGlare);
		createTex(texGlareScratch);
		createTex(texBlurScratch);
	}

	logger::debug("LensFlare: Creating samplers...");
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

		D3D11_SAMPLER_DESC borderDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
			.AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
			.AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&borderDesc, borderSampler.put()));
	}

	CompileComputeShaders();
}

void LensFlare::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&thresholdCS, &ghostHaloCS, &blurDownCS, &blurUpCS, &glareStreakCS, &mixCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void LensFlare::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines = {};
		std::string entry = "main";
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &thresholdCS, "lensflare.cs.hlsl", {}, "CSThreshold" },
		{ &ghostHaloCS, "lensflare.cs.hlsl", {}, "CSGhostHalo" },
		{ &blurDownCS, "lensflare.cs.hlsl", {}, "CSFlareDown" },
		{ &blurUpCS, "lensflare.cs.hlsl", {}, "CSFlareUp" },
		{ &glareStreakCS, "lensflare.cs.hlsl", {}, "CSGlareStreak" },
		{ &mixCS, "lensflare.cs.hlsl", {}, "CSMix" },
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\LensFlare") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}

	if (!thresholdCS || !ghostHaloCS || !mixCS) {
		logger::error("Failed to compile lens flare compute shaders!");
	}
}

void LensFlare::Draw(TextureInfo& inout_tex)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("Lens Flare");

	uint width = texFlare->desc.Width;
	uint height = texFlare->desc.Height;
	uint dispatchX = (width + 7) >> 3;
	uint dispatchY = (height + 7) >> 3;

	// Build constant buffer
	LensFlareCB data = {};
	data.ThresholdLevel = settings.ThresholdLevel;
	data.ThresholdRange = settings.ThresholdRange;
	data.ScreenWidth = (float)width;
	data.ScreenHeight = (float)height;
	data.GhostStrength = settings.GhostStrength;
	data.GhostChromaShift = settings.GhostChromaShift;
	data.HaloStrength = settings.HaloStrength;
	data.HaloRadius = settings.HaloRadius;
	data.HaloWidth = settings.HaloWidth;
	data.HaloCompression = settings.HaloCompression;
	data.HaloChromaShift = settings.HaloChromaShift;
	data.Intensity = settings.Intensity;
	data.GlareIntensity = settings.GlareIntensity;
	data.GlareDivider = settings.GlareDivider;
	data.GlareDirection[0] = 0.f;
	data.GlareDirection[1] = 0.f;
	std::memcpy(data.GlareScale, settings.GlareScale, sizeof(float) * 3);
	data.DownsizeScale = 1;
	std::memcpy(data.Tint, settings.Tint, sizeof(float) * 3);
	data.GLocalMask = settings.GLocalMask ? 1 : 0;

	for (int i = 0; i < NUM_GHOSTS; i++) {
		std::memcpy(&data.GhostColors[i * 4], settings.Ghosts[i].Color, sizeof(float) * 4);
		data.GhostScales[i] = settings.Ghosts[i].Scale;
	}

	lensFlareCB->Update(data);

	std::array<ID3D11ShaderResourceView*, 2> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { colorSampler.get(), borderSampler.get() };
	auto cb = lensFlareCB->CB();

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// === Pass 1: Threshold ===
	if (!debugsettings.disableThreshold && thresholdCS) {
		srvs.at(0) = inout_tex.srv;
		uavs.at(0) = texThreshold->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(thresholdCS.get(), nullptr, 0);
		context->Dispatch(dispatchX, dispatchY, 1);
		resetViews();
	} else {
		// Copy input directly as threshold
		context->CopyResource(texThreshold->resource.get(), inout_tex.tex);
	}

	// === Pass 2: Ghost + Halo ===
	if (!debugsettings.disableGhosts && ghostHaloCS) {
		srvs.at(0) = texThreshold->srv.get();
		uavs.at(0) = texFlare->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(ghostHaloCS.get(), nullptr, 0);
		context->Dispatch(dispatchX, dispatchY, 1);
		resetViews();
	}

	// === Pass 3: Blur ghost+halo (Kawase down/up) ===
	if (!debugsettings.disableBlur && blurDownCS && blurUpCS) {
		context->CopyResource(texBlurScratch->resource.get(), texFlare->resource.get());

		// Downsample passes
		context->CSSetShader(blurDownCS.get(), nullptr, 0);
		for (int i = 0; i < debugsettings.downsampleTimes; i++) {
			data.DownsizeScale = 1 << (i + 1);
			lensFlareCB->Update(data);
			cb = lensFlareCB->CB();
			context->CSSetConstantBuffers(1, 1, &cb);

			srvs.at(1) = texBlurScratch->srv.get();
			uavs.at(0) = texFlare->uav.get();
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->Dispatch(dispatchX, dispatchY, 1);
			resetViews();
			context->CopyResource(texBlurScratch->resource.get(), texFlare->resource.get());
		}

		// Upsample passes
		context->CSSetShader(blurUpCS.get(), nullptr, 0);
		for (int i = debugsettings.upsampleTimes - 1; i >= 0; i--) {
			data.DownsizeScale = 1 << (i + 1);
			lensFlareCB->Update(data);
			cb = lensFlareCB->CB();
			context->CSSetConstantBuffers(1, 1, &cb);

			srvs.at(1) = texBlurScratch->srv.get();
			uavs.at(0) = texFlare->uav.get();
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->Dispatch(dispatchX, dispatchY, 1);
			resetViews();
			context->CopyResource(texBlurScratch->resource.get(), texFlare->resource.get());
		}

		// Restore DownsizeScale
		data.DownsizeScale = 1;
		lensFlareCB->Update(data);
		cb = lensFlareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);
	}

	// === Pass 4: Glare streaks (3 directions for 6-point star) ===
	if (!debugsettings.disableGlare && glareStreakCS && settings.GlareIntensity > 0.0001f) {
		// Clear glare buffer
		float clearColor[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewFloat(texGlare->uav.get(), clearColor);

		static const float angles[3] = {
			1.5707963f,  // 90 deg
			0.5235988f,  // 30 deg (90 - 60)
			2.6179938f   // 150 deg (90 + 60)
		};

		context->CSSetShader(glareStreakCS.get(), nullptr, 0);

		for (int dir = 0; dir < 3; dir++) {
			float glareScaleForDir = settings.GlareScale[dir];
			if (glareScaleForDir < 0.0001f)
				continue;

			data.GlareDirection[0] = cosf(angles[dir]) * glareScaleForDir;
			data.GlareDirection[1] = sinf(angles[dir]) * glareScaleForDir;
			lensFlareCB->Update(data);
			cb = lensFlareCB->CB();
			context->CSSetConstantBuffers(1, 1, &cb);

			srvs.at(0) = texThreshold->srv.get();
			uavs.at(0) = texGlare->uav.get();
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->Dispatch(dispatchX, dispatchY, 1);
			resetViews();
		}
	}

	// === Pass 5: Mix ghost+halo + glare → final output ===
	if (mixCS) {
		srvs.at(0) = texFlare->srv.get();
		srvs.at(1) = texGlare->srv.get();
		uavs.at(0) = texThreshold->uav.get();  // reuse threshold buffer as output
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(mixCS.get(), nullptr, 0);
		context->Dispatch(dispatchX, dispatchY, 1);
		resetViews();

		// Copy mix result to texFlare for output
		context->CopyResource(texFlare->resource.get(), texThreshold->resource.get());
	}

	// Cleanup
	resetViews();
	cb = nullptr;
	samplers.fill(nullptr);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);

	// Flare result is in texFlare, available via GetFlareOutput()
	inout_tex = { texFlare->resource.get(), texFlare->srv.get() };
	state->EndPerfEvent();
}
