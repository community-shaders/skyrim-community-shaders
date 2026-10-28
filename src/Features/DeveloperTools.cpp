// Developer Tools: RenderDoc, Nsight Perf and devbench in one feature. The tools are the RenderDoc and
// RemoteControl components; this file drives their lifecycle and lays out the panel as three cards
// that pick which tool's panel shows below them.

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
#include "State.h"
#include "Utils/UI.h"

#include <array>
#include <format>
#include <imgui.h>
#include <optional>
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

void DeveloperTools::DrawSettings()
{
	const auto& palette = Menu::GetSingleton()->GetTheme().StatusPalette;
	auto& renderDoc = globals::features::renderDoc;
	auto& remoteControl = globals::features::remoteControl;

	ImGui::TextWrapped("%s", T(TKEY("intro"), "Tools for capturing, profiling and remotely driving Community Shaders. Pick a tool to open its settings."));
	ImGui::Spacing();

	// RenderDoc: the status line's action moves it to the next state, or captures once it is running
	std::array<Util::OptionCardContent, static_cast<size_t>(Tool::Count)> cards{};
	auto& renderDocCard = cards[static_cast<size_t>(Tool::RenderDoc)];
	renderDocCard.icon = ICON_TI_APERTURE;
	renderDocCard.title = T(TKEY("renderdoc"), "RenderDoc");
	renderDocCard.description = T(TKEY("renderdoc_blurb"), "Frame captures for debugging rendering issues.");
	const std::string captureLabel = std::format("{} (F12)", T(TKEY("renderdoc_capture"), "Capture frame"));
	const bool renderDocActive = renderDoc.IsAvailable() && renderDoc.enableRenderDocCapture;
	if (renderDocActive) {
		renderDocCard.status = T(TKEY("renderdoc_active"), "Capture active");
		renderDocCard.statusColor = palette.SuccessColor;
		renderDocCard.action = captureLabel.c_str();
		renderDocCard.actionTooltip = T(TKEY("renderdoc_capture_tooltip"), "Captures the next frame, or the frame count set below. F12 or Print Screen does the same in game, without the menu in the shot.");
	} else if (renderDoc.IsAvailable()) {
		renderDocCard.status = T(TKEY("renderdoc_restart_disable"), "Restart to disable");
		renderDocCard.statusColor = palette.Warning;
		renderDocCard.action = T(TKEY("renderdoc_keep_on"), "Keep enabled");
		renderDocCard.actionTooltip = T(TKEY("renderdoc_keep_on_tooltip"), "Leave capture on, so nothing changes at the next start.");
	} else if (renderDoc.enableRenderDocCapture) {
		renderDocCard.status = T(TKEY("renderdoc_restart_enable"), "Restart to enable");
		renderDocCard.statusColor = palette.RestartNeeded;
		renderDocCard.action = T(TKEY("renderdoc_keep_off"), "Keep disabled");
		renderDocCard.actionTooltip = T(TKEY("renderdoc_keep_off_tooltip"), "Leave capture off, so nothing changes at the next start.");
	} else {
		renderDocCard.status = T(TKEY("renderdoc_off"), "Off");
		renderDocCard.statusColor = palette.Disable;
		renderDocCard.action = T(TKEY("renderdoc_enable"), "Enable");
		renderDocCard.actionTooltip = T(TKEY("renderdoc_enable_tooltip"), "Turn capture on. RenderDoc loads at the next start, and performance drops while it is loaded.");
	}

	// Nsight Perf. Probe() loads the profiler library once; later calls are cheap.
	NvPerf::Probe();
	const auto device = NvPerf::GetDeviceInfo();
	remoteControl.UpdateNvPerfHistory();
	const auto nvperfState = NvPerf::GetStatus().state;
	const bool nvperfBusy = nvperfState == NvPerf::CaptureState::Waiting || nvperfState == NvPerf::CaptureState::Capturing ||
	                        nvperfState == NvPerf::CaptureState::Decoding;
	auto& nvperfCard = cards[static_cast<size_t>(Tool::NsightPerf)];
	nvperfCard.icon = ICON_TI_CHART_BAR;
	nvperfCard.title = T(TKEY("nsight"), "Nsight Perf");
	nvperfCard.description = T(TKEY("nsight_blurb"), "NVIDIA GPU hardware counters per render pass.");
	if (!device.built) {
		nvperfCard.status = T(TKEY("nvperf_not_built"), "Not in this build");
		nvperfCard.statusColor = palette.Disable;
		nvperfCard.unavailable = true;
	} else if (!device.gpuSupported) {
		nvperfCard.status = T(TKEY("nvperf_unsupported"), "GPU not supported");
		nvperfCard.statusColor = palette.Warning;
		nvperfCard.unavailable = true;
	} else if (nvperfBusy) {
		nvperfCard.status = T(TKEY("nvperf_capturing"), "Capturing...");
		nvperfCard.statusColor = palette.InfoColor;
		nvperfCard.action = T(TKEY("nvperf_cancel"), "Cancel");
	} else {
		nvperfCard.status = T(TKEY("nvperf_ready"), "Ready");
		nvperfCard.statusColor = palette.SuccessColor;
		nvperfCard.action = T(TKEY("nvperf_capture"), "Capture");
		nvperfCard.actionTooltip = T(TKEY("nvperf_capture_tooltip"), "Measures the next frames with the metrics and ranges set below.");
	}

	// devbench
	auto& devbenchCard = cards[static_cast<size_t>(Tool::DevBench)];
	devbenchCard.icon = ICON_TI_ROBOT;
	devbenchCard.title = T(TKEY("devbench"), "devbench");
	devbenchCard.description = T(TKEY("devbench_blurb"), "Drive Community Shaders from AI assistants over MCP and REST.");
	const bool devbenchPresent = RemoteControl::IsDevBenchHostPresent();
	const int devbenchPort = devbenchPresent ? RemoteControl::GetDevBenchPort() : 0;
	const std::string devbenchConnected = devbenchPort > 0 ?
	                                          std::vformat(T(TKEY("devbench_connected_port"), "Connected on port {}"), std::make_format_args(devbenchPort)) :
	                                          std::string(T(TKEY("devbench_connected"), "Connected"));
	if (!RemoteControl::IsDevBenchBridgeBuilt()) {
		devbenchCard.status = T(TKEY("devbench_not_built"), "Not in this build");
		devbenchCard.statusColor = palette.Disable;
		devbenchCard.unavailable = true;
	} else if (devbenchPresent) {
		devbenchCard.status = devbenchConnected.c_str();
		devbenchCard.statusColor = palette.SuccessColor;
		if (devbenchPort > 0) {
			devbenchCard.action = T(TKEY("devbench_copy_endpoint"), "Copy REST URL");
			devbenchCard.actionTooltip = T(TKEY("devbench_copy_endpoint_tooltip"), "Copies the devbench REST endpoint to the clipboard.");
		}
	} else {
		devbenchCard.status = T(TKEY("devbench_missing"), "devbench not installed");
		devbenchCard.statusColor = palette.Warning;
		devbenchCard.action = T(TKEY("devbench_get"), "Get devbench");
		devbenchCard.actionTooltip = kDevBenchNexusUrl;
	}

	// Opens on the tool in use the first time; after that the last pick stays for the session
	if (!selectionInitialized) {
		selectionInitialized = true;
		if (renderDocActive)
			selectedTool = Tool::RenderDoc;
		else if (devbenchPresent)
			selectedTool = Tool::DevBench;
	}

	// Every card takes the tallest one's height so the row lines up
	std::optional<Tool> actionTool;
	if (ImGui::BeginTable("##DeveloperToolCards", static_cast<int>(Tool::Count), ImGuiTableFlags_SizingStretchSame)) {
		const float cellWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().CellPadding.x * 2.0f * cards.size()) / cards.size();
		float height = 0.0f;
		for (const auto& card : cards)
			height = std::max(height, Util::OptionCardHeight(card, cellWidth));
		for (size_t i = 0; i < cards.size(); ++i) {
			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(i));
			const auto tool = static_cast<Tool>(i);
			const auto click = Util::OptionCard("##ToolCard", cards[i], selectedTool == tool, ImVec2(0.0f, height));
			if (click != Util::OptionCardClick::None)
				selectedTool = tool;
			if (click == Util::OptionCardClick::Action)
				actionTool = tool;
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (actionTool) {
		switch (*actionTool) {
		case Tool::RenderDoc:
			if (renderDocActive) {
				renderDoc.CaptureNow();
			} else {
				renderDoc.SetCaptureEnabled(!renderDoc.enableRenderDocCapture);
				globals::state->InvalidateEditsUnderBaselines(this);
				globals::state->CheckFeatureSettingsChanged(this);
			}
			break;
		case Tool::NsightPerf:
			if (nvperfBusy)
				NvPerf::CancelCapture();
			else
				remoteControl.RequestNvPerfCapture();
			break;
		case Tool::DevBench:
			if (devbenchPresent)
				ImGui::SetClipboardText(std::format("http://127.0.0.1:{}", devbenchPort).c_str());
			else
				OpenDevBenchNexusPage();
			break;
		default:
			break;
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	switch (selectedTool) {
	case Tool::RenderDoc:
		renderDoc.DrawSettings();
		break;
	case Tool::NsightPerf:
		remoteControl.DrawNvPerfPanel();
		break;
	case Tool::DevBench:
		if (Icons::LabeledButton("##DevBenchNexus", Icons::TI(ICON_TI_EXTERNAL_LINK), T(TKEY("devbench_nexus"), "devbench on Nexus Mods")))
			OpenDevBenchNexusPage();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", kDevBenchNexusUrl);
		ImGui::Spacing();
		remoteControl.DrawDevBenchPanel();
		break;
	default:
		break;
	}
}

#undef I18N_KEY_PREFIX
