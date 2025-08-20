#include "ENBDepthOfField.h"
#include "EffectManager.h"
#include "Globals.h"
#include "Utils/D3D.h"
#include <imgui.h>

void ENBDepthOfField::Execute()
{
	auto renderer = globals::game::renderer;

	auto& effectManager = EffectManager::GetSingleton();

	const std::string texturePreviousApertureName = (effectManager.textureSwap & 1) ? "TextureApertureSwap" : "TextureAperture";
	const std::string textureApertureName = (effectManager.textureSwap & 1) ? "TextureAperture" : "TextureApertureSwap";

	auto texturePrevious = effect->GetVariableByName("TexturePrevious")->AsShaderResource();
	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(effectTextureCache[texturePreviousApertureName].srv.Get());
	}

	Texture nullInputTexture{};
	nullInputTexture.texture = nullptr;
	nullInputTexture.srv = nullptr;
	nullInputTexture.rtv = nullptr;

	ExecuteTechnique("Aperture", nullInputTexture, effectTextureCache[textureApertureName]);

	ExecuteTechnique("ReadFocus", nullInputTexture, effectTextureCache["TextureReadFocus"]);

	const std::string texturePreviousFocusName = (effectManager.textureSwap & 1) ? "TextureFocusSwap" : "TextureFocus";
	const std::string textureFocusName = (effectManager.textureSwap & 1) ? "TextureFocus" : "TextureFocusSwap";

	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(effectTextureCache[texturePreviousApertureName].srv.Get());
	}

	auto textureCurrent = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (textureCurrent && textureCurrent->IsValid()) {
		textureCurrent->SetResource(effectTextureCache["TextureReadFocus"].srv.Get());
	}

	ExecuteTechnique("Focus", nullInputTexture, effectTextureCache[textureFocusName]);

	auto textureOriginal = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	Texture textureHDR{};
	textureHDR.texture = textureOriginal.texture;
	textureHDR.srv = textureOriginal.SRV;
	textureHDR.rtv = textureOriginal.RTV;

	auto textureHDRTemp = effectManager.GetCommonTexture("TextureHDRTemp");

	ExecuteTechniqueSequence(GetSelectedTechnique(), textureHDR, *textureHDRTemp, textureHDR);
}

void ENBDepthOfField::UpdateEffectVariables()
{
	auto& effectManager = EffectManager::GetSingleton();

	float4 dofParameters{};
	dofParameters.z = effectManager.enbSettings.DEPTHOFFIELD.ApertureTime * (*globals::game::deltaTime);
	dofParameters.w = effectManager.enbSettings.DEPTHOFFIELD.FocusingTime * (*globals::game::deltaTime);

	auto DofParameters = effect->GetVariableByName("DofParameters")->AsVector();
	if (DofParameters && DofParameters->IsValid())
		DofParameters->SetRawValue(&dofParameters, 0, sizeof(dofParameters));
}

void ENBDepthOfField::CreateEffectTextures()
{
	auto device = globals::d3d::device;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;  // R32F format (red channel only)
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	// Create TextureAperture (1x1 R32F) - computed in PS_Aperture
	{
		texDesc.Width = 1;
		texDesc.Height = 1;

		Texture textureAperture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureAperture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureAperture.texture.Get(), nullptr, textureAperture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureAperture.texture.Get(), nullptr, textureAperture.srv.GetAddressOf()));

		Util::SetResourceName(textureAperture.texture.Get(), "ENBDepthOfField::TextureAperture");
		Util::SetResourceName(textureAperture.rtv.Get(), "ENBDepthOfField::TextureAperture RTV");
		Util::SetResourceName(textureAperture.srv.Get(), "ENBDepthOfField::TextureAperture SRV");

		effectTextureCache["TextureAperture"] = std::move(textureAperture);
	}

	// Create TextureApertureSwap (1x1 R32F) - computed in PS_Aperture
	{
		texDesc.Width = 1;
		texDesc.Height = 1;

		Texture textureAperture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureAperture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureAperture.texture.Get(), nullptr, textureAperture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureAperture.texture.Get(), nullptr, textureAperture.srv.GetAddressOf()));

		Util::SetResourceName(textureAperture.texture.Get(), "ENBDepthOfField::TextureApertureSwap");
		Util::SetResourceName(textureAperture.rtv.Get(), "ENBDepthOfField::TextureApertureSwap RTV");
		Util::SetResourceName(textureAperture.srv.Get(), "ENBDepthOfField::TextureApertureSwap SRV");

		effectTextureCache["TextureApertureSwap"] = std::move(textureAperture);
	}

	// Create TextureReadFocus (16x16 R32F) - computed in PS_ReadFocus
	{
		texDesc.Width = 16;
		texDesc.Height = 16;

		Texture textureFocus{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureFocus.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureFocus.texture.Get(), nullptr, textureFocus.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureFocus.texture.Get(), nullptr, textureFocus.srv.GetAddressOf()));

		Util::SetResourceName(textureFocus.texture.Get(), "ENBDepthOfField::TextureReadFocus");
		Util::SetResourceName(textureFocus.rtv.Get(), "ENBDepthOfField::TextureReadFocus RTV");
		Util::SetResourceName(textureFocus.srv.Get(), "ENBDepthOfField::TextureReadFocus SRV");

		effectTextureCache["TextureReadFocus"] = std::move(textureFocus);
	}

	// Create TextureFocus (16x16 R32F) - computed in PS_ReadFocus
	{
		texDesc.Width = 1;
		texDesc.Height = 1;

		Texture textureFocus{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureFocus.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureFocus.texture.Get(), nullptr, textureFocus.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureFocus.texture.Get(), nullptr, textureFocus.srv.GetAddressOf()));

		Util::SetResourceName(textureFocus.texture.Get(), "ENBDepthOfField::TextureFocus");
		Util::SetResourceName(textureFocus.rtv.Get(), "ENBDepthOfField::TextureFocus RTV");
		Util::SetResourceName(textureFocus.srv.Get(), "ENBDepthOfField::TextureFocus SRV");

		effectTextureCache["TextureFocus"] = std::move(textureFocus);
	}

	{
		texDesc.Width = 1;
		texDesc.Height = 1;

		Texture textureFocus{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureFocus.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureFocus.texture.Get(), nullptr, textureFocus.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureFocus.texture.Get(), nullptr, textureFocus.srv.GetAddressOf()));

		Util::SetResourceName(textureFocus.texture.Get(), "ENBDepthOfField::TextureFocusSwap");
		Util::SetResourceName(textureFocus.rtv.Get(), "ENBDepthOfField::TextureFocusSwap RTV");
		Util::SetResourceName(textureFocus.srv.Get(), "ENBDepthOfField::TextureFocusSwap SRV");

		effectTextureCache["TextureFocusSwap"] = std::move(textureFocus);
	}

	logger::info("[ENBPP] Created depth of field textures: TextureAperture (1x1), TextureReadFocus (16x16), TextureFocus (1x1),");
}
