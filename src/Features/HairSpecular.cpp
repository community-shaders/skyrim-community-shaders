#include "HairSpecular.h"

#include <DirectXTex.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HairSpecular::Settings,
	Enabled,
	HairGlossiness,
	SpecularMult,
	DiffuseMult,
	EnableTangentShift,
	PrimaryTangentShift,
	SecondaryTangentShift,
	HairSaturation,
	SpecularIndirectMult,
	DiffuseIndirectMult,
	BaseColorMult,
	Transmission,
	EnableSelfShadow,
	SelfShadowStrength,
	SelfShadowExponent,
	SelfShadowScale,
	HairMode)

void HairSpecular::DrawSettings()
{
	ImGui::Checkbox("Enabled", (bool*)&settings.Enabled);
	ImGui::Combo("Hair Mode", (int*)&settings.HairMode, "Kajiya-Kay\0Marschner\0");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Select the hair shading model to use.\n"
			"Kajiya-Kay is an empirical model that simulates hair specular highlights.\n"
			"Marschner is a more physically-based model that simulates hair light interaction.\n"
			"Both models are anisotropic and support tangent-based shading.\n"
			"Note that colors in Marschner mode may appear different.\n"
			"Also, without self-shadowing enabled, Marschner mode may look overly bright because of transmission.\n");
	}
	ImGui::Spacing();
	ImGui::SliderFloat("Glossiness", &settings.HairGlossiness, 0.0f, settings.HairMode == 0 ? 256.0f : 100.0f, "%.0f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Controls the glossiness of the hair.\n"
			"Glossiness in Kajiya-Kay mode maps to the specular exponent.\n"
			"In Marschner mode, it controls the roughness of the hair surface.\n");
	}
	ImGui::SliderFloat("Specular Multiplier", &settings.SpecularMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Diffuse Multiplier", &settings.DiffuseMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Indirect Specular Multiplier", &settings.SpecularIndirectMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Indirect Diffuse Multiplier", &settings.DiffuseIndirectMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Hair Base Color Multiplier", &settings.BaseColorMult, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Hair Saturation", &settings.HairSaturation, 0.0f, 5.0f, "%.2f");
	ImGui::SliderFloat("Transmission", &settings.Transmission, 0.0f, 1.0f, "%.2f");
	ImGui::Spacing();
	ImGui::Checkbox("Enable Tangent Shift", (bool*)&settings.EnableTangentShift);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Enables the use of a tangent shift texture to vary specular highlights across hair strands.\n"
			"Result may vary based on the hair model used.\n");
	}
	if (settings.HairMode == 0) {
		ImGui::SliderFloat("Primary Specular Tangent Shift", &settings.PrimaryTangentShift, -1.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Secondary Specular Tangent Shift", &settings.SecondaryTangentShift, -1.0f, 1.0f, "%.2f");
	}
	ImGui::Spacing();
	ImGui::Checkbox("Enable Screen-Space Self Shadow", (bool*)&settings.EnableSelfShadow);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Enables screen-space self-shadowing for hair.\n"
			"Marschner hair model might have overly bright transmission without self-shadowing.\n");
	}
	ImGui::SliderFloat("Self Shadow Strength", &settings.SelfShadowStrength, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat("Self Shadow Exponent", &settings.SelfShadowExponent, 0.0f, 10.0f, "%.2f");
	ImGui::SliderFloat("Self Shadow Scale", &settings.SelfShadowScale, 0.0f, 10.0f, "%.2f");
}

void HairSpecular::LoadSettings(json& o_json)
{
	settings = o_json;
}

void HairSpecular::SaveSettings(json& o_json)
{
	o_json = settings;
}

void HairSpecular::RestoreDefaultSettings()
{
	settings = {};
}

void HairSpecular::SetupResources()
{
	auto device = globals::d3d::device;

	logger::debug("Loading Hair Tangent Shift Texture...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\Hair\\TangentShift.dds";

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

		texTangentShift = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texTangentShift->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 10 }
		};
		texTangentShift->CreateSRV(srvDesc);
	}
}

void HairSpecular::Prepass()
{
	auto context = globals::d3d::context;

	if (texTangentShift) {
		ID3D11ShaderResourceView* srv = texTangentShift->srv.get();
		context->PSSetShaderResources(73, 1, &srv);
	}
}