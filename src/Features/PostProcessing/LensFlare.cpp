#include "LensFlare.h"

#include "BokehResources.h"
#include "Features/PostProcessing.h"
#include "Menu.h"
#include "State.h"
#include "Util.h"

#include "imgui_stdlib.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensFlare::Settings,
	Intensity,
	ThresholdLevel,
	ThresholdRange,
	GhostStrength,
	GhostChromaShift,
	GhostModeInt,
	BokehShape,
	FFTResolution,
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
	Ghosts,
	CustomBokehPath)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensFlare::GhostSettings,
	Color,
	Scale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensFlare::DebugSettings,
	blurIterations,
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

	{
		const char* modeNames[] = { "Fast (Procedural)", "Quality (FFT Bokeh)" };
		ImGui::Combo("Ghost Mode", &settings.GhostModeInt, modeNames, 2);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Fast: procedural radial ghosts (low cost).\nQuality: FFT convolution with bokeh shape (physically-based ghost shapes).");
	}

	if (settings.GhostModeInt == static_cast<int>(GhostMode::Quality)) {
		// Bokeh shape selection
		if (owner) {
			auto& bokeh = owner->bokehResources;
			int shapeCount = bokeh.GetTotalShapeCount();

			if (ImGui::BeginCombo("Bokeh Shape", settings.BokehShape == 0 ? "Circle (Default)" : bokeh.GetShapeName(settings.BokehShape - 1))) {
				if (ImGui::Selectable("Circle (Default)", settings.BokehShape == 0))
					settings.BokehShape = 0;
				for (int i = 0; i < shapeCount; i++) {
					if (bokeh.GetShapeSRV(i)) {
						bool selected = (settings.BokehShape == i + 1);
						if (ImGui::Selectable(bokeh.GetShapeName(i), selected))
							settings.BokehShape = i + 1;
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Select a bokeh shape for FFT ghost convolution.\nShared with Depth of Field.");
		}

		// Custom bokeh path
		if (ImGui::TreeNode("Custom Bokeh Texture")) {
			ImGui::InputText("File Path", &settings.CustomBokehPath);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Path to a PNG/DDS/BMP/TGA file for a custom bokeh shape.\nRecommended: square, grayscale, 256x256 or 512x512.");
			if (ImGui::Button("Load Custom Bokeh")) {
				if (!settings.CustomBokehPath.empty() && owner) {
					if (owner->bokehResources.LoadCustomShape(settings.CustomBokehPath, 0)) {
						settings.BokehShape = BokehResources::NUM_BUILTIN_SHAPES + 1;
						bokehFFTDirty = true;
						logger::info("LensFlare: Loaded custom bokeh from {}", settings.CustomBokehPath);
					} else {
						logger::warn("LensFlare: Failed to load custom bokeh from {}", settings.CustomBokehPath);
					}
				}
			}
			ImGui::TreePop();
		}

		// FFT Resolution
		{
			const char* resNames[] = { "128", "256", "512" };
			int resValues[] = { 128, 256, 512 };
			int curIdx = 1;
			for (int i = 0; i < 3; i++)
				if (resValues[i] == settings.FFTResolution)
					curIdx = i;

			if (ImGui::Combo("FFT Resolution", &curIdx, resNames, 3))
				settings.FFTResolution = resValues[curIdx];

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Resolution of the FFT convolution. Higher = sharper bokeh ghost shapes but more expensive.");
		}
	}

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
				ImGui::ColorEdit4("Color", settings.Ghosts[i].Color.data());
				ImGui::SliderFloat("Scale", &settings.Ghosts[i].Scale, -15.0f, 15.0f, "%.2f");
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (ImGui::Button("Reset Ghosts to Default")) {
			GhostSettings defaults[NUM_GHOSTS] = {
				{ { { 1.0f, 0.8f, 0.4f, 1.0f } }, -1.5f },
				{ { { 1.0f, 1.0f, 0.6f, 1.0f } }, 2.5f },
				{ { { 0.8f, 0.8f, 1.0f, 1.0f } }, -5.0f },
				{ { { 0.5f, 1.0f, 0.4f, 1.0f } }, 10.0f },
				{ { { 0.5f, 0.8f, 1.0f, 1.0f } }, 0.7f },
				{ { { 0.9f, 1.0f, 0.8f, 1.0f } }, -0.4f },
				{ { { 1.0f, 0.8f, 0.4f, 1.0f } }, -0.2f },
				{ { { 0.9f, 0.7f, 0.7f, 1.0f } }, -0.1f },
			};
			std::memcpy(settings.Ghosts.data(), defaults, sizeof(settings.Ghosts));
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
	ImGui::SliderFloat3("Glare Scale (per branch)", settings.GlareScale.data(), 0.0f, 5.0f, "%.2f");

	// Tint
	ImGui::Spacing();
	ImGui::Text("Color Tint");
	ImGui::Separator();
	ImGui::ColorEdit3("Tint", settings.Tint.data());
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
		ImGui::SliderInt("Blur Iterations", &debugsettings.blurIterations, 1, 4);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Kawase blur cycles (down+up). 1 = sharp, 2+ = smoother");
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

		D3D11_TEXTURE2D_DESC baseDesc;
		gameTexMainCopy.texture->GetDesc(&baseDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto createTex = [&](eastl::unique_ptr<Texture2D>& tex, uint width, uint height) {
			D3D11_TEXTURE2D_DESC texDesc = baseDesc;
			texDesc.Width = width;
			texDesc.Height = height;
			texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			texDesc.MipLevels = 1;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			texDesc.MiscFlags = 0;
			tex = eastl::make_unique<Texture2D>(texDesc);
			tex->CreateSRV(srvDesc);
			tex->CreateUAV(uavDesc);
		};

		uint fullW = baseDesc.Width;
		uint fullH = baseDesc.Height;
		uint halfW = std::max(fullW / 2, 1u);
		uint halfH = std::max(fullH / 2, 1u);
		uint quarterW = std::max(fullW / 4, 1u);
		uint quarterH = std::max(fullH / 4, 1u);

		createTex(texFlare, fullW, fullH);           // full resolution (final output)
		createTex(texThreshold, halfW, halfH);       // half resolution
		createTex(texGhostHalo, halfW, halfH);       // half resolution
		createTex(texGlare, halfW, halfH);           // half resolution
		createTex(texBlurTemp, quarterW, quarterH);  // quarter resolution

		logger::debug("LensFlare: textures created - full {}x{}, half {}x{}, quarter {}x{}", fullW, fullH, halfW, halfH, quarterW, quarterH);
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

	// Create initial FFT textures
	CreateFFTTextures(std::clamp((uint)settings.FFTResolution, FFT_MIN, FFT_MAX));
}

void LensFlare::CreateFFTTextures(uint resolution)
{
	currentFFTResolution = resolution;
	bokehFFTDirty = true;

	D3D11_TEXTURE2D_DESC texDesc = {
		.Width = resolution,
		.Height = resolution,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R32G32_FLOAT,
		.SampleDesc = { .Count = 1, .Quality = 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
	};

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

	// FFT ping-pong textures (RG32F)
	for (int pp = 0; pp < 2; pp++) {
		texFFT[pp] = eastl::make_unique<Texture2D>(texDesc);
		texFFT[pp]->CreateSRV(srvDesc);
		texFFT[pp]->CreateUAV(uavDesc);
	}

	// Bokeh kernel FFT cache (RG32F)
	texBokehFFT = eastl::make_unique<Texture2D>(texDesc);
	texBokehFFT->CreateSRV(srvDesc);
	texBokehFFT->CreateUAV(uavDesc);

	// FFT result (RGBA16F at FFT resolution)
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	texFFTResult = eastl::make_unique<Texture2D>(texDesc);
	texFFTResult->CreateSRV(srvDesc);
	texFFTResult->CreateUAV(uavDesc);

	auto context = globals::d3d::context;
	const FLOAT clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
	context->ClearUnorderedAccessViewFloat(texFFTResult->uav.get(), clearColor);

	logger::debug("LensFlare: FFT textures created at {}x{}", resolution, resolution);
}

void LensFlare::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&thresholdCS, &ghostHaloCS, &blurDownCS, &blurUpCS, &glareStreakCS, &mixCS,
		&fftRowCS, &fftColCS, &fftRowInvCS, &fftColInvCS, &fftMultiplyCS,
		&bokehPrepareCS, &fftThresholdCS, &fftGhostComposeCS
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
		// FFT ghost pipeline shaders
		{ &bokehPrepareCS, "lensflare_fft.cs.hlsl", {}, "CSBokehPrepare" },
		{ &fftThresholdCS, "lensflare_fft.cs.hlsl", {}, "CSFFTThreshold" },
		{ &fftGhostComposeCS, "lensflare_fft.cs.hlsl", {}, "CSFFTGhostCompose" },
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\LensFlare") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}

	// FFT shaders — self-contained in lensflare_fft.cs.hlsl with LensFlareConstants CB
	std::vector<ShaderCompileInfo> fftShaderInfos = {
		{ &fftRowCS, "lensflare_fft.cs.hlsl", { { "ROW_PASS", "" }, { "FORWARD", "" } }, "CS_FFT" },
		{ &fftColCS, "lensflare_fft.cs.hlsl", { { "COL_PASS", "" }, { "FORWARD", "" } }, "CS_FFT" },
		{ &fftRowInvCS, "lensflare_fft.cs.hlsl", { { "ROW_PASS", "" }, { "INVERSE", "" } }, "CS_FFT" },
		{ &fftColInvCS, "lensflare_fft.cs.hlsl", { { "COL_PASS", "" }, { "INVERSE", "" } }, "CS_FFT" },
		{ &fftMultiplyCS, "lensflare_fft.cs.hlsl", {}, "CS_Multiply" },
	};

	for (auto& info : fftShaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\LensFlare") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}

	if (!thresholdCS || !ghostHaloCS || !mixCS) {
		logger::error("Failed to compile lens flare compute shaders!");
	}
}

void LensFlare::DispatchFFT(ID3D11ComputeShader* shader, Texture2D* input, Texture2D* output, uint resolution)
{
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* srv = input->srv.get();
	ID3D11UnorderedAccessView* uav = output->uav.get();

	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShader(shader, nullptr, 0);
	context->Dispatch(resolution, 1, 1);

	srv = nullptr;
	uav = nullptr;
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
}

void LensFlare::PrepareBokehFFT()
{
	auto context = globals::d3d::context;

	// Step 1: Prepare bokeh kernel from texture → RG32F (real=luminance, imag=0), centered + zero-padded
	{
		ID3D11ShaderResourceView* bokehSRV = nullptr;
		if (settings.BokehShape > 0 && owner) {
			bokehSRV = owner->bokehResources.GetShapeSRV(settings.BokehShape - 1);
		}

		std::array<ID3D11ShaderResourceView*, 1> srvs = { bokehSRV };
		std::array<ID3D11UnorderedAccessView*, 1> uavs = { texFFT[0]->uav.get() };

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		if (owner) {
			ID3D11SamplerState* sampler = owner->bokehResources.bokehSampler.get();
			context->CSSetSamplers(0, 1, &sampler);
		}

		context->CSSetShader(bokehPrepareCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	}

	// Step 2: Forward FFT bokeh kernel: texFFT[0] → texFFT[1] → texBokehFFT
	DispatchFFT(fftRowCS.get(), texFFT[0].get(), texFFT[1].get(), currentFFTResolution);
	DispatchFFT(fftColCS.get(), texFFT[1].get(), texBokehFFT.get(), currentFFTResolution);

	cachedBokehShape = settings.BokehShape;
	bokehFFTDirty = false;
}

void LensFlare::DrawFast(TextureInfo& inout_tex, LensFlareCB& data)
{
	auto context = globals::d3d::context;
	uint halfW = texThreshold->desc.Width;
	uint halfH = texThreshold->desc.Height;
	uint quarterW = texBlurTemp->desc.Width;
	uint quarterH = texBlurTemp->desc.Height;

	std::array<ID3D11ShaderResourceView*, 2> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	// === Pass 2: Ghost + Halo — half res → half res ===
	if (!debugsettings.disableGhosts && ghostHaloCS) {
		data.OutputWidth = (float)halfW;
		data.OutputHeight = (float)halfH;
		data.InputWidth = (float)halfW;
		data.InputHeight = (float)halfH;
		lensFlareCB->Update(data);
		auto cb = lensFlareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);

		srvs.at(0) = texThreshold->srv.get();
		uavs.at(0) = texGhostHalo->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(ghostHaloCS.get(), nullptr, 0);
		context->Dispatch((halfW + 7) >> 3, (halfH + 7) >> 3, 1);
		resetViews();
	}

	// === Pass 3: Kawase blur ===
	if (!debugsettings.disableBlur && blurDownCS && blurUpCS) {
		for (int iter = 0; iter < debugsettings.blurIterations; iter++) {
			data.OutputWidth = (float)quarterW;
			data.OutputHeight = (float)quarterH;
			data.InputWidth = (float)halfW;
			data.InputHeight = (float)halfH;
			lensFlareCB->Update(data);
			auto cb = lensFlareCB->CB();
			context->CSSetConstantBuffers(1, 1, &cb);

			srvs.at(0) = texGhostHalo->srv.get();
			uavs.at(0) = texBlurTemp->uav.get();
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(blurDownCS.get(), nullptr, 0);
			context->Dispatch((quarterW + 7) >> 3, (quarterH + 7) >> 3, 1);
			resetViews();

			data.OutputWidth = (float)halfW;
			data.OutputHeight = (float)halfH;
			data.InputWidth = (float)quarterW;
			data.InputHeight = (float)quarterH;
			lensFlareCB->Update(data);
			cb = lensFlareCB->CB();
			context->CSSetConstantBuffers(1, 1, &cb);

			srvs.at(0) = texBlurTemp->srv.get();
			uavs.at(0) = texGhostHalo->uav.get();
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(blurUpCS.get(), nullptr, 0);
			context->Dispatch((halfW + 7) >> 3, (halfH + 7) >> 3, 1);
			resetViews();
		}
	}
}

void LensFlare::DrawQuality(TextureInfo& inout_tex, LensFlareCB& data)
{
	auto context = globals::d3d::context;
	uint N = currentFFTResolution;
	uint halfW = texThreshold->desc.Width;
	uint halfH = texThreshold->desc.Height;

	// Handle FFT resolution change
	uint targetRes = std::clamp((uint)settings.FFTResolution, FFT_MIN, FFT_MAX);
	if (targetRes != currentFFTResolution) {
		CreateFFTTextures(targetRes);
		N = targetRes;
	}

	// Regenerate bokeh FFT if shape changed
	if (bokehFFTDirty || cachedBokehShape != settings.BokehShape) {
		data.FFTResolution = N;
		lensFlareCB->Update(data);
		auto cb = lensFlareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);
		PrepareBokehFFT();
	}

	data.FFTResolution = N;

	// === Step 1: Threshold scene → FFT format (RG32F, N×N) ===
	if (!debugsettings.disableGhosts && fftThresholdCS) {
		data.OutputWidth = (float)N;
		data.OutputHeight = (float)N;
		data.InputWidth = (float)halfW;
		data.InputHeight = (float)halfH;
		lensFlareCB->Update(data);
		auto cb = lensFlareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);

		ID3D11ShaderResourceView* srv = texThreshold->srv.get();
		ID3D11UnorderedAccessView* uav = texFFT[0]->uav.get();
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(fftThresholdCS.get(), nullptr, 0);
		context->Dispatch((N + 7) >> 3, (N + 7) >> 3, 1);

		srv = nullptr;
		uav = nullptr;
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	}

	// === Step 2: Forward FFT scene ===
	if (!debugsettings.disableGhosts) {
		DispatchFFT(fftRowCS.get(), texFFT[0].get(), texFFT[1].get(), N);
		DispatchFFT(fftColCS.get(), texFFT[1].get(), texFFT[0].get(), N);
	}

	// === Step 3: Frequency-domain multiply (scene * bokeh) ===
	if (!debugsettings.disableGhosts && fftMultiplyCS) {
		std::array<ID3D11ShaderResourceView*, 2> mulSrvs = { texFFT[0]->srv.get(), texBokehFFT->srv.get() };
		std::array<ID3D11UnorderedAccessView*, 1> mulUavs = { texFFT[1]->uav.get() };

		context->CSSetShaderResources(0, (uint)mulSrvs.size(), mulSrvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)mulUavs.size(), mulUavs.data(), nullptr);
		context->CSSetShader(fftMultiplyCS.get(), nullptr, 0);
		context->Dispatch((N + 7) >> 3, (N + 7) >> 3, 1);

		mulSrvs.fill(nullptr);
		mulUavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)mulSrvs.size(), mulSrvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)mulUavs.size(), mulUavs.data(), nullptr);
	}

	// === Step 4: Inverse FFT ===
	if (!debugsettings.disableGhosts) {
		DispatchFFT(fftRowInvCS.get(), texFFT[1].get(), texFFT[0].get(), N);
		DispatchFFT(fftColInvCS.get(), texFFT[0].get(), texFFT[1].get(), N);
	}

	// === Step 5: Compose FFT result → multi-ghost with scales + chroma + halo → texGhostHalo ===
	if (!debugsettings.disableGhosts && fftGhostComposeCS) {
		data.OutputWidth = (float)halfW;
		data.OutputHeight = (float)halfH;
		data.InputWidth = (float)N;
		data.InputHeight = (float)N;
		lensFlareCB->Update(data);
		auto cb = lensFlareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);

		// t0 = FFT convolution result (RG32F, luminance in .x)
		// t1 = threshold texture (for halo)
		std::array<ID3D11ShaderResourceView*, 2> srvs = { texFFT[1]->srv.get(), texThreshold->srv.get() };
		std::array<ID3D11UnorderedAccessView*, 1> uavs = { texGhostHalo->uav.get() };

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(fftGhostComposeCS.get(), nullptr, 0);
		context->Dispatch((halfW + 7) >> 3, (halfH + 7) >> 3, 1);

		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	}
}

void LensFlare::Draw(TextureInfo& inout_tex)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("Lens Flare");

	uint fullW = texFlare->desc.Width;
	uint fullH = texFlare->desc.Height;
	uint halfW = texThreshold->desc.Width;
	uint halfH = texThreshold->desc.Height;

	// Build base constant buffer data
	LensFlareCB data = {};
	data.ThresholdLevel = settings.ThresholdLevel;
	data.ThresholdRange = settings.ThresholdRange;
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
	data.FFTResolution = currentFFTResolution;
	std::memcpy(data.GlareScale, settings.GlareScale.data(), sizeof(float) * 3);
	std::memcpy(data.Tint, settings.Tint.data(), sizeof(float) * 3);
	data.GLocalMask = settings.GLocalMask ? 1 : 0;

	for (int i = 0; i < NUM_GHOSTS; i++) {
		std::memcpy(&data.GhostColors[i * 4], settings.Ghosts[i].Color.data(), sizeof(float) * 4);
		data.GhostScalesPacked[i] = settings.Ghosts[i].Scale;
	}

	std::array<ID3D11ShaderResourceView*, 2> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { colorSampler.get(), borderSampler.get() };

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// === Pass 1: Threshold — full res input → half res output ===
	if (!debugsettings.disableThreshold && thresholdCS) {
		data.OutputWidth = (float)halfW;
		data.OutputHeight = (float)halfH;
		data.InputWidth = (float)fullW;
		data.InputHeight = (float)fullH;
		lensFlareCB->Update(data);
		auto cb = lensFlareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);

		srvs.at(0) = inout_tex.srv;
		uavs.at(0) = texThreshold->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(thresholdCS.get(), nullptr, 0);
		context->Dispatch((halfW + 7) >> 3, (halfH + 7) >> 3, 1);
		resetViews();
	}

	// === Ghost + Halo generation (mode-dependent) ===
	GhostMode mode = static_cast<GhostMode>(settings.GhostModeInt);
	if (mode == GhostMode::Quality && fftRowCS && fftColCS && fftMultiplyCS && fftThresholdCS && fftGhostComposeCS) {
		DrawQuality(inout_tex, data);
	} else {
		DrawFast(inout_tex, data);
	}

	// === Pass 4: Glare streaks (3 directions for 6-point star) — half res ===
	if (!debugsettings.disableGlare && glareStreakCS && settings.GlareIntensity > 0.0001f) {
		float clearColor[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewFloat(texGlare->uav.get(), clearColor);

		data.OutputWidth = (float)halfW;
		data.OutputHeight = (float)halfH;
		data.InputWidth = (float)halfW;
		data.InputHeight = (float)halfH;

		static const float angles[3] = {
			1.5707963f,  // 90 deg
			0.5235988f,  // 30 deg
			2.6179938f   // 150 deg
		};

		context->CSSetShader(glareStreakCS.get(), nullptr, 0);

		for (int dir = 0; dir < 3; dir++) {
			float glareScaleForDir = settings.GlareScale[dir];
			if (glareScaleForDir < 0.0001f)
				continue;

			data.GlareDirection[0] = cosf(angles[dir]) * glareScaleForDir;
			data.GlareDirection[1] = sinf(angles[dir]) * glareScaleForDir;
			lensFlareCB->Update(data);
			auto cb = lensFlareCB->CB();
			context->CSSetConstantBuffers(1, 1, &cb);

			srvs.at(0) = texThreshold->srv.get();
			uavs.at(0) = texGlare->uav.get();
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->Dispatch((halfW + 7) >> 3, (halfH + 7) >> 3, 1);
			resetViews();
		}
	}

	// === Pass 5: Mix ghost+halo + glare → full res output ===
	if (mixCS) {
		data.OutputWidth = (float)fullW;
		data.OutputHeight = (float)fullH;
		data.InputWidth = (float)halfW;
		data.InputHeight = (float)halfH;
		lensFlareCB->Update(data);
		auto cb = lensFlareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);

		srvs.at(0) = texGhostHalo->srv.get();
		srvs.at(1) = texGlare->srv.get();
		uavs.at(0) = texFlare->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(mixCS.get(), nullptr, 0);
		context->Dispatch((fullW + 7) >> 3, (fullH + 7) >> 3, 1);
		resetViews();
	}

	// Cleanup
	resetViews();
	auto nullCB = (ID3D11Buffer*)nullptr;
	samplers.fill(nullptr);
	context->CSSetConstantBuffers(1, 1, &nullCB);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texFlare->resource.get(), texFlare->srv.get() };
	state->EndPerfEvent();
}
