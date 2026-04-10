#include "PhysicalGlare.h"

#include "Globals.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PhysicalGlare::Settings,
	ThresholdEV,
	Intensity,
	ApertureBlades,
	ApertureRotation,
	ScatterStrength,
	ChromaticDispersion,
	AdaptSpeed,
	FFTResolution)

void PhysicalGlare::DrawSettings()
{
	ImGui::SliderFloat("Threshold", &settings.ThresholdEV, -6.f, 6.f, "%+.2f EV");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Brightness threshold for glare extraction.");

	ImGui::SliderFloat("Intensity", &settings.Intensity, 0.f, 2.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Overall glare intensity.");

	ImGui::SliderInt("Aperture Blades", &settings.ApertureBlades, 3, 10);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Number of aperture blades. Controls starburst pattern.");

	ImGui::SliderFloat("Aperture Rotation", &settings.ApertureRotation, -180.f, 180.f, "%.1f deg");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Rotation angle of the aperture.");

	ImGui::SliderFloat("Scatter Strength", &settings.ScatterStrength, 0.f, 3.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Controls the broad glow from eye scattering (CIE model).");

	ImGui::SliderFloat("Chromatic Dispersion", &settings.ChromaticDispersion, 0.f, 3.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Wavelength-dependent PSF scaling. Higher values produce colored fringes.");

	ImGui::SliderFloat("Adapt Speed", &settings.AdaptSpeed, 0.5f, 10.f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("How fast the glare adapts to brightness changes.");

	{
		const char* resNames[] = { "128", "256", "512" };
		int resValues[] = { 128, 256, 512 };
		int curIdx = 1;
		for (int i = 0; i < 3; i++)
			if (resValues[i] == settings.FFTResolution)
				curIdx = i;

		if (ImGui::Combo("FFT Resolution", &curIdx, resNames, 3))
			settings.FFTResolution = resValues[curIdx];

		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Resolution of the FFT convolution. Higher = sharper starburst but more expensive.");
	}

	if (ImGui::CollapsingHeader("Debug")) {
		if (texGlareResult)
			ImGui::Image(texGlareResult->srv.get(), { 256.f, 256.f });
	}
}

void PhysicalGlare::RestoreDefaultSettings()
{
	settings = {};
}

void PhysicalGlare::LoadSettings(json& o_json)
{
	settings = o_json;
}

void PhysicalGlare::SaveSettings(json& o_json)
{
	o_json = settings;
}

void PhysicalGlare::CreateFFTTextures(uint resolution)
{
	currentFFTResolution = resolution;
	psfDirty = true;

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

	// FFT ping-pong textures (RG32F) for 3 channels
	for (int ch = 0; ch < 3; ch++) {
		for (int pp = 0; pp < 2; pp++) {
			texFFT[ch][pp] = eastl::make_unique<Texture2D>(texDesc);
			texFFT[ch][pp]->CreateSRV(srvDesc);
			texFFT[ch][pp]->CreateUAV(uavDesc);
		}
	}

	// PSF FFT cache (RG32F) for 3 channels
	for (int ch = 0; ch < 3; ch++) {
		texPSF_FFT[ch] = eastl::make_unique<Texture2D>(texDesc);
		texPSF_FFT[ch]->CreateSRV(srvDesc);
		texPSF_FFT[ch]->CreateUAV(uavDesc);
	}

	// Glare result and history (RGBA16F, FFT resolution)
	D3D11_TEXTURE2D_DESC glareDesc = texDesc;
	glareDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = glareDesc.Format;
	uavDesc.Format = glareDesc.Format;

	texGlareResult = eastl::make_unique<Texture2D>(glareDesc);
	texGlareResult->CreateSRV(srvDesc);
	texGlareResult->CreateUAV(uavDesc);

	texGlarePrev = eastl::make_unique<Texture2D>(glareDesc);
	texGlarePrev->CreateSRV(srvDesc);
	texGlarePrev->CreateUAV(uavDesc);
}

void PhysicalGlare::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("PhysicalGlare: Creating buffers...");
	{
		glareCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<GlareCB>());
	}

	logger::debug("PhysicalGlare: Creating FFT textures...");
	{
		currentFFTResolution = std::clamp((uint)settings.FFTResolution, FFT_MIN, FFT_MAX);
		CreateFFTTextures(currentFFTResolution);
	}

	logger::debug("PhysicalGlare: Creating output texture...");
	{
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

		texDesc.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateUAV(uavDesc);
	}

	logger::debug("PhysicalGlare: Creating sampler...");
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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));
	}

	CompileComputeShaders();
}

void PhysicalGlare::ClearShaderCache()
{
	auto const shaderPtrs = std::array{
		&thresholdCS, &psfCS, &fftRowCS, &fftColCS, &fftRowInvCS, &fftColInvCS, &multiplyCS, &compositeCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void PhysicalGlare::CompileComputeShaders()
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
			{ &thresholdCS, "threshold.cs.hlsl", {}, "CS_Threshold" },
			{ &psfCS, "psf.cs.hlsl", {}, "CS_GeneratePSF" },
			{ &fftRowCS, "fft.cs.hlsl", { { "ROW_PASS", "" }, { "FORWARD", "" } }, "CS_FFT" },
			{ &fftColCS, "fft.cs.hlsl", { { "COL_PASS", "" }, { "FORWARD", "" } }, "CS_FFT" },
			{ &fftRowInvCS, "fft.cs.hlsl", { { "ROW_PASS", "" }, { "INVERSE", "" } }, "CS_FFT" },
			{ &fftColInvCS, "fft.cs.hlsl", { { "COL_PASS", "" }, { "INVERSE", "" } }, "CS_FFT" },
			{ &multiplyCS, "multiply.cs.hlsl", {}, "CS_Multiply" },
			{ &compositeCS, "composite.cs.hlsl", {}, "CS_Composite" },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\PhysicalGlare") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
}

bool PhysicalGlare::NeedsPSFRegeneration() const
{
	return psfDirty ||
	       cachedPSFParams.ApertureBlades != settings.ApertureBlades ||
	       cachedPSFParams.ApertureRotation != settings.ApertureRotation ||
	       cachedPSFParams.ScatterStrength != settings.ScatterStrength ||
	       cachedPSFParams.ChromaticDispersion != settings.ChromaticDispersion ||
	       cachedPSFParams.FFTResolution != settings.FFTResolution;
}

void PhysicalGlare::GeneratePSF()
{
	auto context = globals::d3d::context;

	// Build the CB data for PSF generation
	GlareCB cbData = {
		.Threshold = exp2(settings.ThresholdEV) * 0.125f,
		.Intensity = settings.Intensity,
		.ScatterStrength = settings.ScatterStrength,
		.ChromaticDispersion = settings.ChromaticDispersion,
		.ApertureBlades = settings.ApertureBlades,
		.ApertureRotation = settings.ApertureRotation * 3.14159265f / 180.f,
		.AdaptSpeed = settings.AdaptSpeed,
		.DeltaTime = 0.f,
		.FFTResolution = currentFFTResolution,
		.RcpFFTResolution = 1.f / (float)currentFFTResolution,
		.ScreenWidth = texOutput ? (float)texOutput->desc.Width : 1920.f,
		.ScreenHeight = texOutput ? (float)texOutput->desc.Height : 1080.f,
		.ChannelIndex = 0,
	};

	// Generate PSF into texFFT[ch][0] (real=PSF, imag=0) per channel
	for (int ch = 0; ch < 3; ch++) {
		cbData.ChannelIndex = (uint)ch;
		glareCB->Update(cbData);

		ID3D11Buffer* cb = glareCB->CB();
		context->CSSetConstantBuffers(1, 1, &cb);

		ID3D11UnorderedAccessView* uav = texFFT[ch][0]->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(psfCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		// Cleanup UAV before next use
		uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	}

	// FFT the PSF: row pass then col pass, store in texPSF_FFT
	for (int ch = 0; ch < 3; ch++) {
		// Row FFT: texFFT[ch][0] -> texFFT[ch][1]
		DispatchFFT(fftRowCS.get(), texFFT[ch][0].get(), texFFT[ch][1].get(), currentFFTResolution);
		// Col FFT: texFFT[ch][1] -> texPSF_FFT[ch]
		DispatchFFT(fftColCS.get(), texFFT[ch][1].get(), texPSF_FFT[ch].get(), currentFFTResolution);
	}

	// Cache parameters
	cachedPSFParams.ApertureBlades = settings.ApertureBlades;
	cachedPSFParams.ApertureRotation = settings.ApertureRotation;
	cachedPSFParams.ScatterStrength = settings.ScatterStrength;
	cachedPSFParams.ChromaticDispersion = settings.ChromaticDispersion;
	cachedPSFParams.FFTResolution = settings.FFTResolution;
	psfDirty = false;
}

void PhysicalGlare::DispatchFFT(ID3D11ComputeShader* shader, Texture2D* input, Texture2D* output, uint resolution)
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

void PhysicalGlare::Draw(TextureInfo& inout_tex)
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("Physical Glare");

	// Handle FFT resolution change
	uint targetRes = std::clamp((uint)settings.FFTResolution, FFT_MIN, FFT_MAX);
	if (targetRes != currentFFTResolution) {
		CreateFFTTextures(targetRes);
	}

	// Update constant buffer
	GlareCB cbData = {
		.Threshold = exp2(settings.ThresholdEV) * 0.125f,
		.Intensity = settings.Intensity,
		.ScatterStrength = settings.ScatterStrength,
		.ChromaticDispersion = settings.ChromaticDispersion,
		.ApertureBlades = settings.ApertureBlades,
		.ApertureRotation = settings.ApertureRotation * 3.14159265f / 180.f,
		.AdaptSpeed = settings.AdaptSpeed,
		.DeltaTime = *globals::game::deltaTime,
		.FFTResolution = currentFFTResolution,
		.RcpFFTResolution = 1.f / (float)currentFFTResolution,
		.ScreenWidth = (float)texOutput->desc.Width,
		.ScreenHeight = (float)texOutput->desc.Height,
		.ChannelIndex = 0,
	};
	glareCB->Update(cbData);

	ID3D11Buffer* cb = glareCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	// ========== Step 1: Regenerate PSF if parameters changed ==========
	if (NeedsPSFRegeneration()) {
		GeneratePSF();
	}

	// ========== Step 2: Threshold + downsample scene into FFT textures ==========
	{
		// We write R, G, B channels into texFFT[0..2][0]
		ID3D11ShaderResourceView* srv = inout_tex.srv;
		std::array<ID3D11UnorderedAccessView*, 3> uavs = {
			texFFT[0][0]->uav.get(),
			texFFT[1][0]->uav.get(),
			texFFT[2][0]->uav.get()
		};

		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(thresholdCS.get(), nullptr, 0);
		context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

		srv = nullptr;
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	}

	// ========== Step 3: Forward FFT on scene (per channel) ==========
	for (int ch = 0; ch < 3; ch++) {
		// Row FFT: texFFT[ch][0] -> texFFT[ch][1]
		DispatchFFT(fftRowCS.get(), texFFT[ch][0].get(), texFFT[ch][1].get(), currentFFTResolution);
		// Col FFT: texFFT[ch][1] -> texFFT[ch][0]
		DispatchFFT(fftColCS.get(), texFFT[ch][1].get(), texFFT[ch][0].get(), currentFFTResolution);
	}

	// ========== Step 4: Frequency-domain multiply (scene * PSF) ==========
	{
		// Input: texFFT[ch][0] (scene FFT), texPSF_FFT[ch]
		// Output: texFFT[ch][1]
		std::array<ID3D11ShaderResourceView*, 2> srvs = { nullptr, nullptr };
		std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };

		for (int ch = 0; ch < 3; ch++) {
			srvs[0] = texFFT[ch][0]->srv.get();
			srvs[1] = texPSF_FFT[ch]->srv.get();
			uavs[0] = texFFT[ch][1]->uav.get();

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(multiplyCS.get(), nullptr, 0);
			context->Dispatch((currentFFTResolution + 7) >> 3, (currentFFTResolution + 7) >> 3, 1);

			srvs.fill(nullptr);
			uavs.fill(nullptr);
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		}
	}

	// ========== Step 5: Inverse FFT (per channel) ==========
	for (int ch = 0; ch < 3; ch++) {
		// Row IFFT: texFFT[ch][1] -> texFFT[ch][0]
		DispatchFFT(fftRowInvCS.get(), texFFT[ch][1].get(), texFFT[ch][0].get(), currentFFTResolution);
		// Col IFFT: texFFT[ch][0] -> texFFT[ch][1]
		DispatchFFT(fftColInvCS.get(), texFFT[ch][0].get(), texFFT[ch][1].get(), currentFFTResolution);
	}

	// ========== Step 6: Composite (temporal blend + upsample + add to scene) ==========
	{
		// t0 = scene, t1/t2/t3 = IFFT result R/G/B (texFFT[ch][1]),
		// t4 = previous glare, u0 = output, u1 = glare history update
		std::array<ID3D11ShaderResourceView*, 5> srvs = {
			inout_tex.srv,
			texFFT[0][1]->srv.get(),
			texFFT[1][1]->srv.get(),
			texFFT[2][1]->srv.get(),
			texGlarePrev->srv.get()
		};
		std::array<ID3D11UnorderedAccessView*, 2> uavs = {
			texOutput->uav.get(),
			texGlareResult->uav.get()
		};
		ID3D11SamplerState* sampler = linearSampler.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(compositeCS.get(), nullptr, 0);

		float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
		res = Util::ConvertToDynamic(res);
		context->Dispatch(((uint)res.x + 7) >> 3, ((uint)res.y + 7) >> 3, 1);

		srvs.fill(nullptr);
		uavs.fill(nullptr);
		sampler = nullptr;
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetSamplers(0, 1, &sampler);
	}

	// ========== Step 7: Swap glare history ==========
	std::swap(texGlarePrev, texGlareResult);

	// Cleanup
	cb = nullptr;
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
}

void PhysicalGlare::Reset()
{
	psfDirty = true;
}
