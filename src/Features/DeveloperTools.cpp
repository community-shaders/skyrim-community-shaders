// Developer Tools: RenderDoc, Nsight Perf and devbench in one feature. The tools are the RenderDoc and
// RemoteControl components; this file drives their lifecycle and lays out the panel as three cards
// over a tab per tool.

#include "Features/DeveloperTools.h"

#include "Features/RemoteControl.h"
#include "Features/RemoteControl/NvPerf.h"
#include "Features/RenderDoc.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Utils/UI.h"

#include <imgui.h>
#include <shellapi.h>

#define I18N_KEY_PREFIX "feature.developer_tools."

DeveloperTools* DeveloperTools::GetSingleton()
{
	return &globals::features::developerTools;
}

void DeveloperTools::Load()
{
	globals::features::renderDoc.Load();
}

void DeveloperTools::DataLoaded()
{
	globals::features::remoteControl.DataLoaded();
}

void DeveloperTools::SaveSettings(json& o_json)
{
	globals::features::renderDoc.SaveSettings(o_json["RenderDoc"]);
}

void DeveloperTools::LoadSettings(json& o_json)
{
	if (o_json.contains("RenderDoc") && o_json["RenderDoc"].is_object())
		globals::features::renderDoc.LoadSettings(o_json["RenderDoc"]);
}

void DeveloperTools::RestoreDefaultSettings()
{
	globals::features::renderDoc.RestoreDefaultSettings();
}

void DeveloperTools::OpenDevBenchNexusPage()
{
	ShellExecuteA(nullptr, "open", kDevBenchNexusUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

void DeveloperTools::DrawToolCard(Tool a_tool, const char* a_icon, const char* a_title, const char* a_status, const ImVec4& a_statusColor, const char* a_blurb)
{
	ImGui::PushID(static_cast<int>(a_tool));
	if (ImGui::BeginChild("##ToolCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
		{
			MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
			Icons::Text(Icons::TI(a_icon));
			ImGui::SameLine();
			ImGui::TextUnformatted(a_title);
		}
		ImGui::TextColored(a_statusColor, "%s", a_status);
		{
			MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
			ImGui::TextWrapped("%s", a_blurb);
		}
		ImGui::Spacing();
		if (Icons::LabeledButton("##OpenTool", Icons::TI(ICON_TI_CHEVRON_RIGHT), T(TKEY("open_tool"), "Open"), ImVec2(-FLT_MIN, 0.0f)))
			pendingTab = static_cast<int>(a_tool);
	}
	ImGui::EndChild();
	ImGui::PopID();
}

void DeveloperTools::DrawSettings()
{
	const auto& palette = Menu::GetSingleton()->GetTheme().StatusPalette;
	auto& renderDoc = globals::features::renderDoc;
	auto& remoteControl = globals::features::remoteControl;

	ImGui::TextWrapped("%s", T(TKEY("intro"), "Tools for capturing, profiling and remotely driving Community Shaders. Pick a tool below."));
	ImGui::Spacing();

	// RenderDoc status
	const char* renderDocStatus;
	ImVec4 renderDocColor;
	if (renderDoc.IsAvailable() && renderDoc.enableRenderDocCapture) {
		renderDocStatus = T(TKEY("renderdoc_active"), "Capture active");
		renderDocColor = palette.SuccessColor;
	} else if (renderDoc.IsAvailable()) {
		renderDocStatus = T(TKEY("renderdoc_restart_disable"), "Restart to disable");
		renderDocColor = palette.Warning;
	} else if (renderDoc.enableRenderDocCapture) {
		renderDocStatus = T(TKEY("renderdoc_restart_enable"), "Restart to enable");
		renderDocColor = palette.RestartNeeded;
	} else {
		renderDocStatus = T(TKEY("renderdoc_off"), "Off");
		renderDocColor = palette.Disable;
	}

	// Nsight Perf status. Probe() loads the profiler library once; later calls are cheap.
	NvPerf::Probe();
	const auto device = NvPerf::GetDeviceInfo();
	const char* nvperfStatus;
	ImVec4 nvperfColor;
	if (!device.built) {
		nvperfStatus = T(TKEY("nvperf_not_built"), "Not in this build");
		nvperfColor = palette.Disable;
	} else if (!device.gpuSupported) {
		nvperfStatus = T(TKEY("nvperf_unsupported"), "GPU not supported");
		nvperfColor = palette.Warning;
	} else {
		nvperfStatus = T(TKEY("nvperf_ready"), "Ready");
		nvperfColor = palette.SuccessColor;
	}

	// devbench status
	const char* devbenchStatus;
	ImVec4 devbenchColor;
	const bool devbenchPresent = RemoteControl::IsDevBenchHostPresent();
	if (!RemoteControl::IsDevBenchBridgeBuilt()) {
		devbenchStatus = T(TKEY("devbench_not_built"), "Not in this build");
		devbenchColor = palette.Disable;
	} else if (devbenchPresent) {
		devbenchStatus = T(TKEY("devbench_connected"), "Connected");
		devbenchColor = palette.SuccessColor;
	} else {
		devbenchStatus = T(TKEY("devbench_missing"), "devbench not installed");
		devbenchColor = palette.Warning;
	}

	if (ImGui::BeginTable("##DeveloperToolCards", static_cast<int>(Tool::Count), ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextColumn();
		DrawToolCard(Tool::RenderDoc, ICON_TI_APERTURE, T(TKEY("renderdoc"), "RenderDoc"), renderDocStatus, renderDocColor,
			T(TKEY("renderdoc_blurb"), "Frame captures for debugging rendering issues."));
		ImGui::TableNextColumn();
		DrawToolCard(Tool::NsightPerf, ICON_TI_CHART_BUBBLE, T(TKEY("nsight"), "Nsight Perf"), nvperfStatus, nvperfColor,
			T(TKEY("nsight_blurb"), "NVIDIA GPU hardware counters per render pass."));
		ImGui::TableNextColumn();
		DrawToolCard(Tool::DevBench, ICON_TI_SHARE_2, T(TKEY("devbench"), "devbench"), devbenchStatus, devbenchColor,
			T(TKEY("devbench_blurb"), "Drive Community Shaders from AI assistants over MCP and REST."));
		ImGui::EndTable();
	}

	ImGui::Spacing();

	const int selectTab = pendingTab;
	pendingTab = -1;
	auto tabFlags = [selectTab](Tool a_tool) {
		return selectTab == static_cast<int>(a_tool) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
	};

	if (Util::BeginPillTabBar("##DeveloperToolTabs")) {
		if (Util::BeginPillTabItem(T(TKEY("renderdoc"), "RenderDoc"), nullptr, tabFlags(Tool::RenderDoc))) {
			renderDoc.DrawSettings();
			Util::EndPillTabItem();
		}
		if (Util::BeginPillTabItem(T(TKEY("nsight"), "Nsight Perf"), nullptr, tabFlags(Tool::NsightPerf))) {
			remoteControl.DrawNvPerfPanel();
			Util::EndPillTabItem();
		}
		if (Util::BeginPillTabItem(T(TKEY("devbench"), "devbench"), nullptr, tabFlags(Tool::DevBench))) {
			if (Icons::LabeledButton("##DevBenchNexus", Icons::TI(ICON_TI_EXTERNAL_LINK), T(TKEY("devbench_nexus"), "devbench on Nexus Mods")))
				OpenDevBenchNexusPage();
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", kDevBenchNexusUrl);
			ImGui::Spacing();
			remoteControl.DrawDevBenchPanel();
			Util::EndPillTabItem();
		}
		Util::EndPillTabBar();
	}
}

#undef I18N_KEY_PREFIX
