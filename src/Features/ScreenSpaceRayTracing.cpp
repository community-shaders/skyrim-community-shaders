#include "ScreenSpaceRayTracing.h"

#include <DDSTextureLoader.h>

#include "Deferred.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"

#include "DynamicCubemaps.h"
#include "Skylighting.h"
#include "Upscaling.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceRayTracing::REBLURSettings,
	MaxAccumulatedFrameNum,
	MaxFastAccumulatedFrameNum,
	MaxStabilizedFrameNum,
	HistoryFixFrameNum,
	HistoryFixBasePixelStride,
	HistoryFixAlternatePixelStride,
	FastHistoryClampingSigmaScale,
	DiffusePrepassBlurRadius,
	MinHitDistanceWeight,
	MinBlurRadius,
	MaxBlurRadius,
	LobeAngleFraction,
	RoughnessFraction,
	PlaneDistanceSensitivity,
	FireflySuppressorMinRelativeScale,
	EnableAntiFirefly,
	ReturnHistoryLengthInsteadOfOcclusion,
	SplitScreen,
	HitDistanceReconstructionMode,
	EnableNRDValidation)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceRayTracing::Settings,
	EnableSpecular,
	Mode,
	MaxSteps,
	MaxMips,
	Thickness,
	NormalBias,
	BRDFBias,
	UseDynamicCubemapsAsFallback,
	UseDynamicCubemapsAsFallbackSpecular,

	EnableDiffuse,
	SpecularMult,
	DiffuseMult,
	AmbientMult,
	OcclusionStrength,
	EnableREBLUR,
	HitDistA,
	HitDistB,
	HitDistC,
	HitDistD,
	DebugMode,
	EnablePrevGIReprojection,
	Reblur)

void ScreenSpaceRayTracing::DrawSettings()
{
	ImGui::Checkbox("Enable Specular", &settings.EnableSpecular);
	ImGui::SameLine();
	ImGui::Checkbox("Enable Diffuse", &settings.EnableDiffuse);

	{
		static const char* modeLabels[] = { "Full", "Full (probabilistic)", "Half", "Quarter" };
		int mode = (int)std::min(settings.Mode, 3u);
		if (ImGui::Combo("Resolution", &mode, modeLabels, 4))
			settings.Mode = (uint)mode;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"Full: each pixel traces both diffuse and specular (2 rays/pixel).\n"
				"Full (probabilistic): each pixel traces ONE ray, randomly diffuse or specular. Requires NRD AREA_3X3 hit-distance reconstruction.\n"
				"Half: checkerboard — half the pixels trace diffuse, the other half specular (~half cost).\n"
				"Quarter: half checkerboard + row alternation — 1/4 of pixels traced per frame. Best performance, slight quality loss.");
	}

	ImGui::SliderInt("Max Steps", (int*)&settings.MaxSteps, 1, 256);
	ImGui::SliderInt("Max Mip Level", (int*)&settings.MaxMips, 1, numMips, "%d", ImGuiSliderFlags_AlwaysClamp);

	ImGui::SliderFloat("Specular Multiplier", &settings.SpecularMult, 0.0f, 5.0f, "%.2f");
	ImGui::SliderFloat("Diffuse Multiplier", &settings.DiffuseMult, 0.01f, 5.0f, "%.2f");
	ImGui::SliderFloat("Occlusion Strength", &settings.OcclusionStrength, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat("AO Strength", &settings.AmbientMult, 0.0f, 1.0f, "%.2f");
	ImGui::Checkbox("Previous GI Reprojection", &settings.EnablePrevGIReprojection);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Samples last frame's resolved GI at ray-hit locations for multi-bounce illumination.");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Applies REBLUR denoised hit distance as ambient occlusion. Darkens areas where diffuse rays hit nearby geometry.");

	ImGui::Separator();

	ImGui::SliderFloat("Thickness", &settings.Thickness, 0.0f, 50.0f, "%.2f");
	ImGui::SliderFloat("Normal Bias", &settings.NormalBias, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("To avoid false hits from nearby geometry, increase this value to push the ray origin along the normal.");
	ImGui::SliderFloat("BRDF Bias", &settings.BRDFBias, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Specular only. Higher BRDF bias reduces noise but makes reflections more glossy.");
	ImGui::Checkbox("Use Dynamic Cubemaps as Fallback for Diffuse", &settings.UseDynamicCubemapsAsFallback);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("When ray marching misses, use dynamic cubemaps for reflections.");
	ImGui::Checkbox("Use Dynamic Cubemaps as Fallback for Specular", &settings.UseDynamicCubemapsAsFallbackSpecular);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("When ray marching misses, use dynamic cubemaps for reflections. Recommended for specular.");
	ImGui::SeparatorText("REBLUR Denoiser");
	ImGui::SliderFloat("Hit Dist A", &settings.HitDistA, 1.0f, 1000.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("NRD hitDistanceParameters.A: typical hit distance in Skyrim units (default 210 = 3 * 70 units/meter).");
	ImGui::SliderFloat("Hit Dist B", &settings.HitDistB, 0.0f, 1.0f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("NRD hitDistanceParameters.B: hit distance correction factor (default 0.1).");
	ImGui::SliderFloat("Hit Dist C", &settings.HitDistC, 0.0f, 200.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("NRD hitDistanceParameters.C: sky / unoccluded hit distance in Skyrim units (default 20).");
	ImGui::SliderFloat("Hit Dist D", &settings.HitDistD, -200.0f, 0.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("NRD hitDistanceParameters.D: thin surface correction, must be <= 0 (default -25).");
	ImGui::Checkbox("Enable REBLUR", &settings.EnableREBLUR);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("NVIDIA REBLUR temporal denoiser for diffuse GI.");
	if (settings.EnableREBLUR) {
		auto& r = settings.Reblur;

		ImGui::SeparatorText("Accumulation");
		{
			int v = (int)r.MaxAccumulatedFrameNum;
			if (ImGui::SliderInt("Max Accumulated Frames", &v, 1, (int)nrd::REBLUR_MAX_HISTORY_FRAME_NUM))
				r.MaxAccumulatedFrameNum = (uint32_t)v;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Maximum frames for slow history accumulation. Higher = less noise, more lag.");

			v = (int)r.MaxFastAccumulatedFrameNum;
			if (ImGui::SliderInt("Max Fast Accumulated Frames", &v, 1, (int)r.MaxAccumulatedFrameNum))
				r.MaxFastAccumulatedFrameNum = (uint32_t)v;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Maximum frames for fast history (reacts faster to disocclusion).");

			v = (int)r.MaxStabilizedFrameNum;
			if (ImGui::SliderInt("Max Stabilized Frames", &v, 0, (int)r.MaxAccumulatedFrameNum))
				r.MaxStabilizedFrameNum = (uint32_t)v;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Temporal stabilization pass frame count. 0 = disabled.");
		}

		ImGui::SeparatorText("History Fix");
		{
			int v = (int)r.HistoryFixFrameNum;
			if (ImGui::SliderInt("History Fix Frame Num", &v, 0, 4))
				r.HistoryFixFrameNum = (uint32_t)v;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Frames to apply history reconstruction after disocclusion.");

			v = (int)r.HistoryFixBasePixelStride;
			if (ImGui::SliderInt("History Fix Pixel Stride", &v, 1, 20))
				r.HistoryFixBasePixelStride = (uint32_t)v;

			v = (int)r.HistoryFixAlternatePixelStride;
			if (ImGui::SliderInt("History Fix Alternate Stride", &v, 1, 20))
				r.HistoryFixAlternatePixelStride = (uint32_t)v;
		}

		ImGui::SeparatorText("Spatial Filter");
		{
			ImGui::SliderFloat("Min Blur Radius", &r.MinBlurRadius, 0.0f, 10.0f, "%.1f px");
			ImGui::SliderFloat("Max Blur Radius", &r.MaxBlurRadius, 0.0f, 60.0f, "%.1f px");
			ImGui::SliderFloat("Diffuse Prepass Blur Radius", &r.DiffusePrepassBlurRadius, 0.0f, 60.0f, "%.1f px");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Spatial blur before temporal accumulation. Helps with very noisy input.");

			ImGui::SliderFloat("Lobe Angle Fraction", &r.LobeAngleFraction, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Controls normal-based weight in spatial filter. Higher = sharper boundaries.");

			ImGui::SliderFloat("Roughness Fraction", &r.RoughnessFraction, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Plane Distance Sensitivity", &r.PlaneDistanceSensitivity, 0.0f, 0.1f, "%.4f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Geometry rejection threshold. Higher = more aggressive edge stopping.");
		}

		ImGui::SeparatorText("Quality");
		{
			ImGui::SliderFloat("Fast History Clamping Sigma", &r.FastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Standard deviations for fast history clamping. Lower = more responsive but flickery.");

			ImGui::SliderFloat("Min Hit Distance Weight", &r.MinHitDistanceWeight, 0.0001f, 0.2f, "%.4f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Minimum weight for hit distance blending in spatial passes.");

			ImGui::Checkbox("Enable Anti-Firefly", &r.EnableAntiFirefly);
			if (r.EnableAntiFirefly) {
				ImGui::SliderFloat("Firefly Suppressor Scale", &r.FireflySuppressorMinRelativeScale, 1.0f, 3.0f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Scale of the firefly suppressor. Higher = more aggressive firefly removal.");
			}

			ImGui::Checkbox("Return History Length", &r.ReturnHistoryLengthInsteadOfOcclusion);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Output history length in .w instead of AO. Useful for debugging.");
		}

		ImGui::SeparatorText("Debug");
		{
			ImGui::SliderFloat("Split Screen", &r.SplitScreen, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Left of threshold shows raw input, right shows denoised output.");

			ImGui::Checkbox("NRD Validation Layer", &r.EnableNRDValidation);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("NRD internal debug overlay. Output textures show convergence/disocclusion info.");

			static const char* hitDistReconModes[] = { "OFF", "AREA_3X3 (performance)", "AREA_5X5" };
			int hdMode = (int)r.HitDistanceReconstructionMode;
			if (ImGui::Combo("Hit Distance Reconstruction", &hdMode, hitDistReconModes, 3))
				r.HitDistanceReconstructionMode = (uint32_t)hdMode;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("AREA_3X3 enables sparse raycast (performance mode): NRD reconstructs missing hit distances from neighbours.");
		}
	}

	ImGui::SeparatorText("Debug");
	{
		static const char* debugModes[] = {
			"None",
			"Denoised Specular Radiance",
			"Denoised Diffuse GI",
			"AO (SH normHitDist)",
		};
		int dbg = (int)std::min(settings.DebugMode, 3u);
		if (ImGui::Combo("Debug Mode", &dbg, debugModes, 4))
			settings.DebugMode = (uint)dbg;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Replaces the final image with the selected REBLUR layer for validation.");
	}

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texDepth, debugRescale)
		BUFFER_VIEWER_NODE(texNRDInputSpecRadianceHitDist, debugRescale)
		BUFFER_VIEWER_NODE(texNRDOutputSpecRadianceHitDist, debugRescale)
		BUFFER_VIEWER_NODE(texNRDInputSH0, debugRescale)
		BUFFER_VIEWER_NODE(texNRDInputSH1, debugRescale)
		BUFFER_VIEWER_NODE(texNRDOutputSH0, debugRescale)
		BUFFER_VIEWER_NODE(texNRDOutputSH1, debugRescale)
		BUFFER_VIEWER_NODE(texNRDViewZ, debugRescale)
		BUFFER_VIEWER_NODE(texNRDNormalRoughness, debugRescale)

		ImGui::TreePop();
	}
}

void ScreenSpaceRayTracing::RestoreDefaultSettings()
{
	settings = {};
}

void ScreenSpaceRayTracing::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ScreenSpaceRayTracing::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ScreenSpaceRayTracing::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		ssrtCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSRTCB>());
	}

	logger::debug("Creating textures...");
	{
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		D3D11_TEXTURE2D_DESC texDesc = {};
		mainTex.texture->GetDesc(&texDesc);
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = 1;

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

		uint32_t fullW = texDesc.Width;
		uint32_t fullH = texDesc.Height;

		// -- NRD full-res textures (checkerboard packing: data in left half, output full-res) --

		// NRD SH textures (RGBA16F)
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
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

		// NRD ViewZ (R32F, full-res) — avoids needing viewZScale for FP16 range compression
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texNRDViewZ = eastl::make_unique<Texture2D>(texDesc);
		texNRDViewZ->CreateSRV(srvDesc);
		texNRDViewZ->CreateUAV(uavDesc);

		// NRD NormalRoughness: R8G8B8A8_UNORM is guaranteed UAV store on D3D11.1.
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texNRDNormalRoughness = eastl::make_unique<Texture2D>(texDesc);
		texNRDNormalRoughness->CreateSRV(srvDesc);
		texNRDNormalRoughness->CreateUAV(uavDesc);

		// Hi-Z depth pyramid (full-res mip0, R32F, mip count derived from resolution)
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		{
			uint minDim = std::min(fullW, fullH);
			numMips = 1;
			while ((minDim >> numMips) >= 16 && numMips < kMaxMipSlots)
				++numMips;
		}
		texDesc.MipLevels = numMips;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		texDepth = eastl::make_unique<Texture2D>(texDesc);
		texDepth->CreateSRV(srvDesc);
		texDepth->CreateUAV(uavDesc);

		for (uint i = 0; i < numMips; i++) {
			D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc = {
				.Format = texDesc.Format,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MostDetailedMip = i, .MipLevels = 1 }
			};
			DX::ThrowIfFailed(device->CreateShaderResourceView(texDepth->resource.get(), &mipSrvDesc, depthSRVs[i].put()));

			D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
				.Format = texDesc.Format,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = i }
			};
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texDepth->resource.get(), &mipUavDesc, depthUAVs[i].put()));
		}

		// NRD specular textures (checkerboard, full-res, RGBA16F)
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.Width = fullW;
		texDesc.Height = fullH;
		texDesc.MipLevels = 1;
		srvDesc.Texture2D.MipLevels = 1;
		texNRDInputSpecRadianceHitDist = eastl::make_unique<Texture2D>(texDesc);
		texNRDInputSpecRadianceHitDist->CreateSRV(srvDesc);
		texNRDInputSpecRadianceHitDist->CreateUAV(uavDesc);

		texNRDOutputSpecRadianceHitDist = eastl::make_unique<Texture2D>(texDesc);
		texNRDOutputSpecRadianceHitDist->CreateSRV(srvDesc);
		texNRDOutputSpecRadianceHitDist->CreateUAV(uavDesc);

		// Initialize NRD at full resolution (checkerboard packing handled by NRD internally)
		nrdReblur.Init(fullW, fullH, nrd::Denoiser::REBLUR_DIFFUSE_SH, 0);
		nrdReblurSpecular.Init(fullW, fullH, nrd::Denoiser::REBLUR_SPECULAR, 1);
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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointSampler.put()));
	}

	logger::debug("Loading noise texture...");
	{
		DirectX::CreateDDSTextureFromFile(device, globals::d3d::context, L"Data\\Shaders\\ScreenSpaceRayTracing\\noise.dds",
			nullptr, noiseSRV.put());
	}

	CompileComputeShaders();
}

void ScreenSpaceRayTracing::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&raymarchSpecularCS,
		&raymarchDiffuseCS,
		&prefilterDepthCS,
		&depthDownsampleCS,
		&prepareNRDGuidesCS,
		&applyGIToMainCS,
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void ScreenSpaceRayTracing::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<std::pair<const char*, const char*>> defines;

	if (globals::features::dynamicCubemaps.loaded)
		defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

	if (globals::features::skylighting.loaded)
		defines.push_back({ "SKYLIGHTING", nullptr });

	auto definesSpecular = defines;
	definesSpecular.push_back({ "SSRT_SPECULAR", nullptr });

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &raymarchDiffuseCS, "ssrt_raymarch.hlsl", defines },
			{ &raymarchSpecularCS, "ssrt_raymarch.hlsl", definesSpecular },
			{ &prefilterDepthCS, "ssrt_prefilterDepths.hlsl", {} },
			{ &depthDownsampleCS, "ssrt_depth_downsample.hlsl", {} },
			{ &prepareNRDGuidesCS, "ssrt_preprocess_nrd_guides.hlsl", {} },
			{ &applyGIToMainCS, "ssrt_apply_gi_to_main.hlsl", {} },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\ScreenSpaceRayTracing") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
	}
}

void ScreenSpaceRayTracing::Prepass()
{
	if (recompileFlag) {
		recompileFlag = false;
		CompileComputeShaders();
	}

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto state = globals::state;

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	float2 res = state->screenSize;
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	float2 size = dynres;

	SSRTCB ssrCBData = {};
	{
		ssrCBData.MaxSteps = settings.MaxSteps;
		ssrCBData.MaxMips = std::min(settings.MaxMips, numMips);
		ssrCBData.Thickness = settings.Thickness;
		ssrCBData.NormalBias = settings.NormalBias;
		ssrCBData.BRDFBias = settings.BRDFBias;
		ssrCBData.UseDynamicCubemapsAsFallback = 0;
		ssrCBData.OcclusionStrength = settings.OcclusionStrength;
		ssrCBData._pad0 = 0;

		ssrCBData.TexDim = res;
		ssrCBData.RcpTexDim = float2(1.0f) / res;
		ssrCBData.FrameDim = dynres;
		ssrCBData.RcpFrameDim = float2(1.0f) / dynres;
		ssrCBData.HitDistA = settings.HitDistA;
		ssrCBData.HitDistB = settings.HitDistB;
		ssrCBData.HitDistC = settings.HitDistC;
		ssrCBData.HitDistD = settings.HitDistD;
		ssrCBData.FrameIndex = frameIndex;
		ssrCBData.TracingMode = settings.Mode;
	}
	ssrtCB->Update(ssrCBData);
	auto buffer = ssrtCB->CB();
	context->CSSetConstantBuffers(1, 1, &buffer);

	std::array<ID3D11ShaderResourceView*, 5> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { nullptr };

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	std::array<ID3D11SamplerState*, 2> samplers = { pointSampler.get(), linearSampler.get() };
	context->CSSetSamplers(0, 2, samplers.data());

	state->BeginPerfEvent("SSRT Prepass");

	// prefilter depth: full-res NDC depth → full-res texDepth mip0 (1:1 copy; downsample loop produces Hi-Z mips)
	{
		auto srv = depth.depthSRV;
		context->CSSetShaderResources(0, 1, &srv);

		ID3D11UnorderedAccessView* uavs2[] = { depthUAVs[0].get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs2, nullptr);

		context->CSSetShader(prefilterDepthCS.get(), nullptr, 0);
		context->Dispatch(((uint)dynres.x + 7) >> 3, ((uint)dynres.y + 7) >> 3, 1);

		resetViews();
	}

	// downsample depth Hi-Z
	{
		state->BeginPerfEvent("Downsample Depth - HiZ Buffer");
		for (uint i = 0; i < numMips - 1; ++i) {
			uint outW = std::max(1u, (uint)size.x >> (i + 1));
			uint outH = std::max(1u, (uint)size.y >> (i + 1));

			uavs.at(0) = depthUAVs[i + 1].get();
			srvs.at(0) = depthSRVs[i].get();

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(depthDownsampleCS.get(), nullptr, 0);

			context->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);

			resetViews();
		}
		state->EndPerfEvent();
	}

	state->EndPerfEvent();

	auto view = texDepth->srv.get();
	context->PSSetShaderResources(99, 1, &view);

	// --- NRD guide textures (full-res). Raymarch samples MAIN directly and converts inline. ---
	if (prepareNRDGuidesCS && settings.EnableREBLUR) {
		auto normal = renderer->GetRuntimeData().renderTargets[globals::deferred->normalRoughnessRT];

		state->BeginPerfEvent("SSRT NRD Guide Preprocess");

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
		context->CSSetShader(prepareNRDGuidesCS.get(), nullptr, 0);
		context->Dispatch(((uint)dynres.x + 7) / 8, ((uint)dynres.y + 7) / 8, 1);

		std::array<ID3D11ShaderResourceView*, 2> nullSRVs = { nullptr };
		std::array<ID3D11UnorderedAccessView*, 2> nullUAVs = { nullptr };
		context->CSSetShaderResources(0, (uint)nullSRVs.size(), nullSRVs.data());
		context->CSSetUnorderedAccessViews(0, (uint)nullUAVs.size(), nullUAVs.data(), nullptr);

		state->EndPerfEvent();
	}
}

void ScreenSpaceRayTracing::DrawSSRTSpecular()
{
	if (!settings.EnableSpecular)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto state = globals::state;

	state->BeginPerfEvent("SSRT Compute");

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
	auto normal = renderer->GetRuntimeData().renderTargets[globals::deferred->normalRoughnessRT];
	auto motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	auto& dynamicCubemaps = globals::features::dynamicCubemaps;

	auto& skylighting = globals::features::skylighting;

	float2 res = state->screenSize;
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	float2 traceRes = dynres;
	if (settings.Mode == TRACING_MODE_HALF || settings.Mode == TRACING_MODE_QUARTER)
		traceRes.x = ceil(dynres.x * 0.5f);
	if (settings.Mode == TRACING_MODE_QUARTER)
		traceRes.y = ceil(dynres.y * 0.5f);

	float2 dispatchCount = { (traceRes.x + 7) / 8, (traceRes.y + 7) / 8 };

	SSRTCB ssrCBData = {};
	{
		ssrCBData.MaxSteps = settings.MaxSteps;
		ssrCBData.MaxMips = std::min(settings.MaxMips, numMips);
		ssrCBData.Thickness = settings.Thickness;
		ssrCBData.NormalBias = settings.NormalBias;
		ssrCBData.BRDFBias = settings.BRDFBias;
		ssrCBData.UseDynamicCubemapsAsFallback = (uint)settings.UseDynamicCubemapsAsFallbackSpecular && dynamicCubemaps.loaded;
		ssrCBData.OcclusionStrength = settings.OcclusionStrength;
		ssrCBData._pad0 = 0;

		ssrCBData.TexDim = res;
		ssrCBData.RcpTexDim = float2(1.0f) / res;
		ssrCBData.FrameDim = dynres;
		ssrCBData.RcpFrameDim = float2(1.0f) / dynres;
		ssrCBData.HitDistA = settings.HitDistA;
		ssrCBData.HitDistB = settings.HitDistB;
		ssrCBData.HitDistC = settings.HitDistC;
		ssrCBData.HitDistD = settings.HitDistD;
		ssrCBData.FrameIndex = settings.EnableDiffuse ? frameIndex - 1 : frameIndex;
		ssrCBData.TracingMode = settings.Mode;
	}
	ssrtCB->Update(ssrCBData);
	auto buffer = ssrtCB->CB();
	context->CSSetConstantBuffers(1, 1, &buffer);

	std::array<ID3D11ShaderResourceView*, 12> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	std::array<ID3D11SamplerState*, 2> samplers = { pointSampler.get(), linearSampler.get() };

	context->CSSetSamplers(0, 2, samplers.data());

	const auto envTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr;
	const auto envReflectionsTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr;

	bool inInterior = true;

	if (auto player = RE::PlayerCharacter::GetSingleton()) {
		if (auto parentCell = player->GetParentCell()) {
			inInterior = parentCell->IsInteriorCell();
		}
	}

	state->BeginPerfEvent("Raymarch");

	uavs.at(0) = texNRDInputSpecRadianceHitDist->uav.get();

	srvs.at(1) = motion.SRV;
	srvs.at(2) = normal.SRV;
	srvs.at(3) = main.SRV;
	srvs.at(4) = depth.depthSRV;
	srvs.at(5) = texDepth->srv.get();
	srvs.at(6) = noiseSRV.get();
	srvs.at(7) = envTexture;
	srvs.at(8) = inInterior ? envTexture : envReflectionsTexture;
	srvs.at(9) = nullptr;  // t9 unused
	srvs.at(10) = dynamicCubemaps.loaded && skylighting.loaded ? skylighting.texProbeArray->srv.get() : nullptr;

	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShader(raymarchSpecularCS.get(), nullptr, 0);
	context->CSSetConstantBuffers(1, 1, &buffer);

	context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

	state->EndPerfEvent();

	resetViews();

	// --- REBLUR_SPECULAR denoising ---
	if (settings.EnableREBLUR && nrdReblurSpecular.IsValid()) {
		state->BeginPerfEvent("SSRT REBLUR Specular");

		nrd::CommonSettings specCommonSettings{};
		{
			uint16_t fw = (uint16_t)dynres.x;
			uint16_t fh = (uint16_t)dynres.y;

			specCommonSettings.resourceSize[0] = (uint16_t)texNRDInputSpecRadianceHitDist->desc.Width;
			specCommonSettings.resourceSize[1] = (uint16_t)texNRDInputSpecRadianceHitDist->desc.Height;
			specCommonSettings.resourceSizePrev[0] = specCommonSettings.resourceSize[0];
			specCommonSettings.resourceSizePrev[1] = specCommonSettings.resourceSize[1];
			specCommonSettings.rectSize[0] = fw;
			specCommonSettings.rectSize[1] = fh;
			specCommonSettings.rectSizePrev[0] = fw;
			specCommonSettings.rectSizePrev[1] = fh;

			auto viewMat = globals::game::frameBufferCached.GetCameraView(0).Transpose();
			auto projMat = globals::game::frameBufferCached.GetCameraProj(0).Transpose();

			memcpy(specCommonSettings.viewToClipMatrix, &projMat, sizeof(float) * 16);
			memcpy(specCommonSettings.viewToClipMatrixPrev, &prevProjMatrix, sizeof(float) * 16);
			memcpy(specCommonSettings.worldToViewMatrix, &viewMat, sizeof(float) * 16);
			memcpy(specCommonSettings.worldToViewMatrixPrev, &prevViewMatrix, sizeof(float) * 16);

			specCommonSettings.motionVectorScale[0] = 1.0f;
			specCommonSettings.motionVectorScale[1] = 1.0f;
			specCommonSettings.motionVectorScale[2] = 0.0f;
			specCommonSettings.isMotionVectorInWorldSpace = false;

            auto jitter = globals::features::upscaling.jitter;
			specCommonSettings.cameraJitter[0] = jitter.x;
			specCommonSettings.cameraJitter[1] = jitter.y;
			specCommonSettings.cameraJitterPrev[0] = prevJitter.x;
			specCommonSettings.cameraJitterPrev[1] = prevJitter.y;

			// If diffuse ran first it already incremented frameIndex; use same frame value.
			specCommonSettings.frameIndex = settings.EnableDiffuse ? frameIndex - 1 : frameIndex++;
			specCommonSettings.denoisingRange = 1e6f;
			specCommonSettings.splitScreen = settings.Reblur.SplitScreen;
			specCommonSettings.enableValidation = settings.Reblur.EnableNRDValidation;
		}
		nrdReblurSpecular.SetCommonSettings(specCommonSettings);

		{
			const auto& r = settings.Reblur;
			reblurSpecularSettings.maxAccumulatedFrameNum = std::min((uint32_t)r.MaxAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
			reblurSpecularSettings.maxFastAccumulatedFrameNum = std::min((uint32_t)r.MaxFastAccumulatedFrameNum, reblurSpecularSettings.maxAccumulatedFrameNum);
			reblurSpecularSettings.maxStabilizedFrameNum = std::min((uint32_t)r.MaxStabilizedFrameNum, reblurSpecularSettings.maxAccumulatedFrameNum);
			reblurSpecularSettings.historyFixFrameNum = reblurSpecularSettings.maxFastAccumulatedFrameNum > 0 ? std::min((uint32_t)r.HistoryFixFrameNum, reblurSpecularSettings.maxFastAccumulatedFrameNum - 1) : 0;
			reblurSpecularSettings.historyFixBasePixelStride = std::max(r.HistoryFixBasePixelStride, 1u);
			reblurSpecularSettings.historyFixAlternatePixelStride = std::max(r.HistoryFixAlternatePixelStride, 1u);
			reblurSpecularSettings.fastHistoryClampingSigmaScale = std::clamp(r.FastHistoryClampingSigmaScale, 1.0f, 3.0f);
			reblurSpecularSettings.minHitDistanceWeight = std::clamp(r.MinHitDistanceWeight, 0.0001f, 0.2f);
			reblurSpecularSettings.minBlurRadius = std::max(r.MinBlurRadius, 0.0f);
			reblurSpecularSettings.maxBlurRadius = std::max(r.MaxBlurRadius, reblurSpecularSettings.minBlurRadius);
			reblurSpecularSettings.lobeAngleFraction = std::clamp(r.LobeAngleFraction, 0.0f, 1.0f);
			reblurSpecularSettings.roughnessFraction = std::clamp(r.RoughnessFraction, 0.0f, 1.0f);
			reblurSpecularSettings.planeDistanceSensitivity = std::max(r.PlaneDistanceSensitivity, 0.0f);
			reblurSpecularSettings.fireflySuppressorMinRelativeScale = std::clamp(r.FireflySuppressorMinRelativeScale, 1.0f, 3.0f);
			reblurSpecularSettings.enableAntiFirefly = r.EnableAntiFirefly;
			reblurSpecularSettings.returnHistoryLengthInsteadOfOcclusion = r.ReturnHistoryLengthInsteadOfOcclusion;
			reblurSpecularSettings.specularPrepassBlurRadius = std::max(r.DiffusePrepassBlurRadius, 0.0f);
			auto specHitDistRecon = static_cast<nrd::HitDistanceReconstructionMode>(
				std::min(r.HitDistanceReconstructionMode, 2u));
			if ((settings.Mode == TRACING_MODE_FULL_PROBABILISTIC || settings.Mode == TRACING_MODE_QUARTER) &&
				specHitDistRecon == nrd::HitDistanceReconstructionMode::OFF)
				specHitDistRecon = nrd::HitDistanceReconstructionMode::AREA_3X3;
			reblurSpecularSettings.hitDistanceReconstructionMode = specHitDistRecon;
			reblurSpecularSettings.hitDistanceParameters.A = settings.HitDistA;
			reblurSpecularSettings.hitDistanceParameters.B = settings.HitDistB;
			reblurSpecularSettings.hitDistanceParameters.C = settings.HitDistC;
			reblurSpecularSettings.checkerboardMode = (settings.Mode == TRACING_MODE_HALF || settings.Mode == TRACING_MODE_QUARTER) ? nrd::CheckerboardMode::BLACK : nrd::CheckerboardMode::OFF;
		}
		nrdReblurSpecular.SetDenoiserSettings(&reblurSpecularSettings);

		nrdReblurSpecular.SetNamedSRV(nrd::ResourceType::IN_MV, motion.SRV);
		nrdReblurSpecular.SetNamedSRV(nrd::ResourceType::IN_NORMAL_ROUGHNESS, texNRDNormalRoughness->srv.get());
		nrdReblurSpecular.SetNamedSRV(nrd::ResourceType::IN_VIEWZ, texNRDViewZ->srv.get());
		nrdReblurSpecular.SetNamedSRV(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, texNRDInputSpecRadianceHitDist->srv.get());
		nrdReblurSpecular.SetNamedSRV(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, texNRDOutputSpecRadianceHitDist->srv.get());
		nrdReblurSpecular.SetNamedUAV(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, texNRDOutputSpecRadianceHitDist->uav.get());

		nrdReblurSpecular.Dispatch();

		state->EndPerfEvent();
	}

	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();
}

void ScreenSpaceRayTracing::DrawSSRTDiffuse()
{
	if (!settings.EnableDiffuse)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto state = globals::state;

	state->BeginPerfEvent("SSRT Diffuse Compute");

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
	auto normal = renderer->GetRuntimeData().renderTargets[globals::deferred->normalRoughnessRT];
	auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
	auto motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	auto& dynamicCubemaps = globals::features::dynamicCubemaps;

	auto& skylighting = globals::features::skylighting;

	float2 res = state->screenSize;
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	float2 traceRes = dynres;
	if (settings.Mode == TRACING_MODE_HALF || settings.Mode == TRACING_MODE_QUARTER)
		traceRes.x = ceil(dynres.x * 0.5f);
	if (settings.Mode == TRACING_MODE_QUARTER)
		traceRes.y = ceil(dynres.y * 0.5f);

	float2 dispatchCount = { (traceRes.x + 7) / 8, (traceRes.y + 7) / 8 };

	SSRTCB ssrCBData = {};
	{
		ssrCBData.MaxSteps = settings.MaxSteps;
		ssrCBData.MaxMips = std::min(settings.MaxMips, numMips);
		ssrCBData.Thickness = settings.Thickness;
		ssrCBData.NormalBias = settings.NormalBias;
		ssrCBData.BRDFBias = settings.BRDFBias;
		ssrCBData.UseDynamicCubemapsAsFallback = (uint)settings.UseDynamicCubemapsAsFallback && dynamicCubemaps.loaded;
		ssrCBData.OcclusionStrength = settings.OcclusionStrength;
		ssrCBData._pad0 = 0;

		ssrCBData.TexDim = res;
		ssrCBData.RcpTexDim = float2(1.0f) / res;
		ssrCBData.FrameDim = dynres;
		ssrCBData.RcpFrameDim = float2(1.0f) / dynres;
		ssrCBData.HitDistA = settings.HitDistA;
		ssrCBData.HitDistB = settings.HitDistB;
		ssrCBData.HitDistC = settings.HitDistC;
		ssrCBData.HitDistD = settings.HitDistD;
		ssrCBData.FrameIndex = frameIndex;
		ssrCBData.TracingMode = settings.Mode;
	}
	ssrtCB->Update(ssrCBData);
	auto buffer = ssrtCB->CB();
	context->CSSetConstantBuffers(1, 1, &buffer);

	std::array<ID3D11ShaderResourceView*, 15> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { nullptr };

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	std::array<ID3D11SamplerState*, 2> samplers = { pointSampler.get(), linearSampler.get() };

	context->CSSetSamplers(0, 2, samplers.data());

	const auto envTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr;
	const auto envReflectionsTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr;

	bool inInterior = true;

	if (auto player = RE::PlayerCharacter::GetSingleton()) {
		if (auto parentCell = player->GetParentCell()) {
			inInterior = parentCell->IsInteriorCell();
		}
	}

	// --- Apply reprojected multi-bounce GI directly into MAIN before the raymarch. ---
	//     Reads last frame's denoised NRD SH and adds a gamma-space contribution so the
	//     raymarch (which samples MAIN) picks up indirect bounce light.
	//     Runs here (post-opaque, pre-raymarch) so the UAV writes aren't clobbered by
	//     the opaque geometry pass that binds MAIN as an RTV.
	if (applyGIToMainCS && settings.EnablePrevGIReprojection) {
		state->BeginPerfEvent("SSRT Apply GI to Main");

		std::array<ID3D11ShaderResourceView*, 7> giSRVs = { nullptr };
		giSRVs[0] = texNRDOutputSH0->srv.get();
		giSRVs[1] = texNRDOutputSH1->srv.get();
		giSRVs[2] = normal.SRV;
		giSRVs[3] = depth.depthSRV;
		giSRVs[4] = motion.SRV;
		giSRVs[5] = texNRDViewZ->srv.get();
		giSRVs[6] = albedo.SRV;

		ID3D11UnorderedAccessView* mainUAV = main.UAV;

		context->CSSetShaderResources(0, (uint)giSRVs.size(), giSRVs.data());
		context->CSSetUnorderedAccessViews(0, 1, &mainUAV, nullptr);
		context->CSSetShader(applyGIToMainCS.get(), nullptr, 0);
		context->Dispatch(((uint)dynres.x + 7) >> 3, ((uint)dynres.y + 7) >> 3, 1);

		std::array<ID3D11ShaderResourceView*, 7> nullSRVs = { nullptr };
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, (uint)nullSRVs.size(), nullSRVs.data());
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

		state->EndPerfEvent();
	}

	// --- Ray march: outputs directly to NRD input textures ---
	uavs.at(0) = texNRDInputSH0->uav.get();
	uavs.at(1) = texNRDInputSH1->uav.get();

	srvs.at(1) = motion.SRV;
	srvs.at(2) = normal.SRV;
	srvs.at(3) = main.SRV;
	srvs.at(4) = depth.depthSRV;
	srvs.at(5) = texDepth->srv.get();
	srvs.at(6) = noiseSRV.get();
	srvs.at(7) = envTexture;
	srvs.at(8) = inInterior ? envTexture : envReflectionsTexture;
	srvs.at(9) = nullptr;
	srvs.at(10) = dynamicCubemaps.loaded && skylighting.loaded ? skylighting.texProbeArray->srv.get() : nullptr;
	srvs.at(12) = albedo.SRV;

	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetConstantBuffers(1, 1, &buffer);
	context->CSSetShader(raymarchDiffuseCS.get(), nullptr, 0);
	context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);
	resetViews();

	// --- REBLUR denoising ---
	if (settings.EnableREBLUR && nrdReblur.IsValid()) {
		state->BeginPerfEvent("SSRT REBLUR");

		// Update NRD common settings
		nrd::CommonSettings commonSettings{};
		{
			uint16_t fw = (uint16_t)dynres.x;
			uint16_t fh = (uint16_t)dynres.y;

			commonSettings.resourceSize[0] = (uint16_t)texNRDInputSH0->desc.Width;
			commonSettings.resourceSize[1] = (uint16_t)texNRDInputSH0->desc.Height;
			commonSettings.resourceSizePrev[0] = commonSettings.resourceSize[0];
			commonSettings.resourceSizePrev[1] = commonSettings.resourceSize[1];
			commonSettings.rectSize[0] = fw;
			commonSettings.rectSize[1] = fh;
			commonSettings.rectSizePrev[0] = fw;
			commonSettings.rectSizePrev[1] = fh;

			// NRD wants column-major matrices; game stores row-major — transpose to convert.
			// Use unjittered projection to avoid NRD temporal artifacts.
			auto viewMat = globals::game::frameBufferCached.GetCameraView(0).Transpose();
			auto projMat = globals::game::frameBufferCached.GetCameraProj(0).Transpose();

			memcpy(commonSettings.viewToClipMatrix, &projMat, sizeof(float) * 16);
			memcpy(commonSettings.viewToClipMatrixPrev, &prevProjMatrix, sizeof(float) * 16);
			memcpy(commonSettings.worldToViewMatrix, &viewMat, sizeof(float) * 16);
			memcpy(commonSettings.worldToViewMatrixPrev, &prevViewMatrix, sizeof(float) * 16);

			prevViewMatrix = viewMat;
			prevProjMatrix = projMat;

			// 2D screen-space motion vectors (pixelUvPrev = pixelUv + mv)
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
			commonSettings.splitScreen = settings.Reblur.SplitScreen;
			commonSettings.enableValidation = settings.Reblur.EnableNRDValidation;
		}
		nrdReblur.SetCommonSettings(commonSettings);

		// Update REBLUR settings (clamped to valid ranges)
		{
			const auto& r = settings.Reblur;
			reblurSettings.maxAccumulatedFrameNum = std::min((uint32_t)r.MaxAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
			reblurSettings.maxFastAccumulatedFrameNum = std::min((uint32_t)r.MaxFastAccumulatedFrameNum, reblurSettings.maxAccumulatedFrameNum);
			reblurSettings.maxStabilizedFrameNum = std::min((uint32_t)r.MaxStabilizedFrameNum, reblurSettings.maxAccumulatedFrameNum);
			reblurSettings.historyFixFrameNum = reblurSettings.maxFastAccumulatedFrameNum > 0 ? std::min((uint32_t)r.HistoryFixFrameNum, reblurSettings.maxFastAccumulatedFrameNum - 1) : 0;
			reblurSettings.historyFixBasePixelStride = std::max(r.HistoryFixBasePixelStride, 1u);
			reblurSettings.historyFixAlternatePixelStride = std::max(r.HistoryFixAlternatePixelStride, 1u);
			reblurSettings.fastHistoryClampingSigmaScale = std::clamp(r.FastHistoryClampingSigmaScale, 1.0f, 3.0f);
			reblurSettings.diffusePrepassBlurRadius = std::max(r.DiffusePrepassBlurRadius, 0.0f);
			reblurSettings.minHitDistanceWeight = std::clamp(r.MinHitDistanceWeight, 0.0001f, 0.2f);
			reblurSettings.minBlurRadius = std::max(r.MinBlurRadius, 0.0f);
			reblurSettings.maxBlurRadius = std::max(r.MaxBlurRadius, reblurSettings.minBlurRadius);
			reblurSettings.lobeAngleFraction = std::clamp(r.LobeAngleFraction, 0.0f, 1.0f);
			reblurSettings.roughnessFraction = std::clamp(r.RoughnessFraction, 0.0f, 1.0f);
			reblurSettings.planeDistanceSensitivity = std::max(r.PlaneDistanceSensitivity, 0.0f);
			reblurSettings.fireflySuppressorMinRelativeScale = std::clamp(r.FireflySuppressorMinRelativeScale, 1.0f, 3.0f);
			reblurSettings.enableAntiFirefly = r.EnableAntiFirefly;
			reblurSettings.returnHistoryLengthInsteadOfOcclusion = r.ReturnHistoryLengthInsteadOfOcclusion;
			auto diffHitDistRecon = static_cast<nrd::HitDistanceReconstructionMode>(
				std::min(r.HitDistanceReconstructionMode, 2u));
			if ((settings.Mode == TRACING_MODE_FULL_PROBABILISTIC || settings.Mode == TRACING_MODE_QUARTER) &&
				diffHitDistRecon == nrd::HitDistanceReconstructionMode::OFF)
				diffHitDistRecon = nrd::HitDistanceReconstructionMode::AREA_3X3;
			reblurSettings.hitDistanceReconstructionMode = diffHitDistRecon;
			reblurSettings.hitDistanceParameters.A = settings.HitDistA;
			reblurSettings.hitDistanceParameters.B = settings.HitDistB;
			reblurSettings.hitDistanceParameters.C = settings.HitDistC;
			reblurSettings.checkerboardMode = (settings.Mode == TRACING_MODE_HALF || settings.Mode == TRACING_MODE_QUARTER) ? nrd::CheckerboardMode::WHITE : nrd::CheckerboardMode::OFF;
		}
		nrdReblur.SetDenoiserSettings(&reblurSettings);

		// Bind named resources
		nrdReblur.SetNamedSRV(nrd::ResourceType::IN_MV, motion.SRV);
		nrdReblur.SetNamedSRV(nrd::ResourceType::IN_NORMAL_ROUGHNESS, texNRDNormalRoughness->srv.get());
		nrdReblur.SetNamedSRV(nrd::ResourceType::IN_VIEWZ, texNRDViewZ->srv.get());
		nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_SH0, texNRDInputSH0->srv.get());
		nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_SH1, texNRDInputSH1->srv.get());
		// OUT_DIFF_SH0/SH1 serve as temp storage (DIFF_TEMP1/SH_TEMP1) between PrePass→TA→Blur,
		// then TemporalStabilization overwrites them with the final denoised result.
		// Both SRV (read by TA and Blur) and UAV (written by PrePass, HistoryFix, TS) must be bound.
		nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_SH0, texNRDOutputSH0->srv.get());
		nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_SH1, texNRDOutputSH1->srv.get());
		nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_SH0, texNRDOutputSH0->uav.get());
		nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_SH1, texNRDOutputSH1->uav.get());

		nrdReblur.Dispatch();

		state->EndPerfEvent();
	}

	state->EndPerfEvent();

	context->CSSetShader(nullptr, nullptr, 0);
}

ScreenSpaceRayTracing::DiffuseOutput ScreenSpaceRayTracing::GetDiffuseOutputTextures()
{
	DiffuseOutput output;
	if (settings.EnableREBLUR && nrdReblur.IsValid()) {
		output.sh[0] = texNRDOutputSH0->srv.get();
		output.sh[1] = texNRDOutputSH1->srv.get();
	} else {
		output.sh[0] = texNRDInputSH0->srv.get();
		output.sh[1] = texNRDInputSH1->srv.get();
	}
	return output;
}

ID3D11ShaderResourceView* ScreenSpaceRayTracing::GetSpecularOutputTexture()
{
	if (settings.EnableREBLUR && nrdReblurSpecular.IsValid())
		return texNRDOutputSpecRadianceHitDist->srv.get();
	return texNRDInputSpecRadianceHitDist->srv.get();
}

ScreenSpaceRayTracing::SharedData ScreenSpaceRayTracing::GetCommonBufferData()
{
	SharedData data;
	data.EnableSpecular = settings.EnableSpecular;
	data.SpecularMult = settings.SpecularMult;
	data.DiffuseMult = settings.EnableDiffuse ? settings.DiffuseMult : 0.0f;
	data.AmbientMult = settings.AmbientMult;
	data.DebugMode = settings.DebugMode;
	data.EnablePrevGIReprojection = settings.EnablePrevGIReprojection ? 1u : 0u;
	data._pad0[0] = data._pad0[1] = 0.0f;
	return data;
}
