#include "ScreenSpaceRayTracing.h"

#include <DDSTextureLoader.h>

#include "Deferred.h"
#include "Menu.h"
#include "State.h"
#include "ShaderCache.h"

#include "DynamicCubemaps.h"
#include "ScreenSpaceGI.h"
#include "Skylighting.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ScreenSpaceRayTracing::Settings,
    EnableSpecular,
    MaxSteps,
    MaxMips,
    Thickness,
    NormalBias,
    BRDFBias,
    UseDynamicCubemapsAsFallback,
    UseDynamicCubemapsAsFallbackSpecular,
    DiffuseSPP,
    EnableDiffuse,
    SpecularMult,
    DiffuseMult,
    AmbientMult,
    OcclusionStrength,
    CubemapNormalization,
    EnableREBLUR,
    REBLURMaxAccumFrames,
    REBLURSplitScreen
)

void ScreenSpaceRayTracing::DrawSettings()
{
    ImGui::Checkbox("Enable Specular", &settings.EnableSpecular);
    ImGui::SameLine();
    ImGui::Checkbox("Enable Diffuse", &settings.EnableDiffuse);
    ImGui::SliderInt("Max Steps", (int*)&settings.MaxSteps, 1, 256);
    ImGui::SliderInt("Max Mip Level", (int*)&settings.MaxMips, 1, maxMips, "%d", ImGuiSliderFlags_AlwaysClamp);
    recompileFlag |= ImGui::SliderInt("Diffuse SPP", (int*)&settings.DiffuseSPP, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("Samples per pixel for diffuse component. Higher values reduce noise but impact performance.");
    ImGui::SliderFloat("Specular Multiplier", &settings.SpecularMult, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Diffuse Multiplier", &settings.DiffuseMult, 0.01f, 5.0f, "%.2f");
    ImGui::SliderFloat("Occlusion Strength", &settings.OcclusionStrength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Ambient Multiplier", &settings.AmbientMult, 0.0f, 1.0f, "%.2f");
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("Mix diffuse with vanilla ambient color. Not suggested if using dynamic cubemaps as fallback.");

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
    ImGui::SliderFloat("Cubemap Normalization", &settings.CubemapNormalization, 0.0f, 1.0f, "%.2f");
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("Matches cubemap luminance with ambient color.");

    ImGui::SeparatorText("REBLUR Denoiser");
    ImGui::Checkbox("Enable REBLUR", &settings.EnableREBLUR);
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("NVIDIA REBLUR temporal denoiser for diffuse GI. Reduces noise at the cost of temporal lag.");
    if (settings.EnableREBLUR) {
        int maxFrames = (int)settings.REBLURMaxAccumFrames;
        if (ImGui::SliderInt("Max Accum Frames", &maxFrames, 1, 63)) {
            settings.REBLURMaxAccumFrames = (uint32_t)maxFrames;
        }
        if (auto _tt = Util::HoverTooltipWrapper())
            ImGui::Text("Maximum frames REBLUR accumulates. Lower = less lag but more noise.");
        ImGui::SliderFloat("Split Screen", &settings.REBLURSplitScreen, 0.0f, 1.0f, "%.2f");
        if (auto _tt = Util::HoverTooltipWrapper())
            ImGui::Text("Debug: left side shows raw (no temporal) output, right shows denoised. 0 = disabled.");
    }

    ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texDepth, debugRescale)
		BUFFER_VIEWER_NODE(texHalfResDepth, debugRescale)
        BUFFER_VIEWER_NODE(texColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRTDiffuseColor, debugRescale)
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

        // -- 1/2 resolution textures --

        texDesc.Width /= 2;
		texDesc.Height /= 2;

        uint32_t halfW = texDesc.Width;
        uint32_t halfH = texDesc.Height;

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

        // NRD ViewZ (R16F)
        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
        texNRDViewZ = eastl::make_unique<Texture2D>(texDesc);
        texNRDViewZ->CreateSRV(srvDesc);
        texNRDViewZ->CreateUAV(uavDesc);

        // NRD NormalRoughness: R8G8B8A8_UNORM is guaranteed UAV store on D3D11.1.
        // We use NRD_NORMAL_ENCODING=2 packing math; the [0,1] values decode correctly
        // regardless of whether the backing texture is R10G10B10A2 or R8G8B8A8.
        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texNRDNormalRoughness = eastl::make_unique<Texture2D>(texDesc);
        texNRDNormalRoughness->CreateSRV(srvDesc);
        texNRDNormalRoughness->CreateUAV(uavDesc);

        // NRD MotionVectors at 1/2 res (R16G16F)
        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
        texNRDMotionVectors = eastl::make_unique<Texture2D>(texDesc);
        texNRDMotionVectors->CreateSRV(srvDesc);
        texNRDMotionVectors->CreateUAV(uavDesc);

        // Half-res linear depth
        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        texHalfResDepth = eastl::make_unique<Texture2D>(texDesc);
        texHalfResDepth->CreateSRV(srvDesc);
        texHalfResDepth->CreateUAV(uavDesc);

        // -- 1/4 resolution textures --
        texDesc.Width /= 2;
		texDesc.Height /= 2;

        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        srvDesc.Texture2D.MipLevels = 1;
        texColor = eastl::make_unique<Texture2D>(texDesc);
        texColor->CreateSRV(srvDesc);
        texColor->CreateUAV(uavDesc);

        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.Texture2D.MipLevels = 1;

        texSSRColor = eastl::make_unique<Texture2D>(texDesc);
        texSSRColor->CreateSRV(srvDesc);
        texSSRColor->CreateUAV(uavDesc);
        texSSRTDiffuseColor = eastl::make_unique<Texture2D>(texDesc);
        texSSRTDiffuseColor->CreateSRV(srvDesc);
        texSSRTDiffuseColor->CreateUAV(uavDesc);

        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R8G8_UNORM;
        texSSRTDiffuseDirection = eastl::make_unique<Texture2D>(texDesc);
        texSSRTDiffuseDirection->CreateSRV(srvDesc);
        texSSRTDiffuseDirection->CreateUAV(uavDesc);

        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        texDesc.MipLevels = maxMips;
        srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
        texDepth = eastl::make_unique<Texture2D>(texDesc);
        texDepth->CreateSRV(srvDesc);
        texDepth->CreateUAV(uavDesc);

        for (uint i = 0; i < maxMips; i++) {
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

        // Initialize NRD
        nrdReblur.Init(halfW, halfH, nrd::Denoiser::REBLUR_DIFFUSE_SH, 0);
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
        &raymarchSpecularCS, &raymarchDiffuseCS, &prefilterRadianceCS, &prefilterDepthCS,
        &depthDownsampleCS, &upscaleDiffuseCS, &prepareNRDGuidesCS,
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

    if (globals::features::screenSpaceGI.loaded)
		defines.push_back({ "SSGI", nullptr });

    if (globals::features::skylighting.loaded)
		defines.push_back({ "SKYLIGHTING", nullptr });

    const std::string DiffuseSPPStr = std::to_string(settings.DiffuseSPP);

    defines.push_back({ "DIFFUSE_SPP", DiffuseSPPStr.c_str() });

    auto definesSpecular = defines;
    definesSpecular.push_back({ "SSRT_SPECULAR", nullptr });

    std::vector<ShaderCompileInfo>
        shaderInfos = {
            { &raymarchDiffuseCS, "ssrt_raymarch.hlsl", defines },
            { &raymarchSpecularCS, "ssrt_raymarch.hlsl", definesSpecular },
            { &prefilterRadianceCS, "ssrt_prefilterRadiance.hlsl", {} },
            { &prefilterDepthCS, "ssrt_prefilterDepths.hlsl", {} },
            { &depthDownsampleCS, "ssrt_depth_downsample.hlsl", {} },
            { &upscaleDiffuseCS, "ssrt_upscale_diffuse.hlsl", {} },
            { &prepareNRDGuidesCS, "ssrt_preprocess_nrd_guides.hlsl", {} },
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
    float2 dispatchCount = { (size.x / 4 + 7) / 8, (size.y / 4 + 7) / 8 };

    SSRTCB ssrCBData;
    {
        ssrCBData.MaxSteps = settings.MaxSteps;
        ssrCBData.MaxMips = settings.MaxMips;
        ssrCBData.Thickness = settings.Thickness;
        ssrCBData.NormalBias = settings.NormalBias;
        ssrCBData.BRDFBias = settings.BRDFBias;
        ssrCBData.UseDynamicCubemapsAsFallback = 0;
        ssrCBData.OcclusionStrength = settings.OcclusionStrength;
        ssrCBData.CubemapNormalization = settings.CubemapNormalization;

        ssrCBData.TexDim = res;
        ssrCBData.RcpTexDim = float2(1.0f) / res;
        ssrCBData.FrameDim = dynres;
        ssrCBData.RcpFrameDim = float2(1.0f) / dynres;
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

    // prefilter depth: full-res NDC depth → quarter-res min-filtered base of Hi-Z pyramid
    {
		auto srv = depth.depthSRV;
		context->CSSetShaderResources(0, 1, &srv);

        ID3D11UnorderedAccessView* uavs2[] = { depthUAVs[0].get(), texHalfResDepth->uav.get() };
		context->CSSetUnorderedAccessViews(0, 2, uavs2, nullptr);

        context->CSSetShader(prefilterDepthCS.get(), nullptr, 0);
        context->Dispatch(((uint)dynres.x + 15) >> 4, ((uint)dynres.y + 15) >> 4, 1);

        resetViews();
    }

    // downsample depth Hi-Z
    {
		float2 dispatchCountDownsample = { ((size.x / 2) + 7) / 8, ((size.y / 2) + 7) / 8 };

        state->BeginPerfEvent("Downsample Depth - HiZ Buffer");
        for (int i = 0; i < maxMips - 1; ++i) {
            uavs.at(0) = depthUAVs[i + 1].get();
            srvs.at(0) = depthSRVs[i].get();

            context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
            context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
            context->CSSetShader(depthDownsampleCS.get(), nullptr, 0);

            context->Dispatch((uint)dispatchCountDownsample.x >> i, (uint)dispatchCountDownsample.y >> i, 1);

            resetViews();
        }
        state->EndPerfEvent();
    }

    state->EndPerfEvent();

    auto view = texDepth->srv.get();
    context->PSSetShaderResources(99, 1, &view);
}

void ScreenSpaceRayTracing::DrawSSRTSpecular()
{
    if (!settings.EnableSpecular)
        return;

    auto renderer = globals::game::renderer;
    auto context = globals::d3d::context;
    auto state = globals::state;

    state->BeginPerfEvent("SSRT Compute");

    auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
    auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
    auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
    auto normal = renderer->GetRuntimeData().renderTargets[globals::deferred->normalRoughnessRT];
    auto motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

    auto& dynamicCubemaps = globals::features::dynamicCubemaps;
    auto& ssgi = globals::features::screenSpaceGI;
    auto& skylighting = globals::features::skylighting;

    float2 res = state->screenSize;
    float2 dynres = Util::ConvertToDynamic(res);
    dynres = { floor(dynres.x), floor(dynres.y) };

    float2 dispatchCount = { (dynres.x / 4 + 7) / 8, (dynres.y / 4 + 7) / 8 };

    SSRTCB ssrCBData;
    {
        ssrCBData.MaxSteps = settings.MaxSteps;
        ssrCBData.MaxMips = settings.MaxMips;
        ssrCBData.Thickness = settings.Thickness;
        ssrCBData.NormalBias = settings.NormalBias;
        ssrCBData.BRDFBias = settings.BRDFBias;
        ssrCBData.UseDynamicCubemapsAsFallback = (uint)settings.UseDynamicCubemapsAsFallbackSpecular && dynamicCubemaps.loaded;
        ssrCBData.OcclusionStrength = settings.OcclusionStrength;
        ssrCBData.CubemapNormalization = settings.CubemapNormalization;

        ssrCBData.TexDim = res;
        ssrCBData.RcpTexDim = float2(1.0f) / res;
        ssrCBData.FrameDim = dynres;
        ssrCBData.RcpFrameDim = float2(1.0f) / dynres;
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

    // prefilter radiance
    {
        ID3D11UnorderedAccessView* colorUav = texColor->uav.get();

        srvs.at(0) = main.SRV;

        context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
        context->CSSetUnorderedAccessViews(0, 1, &colorUav, nullptr);
        context->CSSetShader(prefilterRadianceCS.get(), nullptr, 0);
        context->Dispatch(((uint)dynres.x + 15) >> 4, ((uint)dynres.y + 15) >> 4, 1);

        srvs.fill(nullptr);
        ID3D11UnorderedAccessView* nullUav = nullptr;
        context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
        context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
    }

    const auto envTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr;
	const auto envReflectionsTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr;

    auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec] = ssgi.GetOutputTextures();

    bool inInterior = true;

    if (auto player = RE::PlayerCharacter::GetSingleton()) {
        if (auto parentCell = player->GetParentCell()) {
            inInterior = parentCell->IsInteriorCell();
        }
    }

    state->BeginPerfEvent("Raymarch");

    uavs.at(0) = texSSRColor->uav.get();

    srvs.at(1) = motion.SRV;
    srvs.at(2) = normal.SRV;
    srvs.at(3) = texColor->srv.get();
    srvs.at(4) = depth.depthSRV;
    srvs.at(5) = texDepth->srv.get();
    srvs.at(6) = noiseSRV.get();
    srvs.at(7) = envTexture;
    srvs.at(8) = inInterior ? envTexture : envReflectionsTexture;
    srvs.at(9) = ssgi_ao;
    srvs.at(10) = dynamicCubemaps.loaded && skylighting.loaded ? skylighting.texProbeArray->srv.get() : nullptr;
    srvs.at(11) = dynamicCubemaps.loaded && skylighting.loaded ? skylighting.stbn_vec3_2Dx1D_128x128x64.get() : nullptr;

    context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
    context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
    context->CSSetShader(raymarchSpecularCS.get(), nullptr, 0);
    context->CSSetConstantBuffers(1, 1, &buffer);

    context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

    state->EndPerfEvent();

    resetViews();

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

    auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
    auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
    auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
    auto normal = renderer->GetRuntimeData().renderTargets[globals::deferred->normalRoughnessRT];
    auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
    auto motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

    auto& dynamicCubemaps = globals::features::dynamicCubemaps;
    auto& ssgi = globals::features::screenSpaceGI;
    auto& skylighting = globals::features::skylighting;

    float2 res = state->screenSize;
    float2 dynres = Util::ConvertToDynamic(res);
    dynres = { floor(dynres.x), floor(dynres.y) };

    float2 dispatchCount = { (dynres.x / 4 + 7) / 8, (dynres.y / 4 + 7) / 8 };

    SSRTCB ssrCBData;
    {
        ssrCBData.MaxSteps = settings.MaxSteps;
        ssrCBData.MaxMips = settings.MaxMips;
        ssrCBData.Thickness = settings.Thickness;
        ssrCBData.NormalBias = settings.NormalBias;
        ssrCBData.BRDFBias = settings.BRDFBias;
        ssrCBData.UseDynamicCubemapsAsFallback = (uint)settings.UseDynamicCubemapsAsFallback && dynamicCubemaps.loaded;
        ssrCBData.OcclusionStrength = settings.OcclusionStrength;
        ssrCBData.CubemapNormalization = settings.CubemapNormalization;

        ssrCBData.TexDim = res;
        ssrCBData.RcpTexDim = float2(1.0f) / res;
        ssrCBData.FrameDim = dynres;
        ssrCBData.RcpFrameDim = float2(1.0f) / dynres;
    }
    ssrtCB->Update(ssrCBData);
    auto buffer = ssrtCB->CB();
    context->CSSetConstantBuffers(1, 1, &buffer);

    std::array<ID3D11ShaderResourceView*, 13> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 3> uavs = { nullptr };

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

    auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec] = ssgi.GetOutputTextures();

    bool inInterior = true;

    if (auto player = RE::PlayerCharacter::GetSingleton()) {
        if (auto parentCell = player->GetParentCell()) {
            inInterior = parentCell->IsInteriorCell();
        }
    }

    // --- Ray march ---
    uavs.at(0) = texSSRTDiffuseColor->uav.get();
    uavs.at(1) = texSSRTDiffuseDirection->uav.get();

    srvs.at(1) = motion.SRV;
    srvs.at(2) = normal.SRV;
    srvs.at(3) = main.SRV;
    srvs.at(4) = depth.depthSRV;
    srvs.at(5) = texDepth->srv.get();
    srvs.at(6) = noiseSRV.get();
    srvs.at(7) = envTexture;
    srvs.at(8) = inInterior ? envTexture : envReflectionsTexture;
    srvs.at(9) = ssgi_ao;
    srvs.at(10) = dynamicCubemaps.loaded && skylighting.loaded ? skylighting.texProbeArray->srv.get() : nullptr;
    srvs.at(11) = dynamicCubemaps.loaded && skylighting.loaded ? skylighting.stbn_vec3_2Dx1D_128x128x64.get() : nullptr;
    srvs.at(12) = albedo.SRV;

    context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
    context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
    context->CSSetConstantBuffers(1, 1, &buffer);
    context->CSSetShader(raymarchDiffuseCS.get(), nullptr, 0);
    context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);
    resetViews();

    // --- Preprocess NRD guide textures (1/2 res) ---
    if (settings.EnableREBLUR && nrdReblur.IsValid() && prepareNRDGuidesCS) {
        state->BeginPerfEvent("SSRT NRD Guide Preprocess");

        std::array<ID3D11ShaderResourceView*, 3> guideSRVs = {
            depth.depthSRV,
            normal.SRV,
            motion.SRV
        };
        std::array<ID3D11UnorderedAccessView*, 3> guideUAVs = {
            texNRDViewZ->uav.get(),
            texNRDNormalRoughness->uav.get(),
            texNRDMotionVectors->uav.get()
        };

        context->CSSetShaderResources(0, (uint)guideSRVs.size(), guideSRVs.data());
        context->CSSetUnorderedAccessViews(0, (uint)guideUAVs.size(), guideUAVs.data(), nullptr);
        context->CSSetShader(prepareNRDGuidesCS.get(), nullptr, 0);
        context->Dispatch(((uint)dynres.x / 2 + 7) / 8, ((uint)dynres.y / 2 + 7) / 8, 1);
        resetViews();

        state->EndPerfEvent();
    }

    // --- Upscale diffuse to NRD-packed SH (1/2 res, 2 textures) ---
    {
        state->BeginPerfEvent("SSRT Diffuse Upscale");

        srvs.fill(nullptr);
        srvs.at(0) = texSSRTDiffuseColor->srv.get();
        srvs.at(1) = texSSRTDiffuseDirection->srv.get();
		srvs.at(2) = texHalfResDepth->srv.get();
		srvs.at(3) = depthSRVs[0].get();

        std::array<ID3D11UnorderedAccessView*, 2> shUavs = {
            texNRDInputSH0->uav.get(),
            texNRDInputSH1->uav.get()
        };

        context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
        context->CSSetUnorderedAccessViews(0, (uint)shUavs.size(), shUavs.data(), nullptr);
        context->CSSetShader(upscaleDiffuseCS.get(), nullptr, 0);

        context->Dispatch(((uint)dynres.x / 2 + 7) / 8, ((uint)dynres.y / 2 + 7) / 8, 1);
        resetViews();

        state->EndPerfEvent();
    }

    // --- REBLUR denoising ---
    if (settings.EnableREBLUR && nrdReblur.IsValid()) {
        state->BeginPerfEvent("SSRT REBLUR");

        // Update NRD common settings
        nrd::CommonSettings commonSettings{};
        {
            float2 halfDynres = { floor(dynres.x / 2), floor(dynres.y / 2) };
            uint16_t hw = (uint16_t)halfDynres.x;
            uint16_t hh = (uint16_t)halfDynres.y;

            // Resource size = half-res allocations
            commonSettings.resourceSize[0] = (uint16_t)texNRDInputSH0->desc.Width;
            commonSettings.resourceSize[1] = (uint16_t)texNRDInputSH0->desc.Height;
            commonSettings.resourceSizePrev[0] = commonSettings.resourceSize[0];
            commonSettings.resourceSizePrev[1] = commonSettings.resourceSize[1];
            commonSettings.rectSize[0] = hw;
            commonSettings.rectSize[1] = hh;
            commonSettings.rectSizePrev[0] = hw;
            commonSettings.rectSizePrev[1] = hh;

            // NRD wants column-major matrices; game stores row-major — transpose to convert.
            // Use unjittered projection to avoid NRD temporal artifacts.
            auto viewMat = globals::game::frameBufferCached.GetCameraView(0).Transpose();
            auto projMat = globals::game::frameBufferCached.GetCameraProjUnjittered(0).Transpose();

            memcpy(commonSettings.viewToClipMatrix,      &projMat,        sizeof(float) * 16);
            memcpy(commonSettings.viewToClipMatrixPrev,  &prevProjMatrix, sizeof(float) * 16);
            memcpy(commonSettings.worldToViewMatrix,     &viewMat,        sizeof(float) * 16);
            memcpy(commonSettings.worldToViewMatrixPrev, &prevViewMatrix, sizeof(float) * 16);

            prevViewMatrix = viewMat;
            prevProjMatrix = projMat;

            // 2D screen-space motion vectors (pixelUvPrev = pixelUv + mv)
            commonSettings.motionVectorScale[0] = 1.0f;
            commonSettings.motionVectorScale[1] = 1.0f;
            commonSettings.motionVectorScale[2] = 0.0f;
            commonSettings.isMotionVectorInWorldSpace = false;

            commonSettings.frameIndex = frameIndex++;

            // Skyrim world units can be very large (far plane ~300k+ units).
            // Default denoisingRange=500000 may clip distant geometry; use a safe margin.
            commonSettings.denoisingRange = 1e6f;

            // Debug: expose split-screen so left half shows raw (no-temporal) output.
            commonSettings.splitScreen = settings.REBLURSplitScreen;
        }
        nrdReblur.SetCommonSettings(commonSettings);

        // Update REBLUR settings
        reblurSettings.maxAccumulatedFrameNum = settings.REBLURMaxAccumFrames;
        nrdReblur.SetDenoiserSettings(&reblurSettings);

        // Bind named resources
        nrdReblur.SetNamedSRV(nrd::ResourceType::IN_MV,               texNRDMotionVectors->srv.get());
        nrdReblur.SetNamedSRV(nrd::ResourceType::IN_NORMAL_ROUGHNESS,  texNRDNormalRoughness->srv.get());
        nrdReblur.SetNamedSRV(nrd::ResourceType::IN_VIEWZ,             texNRDViewZ->srv.get());
        nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_SH0,          texNRDInputSH0->srv.get());
        nrdReblur.SetNamedSRV(nrd::ResourceType::IN_DIFF_SH1,          texNRDInputSH1->srv.get());
        // OUT_DIFF_SH0/SH1 serve as temp storage (DIFF_TEMP1/SH_TEMP1) between PrePass→TA→Blur,
        // then TemporalStabilization overwrites them with the final denoised result.
        // Both SRV (read by TA and Blur) and UAV (written by PrePass, HistoryFix, TS) must be bound.
        nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_SH0,         texNRDOutputSH0->srv.get());
        nrdReblur.SetNamedSRV(nrd::ResourceType::OUT_DIFF_SH1,         texNRDOutputSH1->srv.get());
        nrdReblur.SetNamedUAV(nrd::ResourceType::IN_MV,                texNRDMotionVectors->uav.get());  // temporal stabilization writes disocclusion correction here
        nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_SH0,         texNRDOutputSH0->uav.get());
        nrdReblur.SetNamedUAV(nrd::ResourceType::OUT_DIFF_SH1,         texNRDOutputSH1->uav.get());

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
        // Fallback: return NRD input textures (upscaled SH without denoising)
        output.sh[0] = texNRDInputSH0->srv.get();
        output.sh[1] = texNRDInputSH1->srv.get();
    }
    return output;
}

ScreenSpaceRayTracing::SharedData ScreenSpaceRayTracing::GetCommonBufferData()
{
    SharedData data;
    data.EnableSpecular = settings.EnableSpecular;
    data.SpecularMult = settings.SpecularMult;
    data.DiffuseMult = settings.EnableDiffuse ? settings.DiffuseMult : 0.0f;
    data.AmbientMult = settings.AmbientMult;
    return data;
}
