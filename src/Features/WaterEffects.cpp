#include "WaterEffects.h"

#include <DDSTextureLoader.h>

void WaterEffects::SetupResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\WaterEffects\\watercaustics.dds", nullptr, causticsView.put());
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
