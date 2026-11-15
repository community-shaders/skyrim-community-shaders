#include "WaterEffects.h"

#include <DDSTextureLoader.h>

#include "Utils/D3D.h"

void WaterEffects::SetupResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	winrt::com_ptr<ID3D11Resource> causticsTexture;
	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\WaterEffects\\watercaustics.dds", causticsTexture.put(), causticsView.put());
	Util::SetResourceName(causticsTexture.get(), "WaterEffects::Caustics");
	Util::SetResourceName(causticsView.get(), "WaterEffects::Caustics SRV");
}

void WaterEffects::Prepass()
{
	auto context = globals::d3d::context;
	auto srv = causticsView.get();
	context->PSSetShaderResources(65, 1, &srv);
}

bool WaterEffects::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}
