// Remote Control: status panel for the devbench bridge.
//
// The plugin's Model Context Protocol tools register into the external devbench host (the
// devbench SKSE plugin, https://github.com/alandtse/devbench) via DevBenchBridge
// (src/Features/RemoteControl/DevBenchBridge.cpp), exposed over both MCP and REST. This feature is a read-only
// panel: it reports whether devbench is present, what was registered, and the port
// devbench bound, so users can confirm the integration without leaving the game.

#include "Features/RemoteControl.h"

#include "Features/RemoteControl/DevBenchBridge.h"
#include "Globals.h"
#include "Menu.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using json = nlohmann::json;

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <DevBenchAPI.h>
#endif

namespace
{
	// devbench writes the host port it bound to here on startup. We only read it for
	// display; the bridge itself talks to devbench in-process via the C-ABI, not the port.
	constexpr const char* kRuntimeJsonPath = "Data/SKSE/Plugins/devbench/runtime.json";

	/**
	 * @brief Retrieves the bound port from devbench's runtime configuration.
	 * @return The port number from runtime.json, or 0 if the file is missing, unreadable, or malformed.
	 */
	int ReadDevBenchPort()
	{
		std::error_code ec;
		if (!std::filesystem::exists(kRuntimeJsonPath, ec))
			return 0;
		try {
			std::ifstream in(kRuntimeJsonPath);
			if (!in)
				return 0;
			json runtime = json::parse(in, nullptr, /*allow_exceptions=*/false);
			if (runtime.is_discarded() || !runtime.is_object())
				return 0;
			return runtime.value("port", 0);
		} catch (...) {
			return 0;  // malformed runtime.json is non-fatal — just hide the port
		}
	}
}

/**
 * @brief Obtains the singleton instance.
 * @return RemoteControl* The global singleton instance.
 */
RemoteControl* RemoteControl::GetSingleton()
{
	return &globals::features::remoteControl;
}

/**
 * @brief Registers the plugin's tools into the devbench host.
 *
 * Idempotent and inert when the host is absent or the devbench bridge is disabled.
 */
void RemoteControl::DataLoaded()
{
	// Register the plugin's tools into the devbench host. This feature owns the install; it runs at
	// DataLoaded rather than Load because devbench publishes its cross-plugin interface at
	// kPostLoad — by DataLoaded it's ready. Inert (logged) when no host is present or the
	// bridge was built disabled; idempotent on the devbench side (re-registering replaces).
	DevBenchBridge::Install();
}

/**
 * @brief Renders the devbench integration status panel and tool list.
 *
 * Displays connection status, host build number, bound port, and the set of devbench-exposed tools
 * (feature management, engine inspection, shader cache control, capture, and settings). Shows a
 * warning if the devbench host is not detected or if the build was compiled without the devbench
 * bridge.
 */
void RemoteControl::DrawSettings()
{
	const auto& theme = Menu::GetSingleton()->GetTheme().StatusPalette;

	ImGui::TextWrapped(
		"Registers graphics-feature, inspect, capture, shader-cache, and settings tools "
		"into the external devbench host so AI assistants (Claude Code, Cursor, etc.) can "
		"toggle features, inspect engine state, trigger captures, and save/load settings "
		"over MCP and REST. There is no in-game server — install the devbench SKSE plugin "
		"to enable the integration.");
	ImGui::Spacing();

#ifdef DEVBENCH_BRIDGE_ENABLED
	auto* dvb = DevBenchAPI::GetDevBenchInterface001();
	if (dvb) {
		ImGui::TextColored(theme.SuccessColor, "devbench host present (build %u)", dvb->GetBuildNumber());

		// Cache the port — runtime.json I/O + JSON parse every frame would hitch the UI while
		// the panel is open. Refresh on a coarse interval (devbench may bind after the panel
		// first opens, so re-read periodically rather than only once). QPC, not std::chrono.
		static int cachedPort = -1;       // -1 = not yet read
		static LONGLONG lastReadQpc = 0;  // ticks at last read
		LARGE_INTEGER freq, nowQpc;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&nowQpc);
		if (cachedPort < 0 || nowQpc.QuadPart - lastReadQpc > 2 * freq.QuadPart) {  // > 2s
			cachedPort = ReadDevBenchPort();
			lastReadQpc = nowQpc.QuadPart;
		}
		if (cachedPort > 0) {
			ImGui::Text("Host bound on port %d (from %s)", cachedPort, kRuntimeJsonPath);
		} else {
			ImGui::TextDisabled(
				"Port unknown — devbench writes it to %s once it binds.",
				kRuntimeJsonPath);
		}
	} else {
		ImGui::TextColored(theme.Warning,
			"devbench host not detected. Install the devbench SKSE plugin; "
			"the tools register automatically once it is present.");
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Tools exposed through devbench:");
	ImGui::BulletText("communityshaders.feature — list / get / set / reset / toggle features");
	ImGui::BulletText("communityshaders.inspect — engine state and shader-cache status");
	ImGui::BulletText("communityshaders.shadercache — clear / delete the compiled cache");
	ImGui::BulletText("communityshaders.capture — RenderDoc / screenshot capture");
	ImGui::BulletText("communityshaders.settings — save / load / reset the global config");
	ImGui::TextDisabled(
		"Note: the console tool is provided by devbench itself, not this plugin.");
#else
	ImGui::TextColored(theme.Warning,
		"This build was compiled without the devbench bridge "
		"(DEVBENCH_BRIDGE=OFF). No tools are registered.");
#endif
}
