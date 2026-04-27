#include "ScreenSpaceShadows.h"

#include "State.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceShadows::Settings,
	Enabled,
	SurfaceThickness,
	ShadowContrast,
	RayLength)

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

		static const char* mipTitles[] = {
			"Shadow Mip 0 - Full res",
			"Shadow Mip 1 - Half res",
			"Shadow Mip 2 - Quarter res",
			"Shadow Mip 3 - Eighth res"
		};
		for (int i = 0; i < 4; ++i)
			BUFFER_VIEWER_NODE_TITLE(texShadowMip[i], mipTitles[i], debugRescale)
		BUFFER_VIEWER_NODE_TITLE(screenSpaceShadowsTexture, "Shadow Final (blurred)", debugRescale)

		ImGui::TreePop();
	}
}

void ScreenSpaceShadows::ClearShaderCache()
{
	prefilterDepthsCS = nullptr;
	shadowsCS = nullptr;
	shadowsRightCS = nullptr;
	upscaleCS = nullptr;
	blurCS = nullptr;
	stereoSyncCS = nullptr;
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

	if (auto* cs = compile(L"Data\\Shaders\\ScreenSpaceShadows\\ShadowsCS.hlsl", baseDefines))
		shadowsCS.attach(cs);

	if (REL::Module::IsVR()) {
		auto rightDefines = baseDefines;
		rightDefines.push_back({ "RIGHT", "" });
		if (auto* cs = compile(L"Data\\Shaders\\ScreenSpaceShadows\\ShadowsCS.hlsl", rightDefines))
			shadowsRightCS.attach(cs);
	}

	if (auto* cs = compile(L"Data\\Shaders\\ScreenSpaceShadows\\UpscaleCS.hlsl", {}))
		upscaleCS.attach(cs);

	if (auto* cs = compile(L"Data\\Shaders\\ScreenSpaceShadows\\BlurCS.hlsl", {}))
		blurCS.attach(cs);
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
	cbData.RayLength = settings.RayLength;
	cbData.LightWorldDir = { -light.x, -light.y, -light.z };

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
			texDepthMip[0]->uav.get(),
			texDepthMip[1]->uav.get(),
			texDepthMip[2]->uav.get(),
			texDepthMip[3]->uav.get()
		};
		context->CSSetShaderResources(0, 1, &srcDepthSRV);
		context->CSSetUnorderedAccessViews(0, 4, depthUAVs, nullptr);
		context->CSSetShader(prefilterDepthsCS.get(), nullptr, 0);
		// Each thread handles a 2x2 full-res block, so dispatch at half resolution.
		uint pfW = (texDepthMip[0]->desc.Width / 2 + 7) / 8;
		uint pfH = (texDepthMip[0]->desc.Height / 2 + 7) / 8;
		context->Dispatch(pfW, pfH, 1);

		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);

		if (state->frameAnnotations)
			state->EndPerfEvent();
	}

	// === Shadow marching — 4 mip levels ===
	{
		for (uint mip = 0; mip < 4u; ++mip) {
			uint mipW = std::max(1u, uint(renderSize.x) >> mip);
			uint mipH = std::max(1u, uint(renderSize.y) >> mip);

			cbData.CurrentMip = mip;
			sssCB->Update(cbData);

			auto* depthSRV = texDepthMip[mip]->srv.get();
			context->CSSetShaderResources(0, 1, &depthSRV);

			auto* shadowUAV = texShadowMip[mip]->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &shadowUAV, nullptr);

			if (!globals::game::isVR) {
				if (state->frameAnnotations)
					state->BeginPerfEvent(std::format("SSS - Shadows Mip{}", mip));
				context->CSSetShader(shadowsCS.get(), nullptr, 0);
				context->Dispatch((mipW + 7u) >> 3, (mipH + 7u) >> 3, 1);
				if (state->frameAnnotations)
					state->EndPerfEvent();
			} else {
				uint perEyeW = std::max(1u, uint(renderSize.x) / 2u >> mip);
				{
					TracyD3D11Zone(state->tracyCtx, "SSS - Left Eye");
					if (state->frameAnnotations)
						state->BeginPerfEvent(std::format("SSS - Shadows Mip{} Left", mip));
					context->CSSetShader(shadowsCS.get(), nullptr, 0);
					context->Dispatch((perEyeW + 7u) >> 3, (mipH + 7u) >> 3, 1);
					if (state->frameAnnotations)
						state->EndPerfEvent();
				}
				{
					TracyD3D11Zone(state->tracyCtx, "SSS - Right Eye");
					if (state->frameAnnotations)
						state->BeginPerfEvent(std::format("SSS - Shadows Mip{} Right", mip));
					context->CSSetShader(shadowsRightCS.get(), nullptr, 0);
					context->Dispatch((perEyeW + 7u) >> 3, (mipH + 7u) >> 3, 1);
					if (state->frameAnnotations)
						state->EndPerfEvent();
				}
			}
		}

		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 1, &nullSRV);
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
			ID3D11ShaderResourceView* blurSRVs[2] = { blurInTex->srv.get(), texDepthMip[mip]->srv.get() };
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
				blurOutTex->srv.get(),               // t0: blurred shadow at CurrentMip
				texDepthMip[mip]->srv.get(),          // t1: depth at CurrentMip
				texShadowMip[mip - 1]->srv.get(),     // t2: marched shadow at CurrentMip-1
				texDepthMip[mip - 1]->srv.get(),      // t3: depth at CurrentMip-1
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

	// === Final blur at mip 0 (full resolution) ===
	{
		cbData.CurrentMip = 0;
		sssCB->Update(cbData);

		if (state->frameAnnotations)
			state->BeginPerfEvent("SSS - Blur Mip0");
		auto* shadowSrc = texShadowWork[0] ? texShadowWork[0]->srv.get() : texShadowMip[0]->srv.get();
		ID3D11ShaderResourceView* finalBlurSRVs[2] = { shadowSrc, texDepthMip[0]->srv.get() };
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

		// Depth mip pyramid — R32_FLOAT at progressively halved resolutions.
		{
			texDesc.Format = DXGI_FORMAT_R32_FLOAT;
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
				snprintf(name, sizeof(name), "SSS::DepthMip%d", i);
				texDepthMip[i] = eastl::make_unique<Texture2D>(texDesc, name);
				texDepthMip[i]->CreateSRV(srvDesc);
				texDepthMip[i]->CreateUAV(uavDesc);
			}
		}

		// Shadow mip textures — R8_UNORM.
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
