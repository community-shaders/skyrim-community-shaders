#include "Vignette.h"

#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Vignette::Settings,
	FocalLength,
	Power)

void Vignette::DrawSettings()
{
	ImGui::SliderFloat("Focal Length", &settings.FocalLength, 0.1f, 2.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("The focal length of the lens, relative to image width.");

	ImGui::SliderFloat("Anamorphic Squeeze", &settings.Anamorphism, 0.1f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("How flat the vignette looks, simulating anamorphic lens.");

	ImGui::SliderFloat("Power", &settings.Power, 0.f, 4.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"The natural vignetting of a camera follows the fourth law, where the vignette is proportional to the fourth power of the incident angle. "
			"The actual power in a camera is usually lower due to designed compensation.");
}

void Vignette::RestoreDefaultSettings()
{
	settings = {};
}

void Vignette::LoadSettings(json& o_json)
{
	settings = o_json;
}

void Vignette::SaveSettings(json& o_json)
{
	o_json = settings;
}

void Vignette::SetupResources()
{
	auto renderer = globals::game::renderer;

	logger::debug("Creating buffers...");
	{
		vignetteCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<VignetteCB>());
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

void Vignette::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&vignetteCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void Vignette::CompileComputeShaders()
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
			{ &vignetteCS, "vignette.cs.hlsl" },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\Vignette") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
}

void Vignette::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;

	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	res = Util::ConvertToDynamic(res);
	VignetteCB data = {
		.settings = settings,
		.AspectRatio = res.y / res.x / settings.Anamorphism,
		.RcpDynRes = float2(1.f) / res
	};
	vignetteCB->Update(data);

	ID3D11ShaderResourceView* srv = inout_tex.srv;
	ID3D11UnorderedAccessView* uav = texOutput->uav.get();
	ID3D11Buffer* cb = vignetteCB->CB();

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetShader(vignetteCS.get(), nullptr, 0);

	context->Dispatch(((uint)res.x + 7) >> 3, ((uint)res.y + 7) >> 3, 1);

	// clean up
	srv = nullptr;
	uav = nullptr;
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
}
