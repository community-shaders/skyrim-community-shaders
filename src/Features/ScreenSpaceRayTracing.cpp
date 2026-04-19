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
    CubemapNormalization
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

    ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texDepth, debugRescale)
        BUFFER_VIEWER_NODE(texColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRTDiffuseColor, debugRescale)
        BUFFER_VIEWER_NODE(texOutput, debugRescale)

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

        // Quarter-resolution textures: color input, output, and specular/diffuse results
        texDesc.Width >>= 2;
        texDesc.Height >>= 2;
        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        // texColor: single-mip for color input
        texDesc.MipLevels = 1;
        texDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        srvDesc.Texture2D.MipLevels = 1;
        texColor = eastl::make_unique<Texture2D>(texDesc);
        texColor->CreateSRV(srvDesc);
        texColor->CreateUAV(uavDesc);

        // Restore single-mip settings for remaining textures
        texDesc.MipLevels = 1;
        texDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.Texture2D.MipLevels = 1;

        texSSRColor = eastl::make_unique<Texture2D>(texDesc);
        texSSRColor->CreateSRV(srvDesc);
        texSSRColor->CreateUAV(uavDesc);
        texSSRTDiffuseColor = eastl::make_unique<Texture2D>(texDesc);
        texSSRTDiffuseColor->CreateSRV(srvDesc);
        texSSRTDiffuseColor->CreateUAV(uavDesc);
        texOutput = eastl::make_unique<Texture2D>(texDesc);
        texOutput->CreateSRV(srvDesc);
        texOutput->CreateUAV(uavDesc);

        // Quarter-resolution depth pyramid
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
        &raymarchSpecularCS, &raymarchDiffuseCS, &prefilterRadianceCS, &prefilterDepthCS, &depthDownsampleCS, &diffuseCompositeCS,
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
            { &diffuseCompositeCS, "ssrt_diffuse_composite.hlsl", {} },
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
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };

    auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

    std::array<ID3D11SamplerState*, 2> samplers = { pointSampler.get(), linearSampler.get() };
    context->CSSetSamplers(0, 2, samplers.data());

    state->BeginPerfEvent("SSRT Prepass");

    // prefilter depth: full-res NDC depth → quarter-res mip 0 (Hi-Z min of 4×4 block)
    {
        uavs.at(0) = depthUAVs[0].get();
        srvs.at(0) = depth.depthSRV;

        context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
        context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
        context->CSSetShader(prefilterDepthCS.get(), nullptr, 0);
        context->Dispatch(((uint)dynres.x + 15) >> 4, ((uint)dynres.y + 15) >> 4, 1);

        resetViews();
    }

    // downsample depth: mips 1-8 chained from quarter-res mip 0
    {
        state->BeginPerfEvent("Downsample Depth - HiZ Buffer");
        for (int i = 0; i < maxMips - 1; ++i) {
            uavs.at(0) = depthUAVs[i + 1].get();
            srvs.at(0) = depthSRVs[i].get();

            context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
            context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
            context->CSSetShader(depthDownsampleCS.get(), nullptr, 0);

            uint32_t width = (uint32_t)dynres.x >> (i + 3);
            uint32_t height = (uint32_t)dynres.y >> (i + 3);
            context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
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

    // prefilter radiance: builds color input (quarter-res)
    {
        ID3D11UnorderedAccessView* colorUav = texColor->uav.get();

        srvs.at(0) = main.SRV;
        srvs.at(1) = specular.SRV;

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

    // raymarch
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

    context->CopyResource(texOutput->resource.get(), texSSRColor->resource.get());

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

    auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec] = ssgi.GetOutputTextures();

    bool inInterior = true;

    if (auto player = RE::PlayerCharacter::GetSingleton()) {
        if (auto parentCell = player->GetParentCell()) {
            inInterior = parentCell->IsInteriorCell();
        }
    }

    uavs.at(0) = texSSRTDiffuseColor->uav.get();

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

    state->EndPerfEvent();

    context->CSSetShader(nullptr, nullptr, 0);
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