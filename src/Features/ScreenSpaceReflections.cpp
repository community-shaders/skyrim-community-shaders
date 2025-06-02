#include "ScreenSpaceReflections.h"

#include "Deferred.h"
#include "Menu.h"
#include "State.h"
#include "ShaderCache.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ScreenSpaceReflections::Settings,
    Enabled,
    MaxSteps,
    NumRays,
    Glossy,
    RoughnessMask
)

void ScreenSpaceReflections::DrawSettings()
{
    ImGui::Checkbox("Enabled", &settings.Enabled);
    ImGui::Checkbox("Glossy", &settings.Glossy);
    ImGui::SliderInt("Max Steps", reinterpret_cast<int*>(&settings.MaxSteps), 1, 16);
    ImGui::SliderInt("Num Rays", reinterpret_cast<int*>(&settings.NumRays), 1, 16);
    ImGui::SliderFloat("Roughness Mask", &settings.RoughnessMask, 0.0f, 1.0f, "%.2f");

    ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texDepth, debugRescale)
        BUFFER_VIEWER_NODE(texColor, debugRescale)
        BUFFER_VIEWER_NODE(texSSRColor, debugRescale)

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
    }

    logger::debug("Creating textures...");
    {
        auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
        D3D11_TEXTURE2D_DESC texDesc = {};
        mainTex.texture->GetDesc(&texDesc);
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        texDesc.MipLevels = 5;
        texDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
        texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;

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

        texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        texDepth = eastl::make_unique<Texture2D>(texDesc);
        texDepth->CreateSRV(srvDesc);
        texDepth->CreateUAV(uavDesc);
        texHitDistance = eastl::make_unique<Texture2D>(texDesc);
        texHitDistance->CreateSRV(srvDesc);
        texHitDistance->CreateUAV(uavDesc);
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

	CompileComputeShaders();
}

void ScreenSpaceReflections::ClearShaderCache()
{
    static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
        &raymarchCS, &prepareColorCS, &preprocessDepthCS
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

    std::vector<ShaderCompileInfo>
        shaderInfos = {
            { &raymarchCS, "ssr_raymarch.hlsl", {} },
            { &prepareColorCS, "ssr_prepare_color.hlsl", {} },
            { &preprocessDepthCS, "ssr_preprocess_depth.hlsl", {} }
        };

    for (auto& info : shaderInfos) {
        auto path = std::filesystem::path("Data\\Shaders\\ScreenSpaceReflections") / info.filename;
        if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
            info.programPtr->attach(rawPtr);
    }
}

void ScreenSpaceReflections::DrawSSR()
{
    if (!settings.Enabled)
        return;

    auto renderer = globals::game::renderer;
    auto context = globals::d3d::context;
    auto state = globals::state;

    state->BeginPerfEvent("SSR");

    auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
    auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
    auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
    auto normal = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];

    float2 size = Util::ConvertToDynamic(state->screenSize);
    float2 dispatchCount = { (size.x + 7) / 8, (size.y + 7) / 8 };
    ssrCBData = {
        settings.MaxSteps,
        settings.NumRays,
        settings.Glossy ? 1u : 0u,
        0u,
        settings.RoughnessMask,
        { 0.0f, 0.0f, 0.0f } // padding
    };
    ssrCB->Update(&ssrCBData);
    ID3D11Buffer* buffer[1] = { ssrCB->CB() };
    context->CSSetConstantBuffers(1, 1, buffer);

    ID3D11ShaderResourceView* srvs[5] = {
        main.SRV,
        specular.SRV,
        normal.SRV,
        nullptr,
        nullptr
    };

    ID3D11UnorderedAccessView* uavs[2] = {
        texColor->uav.get(),
        nullptr
    };

    std::array<ID3D11SamplerState*, 1> samplers = { linearSampler.get() };

    context->CSSetSamplers(0, 1, samplers.data());

    context->CSSetShaderResources(0, 3, srvs);
    context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    context->CSSetShader(prepareColorCS.get(), nullptr, 0);

    context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

    uavs[0] = texDepth->uav.get();
    srvs[4] = depth.depthSRV;

    context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    context->CSSetShaderResources(0, 5, srvs);
    context->CSSetShader(preprocessDepthCS.get(), nullptr, 0);

    context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.y, 1);

    uavs[0] = texSSRColor->uav.get();
    uavs[1] = texHitDistance->uav.get();
    srvs[3] = texColor->srv.get();
    srvs[4] = texDepth->srv.get();

    context->CSSetShaderResources(0, 5, srvs);
    context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    context->CSSetShader(raymarchCS.get(), nullptr, 0);

    context->Dispatch((uint)dispatchCount.x, (uint)dispatchCount.x, 1);

    context->CSSetShader(nullptr, nullptr, 0);
    srvs[0] = nullptr;
    srvs[1] = nullptr;
    srvs[2] = nullptr;
    srvs[3] = nullptr;
    srvs[4] = nullptr;
    uavs[0] = nullptr;
    uavs[1] = nullptr;
    
    context->CSSetShaderResources(0, 5, srvs);
    context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

    state->EndPerfEvent();
}