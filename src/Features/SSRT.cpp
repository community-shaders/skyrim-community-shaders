#include "SSRT.h"

#include "Deferred.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SSRT::Settings,
	Enabled,
	EnableVanillaSSAO,
	RotationCount,
	StepCount,
	Radius,
	ExpFactor,
	JitterSamples,
	ScreenSpaceSampling,
	MipOptimization,
	GIIntensity,
	NormalApproximation,
	AOIntensity,
	Thickness,
	LinearThickness,
	EnableNRD,
	HitDistA,
	HitDistB,
	HitDistC,
	HitDistD,
	MaxAccumulatedFrameNum,
	MaxFastAccumulatedFrameNum,
	DiffusePrepassBlurRadius,
	MinBlurRadius,
	MaxBlurRadius,
	EnableAntiFirefly)

////////////////////////////////////////////////////////////////////////////////////

void SSRT::RestoreDefaultSettings()
{
	settings = {};
	recompileFlag = true;
}

void SSRT::DrawSettings()
{
	static bool showAdvanced;

	if (!ShadersOK())
		ImGui::TextColored({ 1, 0, 0, 1 }, "Compute shaders failed to compile!");

	///////////////////////////////
	ImGui::SeparatorText("Toggles");

	ImGui::Checkbox("Show Advanced Options", &showAdvanced);

	if (ImGui::BeginTable("Toggles", 2)) {
		ImGui::TableNextColumn();
		ImGui::Checkbox("Enabled", &settings.Enabled);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enable SSRT. When disabled, all other settings are ignored.");
		}

		ImGui::TableNextColumn();
		ImGui::Checkbox("Vanilla SSAO", &settings.EnableVanillaSSAO);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enable Skyrim's built-in SSAO. Usually disabled when using SSRT to avoid double-darkening.");
		}

		ImGui::EndTable();
	}

	///////////////////////////////
	ImGui::SeparatorText("Sampling");

	{
		auto samplingGuard = Util::DisableGuard(!settings.Enabled);

		if (ImGui::BeginTable("Presets", 3)) {
			ImGui::TableNextColumn();
			if (ImGui::Button("Low", { -1, 0 })) {
				settings.RotationCount = 1;
				settings.StepCount = 8;
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("1 rotation, 8 steps");

			ImGui::TableNextColumn();
			if (ImGui::Button("Standard", { -1, 0 })) {
				settings.RotationCount = 1;
				settings.StepCount = 12;
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("1 rotation, 12 steps");

			ImGui::TableNextColumn();
			if (ImGui::Button("High", { -1, 0 })) {
				settings.RotationCount = 2;
				settings.StepCount = 16;
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("2 rotations, 16 steps");

			ImGui::EndTable();
		}

		if (showAdvanced) {
			ImGui::SliderInt("Rotations", (int*)&settings.RotationCount, 1, 4);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					"How many directions to sample.\n"
					"Controls noise level.");

			ImGui::SliderInt("Steps Per Rotation", (int*)&settings.StepCount, 1, 32);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					"How many samples per direction.\n"
					"Controls accuracy and noise.");

			ImGui::SliderFloat("Exponential Factor", &settings.ExpFactor, 1.f, 3.f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Controls the exponential step distribution.\nHigher values concentrate samples near the pixel.");

			ImGui::Checkbox("Jitter Samples", &settings.JitterSamples);
			ImGui::Checkbox("Screen Space Sampling", &settings.ScreenSpaceSampling);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Use screen-space sampling instead of world-space. May give different results at different distances.");

			ImGui::Checkbox("Mip Optimization", &settings.MipOptimization);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Use lower mip levels for distant samples to reduce bandwidth.");
		}
	}

	///////////////////////////////
	ImGui::SeparatorText("GI");

	{
		auto giGuard = Util::DisableGuard(!settings.Enabled);

		ImGui::SliderFloat("GI Intensity", &settings.GIIntensity, 0.f, 100.f, "%.1f");

		if (showAdvanced) {
			recompileFlag |= ImGui::Checkbox("Normal Approximation", &settings.NormalApproximation);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Derive normals from sample positions instead of reading from GBuffer.\nFaster but less accurate.");
		}
	}

	///////////////////////////////
	ImGui::SeparatorText("Occlusion");

	{
		auto occGuard = Util::DisableGuard(!settings.Enabled);

		ImGui::SliderFloat("AO Intensity", &settings.AOIntensity, 0.f, 4.f, "%.2f");

		ImGui::SliderFloat("Radius", &settings.Radius, 1.f, 512.f, "%.1f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			std::vector<std::string> tooltipLines = {
				"World-space effect radius.",
				Util::Units::FormatDistance(settings.Radius)
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}

		if (showAdvanced) {
			ImGui::SliderFloat("Thickness", &settings.Thickness, 0.01f, 128.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Controls the assumed thickness of occluders.");

			ImGui::Checkbox("Linear Thickness", &settings.LinearThickness);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Scale thickness linearly with depth.");
		}
	}

	///////////////////////////////
	ImGui::SeparatorText("NRD REBLUR");

	{
		auto nrdGuard = Util::DisableGuard(!settings.Enabled);

		ImGui::Checkbox("Enable NRD", &settings.EnableNRD);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Use NVIDIA Real-time Denoiser (REBLUR) for temporal denoising of GI.");

		auto nrdSettingsGuard = Util::DisableGuard(!settings.EnableNRD);

		ImGui::SliderFloat("Hit Dist A", &settings.HitDistA, 1.f, 500.f, "%.1f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Typical hit distance in Skyrim units.");

		ImGui::SliderFloat("Hit Dist B", &settings.HitDistB, 0.01f, 1.f, "%.3f");
		ImGui::SliderFloat("Hit Dist C", &settings.HitDistC, 1.f, 100.f, "%.1f");
		ImGui::SliderFloat("Hit Dist D", &settings.HitDistD, -50.f, 0.f, "%.1f");

		{
			int v = (int)settings.MaxAccumulatedFrameNum;
			if (ImGui::SliderInt("Max Accumulated Frames", &v, 1, (int)nrd::REBLUR_MAX_HISTORY_FRAME_NUM))
				settings.MaxAccumulatedFrameNum = (uint)v;
		}
		{
			int v = (int)settings.MaxFastAccumulatedFrameNum;
			if (ImGui::SliderInt("Max Fast Accumulated Frames", &v, 1, (int)settings.MaxAccumulatedFrameNum))
				settings.MaxFastAccumulatedFrameNum = (uint)v;
		}

		ImGui::SliderFloat("Prepass Blur Radius", &settings.DiffusePrepassBlurRadius, 0.f, 75.f, "%.1f");
		ImGui::SliderFloat("Min Blur Radius", &settings.MinBlurRadius, 0.f, 10.f, "%.1f");
		ImGui::SliderFloat("Max Blur Radius", &settings.MaxBlurRadius, 0.f, 100.f, "%.1f");
		ImGui::Checkbox("Anti-Firefly", &settings.EnableAntiFirefly);
	}

	///////////////////////////////
	ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texGIOcclusion, debugRescale)
		BUFFER_VIEWER_NODE(texWorkingDepth, debugRescale)
		BUFFER_VIEWER_NODE(texNormals, debugRescale)
		BUFFER_VIEWER_NODE(texRadiance, debugRescale)
		BUFFER_VIEWER_NODE(texNRDViewZ, debugRescale)
		BUFFER_VIEWER_NODE(texNRDNormalRoughness, debugRescale)
		BUFFER_VIEWER_NODE(texNRDInputSH0, debugRescale)
		BUFFER_VIEWER_NODE(texNRDInputSH1, debugRescale)
		BUFFER_VIEWER_NODE(texNRDOutputSH0, debugRescale)
		BUFFER_VIEWER_NODE(texNRDOutputSH1, debugRescale)

		ImGui::TreePop();
	}
}

void SSRT::LoadSettings(json& o_json)
{
	settings = o_json;
	recompileFlag = true;
}

void SSRT::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SSRT::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		ssrtCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSRTCB>());
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
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		// Prefiltered depth (R16F, 5 mip levels)
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 5;
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16_FLOAT;
		{
			texWorkingDepth = eastl::make_unique<Texture2D>(texDesc);
			texWorkingDepth->CreateSRV(srvDesc);
			for (uint i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texWorkingDepth->resource.get(), &uavDesc, uavWorkingDepth[i].put()));
			}
		}

		// Prefiltered radiance (R11G11B10F, 5 mip levels)
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		{
			texRadiance = eastl::make_unique<Texture2D>(texDesc);
			texRadiance->CreateSRV(srvDesc);
			for (uint i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texRadiance->resource.get(), &uavDesc, uavRadiance[i].put()));
			}
		}

		// Prefiltered normals (R8G8_UNORM, 5 mip levels)
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		{
			texNormals = eastl::make_unique<Texture2D>(texDesc);
			texNormals->CreateSRV(srvDesc);
			for (uint i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texNormals->resource.get(), &uavDesc, uavNormals[i].put()));
			}
		}

		// GI+Occlusion output (RGBA16F, single combined output matching SSRT3)
		uavDesc.Texture2D.MipSlice = 0;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		{
			texGIOcclusion = eastl::make_unique<Texture2D>(texDesc);
			texGIOcclusion->CreateSRV(srvDesc);
			texGIOcclusion->CreateUAV(uavDesc);
		}

		// NRD textures (full-res, single mip)
		// NRD SH textures (RGBA16F)
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		{
			texNRDInputSH0 = eastl::make_unique<Texture2D>(texDesc);
			texNRDInputSH0->CreateSRV(srvDesc);
			texNRDInputSH0->CreateUAV(uavDesc);

			texNRDInputSH1 = eastl::make_unique<Texture2D>(texDesc);
			texNRDInputSH1->CreateSRV(srvDesc);
			texNRDInputSH1->CreateUAV(uavDesc);

			texNRDOutputSH0 = eastl::make_unique<Texture2D>(texDesc);
			texNRDOutputSH0->CreateSRV(srvDesc);
			texNRDOutputSH0->CreateUAV(uavDesc);

			texNRDOutputSH1 = eastl::make_unique<Texture2D>(texDesc);
			texNRDOutputSH1->CreateSRV(srvDesc);
			texNRDOutputSH1->CreateUAV(uavDesc);
		}

		// NRD ViewZ (R32F)
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		{
			texNRDViewZ = eastl::make_unique<Texture2D>(texDesc);
			texNRDViewZ->CreateSRV(srvDesc);
			texNRDViewZ->CreateUAV(uavDesc);
		}

		// NRD NormalRoughness (R8G8B8A8_UNORM)
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		{
			texNRDNormalRoughness = eastl::make_unique<Texture2D>(texDesc);
			texNRDNormalRoughness->CreateSRV(srvDesc);
			texNRDNormalRoughness->CreateUAV(uavDesc);
		}

		// Initialize NRD REBLUR
		{
			uint32_t fullW = texDesc.Width;
			uint32_t fullH = texDesc.Height;
			nrdReblur.Init(fullW, fullH, nrd::Denoiser::REBLUR_DIFFUSE_SH, 0);
		}
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

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointClampSampler.put()));
	}

	CompileComputeShaders();
}

void SSRT::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&prefilterDepthsCompute, &prefilterRadianceCompute, &prefilterNormalsCompute, &ssrtCSCompute,
		&prepareNRDGuidesCompute, &resolveNRDCompute
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void SSRT::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &prefilterDepthsCompute, "prefilterDepths.cs.hlsl", {} },
			{ &prefilterRadianceCompute, "prefilterRadiance.cs.hlsl", {} },
			{ &prefilterNormalsCompute, "prefilterNormals.cs.hlsl", {} },
			{ &ssrtCSCompute, "SSRTCS.cs.hlsl", {} },
			{ &prepareNRDGuidesCompute, "prepareNRDGuides.cs.hlsl", {} },
			{ &resolveNRDCompute, "resolveNRD.cs.hlsl", {} },
		};

	if (settings.NormalApproximation)
		shaderInfos.back().defines.push_back({ "NORMAL_APPROXIMATION", "" });

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\SSRT") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
	}

	recompileFlag = false;
}

bool SSRT::ShadersOK()
{
	return prefilterDepthsCompute && prefilterRadianceCompute && prefilterNormalsCompute && ssrtCSCompute && prepareNRDGuidesCompute && resolveNRDCompute;
}

void SSRT::UpdateCB()
{
	float2 res = globals::state->screenSize;
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	SSRTCB data = {};
	{
		{
			auto eye = Util::GetCameraData(0);

			float2 mul = { 2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1) };
			float2 add = { -1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1) };

			data.NDCToViewMul.x = mul.x;
			data.NDCToViewMul.y = mul.y;
			data.NDCToViewAdd.x = add.x;
			data.NDCToViewAdd.y = add.y;
		}

		data.TexDim = res;
		data.RcpTexDim = float2(1.0f) / res;
		data.FrameDim = dynres;
		data.RcpFrameDim = float2(1.0f) / dynres;
		data.FrameIndex = globals::state->frameCount;

		data.RotationCount = settings.RotationCount;
		data.StepCount = settings.StepCount;
		data.Radius = settings.Radius;
		data.ExpFactor = settings.ExpFactor;

		// Compute HalfProjScale: renderHeight / (2 * tan(fov/2)) * 0.5
		// Matching SSRT3: projScale = renderResolution.y / (Mathf.Tan(cam.fieldOfView * Mathf.Deg2Rad * 0.5f) * 2) * 0.5f
		auto eye = Util::GetCameraData(0);
		float tanHalfFov = 1.0f / eye.projMat(1, 1);
		data.HalfProjScale = dynres.y / (2.0f * tanHalfFov) * 0.5f;

		data.JitterSamples = settings.JitterSamples ? 1u : 0u;
		data.ScreenSpaceSampling = settings.ScreenSpaceSampling ? 1u : 0u;
		data.MipOptimization = settings.MipOptimization ? 1u : 0u;

		data.GIIntensity = settings.GIIntensity;
		data.AOIntensity = settings.AOIntensity;
		data.Thickness = settings.Thickness;
		data.LinearThickness = settings.LinearThickness ? 1u : 0u;

		// SSRT3 temporal noise pattern
		// temporalRotations = { 60, 300, 180, 240, 120, 0 }
		// spatialOffsets = { 0, 0.5, 0.25, 0.75 }
		static constexpr float temporalRotations[] = { 60.f, 300.f, 180.f, 240.f, 120.f, 0.f };
		static constexpr float spatialOffsets[] = { 0.f, 0.5f, 0.25f, 0.75f };

		uint frameCount = globals::state->frameCount;
		float temporalRotation = temporalRotations[frameCount % 6];
		float temporalOffset = spatialOffsets[(frameCount / 6) % 4];

		data.TemporalOffsets = temporalOffset;
		data.TemporalDirections = temporalRotation / 360.0f;
		data.pad0 = 0.f;

		data.HitDistA = settings.HitDistA;
		data.HitDistB = settings.HitDistB;
		data.HitDistC = settings.HitDistC;
		data.HitDistD = settings.HitDistD;
	}

	ssrtCB->Update(data);
}

void SSRT::DrawSSRT()
{
	auto context = globals::d3d::context;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISSAOBlurH, imageSpaceManager);

	// Toggle vanilla SSAO
	static bool* enableSSAO = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(BSImagespaceShaderISSAOBlurH.get()) + 0x50LL);
	*enableSSAO = settings.EnableVanillaSSAO;

	if (!(settings.Enabled && ShadersOK())) {
		FLOAT clr[4] = { 0.f, 0.f, 0.f, 1.f };
		context->ClearUnorderedAccessViewFloat(texGIOcclusion->uav.get(), clr);
		return;
	}

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "SSRT");

	if (recompileFlag)
		ClearShaderCache();

	UpdateCB();

	//////////////////////////////////////////////////////

	auto renderer = globals::game::renderer;
	auto rts = renderer->GetRuntimeData().renderTargets;
	auto deferred = globals::deferred;

	float2 size = Util::ConvertToDynamic(globals::state->screenSize);
	auto resolution = std::array{ (uint)size.x, (uint)size.y };

	std::array<ID3D11ShaderResourceView*, 3> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 5> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { pointClampSampler.get(), linearClampSampler.get() };
	auto cb = ssrtCB->CB();

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

	// Prefilter depths
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Prefilter Depths");

		srvs.at(0) = Util::GetCurrentSceneDepthSRV();
		for (uint i = 0; i < 5; ++i)
			uavs.at(i) = uavWorkingDepth[i].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterDepthsCompute.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 15u) >> 4, (resolution[1] + 15u) >> 4, 1);
	}

	// Prefilter radiance
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Prefilter Radiance");

		resetViews();
		srvs.at(0) = rts[deferred->forwardRenderTargets[0]].SRV;
		for (uint i = 0; i < 5; ++i)
			uavs.at(i) = uavRadiance[i].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterRadianceCompute.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 15u) >> 4, (resolution[1] + 15u) >> 4, 1);
	}

	// Prefilter normals
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Prefilter Normals");

		resetViews();
		srvs.at(0) = rts[deferred->normalRoughnessRT].SRV;
		for (uint i = 0; i < 5; ++i)
			uavs.at(i) = uavNormals[i].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterNormalsCompute.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 15u) >> 4, (resolution[1] + 15u) >> 4, 1);
	}

	// SSRT main pass
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Main");

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = texNormals->srv.get();
		srvs.at(2) = texRadiance->srv.get();

		std::array<ID3D11UnorderedAccessView*, 3> mainUavs = {
			texGIOcclusion->uav.get(),
			texNRDInputSH0->uav.get(),
			texNRDInputSH1->uav.get()
		};

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)mainUavs.size(), mainUavs.data(), nullptr);
		context->CSSetShader(ssrtCSCompute.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);
	}

	// NRD path
	if (settings.EnableNRD && nrdReblur.IsValid() && prepareNRDGuidesCompute && resolveNRDCompute) {
		auto depth = Util::GetCurrentSceneDepthSRV();

		// NRD guide preprocess
		{
			TracyD3D11Zone(globals::state->tracyCtx, "SSRT - NRD Guide Preprocess");

			resetViews();
			std::array<ID3D11ShaderResourceView*, 2> guideSRVs = {
				depth,
				rts[deferred->normalRoughnessRT].SRV
			};
			std::array<ID3D11UnorderedAccessView*, 2> guideUAVs = {
				texNRDViewZ->uav.get(),
				texNRDNormalRoughness->uav.get()
			};

			context->CSSetShaderResources(0, (uint)guideSRVs.size(), guideSRVs.data());
			context->CSSetUnorderedAccessViews(0, (uint)guideUAVs.size(), guideUAVs.data(), nullptr);
			context->CSSetShader(prepareNRDGuidesCompute.get(), nullptr, 0);
			context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);

			std::array<ID3D11ShaderResourceView*, 2> nullGuideSRVs = { nullptr };
			std::array<ID3D11UnorderedAccessView*, 2> nullGuideUAVs = { nullptr };
			context->CSSetShaderResources(0, (uint)nullGuideSRVs.size(), nullGuideSRVs.data());
			context->CSSetUnorderedAccessViews(0, (uint)nullGuideUAVs.size(), nullGuideUAVs.data(), nullptr);
		}

		// NRD REBLUR dispatch
		{
			TracyD3D11Zone(globals::state->tracyCtx, "SSRT - REBLUR");

			nrd::CommonSettings commonSettings{};
			{
				uint16_t fw = (uint16_t)resolution[0];
				uint16_t fh = (uint16_t)resolution[1];

				commonSettings.resourceSize[0] = (uint16_t)texNRDInputSH0->desc.Width;
				commonSettings.resourceSize[1] = (uint16_t)texNRDInputSH0->desc.Height;
				commonSettings.resourceSizePrev[0] = commonSettings.resourceSize[0];
				commonSettings.resourceSizePrev[1] = commonSettings.resourceSize[1];
				commonSettings.rectSize[0] = fw;
				commonSettings.rectSize[1] = fh;
				commonSettings.rectSizePrev[0] = fw;
				commonSettings.rectSizePrev[1] = fh;

				auto viewMat = globals::game::frameBufferCached.GetCameraView(0).Transpose();
				auto projMat = globals::game::frameBufferCached.GetCameraProj(0).Transpose();

				memcpy(commonSettings.viewToClipMatrix, &projMat, sizeof(float) * 16);
				memcpy(commonSettings.viewToClipMatrixPrev, &prevProjMatrix, sizeof(float) * 16);
				memcpy(commonSettings.worldToViewMatrix, &viewMat, sizeof(float) * 16);
				memcpy(commonSettings.worldToViewMatrixPrev, &prevViewMatrix, sizeof(float) * 16);

				prevViewMatrix = viewMat;
				prevProjMatrix = projMat;

				commonSettings.motionVectorScale[0] = 1.0f;
				commonSettings.motionVectorScale[1] = 1.0f;
				commonSettings.motionVectorScale[2] = 0.0f;
				commonSettings.isMotionVectorInWorldSpace = false;

				commonSettings.frameIndex = nrdFrameIndex++;
			}
			nrdReblur.SetCommonSettings(commonSettings);

			{
				reblurSettings.maxAccumulatedFrameNum = std::min(settings.MaxAccumulatedFrameNum, (uint)nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
				reblurSettings.maxFastAccumulatedFrameNum = std::min(settings.MaxFastAccumulatedFrameNum, reblurSettings.maxAccumulatedFrameNum);
				reblurSettings.diffusePrepassBlurRadius = std::max(settings.DiffusePrepassBlurRadius, 0.0f);
				reblurSettings.minBlurRadius = std::max(settings.MinBlurRadius, 0.0f);
				reblurSettings.maxBlurRadius = std::max(settings.MaxBlurRadius, reblurSettings.minBlurRadius);
				reblurSettings.enableAntiFirefly = settings.EnableAntiFirefly;
				reblurSettings.hitDistanceParameters.A = settings.HitDistA;
				reblurSettings.hitDistanceParameters.B = settings.HitDistB;
				reblurSettings.hitDistanceParameters.C = settings.HitDistC;
			}
			nrdReblur.SetDenoiserSettings(&reblurSettings);

			auto& motion = rts[RE::RENDER_TARGETS::kMOTION_VECTOR];
			nrdReblur.SetNamedSRV(nrd::ResourceType::IN_MV, motion.SRV);
			nrdReblur.SetNamedSRV(nrd::ResourceType::IN_NORMAL_ROUGHNESS, texNRDNormalRoughness->srv.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::IN_VIEWZ, texNRDViewZ->srv.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_SH0, texNRDInputSH0->srv.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_SH1, texNRDInputSH1->srv.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_SH0, texNRDOutputSH0->srv.get());
			nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_SH1, texNRDOutputSH1->srv.get());
			nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_SH0, texNRDOutputSH0->uav.get());
			nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_SH1, texNRDOutputSH1->uav.get());

			nrdReblur.Dispatch();
		}

		// Resolve NRD SH back to GI color
		{
			TracyD3D11Zone(globals::state->tracyCtx, "SSRT - NRD Resolve");

			resetViews();
			std::array<ID3D11ShaderResourceView*, 4> resolveSRVs = {
				texNRDOutputSH0->srv.get(),
				texNRDOutputSH1->srv.get(),
				rts[deferred->normalRoughnessRT].SRV,
				texWorkingDepth->srv.get()
			};
			std::array<ID3D11UnorderedAccessView*, 1> resolveUAVs = { texGIOcclusion->uav.get() };

			context->CSSetShaderResources(0, (uint)resolveSRVs.size(), resolveSRVs.data());
			context->CSSetUnorderedAccessViews(0, (uint)resolveUAVs.size(), resolveUAVs.data(), nullptr);
			context->CSSetShader(resolveNRDCompute.get(), nullptr, 0);
			context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);
		}
	}

	// cleanup
	resetViews();

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
}
