#include "ScreenSpaceReflections.h"

#include <DDSTextureLoader.h>

#include "Deferred.h"
#include "Menu.h"
#include "State.h"
#include "ShaderCache.h"

#include "DynamicCubemaps.h"
#include "ScreenSpaceGI.h"
#include "Skylighting.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ScreenSpaceReflections::Settings,
    Enabled,
    MaxSteps,
    MaxMips,
    Thickness,
    RoughnessMask,
    BRDFBias,
    SpatialTimes,
    SpatialRadius,
    EnableTemporal,
    TemporalScale,
    TemporalWeight,
    EnableBilateral,
    BilateralScale,
    BilateralColorWeight,
    BilateralDepthWeight,
    BilateralNormalWeight,
    UseDynamicCubemapsAsFallback,
    DiffuseSPP,
    EnableDiffuse,
    SpecularMult,
    DiffuseMult,
    AmbienceMult
)

void ScreenSpaceReflections::DrawSettings()
{
    ImGui::Checkbox("Enabled", &settings.Enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Enable Diffuse", &settings.EnableDiffuse);
    ImGui::SliderInt("Max Steps", (int*)&settings.MaxSteps, 1, 256);
    ImGui::SliderInt("Max Mip Level", (int*)&settings.MaxMips, 1, maxMips, "%d", ImGuiSliderFlags_AlwaysClamp);
    recompileFlag |= ImGui::SliderInt("Diffuse SPP", (int*)&settings.DiffuseSPP, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Specular Multiplier", &settings.SpecularMult, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Diffuse Multiplier", &settings.DiffuseMult, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Ambience Multiplier", &settings.AmbienceMult, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::SliderFloat("Thickness", &settings.Thickness, 0.0f, 50.0f, "%.2f");
    ImGui::SliderFloat("Roughness Mask", &settings.RoughnessMask, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("BRDF Bias", &settings.BRDFBias, 0.0f, 1.0f, "%.2f");
    ImGui::SliderInt("Spatial Times", &settings.SpatialTimes, 0, 2, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Spatial Radius", &settings.SpatialRadius, 0.0f, 5.0f, "%.2f");
    ImGui::Checkbox("Enable Temporal Filtering", &settings.EnableTemporal);
    if (settings.EnableTemporal) {
        ImGui::SliderFloat("Temporal Scale", &settings.TemporalScale, 0.0f, 8.0f, "%.2f");
        ImGui::SliderFloat("Temporal Weight", &settings.TemporalWeight, 0.0f, 0.97f, "%.2f");
    }
    ImGui::Checkbox("Enable Bilateral Filtering", &settings.EnableBilateral);
    if (settings.EnableBilateral) {
        ImGui::SliderFloat("Bilateral Scale", &settings.BilateralScale, 0.01f, 5.0f, "%.2f");
        ImGui::SliderFloat("Bilateral Color Weight", &settings.BilateralColorWeight, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Bilateral Depth Weight", &settings.BilateralDepthWeight, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Bilateral Normal Weight", &settings.BilateralNormalWeight, 0.0f, 1.0f, "%.2f");
    }
    ImGui::Checkbox("Use Dynamic Cubemaps as Fallback", &settings.UseDynamicCubemapsAsFallback);

    ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texDepth, debugRescale)
        BUFFER_VIEWER_NODE(texColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRTDiffuseColor, debugRescale)
        BUFFER_VIEWER_NODE(texHistory, debugRescale)
        BUFFER_VIEWER_NODE(texHitPDF, debugRescale)
        BUFFER_VIEWER_NODE(texSpatial, debugRescale)
        BUFFER_VIEWER_NODE(texTemporal, debugRescale)
        BUFFER_VIEWER_NODE(texBilateral, debugRescale)
        BUFFER_VIEWER_NODE(texOutput, debugRescale)

		ImGui::TreePop();
	}
}

void ScreenSpaceReflections::RestoreDefaultSettings()
{
    settings = {};
}

void ScreenSpaceReflections::LoadSettings(json& o_json)
{
    settings = o_json;
}

void ScreenSpaceReflections::SaveSettings(json& o_json)
{
    o_json = settings;
}

void ScreenSpaceReflections::SetupResources()
{
    auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
        ssrCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSRCB>());
        // spdCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SPDCB>());
    }

    logger::debug("Creating textures...");
    {
        auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
        D3D11_TEXTURE2D_DESC texDesc = {};
        mainTex.texture->GetDesc(&texDesc);
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        texDesc.MipLevels = maxMips;
        texDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

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

        texColor = eastl::make_unique<Texture2D>(texDesc);
        texColor->CreateSRV(srvDesc);
        texColor->CreateUAV(uavDesc);
        texSSRColor = eastl::make_unique<Texture2D>(texDesc);
        texSSRColor->CreateSRV(srvDesc);
        texSSRColor->CreateUAV(uavDesc);
        texSSRTDiffuseColor = eastl::make_unique<Texture2D>(texDesc);
        texSSRTDiffuseColor->CreateSRV(srvDesc);
        texSSRTDiffuseColor->CreateUAV(uavDesc);
        texHitPDF = eastl::make_unique<Texture2D>(texDesc);
        texHitPDF->CreateSRV(srvDesc);
        texHitPDF->CreateUAV(uavDesc);
        texSpatial = eastl::make_unique<Texture2D>(texDesc);
        texSpatial->CreateSRV(srvDesc);
        texSpatial->CreateUAV(uavDesc);
        texTemporal = eastl::make_unique<Texture2D>(texDesc);
        texTemporal->CreateSRV(srvDesc);
        texTemporal->CreateUAV(uavDesc);
        texBilateral = eastl::make_unique<Texture2D>(texDesc);
        texBilateral->CreateSRV(srvDesc);
        texBilateral->CreateUAV(uavDesc);
        texHistory = eastl::make_unique<Texture2D>(texDesc);
        texHistory->CreateSRV(srvDesc);
        texHistory->CreateUAV(uavDesc);
        texOutput = eastl::make_unique<Texture2D>(texDesc);
        texOutput->CreateSRV(srvDesc);
        texOutput->CreateUAV(uavDesc);

        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
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
	}

    logger::debug("Loading noise texture...");
    {
        DirectX::CreateDDSTextureFromFile(device, globals::d3d::context, L"Data\\Shaders\\ScreenSpaceReflections\\noise.dds",
            nullptr, noiseSRV.put());
    }

	CompileComputeShaders();
}

void ScreenSpaceReflections::ClearShaderCache()
{
    static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
        &raymarchSpecularCS, &prepareColorCS, &preprocessDepthCS, &spatialCS, &temporalCS, &bilateralCS, &depthDownsampleCS
    };

    for (auto shader : shaderPtrs)
        *shader = nullptr;

    CompileComputeShaders();
}

void ScreenSpaceReflections::CompileComputeShaders()
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
    definesSpecular.push_back({ "SSSR_SPECULAR", nullptr });

    std::vector<ShaderCompileInfo>
        shaderInfos = {
            { &raymarchDiffuseCS, "ssr_raymarch.hlsl", defines },
            { &raymarchSpecularCS, "ssr_raymarch.hlsl", definesSpecular },
            { &prepareColorCS, "ssr_prepare_color.hlsl", {} },
            { &preprocessDepthCS, "ssr_preprocess_depth.hlsl", {} },
            // { &spdCS, "ssr_spd.hlsl", {} },
            { &spatialCS, "ssr_spatial_filter.hlsl", {} },
            { &temporalCS, "ssr_temporal_filter.hlsl", {} },
            { &bilateralCS, "ssr_bilateral_filter.hlsl", {} },
            { &depthDownsampleCS, "ssr_depth_downsample.hlsl", {} }
        };

    for (auto& info : shaderInfos) {
        auto path = std::filesystem::path("Data\\Shaders\\ScreenSpaceReflections") / info.filename;
        if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
            info.programPtr->attach(rawPtr);
    }
}

void ScreenSpaceReflections::Prepass()
{
    if (recompileFlag) {
        recompileFlag = false;
        CompileComputeShaders();
    }

    auto renderer = globals::game::renderer;
    auto context = globals::d3d::context;
    auto state = globals::state;

    auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

    float2 size = Util::ConvertToDynamic(state->screenSize);
    float2 dispatchCount = { (size.x + 7) / 8, (size.y + 7) / 8 };

    // SPDCB spdCBData;
    // {
    //     spdCBData.numMips = maxMips;
    //     spdCBData.srcDimensions[0] = (uint)size.x;
    //     spdCBData.srcDimensions[1] = (uint)size.y;
    //     spdCBData.workGroupOffset[0] = 0;
    //     spdCBData.workGroupOffset[1] = 0;
    //     spdCBData.numWorkGroups = 256;
    //     spdCBData.slice = 0; // unused
    //     spdCBData._padding = 0; // padding
    // }
    // spdCB->Update(spdCBData);
    // auto spdBuffer = spdCB->CB();
    // context->CSSetConstantBuffers(2, 1, &spdBuffer);

    std::array<ID3D11ShaderResourceView*, 7> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { nullptr };

    auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

    std::array<ID3D11SamplerState*, 1> samplers = { linearSampler.get() };
    context->CSSetSamplers(0, 1, samplers.data());

    state->BeginPerfEvent("SSR Prepass");

    // preprocess depth
    {
        uavs.at(0) = texDepth->uav.get();
        srvs.at(4) = depth.depthSRV;

        context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
        context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
        context->CSSetShader(preprocessDepthCS.get(), nullptr, 0);

        context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

        // context->GenerateMips(texDepth->srv.get());

        resetViews();
    }

    // spd
    // state->BeginPerfEvent("SPD");

    // std::array<ID3D11UnorderedAccessView*, maxMips - 1> uavsSPD = { nullptr };
    // for (int i = 0; i < maxMips - 1; ++i) {
    //     uavsSPD.at(i) = depthUAVs[i + 1].get();
    // }
    // srvs.at(5) = depthSRVs[0].get();

    // context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
    // context->CSSetUnorderedAccessViews(0, (uint)uavsSPD.size(), uavsSPD.data(), nullptr);
    // context->CSSetShader(spdCS.get(), nullptr, 0);

    // context->Dispatch((uint)dispatchCount.x >> 2, (uint)dispatchCount.y >> 2, 1);

    // srvs.fill(nullptr);
    // uavsSPD.fill(nullptr);
    // context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
    // context->CSSetUnorderedAccessViews(0, (uint)uavsSPD.size(), uavsSPD.data(), nullptr);

    // state->EndPerfEvent();
    // resetViews();

    // downsample depth
    {
        state->BeginPerfEvent("Downsample Depth - HiZ Buffer");
        for (int i = 0; i < maxMips - 1; ++i) {
            uavs.at(0) = depthUAVs[i + 1].get();
            srvs.at(0) = depthSRVs[i].get();

            context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
            context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
            context->CSSetShader(depthDownsampleCS.get(), nullptr, 0);

            context->Dispatch((uint)dispatchCount.x >> i, (uint)dispatchCount.y >> i, 1);
            resetViews();
        }
        state->EndPerfEvent();
    }

    state->EndPerfEvent();

    auto view = texDepth->srv.get();
    context->PSSetShaderResources(99, 1, &view);
}

void ScreenSpaceReflections::DrawSSR()
{
    if (!settings.Enabled)
        return;

    auto renderer = globals::game::renderer;
    auto context = globals::d3d::context;
    auto state = globals::state;

    state->BeginPerfEvent("SSR Compute");

    auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
    auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
    auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
    auto normal = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
    auto motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

    auto& dynamicCubemaps = globals::features::dynamicCubemaps;
    auto& ssgi = globals::features::screenSpaceGI;
    auto& skylighting = globals::features::skylighting;

    float2 size = Util::ConvertToDynamic(state->screenSize);
    float2 dispatchCount = { (size.x + 7) / 8, (size.y + 7) / 8 };
    
    SSRCB ssrCBData;
    {
        ssrCBData.MaxSteps = settings.MaxSteps;
        ssrCBData.MaxMips = settings.MaxMips;
        ssrCBData.Thickness = settings.Thickness;
        ssrCBData.SpatialRadius = settings.SpatialRadius;
        ssrCBData.RoughnessMask = settings.RoughnessMask;
        ssrCBData.TemporalScale = settings.TemporalScale;
        ssrCBData.TemporalWeight = settings.TemporalWeight;
        ssrCBData.BilateralScale = settings.BilateralScale;
        ssrCBData.ColorWeight = settings.BilateralColorWeight;
        ssrCBData.DepthWeight = settings.BilateralDepthWeight;
        ssrCBData.NormalWeight = settings.BilateralNormalWeight;
        ssrCBData.BRDFBias = settings.BRDFBias;
        ssrCBData.UseDynamicCubemapsAsFallback = (uint)settings.UseDynamicCubemapsAsFallback && dynamicCubemaps.loaded;
    }
    ssrCB->Update(ssrCBData);
    auto buffer = ssrCB->CB();
    context->CSSetConstantBuffers(1, 1, &buffer);

    std::array<ID3D11ShaderResourceView*, 12> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { nullptr };

    auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

    std::array<ID3D11SamplerState*, 1> samplers = { linearSampler.get() };

    context->CSSetSamplers(0, 1, samplers.data());

    // prepare color
    srvs.at(0) = main.SRV;
    srvs.at(1) = specular.SRV;
    srvs.at(2) = normal.SRV;
    uavs.at(0) = texColor->uav.get();

    context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
    context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
    context->CSSetShader(prepareColorCS.get(), nullptr, 0);

    context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

    resetViews();

    const auto envTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr;
	const auto envReflectionsTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr;

    auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec] = ssgi.GetOutputTextures();

    bool inInterior = true;

    if (auto player = RE::PlayerCharacter::GetSingleton()) {
        if (auto parentCell = player->GetParentCell()) {
            inInterior = parentCell->IsInteriorCell();
        }
    }

    // raymarch
    state->BeginPerfEvent("Raymarch");
    
    uavs.at(0) = texSSRColor->uav.get();
    uavs.at(1) = texHitPDF->uav.get();

    srvs.at(0) = main.SRV;
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

    // spartial filter
    for(int i = 0; i < settings.SpatialTimes; ++i) {
        state->BeginPerfEvent("Spatial Filter");
        if ((i & 1) == 0) {
            uavs.at(0) = texSpatial->uav.get();

            srvs.at(0) = texSSRColor->srv.get();
            srvs.at(1) = texHitPDF->srv.get();
            srvs.at(2) = normal.SRV;
            srvs.at(5) = texDepth->srv.get();
            srvs.at(6) = noiseSRV.get();
        } else {
            uavs.at(0) = texSSRColor->uav.get();

            srvs.at(0) = texSpatial->srv.get();
            srvs.at(1) = texHitPDF->srv.get();
            srvs.at(2) = normal.SRV;
            srvs.at(5) = texDepth->srv.get();
            srvs.at(6) = noiseSRV.get();
        }
        context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
        context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
        context->CSSetShader(spatialCS.get(), nullptr, 0);
        context->CSSetConstantBuffers(1, 1, &buffer);

        context->Dispatch((uint)dispatchCount.x >> 1, (uint)dispatchCount.y >> 1, 1);
        if ((i & 1) == 0) {
            context->CopyResource(texSSRColor->resource.get(), texSpatial->resource.get());
        } else {
            context->CopyResource(texSpatial->resource.get(), texSSRColor->resource.get());
        }
        resetViews();
        state->EndPerfEvent();
    }

    // temporal filter
    if (settings.EnableTemporal) {
        state->BeginPerfEvent("Temporal Filter");
        uavs.at(0) = texTemporal->uav.get();
        srvs.at(0) = texSSRColor->srv.get();
        srvs.at(1) = motion.SRV;
        srvs.at(4) = texHistory->srv.get();
        srvs.at(5) = texDepth->srv.get();
        srvs.at(6) = texHitPDF->srv.get();

        context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
        context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
        context->CSSetShader(temporalCS.get(), nullptr, 0);
        context->CSSetConstantBuffers(1, 1, &buffer);

        context->Dispatch((uint)dispatchCount.x >> 1, (uint)dispatchCount.y >> 1, 1);
        context->CopyResource(texSSRColor->resource.get(), texTemporal->resource.get());
        
        resetViews();
        state->EndPerfEvent();
    }

    // bilateral filter
    if (settings.EnableBilateral) {
        state->BeginPerfEvent("Bilateral Filter");
        uavs.at(0) = texBilateral->uav.get();
        srvs.at(0) = texSSRColor->srv.get();
        srvs.at(2) = normal.SRV;
        srvs.at(4) = depth.depthSRV;

        context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
        context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
        context->CSSetShader(bilateralCS.get(), nullptr, 0);
        context->CSSetConstantBuffers(1, 1, &buffer);

        context->Dispatch((uint)dispatchCount.x >> 1, (uint)dispatchCount.y >> 1, 1);
        context->CopyResource(texSSRColor->resource.get(), texBilateral->resource.get());
        resetViews();
        state->EndPerfEvent();
    }
    resetViews();

    // output
    context->CopyResource(texOutput->resource.get(), texSSRColor->resource.get());
    context->CopyResource(texHistory->resource.get(), texSSRColor->resource.get());

    context->CSSetShader(nullptr, nullptr, 0);

    state->EndPerfEvent();
}

void ScreenSpaceReflections::DrawSSRTDiffuse()
{
    if (!(settings.Enabled && settings.EnableDiffuse))
        return;

    auto renderer = globals::game::renderer;
    auto context = globals::d3d::context;
    auto state = globals::state;

    state->BeginPerfEvent("SSRT Diffuse Compute");

    auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
    auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
    auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
    auto normal = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
    auto motion = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

    auto& dynamicCubemaps = globals::features::dynamicCubemaps;
    auto& ssgi = globals::features::screenSpaceGI;
    auto& skylighting = globals::features::skylighting;

    float2 size = Util::ConvertToDynamic(state->screenSize);
    float2 dispatchCount = { (size.x + 7) / 8, (size.y + 7) / 8 };
    
    SSRCB ssrCBData;
    {
        ssrCBData.MaxSteps = settings.MaxSteps;
        ssrCBData.MaxMips = settings.MaxMips;
        ssrCBData.Thickness = settings.Thickness;
        ssrCBData.SpatialRadius = settings.SpatialRadius;
        ssrCBData.RoughnessMask = settings.RoughnessMask;
        ssrCBData.TemporalScale = settings.TemporalScale;
        ssrCBData.TemporalWeight = settings.TemporalWeight;
        ssrCBData.BilateralScale = settings.BilateralScale;
        ssrCBData.ColorWeight = settings.BilateralColorWeight;
        ssrCBData.DepthWeight = settings.BilateralDepthWeight;
        ssrCBData.NormalWeight = settings.BilateralNormalWeight;
        ssrCBData.BRDFBias = settings.BRDFBias;
        ssrCBData.UseDynamicCubemapsAsFallback = (uint)settings.UseDynamicCubemapsAsFallback && dynamicCubemaps.loaded;
    }
    ssrCB->Update(ssrCBData);
    auto buffer = ssrCB->CB();
    context->CSSetConstantBuffers(1, 1, &buffer);

    std::array<ID3D11ShaderResourceView*, 12> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { nullptr };

    auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

    std::array<ID3D11SamplerState*, 1> samplers = { linearSampler.get() };

    context->CSSetSamplers(0, 1, samplers.data());

    const auto envTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr;
	const auto envReflectionsTexture = dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr;

    auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec] = ssgi.GetOutputTextures();

    bool inInterior = true;

    if (auto player = RE::PlayerCharacter::GetSingleton()) {
        if (auto parentCell = player->GetParentCell()) {
            inInterior = parentCell->IsInteriorCell();
        }
    }

    uavs.at(0) = texSSRTDiffuseColor->uav.get();
    uavs.at(1) = texHitPDF->uav.get();

    srvs.at(0) = main.SRV;
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

    context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
    context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
    context->CSSetShader(raymarchDiffuseCS.get(), nullptr, 0);
    context->CSSetConstantBuffers(1, 1, &buffer);

    context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

    state->EndPerfEvent();

    resetViews();

    context->CSSetShader(nullptr, nullptr, 0);
}

ScreenSpaceReflections::SharedData ScreenSpaceReflections::GetCommonBufferData()
{
    SharedData data;
    data.Enabled = settings.Enabled;
    data.SpecularMult = settings.SpecularMult;
    data.DiffuseMult = settings.EnableDiffuse ? settings.DiffuseMult : 0.0f;
    data.AmbienceMult = settings.AmbienceMult;
    return data;
}