#include "IBL.h"

#include "Deferred.h"
#include "DynamicCubemaps.h"
#include "Shadercache.h"
#include "State.h"
#include "WeatherVariableRegistry.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	IBL::Settings,
	EnableIBL,
	PreserveFogLuminance,
	UseStaticIBL,
	DALCAmount,
	EnvIBLScale,
	SkyIBLScale,
	EnvIBLSaturation,
	SkyIBLSaturation,
	FogAmount,
	DALCMode,
	DisableInInteriors)

void IBL::DrawSettings()
{
	Util::WeatherUI::Checkbox("Enable IBL", this, "EnableIBL", (bool*)&settings.EnableIBL);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Toggle IBL. When enabled, ambient lighting is derived from cubemap spherical harmonics instead of the vanilla system.");
	}
	Util::WeatherUI::SliderFloat("Env IBL Scale", this, "EnvIBLScale", &settings.EnvIBLScale, 0.0f, 10.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Intensity multiplier for the environment IBL (from Dynamic Cubemaps).\nControls how strongly the surrounding environment contributes to ambient lighting.");
	}
	Util::WeatherUI::SliderFloat("Sky IBL Scale", this, "SkyIBLScale", &settings.SkyIBLScale, 0.0f, 10.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Intensity multiplier for the sky IBL (from the game's native reflections cubemap).\nControls how strongly the sky contributes to ambient lighting.");
	}
	Util::WeatherUI::SliderFloat("Env IBL Saturation", this, "EnvIBLSaturation", &settings.EnvIBLSaturation, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Color saturation of the environment IBL.\nLower values produce more neutral ambient light; higher values produce more vivid color.");
	}
	Util::WeatherUI::SliderFloat("Sky IBL Saturation", this, "SkyIBLSaturation", &settings.SkyIBLSaturation, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Color saturation of the sky IBL.\nLower values produce more neutral ambient light; higher values produce more vivid color.");
	}
	Util::WeatherUI::SliderFloat("DALC Amount", this, "DALCAmount", &settings.DALCAmount, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Blends the IBL brightness toward the game's vanilla ambient (DALC) level.\n"
			"0 = no matching (pure IBL brightness), 1 = fully matched to vanilla ambient.");
	}
	{
		static const char* dalcModeNames[] = { "Luminance Ratio", "Color Ratio", "DALC + Sky", "DALC + Sky (Directional)" };
		int dalcMode = static_cast<int>(settings.DALCMode);
		if (ImGui::Combo("DALC Mode", &dalcMode, dalcModeNames, IM_ARRAYSIZE(dalcModeNames))) {
			settings.DALCMode = static_cast<uint>(dalcMode);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"How the DALC-to-IBL brightness ratio is computed:\n"
				"Luminance Ratio: Scalar ratio from overall luminance (loses DALC color tint).\n"
				"Color Ratio: Per-channel ratio (preserves DALC color tint).\n"
				"DALC + Sky: Uses vanilla ambient as base, sky IBL on top. Skylighting only affects sky.\n"
				"DALC + Sky (Directional): Same, but Skylighting also dims vanilla ambient per-direction.");
		}
	}
	ImGui::Checkbox("Use Static IBL For Out-of-World Objects", (bool*)&settings.UseStaticIBL);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Uses pre-baked static IBL cubemap textures for objects rendered outside the game world (e.g. inventory items, loading screens).");
	}
	Util::WeatherUI::SliderFloat("Fog Mix", this, "FogAmount", &settings.FogAmount, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Blends the fog color toward the IBL ambient color.\n0 = vanilla fog, 1 = fog fully tinted by IBL.");
	}
	ImGui::Checkbox("Preserve Fog Luminance", (bool*)&settings.PreserveFogLuminance);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("When Fog Mix is active, rescales the IBL-tinted fog to keep the original fog brightness.\nPrevents fog from becoming too bright or too dark.");
	}
	ImGui::Checkbox("Disable in interiors", (bool*)&settings.DisableInInteriors);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Disables IBL in interior cells.");
	}
}

void IBL::LoadSettings(json& o_json)
{
	settings = o_json;
}

void IBL::SaveSettings(json& o_json)
{
	o_json = settings;
}

void IBL::RestoreDefaultSettings()
{
	settings = {};
}

void IBL::RegisterWeatherVariables()
{
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
	                     ->GetOrCreateFeatureRegistry(GetShortName());
	// Toggle IBL for this weather (SH-based ambient replaces vanilla)
	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"EnableIBL",
		"Enable IBL",
		"Enable or disable SH-based ambient lighting for this weather",
		(bool*)&settings.EnableIBL,
		true,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;  // Switch at transition midpoint
		}));

	// Intensity of environment IBL (from Dynamic Cubemaps)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"EnvIBLScale",
		"Env IBL Scale",
		"Intensity of environment IBL from the Dynamic Cubemaps environment cubemap",
		&settings.EnvIBLScale,
		1.0f,
		0.0f, 10.0f));

	// Intensity of sky IBL (from the game's native reflections cubemap)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"SkyIBLScale",
		"Sky IBL Scale",
		"Intensity of sky IBL from the game's native reflections cubemap",
		&settings.SkyIBLScale,
		1.0f,
		0.0f, 10.0f));

	// Color saturation of environment IBL
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"EnvIBLSaturation",
		"Env IBL Saturation",
		"Color saturation of the environment IBL ambient contribution",
		&settings.EnvIBLSaturation,
		1.0f,
		0.0f, 2.0f));

	// Color saturation of sky IBL
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"SkyIBLSaturation",
		"Sky IBL Saturation",
		"Color saturation of the sky IBL ambient contribution",
		&settings.SkyIBLSaturation,
		1.0f,
		0.0f, 2.0f));

	// How much IBL brightness is matched to vanilla ambient (DALC)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"DALCAmount",
		"DALC Amount",
		"Blend factor toward vanilla ambient brightness (0 = pure IBL, 1 = fully matched to DALC)",
		&settings.DALCAmount,
		1.0f,
		0.0f, 1.0f));

	// Fog color blending toward IBL ambient color
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"FogAmount",
		"Fog Mix",
		"Blends fog color toward IBL ambient color (0 = vanilla fog, 1 = fully IBL-tinted)",
		&settings.FogAmount,
		0.0f,
		0.0f, 1.0f));
}

IBL::Settings IBL::GetCommonBufferData() const
{
	Settings data = settings;
	if (settings.DisableInInteriors && Util::IsInterior())
		data.EnableIBL = 0;
	return data;
}

void IBL::ReflectionsPrepass()
{
	if (loaded) {
		auto context = globals::d3d::context;

		bool interiorDisabled = settings.DisableInInteriors && Util::IsInterior();

		// Set PS shader resource
		{
			std::array<ID3D11ShaderResourceView*, 4> srvs = {
				interiorDisabled ? nullptr : envIBLTexture->srv.get(),
				interiorDisabled ? nullptr : skyIBLTexture->srv.get(),
				staticDiffuseIBLTexture->srv.get(),
				staticSpecularIBLTexture->srv.get()
			};
			context->PSSetShaderResources(76, 4, srvs.data());
		}
	}
}

void IBL::Prepass()
{
	if (settings.DisableInInteriors && Util::IsInterior())
		return;

	auto context = globals::d3d::context;

	auto& dynamicCubemaps = globals::features::dynamicCubemaps;

	auto& envTexture = dynamicCubemaps.envTexture;

	// Unset PS shader resource
	{
		ID3D11ShaderResourceView* views[2]{ nullptr, nullptr };
		context->PSSetShaderResources(76, 2, views);
	}

	std::array<ID3D11ShaderResourceView*, 1> srvs = { (dynamicCubemaps.loaded && envTexture) ? envTexture->srv.get() : nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { envIBLTexture->uav.get() };
	std::array<ID3D11SamplerState*, 1> samplers = { Deferred::GetSingleton()->linearSampler };

	// IBL - Environment cubemap SH projection (skip for DALC-based modes that don't use EnvIBL)
	if (settings.DALCMode < 2) {
		samplers[0] = Deferred::GetSingleton()->linearSampler;

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(GetDiffuseIBLCS(), nullptr, 0);
		context->Dispatch(1, 1, 1);
	} else {
		// Still need to set sampler and shader for sky IBL dispatch below
		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShader(GetDiffuseIBLCS(), nullptr, 0);
	}

	// IBL with sky (use game's native reflections cubemap directly)
	{
		auto renderer = globals::game::renderer;
		auto& reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];
		srvs.at(0) = reflections.SRV;
		uavs.at(0) = skyIBLTexture->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->Dispatch(1, 1, 1);
	}

	// Reset
	{
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		samplers.fill(nullptr);

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Set PS shader resource
	{
		ID3D11ShaderResourceView* views[2]{ envIBLTexture->srv.get(), skyIBLTexture->srv.get() };
		context->PSSetShaderResources(76, 2, views);
	}
}

void IBL::SetupResources()
{
	GetDiffuseIBLCS();

	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 3,
			.Height = 1,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
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

		envIBLTexture = new Texture2D(texDesc);
		envIBLTexture->CreateSRV(srvDesc);
		envIBLTexture->CreateUAV(uavDesc);
		skyIBLTexture = new Texture2D(texDesc);
		skyIBLTexture->CreateSRV(srvDesc);
		skyIBLTexture->CreateUAV(uavDesc);
	}

	auto device = globals::d3d::device;

	logger::debug("Loading static Diffuse IBL textures...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\IBL\\DiffuseIBL.dds";

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		staticDiffuseIBLTexture = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "IBL::StaticDiffuse");

		staticDiffuseIBLTexture->desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = staticDiffuseIBLTexture->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		staticDiffuseIBLTexture->CreateSRV(srvDesc);
	}

	logger::debug("Loading static Specular IBL textures...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\IBL\\SpecIBL.dds";

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		staticSpecularIBLTexture = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "IBL::StaticSpecular");

		staticSpecularIBLTexture->desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = staticSpecularIBLTexture->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = {
				.MostDetailedMip = 0,
				.MipLevels = 8 }
		};
		staticSpecularIBLTexture->CreateSRV(srvDesc);
	}
}

void IBL::ClearShaderCache()
{
	if (diffuseIBLCS)
		diffuseIBLCS->Release();
	diffuseIBLCS = nullptr;
}

ID3D11ComputeShader* IBL::GetDiffuseIBLCS()
{
	std::vector<std::pair<const char*, const char*>> defines;
	if (globals::features::dynamicCubemaps.loaded)
		defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });
	if (!diffuseIBLCS)
		diffuseIBLCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\IBL\\DiffuseIBLCS.hlsl", defines, "cs_5_0"));
	return diffuseIBLCS;
}
