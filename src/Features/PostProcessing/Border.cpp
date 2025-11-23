#include "Border.h"

#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Border::Settings,
    BorderColor,
    DepthThreshold,
    Scale)

void Border::DrawSettings()
{
    ImGui::ColorEdit3("Border Color", reinterpret_cast<float*>(&settings.BorderColor));
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("The color of the border.");

    ImGui::SliderFloat("Depth Threshold", &settings.DepthThreshold, 0.f, 1.f, "%.2f");
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("The depth threshold for the border effect.");

    ImGui::SliderFloat4("Scale (Top, Down, Left, Right)", reinterpret_cast<float*>(&settings.Scale), 0.f, 0.5f, "%.2f");
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("The scale of the border on each side of the screen.");
}

void Border::RestoreDefaultSettings()
{
    settings = {};
}

void Border::LoadSettings(json& o_json)
{
    settings = o_json;
}

void Border::SaveSettings(json& o_json)
{
    o_json = settings;
}

void Border::SetupResources()
{
	auto renderer = globals::game::renderer;

	logger::debug("Creating buffers...");
	{
		borderCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<BorderCB>());
	}

    logger::debug("Creating 2D textures...");
	{
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

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

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateUAV(uavDesc);
	}

	CompileComputeShaders();
}

void Border::ClearShaderCache()
{
    const auto shaderPtrs = std::array{
        &borderCS
    };

    for (auto shader : shaderPtrs)
        if ((*shader)) {
            (*shader)->Release();
            shader->detach();
        }

    CompileComputeShaders();
}

void Border::CompileComputeShaders()
{
    struct ShaderCompileInfo
    {
        winrt::com_ptr<ID3D11ComputeShader>* programPtr;
        std::string_view filename;
        std::vector<std::pair<const char*, const char*>> defines = {};
        std::string entry = "main";
    };

    std::vector<ShaderCompileInfo>
        shaderInfos = {
            { &borderCS, "border.cs.hlsl" },
        };

    for (auto& info : shaderInfos) {
        auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\Border") / info.filename;
        if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
            info.programPtr->attach(rawPtr);
    }
}

void Border::Draw(TextureInfo& inout_tex)
{
    auto renderer = globals::game::renderer;
    auto context = globals::d3d::context;

    float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	res = Util::ConvertToDynamic(res);

    BorderCB data = {
		.BorderColor = float4(settings.BorderColor.x, settings.BorderColor.y, settings.BorderColor.z, settings.DepthThreshold),
        .Scale = settings.Scale
	};
	borderCB->Update(data);

    auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
    ID3D11ShaderResourceView* srvs[2] = { inout_tex.srv, depth.depthSRV };
    context->CSSetShaderResources(0, 2, srvs);
    ID3D11UnorderedAccessView* uav = texOutput->uav.get();
    context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    ID3D11Buffer* cb = borderCB->CB();
    context->CSSetConstantBuffers(1, 1, &cb);
    context->CSSetShader(borderCS.get(), nullptr, 0);   

    context->Dispatch(((uint)res.x + 7) >> 3, ((uint)res.y + 7) >> 3, 1);

    srvs[0] = nullptr;
    srvs[1] = nullptr;
	uav = nullptr;
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

    inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
}