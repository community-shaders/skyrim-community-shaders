#include "ENBDepthOfField.h"
#include "EffectManager.h"
#include "Globals.h"
#include "Utils/D3D.h"
#include <imgui.h>

void ENBDepthOfField::Execute()
{
}

void ENBDepthOfField::UpdateEffectVariables()
{
}

void ENBDepthOfField::CreateEffectTextures()
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
		Texture textureFocus{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureFocus.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureFocus.texture.Get(), nullptr, textureFocus.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureFocus.texture.Get(), nullptr, textureFocus.srv.GetAddressOf()));

		Util::SetResourceName(textureFocus.texture.Get(), "ENBDepthOfField::TextureFocus");
		Util::SetResourceName(textureFocus.rtv.Get(), "ENBDepthOfField::TextureFocus RTV");
		Util::SetResourceName(textureFocus.srv.Get(), "ENBDepthOfField::TextureFocus SRV");

		effectTextureCache["TextureFocus"] = std::move(textureFocus);
	}

	// Create TextureAperture (1x1 R32F) - computed in PS_Aperture
	{
		Texture textureAperture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureAperture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureAperture.texture.Get(), nullptr, textureAperture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureAperture.texture.Get(), nullptr, textureAperture.srv.GetAddressOf()));

		Util::SetResourceName(textureAperture.texture.Get(), "ENBDepthOfField::TextureAperture");
		Util::SetResourceName(textureAperture.rtv.Get(), "ENBDepthOfField::TextureAperture RTV");
		Util::SetResourceName(textureAperture.srv.Get(), "ENBDepthOfField::TextureAperture SRV");

		effectTextureCache["TextureAperture"] = std::move(textureAperture);
	}

	logger::info("[ENBPP] Created depth of field textures: TextureFocus (1x1), TextureAperture (1x1)");
}
