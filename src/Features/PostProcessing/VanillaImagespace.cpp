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
	enableInExOverride,
	enableFade,
	enableTint)

void VanillaImagespace::DrawSettings()
{
	ImGui::SliderFloat3("Blend Factor", &settings.blendFactor.x, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Blend factor for the vanilla imagespace (saturation, brightness, contrast).");
	}

	ImGui::Checkbox("Enable Fade", &settings.enableFade);
	ImGui::Checkbox("Enable Tint", &settings.enableTint);

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

	if (ImGui::TreeNode("Original ImageSpace Values")) {
		ImGui::Text("Base Amount: %.3f", imageSpaceData.baseAmount);
		ImGui::Text("Base Data:");
		ImGui::Text("Cinematic Values:");
		ImGui::Text("Saturation: %.3f", imageSpaceData.baseData.cinematic.saturation);
		ImGui::Text("Brightness: %.3f", imageSpaceData.baseData.cinematic.brightness);
		ImGui::Text("Contrast: %.3f", imageSpaceData.baseData.cinematic.contrast);

		ImGui::Text("HDR Values:");
		ImGui::Text("Eye Adapt Speed: %.3f", imageSpaceData.baseData.hdr.eyeAdaptSpeed);
		ImGui::Text("Bloom Blur Radius: %.3f", imageSpaceData.baseData.hdr.bloomBlurRadius);
		ImGui::Text("Bloom Threshold: %.3f", imageSpaceData.baseData.hdr.bloomThreshold);
		ImGui::Text("Bloom Scale: %.3f", imageSpaceData.baseData.hdr.bloomScale);
		ImGui::Text("Receive Bloom Threshold: %.3f", imageSpaceData.baseData.hdr.receiveBloomThreshold);
		ImGui::Text("White: %.3f", imageSpaceData.baseData.hdr.white);
		ImGui::Text("Sunlight Scale: %.3f", imageSpaceData.baseData.hdr.sunlightScale);
		ImGui::Text("Sky Scale: %.3f", imageSpaceData.baseData.hdr.skyScale);
		ImGui::Text("Eye Adapt Strength: %.3f", imageSpaceData.baseData.hdr.eyeAdaptStrength);

		ImGui::Text("Tint Values:");
		ImGui::Text("Tint Amount: %.3f", imageSpaceData.baseData.tint.amount);
		ImGui::Text("Tint Color: (%.3f, %.3f, %.3f)", imageSpaceData.baseData.tint.color.red, imageSpaceData.baseData.tint.color.green, imageSpaceData.baseData.tint.color.blue);

		ImGui::Text("Depth of Field Values:");
		ImGui::Text("DOF Strength: %.3f", imageSpaceData.baseData.depthOfField.strength);
		ImGui::Text("DOF Distance: %.3f", imageSpaceData.baseData.depthOfField.distance);
		ImGui::Text("DOF Range: %.3f", imageSpaceData.baseData.depthOfField.range);
		ImGui::Text("DOF Flags: %d", imageSpaceData.baseData.depthOfField.flags);
		ImGui::Text("DOF Sky Blur Radius: %d", static_cast<int>(imageSpaceData.baseData.depthOfField.skyBlurRadius.get()));

		ImGui::Text("Mod Amount: %.3f", imageSpaceData.modAmount);
		ImGui::Text("Mod Data:");
		ImGui::Text("Fade Amount: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeAmount]);
		ImGui::Text("Fade Color: (%.3f, %.3f, %.3f)", imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeR], imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeG], imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeB]);
		ImGui::Text("Blur Radius: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kBlurRadius]);
		ImGui::Text("Double Vision Strength: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kDoubleVisionStrength]);
		ImGui::Text("Radial Blur Strength: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kRadialBlurStrength]);
		ImGui::Text("Radial Blur Rampup: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kRadialBlurRampup]);
		ImGui::Text("Radial Blur Start: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kRadialBlurStart]);
		ImGui::Text("Radial Blur Rampdown: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kRadialBlurRampdown]);
		ImGui::Text("Radial Blur Down Start: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kRadialBlurDownStart]);
		ImGui::Text("Radial Blur Center: (%.3f, %.3f)", imageSpaceData.modData.data[RE::ImageSpaceModData::kRadialBlurCenterX], imageSpaceData.modData.data[RE::ImageSpaceModData::kRadialBlurCenterY]);
		ImGui::Text("DOF Strength: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kDOFStrength]);
		ImGui::Text("DOF Distance: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kDOFDistance]);
		ImGui::Text("DOF Range: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kDOFRange]);
		ImGui::Text("DOF Mode: %d", imageSpaceData.modData.data[RE::ImageSpaceModData::kDOFMode]);
		ImGui::Text("Motion Blur Strength: %.3f", imageSpaceData.modData.data[RE::ImageSpaceModData::kMotionBlurStrength]);
		ImGui::TreePop();
	}

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
	float4 cinematic = { 1.0f, 1.0f, 1.0f, 1.0f };
	float4 fade = { 0.0f, 0.0f, 0.0f, 0.0f };
	float4 tint = { 0.0f, 0.0f, 0.0f, 0.0f };
	auto ImageSpace = RE::ImageSpaceManager::GetSingleton();
	if (globals::game::isVR) {
		const auto& iSRuntimeData = ImageSpace->GetVRRuntimeData();
		imageSpaceData = iSRuntimeData.data;
		if (const auto& overrideBaseData = iSRuntimeData.overrideBaseData) {
			imageSpaceData.baseData = *overrideBaseData;
		} else {
			imageSpaceData.baseData = *iSRuntimeData.currentBaseData;
		}
	} else {
		const auto& iSRuntimeData = ImageSpace->GetRuntimeData();
		imageSpaceData = iSRuntimeData.data;
		if (const auto& overrideBaseData = iSRuntimeData.overrideBaseData) {
			imageSpaceData.baseData = *overrideBaseData;
		} else {
			imageSpaceData.baseData = *iSRuntimeData.currentBaseData;
		}
	}

	if (auto sky = globals::game::sky)
		isInInterior = sky->mode.get() != RE::Sky::Mode::kFull;
	else
		isInInterior = true;
	cinematic.x = imageSpaceData.baseData.cinematic.saturation;
	cinematic.y = imageSpaceData.baseData.cinematic.brightness;
	cinematic.z = imageSpaceData.baseData.cinematic.contrast;
	cinematic.w = imageSpaceData.baseAmount;

	if (settings.enableFade) {
		fade.x = imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeR];
		fade.y = imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeG];
		fade.z = imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeB];
		fade.w = imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeAmount];
	}

	if (settings.enableTint) {
		tint.x = imageSpaceData.baseData.tint.color.red;
		tint.y = imageSpaceData.baseData.tint.color.green;
		tint.z = imageSpaceData.baseData.tint.color.blue;
		tint.w = imageSpaceData.baseData.tint.amount;
	}

	VanillaImagespaceCB data = {
		.cinematic = cinematic,
		.fade = fade,
		.tint = tint
	};

	actualValues = (float3(1.0f) - settings.blendFactor) + cinematic * settings.blendFactor;

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