#pragma once

#include <functional>
#include <string>

// Forward declaration
class Menu;

class AdvancedSettingsRenderer
{
public:
	static void RenderAdvancedSettings(
		const std::function<void()>& drawTruePBRSettings,
		const std::function<void()>& drawDisableAtBootSettings,
		std::string& pbrTextureSetSearch,
		std::string& pbrMaterialObjectSearch);

private:
	static void RenderAdvancedSection();
	static void RenderShaderReplacementSection();
	static void RenderPBRSection(
		const std::function<void()>& drawTruePBRSettings,
		std::string& pbrTextureSetSearch,
		std::string& pbrMaterialObjectSearch);
	static void RenderDisableAtBootSection(const std::function<void()>& drawDisableAtBootSettings);
	static void RenderDeveloperSection();
};