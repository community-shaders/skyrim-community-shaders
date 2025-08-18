#include "ENBDepthOfField.h"
#include "Globals.h"
#include <imgui.h>

void ENBDepthOfField::Execute(RE::BSGraphics::RenderTargetData& input,
	RE::BSGraphics::RenderTargetData& swap,
	RE::BSGraphics::RenderTargetData& output)
{
	UpdateDepthOfFieldVariables();
	ExecuteTechniqueSequence(GetSelectedTechnique(), input, swap, output);
}

void ENBDepthOfField::UpdateEffectVariables()
{
}

bool ENBDepthOfField::Apply()
{
	// Call base Apply first
	bool result = Effect::Apply();
	if (result) {
		CreateDepthOfFieldTextures();
	}
	return result;
}

void ENBDepthOfField::Unload()
{
	dofTextures.clear();
	Effect::Unload();
}

void ENBDepthOfField::CreateDepthOfFieldTextures()
{
	auto device = globals::d3d::device;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = 1;
	texDesc.Height = 1;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;  // R32F format (red channel only)
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	// Create TextureFocus (1x1 R32F) - computed in PS_Focus
	{
		DepthOfFieldTexture textureFocus{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureFocus.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureFocus.texture.Get(), nullptr, textureFocus.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureFocus.texture.Get(), nullptr, textureFocus.srv.GetAddressOf()));

		dofTextures["TextureFocus"] = std::move(textureFocus);
	}

	// Create TextureAperture (1x1 R32F) - computed in PS_Aperture
	{
		DepthOfFieldTexture textureAperture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureAperture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureAperture.texture.Get(), nullptr, textureAperture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureAperture.texture.Get(), nullptr, textureAperture.srv.GetAddressOf()));

		dofTextures["TextureAperture"] = std::move(textureAperture);
	}

	logger::info("Created depth of field textures: TextureFocus (1x1), TextureAperture (1x1)");
}

void ENBDepthOfField::UpdateDepthOfFieldVariables()
{
	if (!effect)
		return;

	// Set depth of field textures
	for (auto& [name, dofTexture] : dofTextures) {
		auto variable = effect->GetVariableByName(name.c_str())->AsShaderResource();
		if (variable && variable->IsValid()) {
			variable->SetResource(dofTexture.srv.Get());
		}
	}
}