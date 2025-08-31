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
    NormalBias,
    BRDFBias,
    UseDynamicCubemapsAsFallback,
    DiffuseSPP,
    EnableDiffuse,
    SpecularMult,
    DiffuseMult,
    AmbientMult,
    HistoryWeight,
    OcclusionStrength,
    ReuseRayDiffuse,
    ReuseRaySpecular,
    EnableSharc
)

void ScreenSpaceReflections::DrawSettings()
{
    ImGui::Checkbox("Enabled", &settings.Enabled);
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
        ImGui::Text("Set this to 0 and use Dynamic Cubemaps as fallback if you want full dynamic ambient lighting.");
    ImGui::SliderFloat("Last Frame History Weight", &settings.HistoryWeight, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Reuse Ray For Diffuse", &settings.ReuseRayDiffuse);
    ImGui::SameLine();
    ImGui::Checkbox("Reuse Ray For Specular", &settings.ReuseRaySpecular);
    ImGui::Text("Ray Reusing may help with irradiance accumulation but might introduce artifacts.");
    ImGui::Separator();
    ImGui::SliderFloat("Thickness", &settings.Thickness, 0.0f, 50.0f, "%.2f");
    ImGui::SliderFloat("Normal Bias", &settings.NormalBias, 0.0f, 1.0f, "%.2f");
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("To avoid false hits from nearby geometry, increase this value to push the ray origin along the normal.");
    ImGui::SliderFloat("BRDF Bias", &settings.BRDFBias, 0.0f, 1.0f, "%.2f");
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("Specular only. Higher BRDF bias reduces noise but makes reflections more glossy.");
    ImGui::Checkbox("Use Dynamic Cubemaps as Fallback", &settings.UseDynamicCubemapsAsFallback);
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("When ray marching misses, use dynamic cubemaps for reflections. This with diffuse would provide natural ambient lighting.");
    ImGui::Checkbox("Enable SHARC", &settings.EnableSharc);
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("(Experimental) Enables Spatially Hashed Radiance Cache (SHARC) to improve diffuse quality. This requires more memory and might impact performance.");

    ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texDepth, debugRescale)
        BUFFER_VIEWER_NODE(texColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRTDiffuseColor, debugRescale)
        BUFFER_VIEWER_NODE(texHistory, debugRescale)
        BUFFER_VIEWER_NODE(texHistoryDiffuse, debugRescale)
        BUFFER_VIEWER_NODE(texHitPDF, debugRescale)
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
        texHistory = eastl::make_unique<Texture2D>(texDesc);
        texHistory->CreateSRV(srvDesc);
        texHistory->CreateUAV(uavDesc);
        texHistoryDiffuse = eastl::make_unique<Texture2D>(texDesc);
        texHistoryDiffuse->CreateSRV(srvDesc);
        texHistoryDiffuse->CreateUAV(uavDesc);
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

    logger::debug("Creating buffers...");
	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

        std::uint32_t numEntries = sharcNumEntries;

        // Hash entries buffer - structured buffer with 64-bits entries to store the hashes
        // Voxel data buffer - structured buffer with 128-bit entries which stores accumulated radiance and sample count. Two instances are used to store current and previous frame data

        sbDesc.StructureByteStride = sizeof(std::uint64_t);
        sbDesc.ByteWidth = sbDesc.StructureByteStride * numEntries;
        sharcHashEntries = eastl::make_unique<Buffer>(sbDesc);
        srvDesc.Buffer.NumElements = numEntries;
        uavDesc.Buffer.NumElements = numEntries;
        sharcHashEntries->CreateSRV(srvDesc);
        sharcHashEntries->CreateUAV(uavDesc);

        sbDesc.StructureByteStride = sizeof(std::uint32_t);
        sbDesc.ByteWidth = sbDesc.StructureByteStride * numEntries;
        sharcHashCopyOffsets = eastl::make_unique<Buffer>(sbDesc);
        srvDesc.Buffer.NumElements = numEntries;
        uavDesc.Buffer.NumElements = numEntries;
        sharcHashCopyOffsets->CreateSRV(srvDesc);
        sharcHashCopyOffsets->CreateUAV(uavDesc);

        sbDesc.StructureByteStride = 4 * sizeof(uint32_t);
        sbDesc.ByteWidth = numEntries * sizeof(float4);
        sharcVoxelData = eastl::make_unique<Buffer>(sbDesc);
        srvDesc.Buffer.NumElements = numEntries;
        uavDesc.Buffer.NumElements = numEntries;
        sharcVoxelData->CreateSRV(srvDesc);
        sharcVoxelData->CreateUAV(uavDesc);
        sharcVoxelDataPrev = eastl::make_unique<Buffer>(sbDesc);
        sharcVoxelDataPrev->CreateSRV(srvDesc);
        sharcVoxelDataPrev->CreateUAV(uavDesc);
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
        &raymarchSpecularCS, &raymarchDiffuseCS, &raymarchDiffuseSharcCS, &prepareColorCS, &preprocessDepthCS, &depthDownsampleCS, &sharcResolveCS
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

    auto definesSharcUpdate = defines;
    definesSharcUpdate.push_back({ "SHARC_UPDATE", "1" });

    auto definesSharc = defines;
    definesSharc.push_back({ "SHARC_RENDER", "1" });

    auto definesSpecular = defines;
    definesSpecular.push_back({ "SSSR_SPECULAR", nullptr });

    std::vector<ShaderCompileInfo>
        shaderInfos = {
            { &raymarchDiffuseCS, "ssr_raymarch.hlsl", defines },
            { &raymarchSpecularCS, "ssr_raymarch.hlsl", definesSpecular },
            { &raymarchDiffuseSharcCS, "ssr_raymarch.hlsl", definesSharc },
            { &sharcUpdateRaymarchCS, "ssr_raymarch.hlsl", definesSharcUpdate },
            { &prepareColorCS, "ssr_prepare_color.hlsl", {} },
            { &preprocessDepthCS, "ssr_preprocess_depth.hlsl", {} },
            { &depthDownsampleCS, "ssr_depth_downsample.hlsl", {} },
            { &sharcResolveCS, "sharc_resolve.hlsl", {} }
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

    std::array<ID3D11ShaderResourceView*, 5> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };

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
        ssrCBData.NormalBias = settings.NormalBias;
        ssrCBData.BRDFBias = settings.BRDFBias;
        ssrCBData.UseDynamicCubemapsAsFallback = (uint)settings.UseDynamicCubemapsAsFallback && dynamicCubemaps.loaded;
        ssrCBData.HistoryWeight = settings.HistoryWeight;
        ssrCBData.OcclusionStrength = settings.OcclusionStrength;
        ssrCBData.ReuseRay = settings.ReuseRaySpecular ? 1 : 0;
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

    srvs.at(0) = texHistory->srv.get();
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
        ssrCBData.NormalBias = settings.NormalBias;
        ssrCBData.BRDFBias = settings.BRDFBias;
        ssrCBData.UseDynamicCubemapsAsFallback = (uint)settings.UseDynamicCubemapsAsFallback && dynamicCubemaps.loaded;
        ssrCBData.HistoryWeight = settings.HistoryWeight;
        ssrCBData.OcclusionStrength = settings.OcclusionStrength;
        ssrCBData.ReuseRay = settings.ReuseRayDiffuse ? 1 : 0;
    }
    ssrCB->Update(ssrCBData);
    auto buffer = ssrCB->CB();
    context->CSSetConstantBuffers(1, 1, &buffer);

    std::array<ID3D11ShaderResourceView*, 12> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 6> uavs = { nullptr };

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
    uavs.at(2) = sharcHashEntries->uav.get();
    uavs.at(3) = sharcHashCopyOffsets->uav.get();
    uavs.at(4) = sharcVoxelData->uav.get();
    uavs.at(5) = sharcVoxelDataPrev->uav.get();

    srvs.at(0) = texHistoryDiffuse->srv.get();
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
    context->CSSetConstantBuffers(1, 1, &buffer);

    if (settings.EnableSharc) {
        state->BeginPerfEvent("SHARC Update");
        context->CSSetShader(sharcUpdateRaymarchCS.get(), nullptr, 0);

        context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);
        state->EndPerfEvent();

        state->BeginPerfEvent("SHARC Resolve");
        context->CSSetShader(sharcResolveCS.get(), nullptr, 0);

        context->Dispatch(sharcNumEntries / 256u, 1, 1);
        state->EndPerfEvent();
    }

    context->CSSetShader(settings.EnableSharc ? raymarchDiffuseSharcCS.get() : raymarchDiffuseCS.get(), nullptr, 0);

    context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);
    resetViews();

    if (settings.EnableSharc) {
        std::swap(sharcVoxelData, sharcVoxelDataPrev);
    }

    context->CopyResource(texHistoryDiffuse->resource.get(), texSSRTDiffuseColor->resource.get());

    state->EndPerfEvent();

    context->CSSetShader(nullptr, nullptr, 0);
}

ScreenSpaceReflections::SharedData ScreenSpaceReflections::GetCommonBufferData()
{
    SharedData data;
    data.Enabled = settings.Enabled;
    data.SpecularMult = settings.SpecularMult;
    data.DiffuseMult = settings.EnableDiffuse ? settings.DiffuseMult : 0.0f;
    data.AmbientMult = settings.AmbientMult;
    return data;
}