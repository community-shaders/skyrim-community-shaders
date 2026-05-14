#include "ScreenSpaceGI.h"

#include <DirectXTex.h>

#include "Deferred.h"
#include "State.h"
#include "Upscaling.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceGI::REBLURSettings,
	MaxAccumulatedFrameNum,
	MaxFastAccumulatedFrameNum,
	MaxStabilizedFrameNum,
	HistoryFixFrameNum,
	HistoryFixBasePixelStride,
	HistoryFixAlternatePixelStride,
	FastHistoryClampingSigmaScale,
	MinHitDistanceWeight,
	MinBlurRadius,
	MaxBlurRadius,
	LobeAngleFraction,
	RoughnessFraction,
	PlaneDistanceSensitivity,
	SplitScreen,
	HitDistanceReconstructionMode)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceGI::Settings,
	Enabled,
	EnableGI,
	EnableVanillaSSAO,
	EnableSH,
	NumSteps,
	HalfRes,
	Thickness,
	AOPower,
	GIStrength,
	EnableREBLUR,
	Reblur)

////////////////////////////////////////////////////////////////////////////////////

void ScreenSpaceGI::RestoreDefaultSettings()
{
	settings = {};
	recompileFlag = true;
}

void ScreenSpaceGI::DrawSettings()
{
	static bool showAdvanced;

	if (!ShadersOK())
		ImGui::TextColored({ 1, 0, 0, 1 }, "Compute shaders failed to compile!");

	///////////////////////////////
	ImGui::SeparatorText("Toggles");

	ImGui::Checkbox("Show Advanced Options", &showAdvanced);

	if (ImGui::BeginTable("Toggles", 4)) {
		ImGui::TableNextColumn();
		ImGui::Checkbox("Enabled", &settings.Enabled);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enable Screen Space Global Illumination. When disabled, all other settings are ignored.");
		}

		ImGui::TableNextColumn();
		{
			auto ilToggleGuard = Util::DisableGuard(!settings.Enabled);
			recompileFlag |= ImGui::Checkbox("Indirect Lighting (IL)", &settings.EnableGI);
		}
		ImGui::TableNextColumn();
		{
			auto shGuard = Util::DisableGuard(!settings.Enabled || !settings.EnableGI);
			if (ImGui::Checkbox("SH Mode", &settings.EnableSH)) {
				recompileFlag = true;
				SetupNRDResources();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Use Spherical Harmonics for directional GI. Higher quality but more expensive.");
			}
		}
		ImGui::TableNextColumn();
		{
			auto vanillaSSAOGuard = Util::DisableGuard(globals::game::isVR);
			ImGui::Checkbox("Vanilla SSAO", &settings.EnableVanillaSSAO);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				if (globals::game::isVR)
					ImGui::Text("Vanilla SSAO is not supported in VR.");
				else
					ImGui::Text("Enable Skyrim's built-in SSAO. Usually disabled when using SSGI to avoid double-darkening.");
			}
		}

		ImGui::EndTable();
	}

	///////////////////////////////
	ImGui::SeparatorText("Quality/Performance");

	{
		auto qualityGuard = Util::DisableGuard(!settings.Enabled);

		if (ImGui::Checkbox("Half Resolution (Checkerboard)", &settings.HalfRes)) {
			recompileFlag = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Trace half the columns in a checkerboard pattern. NRD reconstructs the missing pixels.");
		}

		if (showAdvanced) {
			ImGui::SliderInt("Steps Per Slice", (int*)&settings.NumSteps, 1, 32);
		}
	}

	///////////////////////////////
	ImGui::SeparatorText("Visual");

	{
		auto visualGuard = Util::DisableGuard(!settings.Enabled);

		ImGui::SliderFloat("AO Power", &settings.AOPower, 0.f, 6.f, "%.2f");

		{
			auto ilGuard = Util::DisableGuard(!settings.EnableGI);
			ImGui::SliderFloat("IL Source Brightness", &settings.GIStrength, 0.f, 6.f, "%.2f");
		}

		ImGui::Separator();

		if (showAdvanced) {
			ImGui::Separator();
			ImGui::SliderFloat("Thickness", &settings.Thickness, 0.f, 128.0f, "%.1f units");
		}
	}

	///////////////////////////////
	ImGui::SeparatorText("REBLUR Denoiser");

	{
		auto denoiseGuard = Util::DisableGuard(!settings.Enabled);

		ImGui::Checkbox("Enable REBLUR", &settings.EnableREBLUR);

		if (settings.EnableREBLUR) {
			auto& r = settings.Reblur;

			if (showAdvanced) {
				ImGui::SeparatorText("Accumulation");
				{
					int v = (int)r.MaxAccumulatedFrameNum;
					if (ImGui::SliderInt("Max Accumulated Frames", &v, 1, (int)nrd::REBLUR_MAX_HISTORY_FRAME_NUM))
						r.MaxAccumulatedFrameNum = (uint32_t)v;

					v = (int)r.MaxFastAccumulatedFrameNum;
					if (ImGui::SliderInt("Max Fast Accumulated Frames", &v, 1, (int)r.MaxAccumulatedFrameNum))
						r.MaxFastAccumulatedFrameNum = (uint32_t)v;

					v = (int)r.MaxStabilizedFrameNum;
					if (ImGui::SliderInt("Max Stabilized Frames", &v, 0, (int)r.MaxAccumulatedFrameNum))
						r.MaxStabilizedFrameNum = (uint32_t)v;
				}

				ImGui::SeparatorText("Spatial Filter");
				{
					ImGui::SliderFloat("Min Blur Radius", &r.MinBlurRadius, 0.0f, 10.0f, "%.1f px");
					ImGui::SliderFloat("Max Blur Radius", &r.MaxBlurRadius, 0.0f, 60.0f, "%.1f px");
					ImGui::SliderFloat("Lobe Angle Fraction", &r.LobeAngleFraction, 0.0f, 1.0f, "%.2f");
					ImGui::SliderFloat("Roughness Fraction", &r.RoughnessFraction, 0.0f, 1.0f, "%.2f");
					ImGui::SliderFloat("Plane Distance Sensitivity", &r.PlaneDistanceSensitivity, 0.0f, 0.1f, "%.4f");
				}

				ImGui::SeparatorText("Quality");
				{
					ImGui::SliderFloat("Fast History Clamping Sigma", &r.FastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
					ImGui::SliderFloat("Min Hit Distance Weight", &r.MinHitDistanceWeight, 0.0001f, 0.2f, "%.4f");

					int v = (int)r.HistoryFixFrameNum;
					if (ImGui::SliderInt("History Fix Frame Num", &v, 0, 4))
						r.HistoryFixFrameNum = (uint32_t)v;

					v = (int)r.HistoryFixBasePixelStride;
					if (ImGui::SliderInt("History Fix Pixel Stride", &v, 1, 20))
						r.HistoryFixBasePixelStride = (uint32_t)v;
				}

				ImGui::SeparatorText("Debug");
				{
					ImGui::SliderFloat("Split Screen", &r.SplitScreen, 0.0f, 1.0f, "%.2f");

					static const char* hitDistReconModes[] = { "OFF", "AREA_3X3", "AREA_5X5" };
					int hdMode = (int)r.HitDistanceReconstructionMode;
					if (ImGui::Combo("Hit Distance Reconstruction", &hdMode, hitDistReconModes, 3))
						r.HitDistanceReconstructionMode = (uint32_t)hdMode;
				}
			}
		}
	}

	///////////////////////////////
	ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texNoise, debugRescale)
		BUFFER_VIEWER_NODE(texWorkingDepth, debugRescale)
		BUFFER_VIEWER_NODE(texPrevGeo, debugRescale)
		BUFFER_VIEWER_NODE(texRadiance, debugRescale)
		BUFFER_VIEWER_NODE(texNRDInput, debugRescale)
		BUFFER_VIEWER_NODE(texNRDOutput, debugRescale)
		if (texNRDInputSH1) BUFFER_VIEWER_NODE(texNRDInputSH1, debugRescale)
		if (texNRDOutputSH1) BUFFER_VIEWER_NODE(texNRDOutputSH1, debugRescale)
		BUFFER_VIEWER_NODE(texNRDMV, debugRescale)
		BUFFER_VIEWER_NODE(texNRDViewZ, debugRescale)
		BUFFER_VIEWER_NODE(texNRDNormalRoughness, debugRescale)

		ImGui::TreePop();
	}
}

void ScreenSpaceGI::LoadSettings(json& o_json)
{
	settings = o_json;
	recompileFlag = true;
}

void ScreenSpaceGI::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ScreenSpaceGI::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		ssgiCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSGICB>(), "SSGI::CB");
	}

	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 64,
			.Height = 64,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 5;

		{
			texRadiance = eastl::make_unique<Texture2D>(texDesc, "SSGI::Radiance");
			texRadiance->CreateSRV(srvDesc);

			for (uint i = 0; i < 5; ++i) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
					.Format = DXGI_FORMAT_R11G11B10_FLOAT,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texRadiance->resource.get(), &mipUavDesc, uavRadiance[i].put()));
				Util::SetResourceName(uavRadiance[i].get(), "SSGI::Radiance UAV mip%u", i);
			}

		}

		texDesc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;

		{
			texWorkingDepth = eastl::make_unique<Texture2D>(texDesc, "SSGI::WorkingDepth");
			texWorkingDepth->CreateSRV(srvDesc);
			for (int i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texWorkingDepth->resource.get(), &uavDesc, uavWorkingDepth[i].put()));
				Util::SetResourceName(uavWorkingDepth[i].get(), "SSGI::WorkingDepth UAV mip%d", i);
			}
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		{
			texNormal = eastl::make_unique<Texture2D>(texDesc, "SSGI::Normal");
			texNormal->CreateSRV(srvDesc);
			for (uint i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texNormal->resource.get(), &uavDesc, uavNormal[i].put()));
				Util::SetResourceName(uavNormal[i].get(), "SSGI::Normal UAV mip%u", i);
			}
		}

		uavDesc.Texture2D.MipSlice = 0;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		{
			texPrevGeo = eastl::make_unique<Texture2D>(texDesc, "SSGI::PrevGeo");
			texPrevGeo->CreateSRV(srvDesc);
			texPrevGeo->CreateUAV(uavDesc);
		}

		SetupNRDResources();
	}

	logger::debug("Loading noise texture...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ "Data\\Shaders\\ScreenSpaceGI\\fast_2uges.dds" };
			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texNoise = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "SSGI::Noise");

		D3D11_SHADER_RESOURCE_VIEW_DESC noiseSrvDesc = {
			.Format = texNoise->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texNoise->CreateSRV(noiseSrvDesc);
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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));
		Util::SetResourceName(linearClampSampler.get(), "SSGI::LinearClampSampler");

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointClampSampler.put()));
		Util::SetResourceName(pointClampSampler.get(), "SSGI::PointClampSampler");
	}

	CompileComputeShaders();
}

void ScreenSpaceGI::SetupNRDResources()
{
	uint32_t fullW, fullH;
	if (texRadiance) {
		fullW = texRadiance->desc.Width;
		fullH = texRadiance->desc.Height;
	} else {
		auto renderer = globals::game::renderer;
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		D3D11_TEXTURE2D_DESC mainDesc;
		mainTex.texture->GetDesc(&mainDesc);
		fullW = mainDesc.Width;
		fullH = mainDesc.Height;
	}

	D3D11_TEXTURE2D_DESC texDesc{
		.Width = fullW,
		.Height = fullH,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.SampleDesc = { 1, 0 },
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

	nrdReblur.Shutdown();

	texNRDInput = eastl::make_unique<Texture2D>(texDesc, "SSGI::NRDInput");
	texNRDInput->CreateSRV(srvDesc);
	texNRDInput->CreateUAV(uavDesc);

	texNRDOutput = eastl::make_unique<Texture2D>(texDesc, "SSGI::NRDOutput");
	texNRDOutput->CreateSRV(srvDesc);
	texNRDOutput->CreateUAV(uavDesc);

	if (settings.EnableSH) {
		texNRDInputSH1 = eastl::make_unique<Texture2D>(texDesc, "SSGI::NRDInputSH1");
		texNRDInputSH1->CreateSRV(srvDesc);
		texNRDInputSH1->CreateUAV(uavDesc);

		texNRDOutputSH1 = eastl::make_unique<Texture2D>(texDesc, "SSGI::NRDOutputSH1");
		texNRDOutputSH1->CreateSRV(srvDesc);
		texNRDOutputSH1->CreateUAV(uavDesc);
	} else {
		texNRDInputSH1.reset();
		texNRDOutputSH1.reset();
	}

	// NRD MV
	if (!texNRDMV) {
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		texNRDMV = eastl::make_unique<Texture2D>(texDesc, "SSGI::NRDMV");
		texNRDMV->CreateSRV(srvDesc);
		texNRDMV->CreateUAV(uavDesc);
	}

	// NRD ViewZ
	if (!texNRDViewZ) {
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texNRDViewZ = eastl::make_unique<Texture2D>(texDesc, "SSGI::NRDViewZ");
		texNRDViewZ->CreateSRV(srvDesc);
		texNRDViewZ->CreateUAV(uavDesc);
	}

	// NRD NormalRoughness
	if (!texNRDNormalRoughness) {
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		texNRDNormalRoughness = eastl::make_unique<Texture2D>(texDesc, "SSGI::NRDNormalRoughness");
		texNRDNormalRoughness->CreateSRV(srvDesc);
		texNRDNormalRoughness->CreateUAV(uavDesc);
	}

	auto denoiser = settings.EnableSH ? nrd::Denoiser::REBLUR_DIFFUSE_SH : nrd::Denoiser::REBLUR_DIFFUSE;
	nrdReblur.Init(fullW, fullH, denoiser, 0);

	globals::deferred->ClearShaderCache();
}

void ScreenSpaceGI::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&prefilterDepthsCompute, &prefilterRadianceCompute, &prefilterNormalCompute, &giCompute, &stereoSyncCompute, &prepareNRDGuidesCompute
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void ScreenSpaceGI::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &prefilterDepthsCompute, "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "" } } },
			{ &prefilterRadianceCompute, "prefilterRadiance.cs.hlsl", {} },
			{ &prefilterNormalCompute, "prefilterNormal.cs.hlsl", {} },
			{ &giCompute, "gi.cs.hlsl", {} },
		};

	if (REL::Module::IsVR())
		shaderInfos.push_back({ &stereoSyncCompute, "stereoSync.cs.hlsl", { { "FRAMEBUFFER", "" } } });
	for (auto& info : shaderInfos) {
		if (REL::Module::IsVR())
			info.defines.push_back({ "VR", "" });
		if (settings.EnableGI)
			info.defines.push_back({ "GI", "" });
		if (settings.EnableSH && settings.EnableGI)
			info.defines.push_back({ "SSGI_SH", "" });
		if (settings.HalfRes)
			info.defines.push_back({ "SSGI_HALF", "" });
	}

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\ScreenSpaceGI") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
	}

	// NRD guide prep shader (from ScreenSpaceGI directory)
	{
		std::vector<std::pair<const char*, const char*>> defines;
		if (REL::Module::IsVR())
			defines.push_back({ "VR", "" });
		auto path = std::filesystem::path("Data\\Shaders\\ScreenSpaceGI") / "prepareNRDGuides.cs.hlsl";
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), defines, "cs_5_0")))
			prepareNRDGuidesCompute.attach(rawPtr);
	}

	recompileFlag = false;
}

bool ScreenSpaceGI::ShadersOK()
{
	return texNoise && prefilterDepthsCompute && prefilterRadianceCompute && prefilterNormalCompute && giCompute;
}

void ScreenSpaceGI::UpdateSB()
{
	float2 res = { (float)texRadiance->desc.Width, (float)texRadiance->desc.Height };
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	static float4x4 prevInvView[2] = {};

	SSGICB data;
	{
		for (int eyeIndex = 0; eyeIndex < (1 + REL::Module::IsVR()); ++eyeIndex) {
			auto eye = Util::GetCameraData(eyeIndex);

			data.PrevInvViewMat[eyeIndex] = prevInvView[eyeIndex];
			data.NDCToViewMul[eyeIndex] = { 2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1) };
			data.NDCToViewAdd[eyeIndex] = { -1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1) };
			if (REL::Module::IsVR())
				data.NDCToViewMul[eyeIndex].x *= 2;

			prevInvView[eyeIndex] = eye.viewMat.Invert();
		}

		data.TexDim = res;
		data.RcpTexDim = float2(1.0f) / res;
		data.FrameDim = dynres;
		data.RcpFrameDim = float2(1.0f) / dynres;
		data.FrameIndex = globals::state->frameCount;

		data.NumSteps = settings.NumSteps;

		data.Thickness = settings.Thickness;
		data.AOPower = settings.AOPower;
		data.GIStrength = settings.GIStrength;
	}

	ssgiCB->Update(data);
}

void ScreenSpaceGI::DrawSSGI()
{
	auto context = globals::d3d::context;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISSAOBlurH, imageSpaceManager);

	static bool* enableSSAO = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(BSImagespaceShaderISSAOBlurH.get()) + 0x50LL);
	*enableSSAO = settings.EnableVanillaSSAO;

	if (!(settings.Enabled && ShadersOK())) {
		return;
	}

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "SSGI");

	//////////////////////////////////////////////////////

	if (recompileFlag)
		ClearShaderCache();

	UpdateSB();

	//////////////////////////////////////////////////////

	auto renderer = globals::game::renderer;
	auto rts = renderer->GetRuntimeData().renderTargets;
	auto deferred = globals::deferred;

	float2 size = Util::ConvertToDynamic(globals::state->screenSize);
	auto resolution = std::array{ (uint)size.x, (uint)size.y };

	std::array<ID3D11ShaderResourceView*, 11> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 6> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { pointClampSampler.get(), linearClampSampler.get() };
	auto cb = ssgiCB->CB();

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	//////////////////////////////////////////////////////

	context->CSSetConstantBuffers(1, 1, &cb);
	auto* sharedDataBuf = globals::state->sharedDataCB->CB();
	context->CSSetConstantBuffers(5, 1, &sharedDataBuf);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	float2 dynres = Util::ConvertToDynamic(globals::state->screenSize);
	dynres = { floor(dynres.x), floor(dynres.y) };

	// prefilter depths
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Depths");

		srvs.at(0) = Util::GetCurrentSceneDepthSRV();
		for (int i = 0; i < 5; ++i)
			uavs.at(i) = uavWorkingDepth[i].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(prefilterDepthsCompute.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 15) >> 4, (resolution[1] + 15) >> 4, 1);
	}

	// NRD guide textures
	if (prepareNRDGuidesCompute && settings.EnableREBLUR) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - NRD Guide Preprocess");

		auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		auto normal = rts[NORMALROUGHNESS];
		auto motion = rts[RE::RENDER_TARGETS::kMOTION_VECTOR];

		resetViews();
		std::array<ID3D11ShaderResourceView*, 2> guideSRVs = {
			depth.depthSRV,
			normal.SRV
		};
		std::array<ID3D11UnorderedAccessView*, 2> guideUAVs = {
			texNRDViewZ->uav.get(),
			texNRDNormalRoughness->uav.get()
		};

		context->CSSetShaderResources(0, (uint)guideSRVs.size(), guideSRVs.data());
		context->CSSetUnorderedAccessViews(0, (uint)guideUAVs.size(), guideUAVs.data(), nullptr);
		context->CSSetShader(prepareNRDGuidesCompute.get(), nullptr, 0);
		context->Dispatch(((uint)dynres.x + 7) / 8, ((uint)dynres.y + 7) / 8, 1);

		std::array<ID3D11ShaderResourceView*, 2> nullSRVs = { nullptr };
		std::array<ID3D11UnorderedAccessView*, 2> nullUAVs = { nullptr };
		context->CSSetShaderResources(0, (uint)nullSRVs.size(), nullSRVs.data());
		context->CSSetUnorderedAccessViews(0, (uint)nullUAVs.size(), nullUAVs.data(), nullptr);

		// Motion Vector is used as both SRV and as UAV, copy the original over before dispatching
		context->CopyResource(texNRDMV->resource.get(), motion.texture);
	}

	// Prefilter radiance mip chain (reads main RT directly)
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Radiance");

		resetViews();
		srvs.at(0) = rts[deferred->forwardRenderTargets[0]].SRV;
		uavs.at(0) = uavRadiance[0].get();
		uavs.at(1) = uavRadiance[1].get();
		uavs.at(2) = uavRadiance[2].get();
		uavs.at(3) = uavRadiance[3].get();
		uavs.at(4) = uavRadiance[4].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterRadianceCompute.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 15u) >> 4, (resolution[1] + 15u) >> 4, 1);
	}

	// Prefilter normals
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Normals");

		resetViews();
		srvs.at(0) = rts[NORMALROUGHNESS].SRV;
		uavs.at(0) = uavNormal[0].get();
		uavs.at(1) = uavNormal[1].get();
		uavs.at(2) = uavNormal[2].get();
		uavs.at(3) = uavNormal[3].get();
		uavs.at(4) = uavNormal[4].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterNormalCompute.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 15u) >> 4, (resolution[1] + 15u) >> 4, 1);
	}

	// GI → NRD output
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - GI");

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(2) = texRadiance->srv.get();
		srvs.at(3) = texNoise->srv.get();
		srvs.at(8) = texNormal->srv.get();

		uavs.at(0) = texNRDInput->uav.get();
		uavs.at(1) = texPrevGeo->uav.get();
		if (settings.EnableSH && texNRDInputSH1)
			uavs.at(2) = texNRDInputSH1->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(giCompute.get(), nullptr, 0);

		uint dispatchX = settings.HalfRes ? (resolution[0] + 1) / 2 : resolution[0];
		uint dispatchY = resolution[1];
		context->Dispatch((dispatchX + 7u) >> 3, (dispatchY + 7u) >> 3, 1);
	}

	// REBLUR denoising
	if (settings.EnableREBLUR && nrdReblur.IsValid()) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - REBLUR");

		nrd::CommonSettings commonSettings{};
		{
			uint16_t fw = (uint16_t)dynres.x;
			uint16_t fh = (uint16_t)dynres.y;

			commonSettings.resourceSize[0] = (uint16_t)texNRDInput->desc.Width;
			commonSettings.resourceSize[1] = (uint16_t)texNRDInput->desc.Height;
			commonSettings.resourceSizePrev[0] = commonSettings.resourceSize[0];
			commonSettings.resourceSizePrev[1] = commonSettings.resourceSize[1];
			commonSettings.rectSize[0] = fw;
			commonSettings.rectSize[1] = fh;
			commonSettings.rectSizePrev[0] = fw;
			commonSettings.rectSizePrev[1] = fh;

			auto viewMat = globals::game::frameBufferCached.GetCameraView(0).Transpose();
			auto projMat = globals::game::frameBufferCached.GetCameraProj(0).Transpose();

			float3 cameraWorldPos = float3(globals::game::frameBufferCached.GetCameraPosAdjust(0));
			DirectX::XMMATRIX translationMat = DirectX::XMMatrixTranslation(-cameraWorldPos.x, -cameraWorldPos.y, -cameraWorldPos.z);
			worldToViewMat = DirectX::XMMatrixMultiply(translationMat, viewMat);

			memcpy(commonSettings.viewToClipMatrix, &projMat, sizeof(float) * 16);
			memcpy(commonSettings.viewToClipMatrixPrev, &prevProjMatrix, sizeof(float) * 16);
			memcpy(commonSettings.worldToViewMatrix, &worldToViewMat, sizeof(float) * 16);
			memcpy(commonSettings.worldToViewMatrixPrev, &prevWorldToViewMat, sizeof(float) * 16);

			commonSettings.motionVectorScale[0] = 1.0f;
			commonSettings.motionVectorScale[1] = 1.0f;
			commonSettings.motionVectorScale[2] = 0.0f;
			commonSettings.isMotionVectorInWorldSpace = false;

			auto jitter = globals::features::upscaling.jitter;
			commonSettings.cameraJitter[0] = jitter.x;
			commonSettings.cameraJitter[1] = jitter.y;
			commonSettings.cameraJitterPrev[0] = prevJitter.x;
			commonSettings.cameraJitterPrev[1] = prevJitter.y;

			commonSettings.frameIndex = frameIndex++;
			commonSettings.denoisingRange = 1e6f;
			commonSettings.splitScreen = settings.Reblur.SplitScreen;

			prevWorldToViewMat = worldToViewMat;
			prevProjMatrix = projMat;
			prevJitter = jitter;
		}
		nrdReblur.SetCommonSettings(commonSettings);

		{
			const auto& r = settings.Reblur;
			reblurSettings.maxAccumulatedFrameNum = std::min((uint32_t)r.MaxAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
			reblurSettings.maxFastAccumulatedFrameNum = std::min((uint32_t)r.MaxFastAccumulatedFrameNum, reblurSettings.maxAccumulatedFrameNum);
			reblurSettings.maxStabilizedFrameNum = std::min((uint32_t)r.MaxStabilizedFrameNum, reblurSettings.maxAccumulatedFrameNum);
			reblurSettings.historyFixFrameNum = reblurSettings.maxFastAccumulatedFrameNum > 0 ? std::min((uint32_t)r.HistoryFixFrameNum, reblurSettings.maxFastAccumulatedFrameNum - 1) : 0;
			reblurSettings.historyFixBasePixelStride = std::max(r.HistoryFixBasePixelStride, 1u);
			reblurSettings.historyFixAlternatePixelStride = std::max(r.HistoryFixAlternatePixelStride, 1u);
			reblurSettings.fastHistoryClampingSigmaScale = std::clamp(r.FastHistoryClampingSigmaScale, 1.0f, 3.0f);
			reblurSettings.diffusePrepassBlurRadius = 0.0f;
			reblurSettings.minHitDistanceWeight = std::clamp(r.MinHitDistanceWeight, 0.0001f, 0.2f);
			reblurSettings.minBlurRadius = std::max(r.MinBlurRadius, 0.0f);
			reblurSettings.maxBlurRadius = std::max(r.MaxBlurRadius, reblurSettings.minBlurRadius);
			reblurSettings.lobeAngleFraction = std::clamp(r.LobeAngleFraction, 0.0f, 1.0f);
			reblurSettings.roughnessFraction = std::clamp(r.RoughnessFraction, 0.0f, 1.0f);
			reblurSettings.planeDistanceSensitivity = std::max(r.PlaneDistanceSensitivity, 0.0f);
			reblurSettings.enableAntiFirefly = false;
			reblurSettings.hitDistanceReconstructionMode = static_cast<nrd::HitDistanceReconstructionMode>(std::min(r.HitDistanceReconstructionMode, 2u));
			reblurSettings.checkerboardMode = settings.HalfRes ? nrd::CheckerboardMode::WHITE : nrd::CheckerboardMode::OFF;
		}
		nrdReblur.SetDenoiserSettings(&reblurSettings);

		auto motion = rts[RE::RENDER_TARGETS::kMOTION_VECTOR];
		nrdReblur.SetNamedSRV(nrd::ResourceType::IN_MV, texNRDMV->srv.get());
		nrdReblur.SetNamedSRV(nrd::ResourceType::IN_NORMAL_ROUGHNESS, texNRDNormalRoughness->srv.get());
		nrdReblur.SetNamedSRV(nrd::ResourceType::IN_VIEWZ, texNRDViewZ->srv.get());
		nrdReblur.SetNamedUAV(nrd::ResourceType::IN_MV, texNRDMV->uav.get());

		if (settings.EnableSH && texNRDInputSH1) {
			nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_SH0, texNRDInput->srv.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_SH1, texNRDInputSH1->srv.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_SH0, texNRDOutput->srv.get());
			nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_SH0, texNRDOutput->uav.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_SH1, texNRDOutputSH1->srv.get());
			nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_SH1, texNRDOutputSH1->uav.get());
		} else {
			nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, texNRDInput->srv.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, texNRDOutput->srv.get());
			nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, texNRDOutput->uav.get());
		}

		nrdReblur.Dispatch();
	}

	// cleanup
	resetViews();

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
}

ID3D11ShaderResourceView* ScreenSpaceGI::GetDiffuseOutputTexture()
{
	if (loaded && settings.Enabled && settings.EnableREBLUR && nrdReblur.IsValid())
		return texNRDOutput->srv.get();
	else if (loaded && settings.Enabled)
		return texNRDInput->srv.get();
	return nullptr;
}

ID3D11ShaderResourceView* ScreenSpaceGI::GetDiffuseSH1Texture()
{
	if (!loaded || !settings.Enabled || !settings.EnableSH)
		return nullptr;
	if (settings.EnableREBLUR && nrdReblur.IsValid() && texNRDOutputSH1)
		return texNRDOutputSH1->srv.get();
	else if (texNRDInputSH1)
		return texNRDInputSH1->srv.get();
	return nullptr;
}

ScreenSpaceGI::SharedData ScreenSpaceGI::GetCommonBufferData()
{
	SharedData data;
	data.DiffuseMult = (settings.Enabled && settings.EnableGI) ? settings.GIStrength : 0.0f;
	data.DebugMode = 0;
	data._pad = { 0, 0 };
	return data;
}
