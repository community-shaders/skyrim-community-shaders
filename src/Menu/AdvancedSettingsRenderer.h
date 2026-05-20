#pragma once

#include <functional>
#include <string>

// Forward declaration
class Menu;

class AdvancedSettingsRenderer
{
public:
	static void RenderAdvancedSettings(
		const std::function<void()>& drawDisableAtBootSettings);

private:
	static void RenderShadersSection();
	static void RenderDiagnosticsSection();
	static void RenderDisableAtBootSection(const std::function<void()>& drawDisableAtBootSettings);
	static void RenderTestingSection();

	// Helpers used by the sections above
	static void RenderShaderCompileFlags();
	static void RenderShaderThreading();
	static void RenderShaderCacheControls();
	static void RenderShaderReplacementTable();
	static void RenderShaderCompileStatistics();

	static void RenderLoggingControls();
	static void RenderRuntimeDebugControls();
	static void RenderShaderBlockingPanel();
};
