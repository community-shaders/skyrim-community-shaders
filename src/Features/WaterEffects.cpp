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

void WaterEffects::DrawSettings()
{
	// Display feature description at the top
	Util::DisplayFeatureDescription(GetFeatureDescription());
	
	// Display version info at the bottom
	Util::DisplayVersionInfo(version);
}

bool WaterEffects::DrawFailLoadMessage() const
{
	return false;
}

void WaterEffects::DrawUnloadedUI()
{
	// Call base class implementation for standard "not installed" message
	Feature::DrawUnloadedUI();

	// Feature-specific description
	ImGui::TextWrapped(
		"Water Effects enhances Skyrim's water rendering by adding caustic light effects and improved water visuals.\n"
		"Caustics are the patterns of light that form when light passes through a water surface and creates rippling effects on surfaces below.");

	ImGui::Spacing();
	ImGui::TextWrapped("Key features:");
	ImGui::BulletText("Realistic underwater caustic lighting");
	ImGui::BulletText("3D water surface appearance via parallax");
	ImGui::TextWrapped("Requirements:");
	ImGui::BulletText("Parallax Water requires a water mod with displacement textures.");
}
