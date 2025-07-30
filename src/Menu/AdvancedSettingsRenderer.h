#pragma once

#include <functional>

// Forward declaration
class Menu;

class AdvancedSettingsRenderer
{
public:
	static void RenderAdvancedSettings(
		const std::function<void()>& drawTruePBRSettings,
		const std::function<void()>& drawDisableAtBootSettings);

private:
	static void RenderAdvancedSection();
	static void RenderShaderReplacementSection();
	static void RenderDeveloperSection();
};