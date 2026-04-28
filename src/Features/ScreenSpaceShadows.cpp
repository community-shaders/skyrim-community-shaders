#include "ScreenSpaceShadows.h"

#include "State.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceShadows::Settings,
	Enabled,
	BlurDepthPyramid,
	SurfaceThickness,
	ShadowContrast,
	RayLength,
	SampleCount)

void ScreenSpaceShadows::DrawSettings()
{
	if (ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable", &settings.Enabled);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Enable screen-space contact shadows from the sun/moon direction.");

		ImGui::SliderFloat("Surface Thickness", &settings.SurfaceThickness, 0.1f, 20.0f);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Assumed surface thickness for the occlusion test, in view-space world units.");

		ImGui::SliderFloat("Shadow Contrast", &settings.ShadowContrast, 0.0f, 4.0f);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Contrast boost for the shadow transition. Higher values produce harder edges.");

		ImGui::SliderFloat("Ray Length", &settings.RayLength, 1.0f, 2000.0f);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("World-space distance the shadow ray travels. Controls shadow reach.");

		ImGui::SliderInt("Sample Count", &settings.SampleCount, kMinMip0Samples, kMaxMip0Samples);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"Mip 0 ray-march sample count at the reference Ray Length (100). Scales linearly with "
				"Ray Length to keep world-space sample density constant. Lower mips take half the "
				"samples each (mip 1 = N/2, mip 2 = N/4, mip 3 = N/8).");

		ImGui::Checkbox("Blur Depth Pyramid", &settings.BlurDepthPyramid);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"Apply a Gaussian blur to each mip of the depth pyramid after the full chain is built. "
				"Smooths depth discontinuities sampled during the ray-march at a small extra GPU cost.");

		if (globals::game::isVR && globals::state->IsDeveloperMode()) {
			ImGui::Checkbox("VR Stereo Sync", &enableStereoSync);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					"Synchronizes shadow data between left and right eyes via bilateral reprojection "
					"and applies a depth-weighted blur to reduce per-eye noise. "
					"Uses min-blend so if either eye detects an occluder, the shadow is preserved.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = 0.3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.0f, 1.0f);

		static const char* mipSuffix[] = {
			" Mip 0 - Full res",
			" Mip 1 - Half res",
			" Mip 2 - Quarter res",
			" Mip 3 - Eighth res"
		};

		for (int i = 0; i < 4; ++i) {
			std::string title = std::string("Depth Prefiltered") + mipSuffix[i];
			BUFFER_VIEWER_NODE_TITLE(texDepthMipPrefiltered[i], title.c_str(), debugRescale)
		}
		for (int i = 0; i < 4; ++i) {
			std::string title = std::string("Depth Blurred") + mipSuffix[i];
			BUFFER_VIEWER_NODE_TITLE(texDepthMip[i], title.c_str(), debugRescale)
		}
		for (int i = 0; i < 4; ++i) {
			std::string title = std::string("Shadow Marched") + mipSuffix[i];
			BUFFER_VIEWER_NODE_TITLE(texShadowMip[i], title.c_str(), debugRescale)
		}
		for (int i = 0; i < 4; ++i) {
			std::string title = std::string("Shadow Work") + mipSuffix[i];
			BUFFER_VIEWER_NODE_TITLE(texShadowWork[i], title.c_str(), debugRescale)
		}
		BUFFER_VIEWER_NODE_TITLE(screenSpaceShadowsTexture, "Shadow Final (blurred)", debugRescale)
		if (globals::game::isVR && stereoSyncCopyTex) {
			BUFFER_VIEWER_NODE_TITLE(stereoSyncCopyTex, "Stereo Sync Copy", debugRescale)
		}

		ImGui::TreePop();
	}
}

void ScreenSpaceShadows::ClearShaderCache()
{
	prefilterDepthsCS = nullptr;
	blurDepthCS = nullptr;
	for (int i = 0; i < 4; ++i) {
		shadowsCS[i] = nullptr;
		shadowsRightCS[i] = nullptr;
	}
	upscaleCS = nullptr;
	blurCS = nullptr;
	stereoSyncCS = nullptr;
	compiledBaseSampleCount = -1;
	CompileComputeShaders();
}

void ScreenSpaceShadows::CompileComputeShaders()
{
	std::vector<std::pair<const char*, const char*>> baseDefines;
	if (REL::Module::IsVR())
		baseDefines.push_back({ "VR", "" });

	auto compile = [&](std::wstring_view path, std::vector<std::pair<const char*, const char*>> defines) -> ID3D11ComputeShader* {
		return reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.data(), defines, "cs_5_0"));
	};

	if (auto* cs = compile(L"Data\\Shaders\\ScreenSpaceShadows\\PrefilterDepthsCS.hlsl", baseDefines))
		prefilterDepthsCS.attach(cs);

	if (auto* cs = compile(L"Data\\Shaders\\ScreenSpaceShadows\\BlurDepthCS.hlsl", {}))
		blurDepthCS.attach(cs);

	// ShadowsCS is compiled lazily via CompileShadowsCS() so the MIP0_SAMPLE_COUNT
	// define matches the current settings.

	if (auto* cs = compile(L"Data\\Shaders\\ScreenSpaceShadows\\UpscaleCS.hlsl", {}))
		upscaleCS.attach(cs);

	if (auto* cs = compile(L"Data\\Shaders\\ScreenSpaceShadows\\BlurCS.hlsl", {}))
		blurCS.attach(cs);
}

void ScreenSpaceShadows::CompileShadowsCS(int baseSampleCount)
{
	if (baseSampleCount == compiledBaseSampleCount && shadowsCS[3])
		return;

	std::vector<std::pair<const char*, const char*>> baseDefines;
	if (REL::Module::IsVR())
		baseDefines.push_back({ "VR", "" });

	auto compile = [&](std::vector<std::pair<const char*, const char*>> defines) -> ID3D11ComputeShader* {
		return reinterpret_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\ScreenSpaceShadows\\ShadowsCS.hlsl", defines, "cs_5_0"));
	};

	// Mip 3 carries the base sample count; each step toward mip 0 halves it.
	for (int mip = 0; mip < 4; ++mip) {
		int samples = std::max(1, baseSampleCount >> (3 - mip));

		char sampleCountStr[16];
		snprintf(sampleCountStr, sizeof(sampleCountStr), "%d", samples);

		auto defines = baseDefines;
		defines.push_back({ "MIP_SAMPLE_COUNT", sampleCountStr });

		shadowsCS[mip] = nullptr;
		if (auto* cs = compile(defines))
			shadowsCS[mip].attach(cs);

		if (REL::Module::IsVR()) {
			shadowsRightCS[mip] = nullptr;
			auto rightDefines = defines;
			rightDefines.push_back({ "RIGHT", "" });
			if (auto* cs = compile(rightDefines))
				shadowsRightCS[mip].attach(cs);
		}
	}

	compiledBaseSampleCount = baseSampleCount;
}

void ScreenSpaceShadows::DrawShadows()
{
	ZoneScopedS(8);
	auto state = globals::state;
	TracyD3D11Zone(state->tracyCtx, "Screen Space Shadows");

	auto context = globals::d3d::context;

	auto accumulator = *globals::game::currentAccumulator.get();
	auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

	auto& directionNi = dirLight->GetWorldDirection();
	float3 light = { directionNi.x, directionNi.y, directionNi.z };
	light.Normalize();

	float2 renderSize = Util::ConvertToDynamic(state->screenSize);
	float2 texDim = { (float)texShadowMip[0]->desc.Width, (float)texShadowMip[0]->desc.Height };

	SSSCB cbData{};
	cbData.FrameDim = renderSize;
	cbData.RcpTexDim = { 1.0f / texDim.x, 1.0f / texDim.y };
	cbData.TexDim = texDim;
	cbData.DynamicRes = { renderSize.x / texDim.x, renderSize.y / texDim.y };
	cbData.SurfaceThickness = settings.SurfaceThickness;
	cbData.ShadowContrast = settings.ShadowContrast;
	cbData.LightWorldDir = { -light.x, -light.y, -light.z };

	// Mip 3 carries the highest sample count; mip 0 the lowest.  Scale by ray length
	// to hold sample density (samples per world unit) roughly constant.
	const float scaled = settings.SampleCount * settings.RayLength / kReferenceRayLength;
	const int baseSampleCount = std::clamp(static_cast<int>(std::round(scaled)), kMinMip0Samples, kMaxMip0Samples);
	// MIP_SAMPLE_COUNT is a compile-time define — recompile ShadowsCS variants if it changed.
	CompileShadowsCS(baseSampleCount);
	if (!shadowsCS[3])
		return;

	auto* cbPtr = sssCB->CB();
	context->CSSetConstantBuffers(1, 1, &cbPtr);

	ID3D11SamplerState* samplers[2] = { linearClampSampler.get(), pointClampSampler.get() };
	context->CSSetSamplers(0, 2, samplers);

	// Copy game's PerFrame cbuffer (b12) to CS stage for FrameBuffer matrices.
	{
		ID3D11Buffer* perFrameCB = nullptr;
		context->VSGetConstantBuffers(12, 1, &perFrameCB);
		context->CSSetConstantBuffers(12, 1, &perFrameCB);
		if (perFrameCB)
			perFrameCB->Release();
	}

	// === Depth pyramid — prefilter game depth into 4 mip levels ===
	if (prefilterDepthsCS) {
		if (state->frameAnnotations)
			state->BeginPerfEvent("SSS - Prefilter Depths");

		auto* srcDepthSRV = Util::GetCurrentSceneDepthSRV();
		ID3D11UnorderedAccessView* depthUAVs[4] = {
			texDepthMipPrefiltered[0]->uav.get(),
			texDepthMipPrefiltered[1]->uav.get(),
			texDepthMipPrefiltered[2]->uav.get(),
			texDepthMipPrefiltered[3]->uav.get()
		};
		context->CSSetShaderResources(0, 1, &srcDepthSRV);
		context->CSSetUnorderedAccessViews(0, 4, depthUAVs, nullptr);
		context->CSSetShader(prefilterDepthsCS.get(), nullptr, 0);
		// Each thread handles a 2x2 full-res block, so dispatch at half resolution.
		uint pfW = (texDepthMipPrefiltered[0]->desc.Width / 2 + 7) / 8;
		uint pfH = (texDepthMipPrefiltered[0]->desc.Height / 2 + 7) / 8;
		context->Dispatch(pfW, pfH, 1);

		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);

		if (state->frameAnnotations)
			state->EndPerfEvent();
	}

	// === Optional depth pyramid smoothing — blur each mip after the full chain is built. ===
	// PrefilterDepthsCS has already produced all 4 mip levels; we now blur each independently.
	if (settings.BlurDepthPyramid && blurDepthCS) {
		auto runBlurDepthCS = [&](Texture2D* src, Texture2D* dst, uint mip) {
			cbData.CurrentMip = mip;
			sssCB->Update(cbData);
			ID3D11ShaderResourceView* srv = src->srv.get();
			ID3D11UnorderedAccessView* uav = dst->uav.get();
			context->CSSetShaderResources(0, 1, &srv);
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->CSSetShader(blurDepthCS.get(), nullptr, 0);
			uint w = std::max(1u, uint(renderSize.x) >> mip);
			uint h = std::max(1u, uint(renderSize.y) >> mip);
			context->Dispatch((w + 7u) >> 3, (h + 7u) >> 3, 1);
			ID3D11ShaderResourceView* nullSRV2 = nullptr;
			ID3D11UnorderedAccessView* nullUAV = nullptr;
			context->CSSetShaderResources(0, 1, &nullSRV2);
			context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		};

		if (state->frameAnnotations)
			state->BeginPerfEvent("SSS - Depth Pyramid Blur");

		// BlurDepthCS reads the raw prefiltered depth and writes the blurred copy
		// into texDepthMip; no ping-pong needed since the source is preserved.
		for (uint i = 0; i < 4u; ++i) {
			runBlurDepthCS(texDepthMipPrefiltered[i].get(), texDepthMip[i].get(), i);
		}

		if (state->frameAnnotations)
			state->EndPerfEvent();
	}

	// === Shadow marching — cascaded ray segments along a single ray per pixel ===
	// CPU drives the cascade.  Mip 0 dispatches first and marches the segment at
	// the start position (the receiver) with the fewest samples.  Each step toward
	// mip 3 walks farther along the ray toward the light, doubling both segment
	// length and sample count, so mip 3 covers the largest stretch near the light
	// end.  Sample density (samples / length) stays constant across mips.
	// Total ray reach = RayLength * (1/8 + 1/4 + 1/2 + 1) = RayLength * 1.875.
	{
		float segmentStart = 0.0f;
		for (int mip = 0; mip < 4; ++mip) {
			float segmentLength = settings.RayLength / float(1 << (3 - mip));

			uint mipW = std::max(1u, uint(renderSize.x) >> mip);
			uint mipH = std::max(1u, uint(renderSize.y) >> mip);

			cbData.CurrentMip = (uint)mip;
			cbData.SegmentStart = segmentStart;
			cbData.SegmentLength = segmentLength;
			sssCB->Update(cbData);

			auto* sampleDepthSRV = settings.BlurDepthPyramid
			                           ? texDepthMip[mip]->srv.get()
			                           : texDepthMipPrefiltered[mip]->srv.get();
			ID3D11ShaderResourceView* depthSRVs[2] = {
				sampleDepthSRV,
				texDepthMipPrefiltered[mip]->srv.get()
			};
			context->CSSetShaderResources(0, 2, depthSRVs);

			auto* shadowUAV = texShadowMip[mip]->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &shadowUAV, nullptr);

			if (!globals::game::isVR) {
				if (state->frameAnnotations)
					state->BeginPerfEvent(std::format("SSS - Shadows Mip{}", mip));
				context->CSSetShader(shadowsCS[mip].get(), nullptr, 0);
				context->Dispatch((mipW + 7u) >> 3, (mipH + 7u) >> 3, 1);
				if (state->frameAnnotations)
					state->EndPerfEvent();
			} else {
				uint perEyeW = std::max(1u, uint(renderSize.x) / 2u >> mip);
				{
					TracyD3D11Zone(state->tracyCtx, "SSS - Left Eye");
					if (state->frameAnnotations)
						state->BeginPerfEvent(std::format("SSS - Shadows Mip{} Left", mip));
					context->CSSetShader(shadowsCS[mip].get(), nullptr, 0);
					context->Dispatch((perEyeW + 7u) >> 3, (mipH + 7u) >> 3, 1);
					if (state->frameAnnotations)
						state->EndPerfEvent();
				}
				{
					TracyD3D11Zone(state->tracyCtx, "SSS - Right Eye");
					if (state->frameAnnotations)
						state->BeginPerfEvent(std::format("SSS - Shadows Mip{} Right", mip));
					context->CSSetShader(shadowsRightCS[mip].get(), nullptr, 0);
					context->Dispatch((perEyeW + 7u) >> 3, (mipH + 7u) >> 3, 1);
					if (state->frameAnnotations)
						state->EndPerfEvent();
				}
			}

			segmentStart += segmentLength;
		}

		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 2, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}

	// === Cascaded blur → upscale chain (mip 3 down to 1, then final blur at mip 0) ===
	// Pipeline per iteration: blur mip N → upscale+min-combine into mip N-1.
	// Blur ping-pongs between texShadowWork and texShadowMip to avoid SRV/UAV hazards:
	//   mip 3: blurIn=texShadowMip[3], blurOut=texShadowWork[3]
	//   mip 2: blurIn=texShadowWork[2] (upscale output), blurOut=texShadowMip[2] (repurposed)
	//   mip 1: blurIn=texShadowWork[1], blurOut=texShadowMip[1] (repurposed)
	for (int mip = 3; mip >= 1; --mip) {
		uint mipW = std::max(1u, uint(renderSize.x) >> mip);
		uint mipH = std::max(1u, uint(renderSize.y) >> mip);
		uint outW = std::max(1u, uint(renderSize.x) >> (mip - 1));
		uint outH = std::max(1u, uint(renderSize.y) >> (mip - 1));

		cbData.CurrentMip = (uint)mip;
		sssCB->Update(cbData);

		// For mip 3 the march output is in texShadowMip; for mip < 3 the upscale wrote texShadowWork.
		Texture2D* blurInTex = (mip == 3) ? texShadowMip[mip].get() : texShadowWork[mip].get();
		// Blur output ping-pong: mip 3 → texShadowWork[3]; mip < 3 → repurpose texShadowMip[mip].
		Texture2D* blurOutTex = (mip == 3) ? texShadowWork[mip].get() : texShadowMip[mip].get();

		if (blurCS) {
			if (state->frameAnnotations)
				state->BeginPerfEvent(std::format("SSS - Blur Mip{}", mip));
			ID3D11ShaderResourceView* blurSRVs[2] = { blurInTex->srv.get(), texDepthMipPrefiltered[mip]->srv.get() };
			auto* outUAV = blurOutTex->uav.get();
			context->CSSetShaderResources(0, 2, blurSRVs);
			context->CSSetUnorderedAccessViews(0, 1, &outUAV, nullptr);
			context->CSSetShader(blurCS.get(), nullptr, 0);
			context->Dispatch((mipW + 7u) >> 3, (mipH + 7u) >> 3, 1);
			ID3D11ShaderResourceView* nullSRVs2[2] = { nullptr, nullptr };
			ID3D11UnorderedAccessView* nullUAV = nullptr;
			context->CSSetShaderResources(0, 2, nullSRVs2);
			context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
			if (state->frameAnnotations)
				state->EndPerfEvent();
		}

		if (upscaleCS) {
			if (state->frameAnnotations)
				state->BeginPerfEvent(std::format("SSS - Upscale Mip{}→{}", mip, mip - 1));
			ID3D11ShaderResourceView* srvs[4] = {
				blurOutTex->srv.get(),                       // t0: blurred shadow at CurrentMip
				texDepthMipPrefiltered[mip]->srv.get(),       // t1: depth at CurrentMip (unused)
				texShadowMip[mip - 1]->srv.get(),             // t2: marched shadow at CurrentMip-1
				texDepthMipPrefiltered[mip - 1]->srv.get(),   // t3: depth at CurrentMip-1 (unused)
			};
			context->CSSetShaderResources(0, 4, srvs);
			auto* outUAV = texShadowWork[mip - 1]->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &outUAV, nullptr);
			context->CSSetShader(upscaleCS.get(), nullptr, 0);
			context->Dispatch((outW + 7u) >> 3, (outH + 7u) >> 3, 1);
			ID3D11ShaderResourceView* nullSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
			ID3D11UnorderedAccessView* nullUAV = nullptr;
			context->CSSetShaderResources(0, 4, nullSRVs);
			context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
			if (state->frameAnnotations)
				state->EndPerfEvent();
		}
	}

	// === Final blur at mip 0 (full resolution) — writes the final shadow mask. ===
	{
		cbData.CurrentMip = 0;
		sssCB->Update(cbData);

		if (state->frameAnnotations)
			state->BeginPerfEvent("SSS - Blur Mip0");
		ID3D11ShaderResourceView* finalBlurSRVs[2] = { texShadowWork[0]->srv.get(), texDepthMipPrefiltered[0]->srv.get() };
		auto* outUAV = screenSpaceShadowsTexture->uav.get();
		context->CSSetShaderResources(0, 2, finalBlurSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &outUAV, nullptr);
		context->CSSetShader(blurCS.get(), nullptr, 0);
		context->Dispatch((uint(renderSize.x) + 7u) >> 3, (uint(renderSize.y) + 7u) >> 3, 1);
		ID3D11ShaderResourceView* nullSRVs2[2] = { nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 2, nullSRVs2);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		if (state->frameAnnotations)
			state->EndPerfEvent();
	}

	// Clean up
	ID3D11SamplerState* nullSamplers[2] = { nullptr, nullptr };
	ID3D11Buffer* nullCB = nullptr;
	context->CSSetSamplers(0, 2, nullSamplers);
	context->CSSetConstantBuffers(1, 1, &nullCB);
	context->CSSetConstantBuffers(12, 1, &nullCB);
	context->CSSetShader(nullptr, nullptr, 0);
}

void ScreenSpaceShadows::DrawStereoSync()
{
	if (!globals::game::isVR || !enableStereoSync || !stereoSyncCopyTex || !stereoSyncCB)
		return;

	if (!stereoSyncCS)
		stereoSyncCS.attach(reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ScreenSpaceShadows\\StereoSyncCS.hlsl", { { "VR", "" }, { "FRAMEBUFFER", "" } }, "cs_5_0")));
	if (!stereoSyncCS)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "SSS - Stereo Sync");

	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("SSS - Stereo Sync");

	auto context = globals::d3d::context;

	context->CopyResource(stereoSyncCopyTex->resource.get(), screenSpaceShadowsTexture->resource.get());

	float2 resolution = Util::ConvertToDynamic(globals::state->screenSize);

	StereoSyncCB cbData{};
	cbData.FrameDim[0] = resolution.x;
	cbData.FrameDim[1] = resolution.y;
	cbData.RcpFrameDim[0] = 1.0f / resolution.x;
	cbData.RcpFrameDim[1] = 1.0f / resolution.y;

	stereoSyncCB->Update(cbData);
	auto* cbPtr = stereoSyncCB->CB();

	auto* depthSRV = Util::GetCurrentSceneDepthSRV();
	ID3D11ShaderResourceView* srvs[2]{ depthSRV, stereoSyncCopyTex->srv.get() };
	ID3D11UnorderedAccessView* uavs[1]{ screenSpaceShadowsTexture->uav.get() };

	context->CSSetConstantBuffers(1, 1, &cbPtr);
	auto* sharedDataBuf = globals::state->sharedDataCB->CB();
	context->CSSetConstantBuffers(5, 1, &sharedDataBuf);
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	context->CSSetShader(stereoSyncCS.get(), nullptr, 0);

	auto dispatchCount = Util::GetScreenDispatchCount(true);
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

	ID3D11ShaderResourceView* nullSRVs[2]{ nullptr, nullptr };
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ID3D11Buffer* nullCB = nullptr;
	context->CSSetShaderResources(0, 2, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetConstantBuffers(1, 1, &nullCB);
	context->CSSetConstantBuffers(5, 1, &nullCB);
	context->CSSetShader(nullptr, nullptr, 0);

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}

void ScreenSpaceShadows::Prepass()
{
	auto context = globals::d3d::context;

	float white[4] = { 1, 1, 1, 1 };
	for (int i = 0; i < 4; ++i)
		context->ClearUnorderedAccessViewFloat(texShadowMip[i]->uav.get(), white);
	for (int i = 0; i < 4; ++i)
		context->ClearUnorderedAccessViewFloat(texShadowWork[i]->uav.get(), white);
	context->ClearUnorderedAccessViewFloat(screenSpaceShadowsTexture->uav.get(), white);

	if (auto sky = globals::game::sky)
		if (settings.Enabled && sky->mode.get() == RE::Sky::Mode::kFull) {
			DrawShadows();
			DrawStereoSync();
		}

	auto* view = screenSpaceShadowsTexture->srv.get();
	context->PSSetShaderResources(45, 1, &view);
}

void ScreenSpaceShadows::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ScreenSpaceShadows::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ScreenSpaceShadows::RestoreDefaultSettings()
{
	settings = {};
	if (globals::game::isVR) {
		settings.SurfaceThickness = 1.0f;
		settings.ShadowContrast = 4.0f;
	}
}

bool ScreenSpaceShadows::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}

void ScreenSpaceShadows::SetupResources()
{
	auto device = globals::d3d::device;
	auto renderer = globals::game::renderer;

	sssCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSSCB>(), "SSS::CB");

	if (globals::game::isVR)
		stereoSyncCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<StereoSyncCB>(), "SSS::StereoSyncCB");

	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointClampSampler.put()));
		Util::SetResourceName(pointClampSampler.get(), "SSS::PointClampSampler");

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));
		Util::SetResourceName(linearClampSampler.get(), "SSS::LinearClampSampler");
	}

	{
		auto shadowMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSHADOW_MASK];

		D3D11_TEXTURE2D_DESC texDesc{};
		shadowMask.texture->GetDesc(&texDesc);
		texDesc.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		UINT baseWidth = texDesc.Width;
		UINT baseHeight = texDesc.Height;

		// Depth mip pyramid — R32G32_FLOAT (linearZ, linearZ²) for VSM queries.
		// Scratch siblings are allocated alongside for in-place blur ping-pong.
		{
			texDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
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
			for (int i = 0; i < 4; ++i) {
				texDesc.Width = std::max(1u, baseWidth >> i);
				texDesc.Height = std::max(1u, baseHeight >> i);
				char name[64];
				snprintf(name, sizeof(name), "SSS::DepthMipPrefiltered%d", i);
				texDepthMipPrefiltered[i] = eastl::make_unique<Texture2D>(texDesc, name);
				texDepthMipPrefiltered[i]->CreateSRV(srvDesc);
				texDepthMipPrefiltered[i]->CreateUAV(uavDesc);

				snprintf(name, sizeof(name), "SSS::DepthMip%d", i);
				texDepthMip[i] = eastl::make_unique<Texture2D>(texDesc, name);
				texDepthMip[i]->CreateSRV(srvDesc);
				texDepthMip[i]->CreateUAV(uavDesc);
			}
		}

		// Shadow textures — R8_UNORM.  ShadowsCS does its own moments+Chebyshev
		// resolution per ray, so downstream blur/upscale and the final shadow
		// mask all carry plain visibility values.
		{
			texDesc.Format = DXGI_FORMAT_R8_UNORM;
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
			for (int i = 0; i < 4; ++i) {
				texDesc.Width = std::max(1u, baseWidth >> i);
				texDesc.Height = std::max(1u, baseHeight >> i);
				char name[64];
				snprintf(name, sizeof(name), "SSS::ShadowMip%d", i);
				texShadowMip[i] = eastl::make_unique<Texture2D>(texDesc, name);
				texShadowMip[i]->CreateSRV(srvDesc);
				texShadowMip[i]->CreateUAV(uavDesc);
			}

			// Working textures — same format, one per mip (0-3).
			for (int i = 0; i < 4; ++i) {
				texDesc.Width = std::max(1u, baseWidth >> i);
				texDesc.Height = std::max(1u, baseHeight >> i);
				char name[64];
				snprintf(name, sizeof(name), "SSS::ShadowWork%d", i);
				texShadowWork[i] = eastl::make_unique<Texture2D>(texDesc, name);
				texShadowWork[i]->CreateSRV(srvDesc);
				texShadowWork[i]->CreateUAV(uavDesc);
			}

			texDesc.Width = baseWidth;
			texDesc.Height = baseHeight;

			screenSpaceShadowsTexture = eastl::make_unique<Texture2D>(texDesc, "SSS::ShadowTexture");
			screenSpaceShadowsTexture->CreateSRV(srvDesc);
			screenSpaceShadowsTexture->CreateUAV(uavDesc);

			if (globals::game::isVR) {
				stereoSyncCopyTex = eastl::make_unique<Texture2D>(texDesc, "SSS::StereoSyncCopy");
				stereoSyncCopyTex->CreateSRV(srvDesc);
			}
		}
	}

	CompileComputeShaders();
}
