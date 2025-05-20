#include "VanillaImagespace.h"

#include "Menu.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VanillaImagespace::Settings,
	blendFactor,
	InteriorMultiplier,
	ExteriorMultiplier,
	InteriorOverride,
	ExteriorOverride,
	enableInExMultiplier,
	enableInExOverride)

void VanillaImagespace::DrawSettings()
{
	ImGui::SliderFloat3("Blend Factor", &settings.blendFactor.x, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Blend factor for the vanilla imagespace (saturation, brightness, contrast).");
	}

	ImGui::Checkbox("Enable Interior/Exterior Multiplier", &settings.enableInExMultiplier);

	if (settings.enableInExMultiplier) {
		ImGui::SliderFloat3("Exterior Multiplier", &settings.ExteriorMultiplier.x, 0.0f, 2.0f, "%.3f");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Multiplier for the exterior imagespace (saturation, brightness, contrast).");
		}

		ImGui::SliderFloat3("Interior Multiplier", &settings.InteriorMultiplier.x, 0.0f, 2.0f, "%.3f");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Multiplier for the interior imagespace (saturation, brightness, contrast).");
		}
	}

	ImGui::Checkbox("Enable Interior/Exterior Override", &settings.enableInExOverride);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Override the vanilla In/Exterior imagespace values. Would ignore the above multiplier settings.");
	}

	if (settings.enableInExOverride) {
		ImGui::SliderFloat3("Exterior Override", &settings.ExteriorOverride.x, 0.0f, 8.0f, "%.3f");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Override for the exterior imagespace (saturation, brightness, contrast).");
		}

		ImGui::SliderFloat3("Interior Override", &settings.InteriorOverride.x, 0.0f, 8.0f, "%.3f");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Override for the interior imagespace (saturation, brightness, contrast).");
		}
	}

	ImGui::Text("Original ImageSpace Values:");
	ImGui::Text("Saturation: %.3f", vanillaImagespaceData.cinematic.x);
	ImGui::Text("Brightness: %.3f", vanillaImagespaceData.cinematic.y);
	ImGui::Text("Contrast: %.3f", vanillaImagespaceData.cinematic.z);

	ImGui::Text("Current Location: %s", isInInterior ? "Interior" : "Exterior");
	ImGui::Text("Actual Values:");
	ImGui::Text("Saturation: %.3f", actualValues.x);
	ImGui::Text("Brightness: %.3f", actualValues.y);
	ImGui::Text("Contrast: %.3f", actualValues.z);
}

void VanillaImagespace::RestoreDefaultSettings()
{
	settings = {};
}

void VanillaImagespace::LoadSettings(json& o_json)
{
	settings = o_json;
}

void VanillaImagespace::SaveSettings(json& o_json)
{
	o_json = settings;
}

void VanillaImagespace::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		vanillaImagespaceCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<VanillaImagespaceCB>());
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

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
			.AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
			.AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};

		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, colorSampler.put()));
	}

	logger::debug("Creating compute shaders...");
	{
		CompileComputeShaders();
	}
}

void VanillaImagespace::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&vanillaImagespaceCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void VanillaImagespace::CompileComputeShaders()
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
			{ &vanillaImagespaceCS, "vanillais.hlsl" },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\VanillaImagespace") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}

	if (!vanillaImagespaceCS) {
		logger::error("Failed to compile vanilla imagespace compute shader!");
		return;
	}
}

void VanillaImagespace::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	float3 cinematic;
	auto ImageSpace = RE::ImageSpaceManager::GetSingleton();
	RE::ImageSpaceBaseData::Cinematic cinematicdata;
	if (globals::game::isVR) {
		const auto& iSRuntimeData = ImageSpace->GetVRRuntimeData();
		if (const auto& overrideBaseData = iSRuntimeData.overrideBaseData) {
			cinematicdata = overrideBaseData->cinematic;
		} else {
			cinematicdata = iSRuntimeData.currentBaseData->cinematic;
		}
	} else {
		const auto& iSRuntimeData = ImageSpace->GetRuntimeData();
		if (const auto& overrideBaseData = iSRuntimeData.overrideBaseData) {
			cinematicdata = overrideBaseData->cinematic;
		} else {
			cinematicdata = iSRuntimeData.currentBaseData->cinematic;
		}
	}

	if (auto sky = globals::game::sky)
		isInInterior = sky->mode.get() != RE::Sky::Mode::kFull;
	else
		isInInterior = true;
	cinematic.x = cinematicdata.saturation;
	cinematic.y = cinematicdata.brightness;
	cinematic.z = cinematicdata.contrast;

	VanillaImagespaceCB data = {
		.cinematic = cinematic,
		.width = res.x,
		.height = res.y
	};
	vanillaImagespaceData = data;

	actualValues = (float3(1.0f) - settings.blendFactor) + vanillaImagespaceData.cinematic * settings.blendFactor;

	actualValues = actualValues * (settings.enableInExMultiplier ? (isInInterior ? settings.InteriorMultiplier : settings.ExteriorMultiplier) : float3(1.0f));
	if (cinematic.x == 0.0f && cinematic.y == 0.0f && cinematic.z == 0.0f) {
		actualValues = float3(1.0f);
	}

	if (settings.enableInExOverride) {
		actualValues = isInInterior ? settings.InteriorOverride : settings.ExteriorOverride;
	}
	data.cinematic = actualValues;
	vanillaImagespaceCB->Update(data);

	ID3D11ShaderResourceView* srv = inout_tex.srv;
	ID3D11UnorderedAccessView* uav = texOutput->uav.get();
	ID3D11Buffer* cb = vanillaImagespaceCB->CB();

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->CSSetShader(vanillaImagespaceCS.get(), nullptr, 0);
	context->Dispatch(((uint)res.x + 7) >> 3, ((uint)res.y + 7) >> 3, 1);

	srv = nullptr;
	uav = nullptr;
	cb = nullptr;

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
}