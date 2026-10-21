#include "SceneManager.h"

#include <imgui.h>

#include "../EditorWindow.h"
#include "FeatureOverwritesPanel.h"
#include "Features/CSEditor.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/SceneActionIcons.h"
#include "Menu/PresetsPageRenderer.h"
#include "SceneLayerHeader.h"
#include "ScenePresetExport.h"
#include "SceneSettingsManager.h"
#include "SceneSettingsUI.h"
#include "Utils/UI.h"

// The debug view is developer-facing validation output and is intentionally not translated.
namespace
{
	using DebugEntry = SceneSettingsManager::DebugEntry;
	using DebugLayer = SceneSettingsManager::DebugLayer;
	using DebugSnapshot = SceneSettingsManager::DebugSnapshot;
	using PeriodValues = std::array<std::optional<float>, SceneSettingsManager::kPeriodCount>;
	using PeriodWeights = std::array<float, SceneSettingsManager::kPeriodCount>;

	constexpr ImGuiTableFlags kDebugTableFlags =
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

	/// Weights at or below this do not contribute to a blend.
	constexpr float kWeightEpsilon = 1e-4f;

	constexpr const char* kMissingValue = "-";

	/// Starts a two-column label/value table for a debug section.
	bool BeginFieldTable(const char* id)
	{
		return ImGui::BeginTable(id, 2, kDebugTableFlags);
	}

	/// Starts a field row with its label, leaving the value column current.
	void BeginFieldRow(const char* label)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();
	}

	void DrawField(const char* label, const std::string& value)
	{
		BeginFieldRow(label);
		ImGui::TextUnformatted(value.empty() ? kMissingValue : value.c_str());
	}

	void DrawFormIdField(const char* label, RE::FormID formId)
	{
		DrawField(label, std::format("{:08X}", formId));
	}

	void DrawFlag(const char* label, bool value)
	{
		BeginFieldRow(label);
		if (value)
			Util::Text::Success("yes");
		else
			Util::Text::Disabled("no");
	}

	std::string FormatGameHour(float hour)
	{
		const auto wholeHours = static_cast<int>(hour);
		const auto minutes = static_cast<int>((hour - static_cast<float>(wholeHours)) * 60.0f);
		return std::format("{:.4f} ({:02d}:{:02d})", hour, wholeHours, minutes);
	}

	void DrawPeriodHeaders()
	{
		for (const auto* name : SceneSettingsManager::kPeriodNames)
			ImGui::TableSetupColumn(name);
		ImGui::TableHeadersRow();
	}

	/// One row of per-period blend weights, greying out periods that contribute nothing.
	void DrawPeriodWeights(const char* id, const PeriodWeights& weights)
	{
		if (!ImGui::BeginTable(id, SceneSettingsManager::kPeriodCount, kDebugTableFlags))
			return;
		DrawPeriodHeaders();
		ImGui::TableNextRow();
		for (const auto weight : weights) {
			ImGui::TableNextColumn();
			if (weight > kWeightEpsilon)
				Util::Text::Success("%.3f", weight);
			else
				Util::Text::Disabled("%.3f", weight);
		}
		ImGui::EndTable();
	}

	/// One labelled row of per-period override values inside a 1 + period column table.
	void DrawPeriodValues(const char* label, const PeriodValues& values)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label);
		for (const auto& value : values) {
			ImGui::TableNextColumn();
			if (value)
				ImGui::Text("%.4f", *value);
			else
				Util::Text::Disabled(kMissingValue);
		}
	}

	void DrawEntryTable(const char* id, const std::vector<DebugEntry>& entries)
	{
		if (entries.empty()) {
			Util::Text::Disabled("No entries");
			return;
		}
		if (!ImGui::BeginTable(id, 8, kDebugTableFlags))
			return;

		for (const auto* header : { "Feature", "Path", "Setting", "Value", "Period", "Transition", "Source", "State" })
			ImGui::TableSetupColumn(header);
		ImGui::TableHeadersRow();

		for (const auto& entry : entries) {
			ImGui::TableNextRow();
			for (const auto* column : { &entry.feature, &entry.path, &entry.key, &entry.value, &entry.period }) {
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(column->empty() ? kMissingValue : column->c_str());
			}
			ImGui::TableNextColumn();
			if (entry.transitionSeconds)
				ImGui::Text("%.2fs", *entry.transitionSeconds);
			else
				Util::Text::Disabled("global");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(entry.overwrite ? "overwrite" : "user");
			ImGui::TableNextColumn();
			if (entry.resolvable)
				Util::Text::Success("resolvable");
			else if (entry.paused)
				Util::Text::Warning("paused");
			else if (!entry.active)
				Util::Text::Warning("feature paused");
			else
				Util::Text::Disabled("not resolvable");
		}
		ImGui::EndTable();
	}

	void DrawLayers(const std::vector<DebugLayer>& layers)
	{
		if (layers.empty()) {
			Util::Text::Disabled("None");
			return;
		}
		for (size_t index = 0; index < layers.size(); ++index) {
			const auto& layer = layers[index];
			ImGui::PushID(static_cast<int>(index));
			const auto header = std::format("{}{}{} - {} entries",
				layer.matchesCurrentScene ? "[ACTIVE] " : "",
				layer.name,
				layer.detail.empty() ? "" : std::format(" ({})", layer.detail),
				layer.entries.size());
			if (ImGui::TreeNode(header.c_str())) {
				DrawEntryTable("Entries", layer.entries);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}

	void DrawNameList(const std::vector<std::string>& names)
	{
		if (names.empty()) {
			Util::Text::Disabled("None");
			return;
		}
		for (const auto& name : names)
			ImGui::BulletText("%s", name.c_str());
	}

	void DrawSceneContext(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("SceneContext")) {
			DrawFlag("Player and cell ready", snapshot.playerReady);
			DrawFlag("Interior", snapshot.interior);
			DrawFlag("Main or loading menu open", snapshot.menuOpen);
			DrawField("Cell", snapshot.cellName);
			DrawField("Cell editor ID", snapshot.cellEditorId);
			DrawFormIdField("Cell form ID", snapshot.cellId);
			DrawField("Location", snapshot.locationName);
			DrawFormIdField("Location form ID", snapshot.locationId);
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Location targets (applied in order, later targets win)");
		if (snapshot.locationTargets.empty()) {
			Util::Text::Disabled("None");
			return;
		}
		if (ImGui::BeginTable("LocationTargets", 4, kDebugTableFlags)) {
			for (const auto* header : { "Type", "Name", "Form key", "COC" })
				ImGui::TableSetupColumn(header);
			ImGui::TableHeadersRow();
			for (const auto& target : snapshot.locationTargets) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(SceneSettingsManager::GetLocationTargetTypeName(target.type));
				for (const auto* column : { &target.name, &target.formKey, &target.cocCode }) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(column->empty() ? kMissingValue : column->c_str());
				}
			}
			ImGui::EndTable();
		}
	}

	void DrawTimeOfDay(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("TimeOfDay")) {
			DrawField("Game hour", FormatGameHour(snapshot.gameHour));
			DrawField("Current period", SceneSettingsManager::GetPeriodName(snapshot.period));
			DrawField("Hour at last resolve",
				snapshot.lastHour < 0.0f ? std::string(kMissingValue) : FormatGameHour(snapshot.lastHour));
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Live period weights");
		DrawPeriodWeights("LiveWeights", snapshot.timeOfDayFactors);

		ImGui::Spacing();
		ImGui::TextUnformatted("Weights used by the last resolve");
		DrawPeriodWeights("ResolvedWeights", snapshot.blendFactors);
	}

	void DrawWeather(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("Weather")) {
			DrawField("Current weather", snapshot.currentWeatherName);
			DrawFormIdField("Current weather form ID", snapshot.weather.currentWeatherId);
			DrawField("Previous weather", snapshot.previousWeatherName);
			DrawFormIdField("Previous weather form ID", snapshot.weather.previousWeatherId);
			DrawField("Transition lerp", std::format("{:.4f}", snapshot.weather.lerp));
			DrawFormIdField("Current at last resolve", snapshot.lastWeather.currentWeatherId);
			DrawFormIdField("Previous at last resolve", snapshot.lastWeather.previousWeatherId);
			DrawField("Lerp at last resolve", std::format("{:.4f}", snapshot.lastWeather.lerp));
			ImGui::EndTable();
		}
		if (snapshot.interior)
			Util::Text::Disabled("Weather is not blended while in an interior.");
	}

	void DrawResolverState(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("ResolverState")) {
			DrawFlag("Scene data loaded", snapshot.dataLoaded);
			DrawFlag("Weather data loaded", snapshot.weatherDataLoaded);
			DrawFlag("Location data loaded", snapshot.locationDataLoaded);
			DrawFlag("Game data ready", snapshot.gameDataReady);
			DrawFlag("Resolver suspended", snapshot.resolverSuspended);
			DrawFlag("Resolver dirty", snapshot.resolverDirty);
			DrawFlag("Active entry cache dirty", snapshot.activeEntryCacheDirty);
			DrawFlag("Has active scene entries", snapshot.hasActiveSceneEntries);
			DrawFlag("Deferred save pending", snapshot.deferredSceneChangesPending);
			DrawField("Scene layer suspend depth", std::format("{}", snapshot.sceneLayerSuspendDepth));
			DrawFlag("Interior at last resolve", snapshot.lastInterior);
			DrawFormIdField("Cell at last resolve", snapshot.lastCellId);
			DrawFormIdField("Location at last resolve", snapshot.lastLocationId);
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Features failing to apply");
		DrawNameList(snapshot.applyFailures);

		ImGui::Spacing();
		ImGui::TextUnformatted("Features failing to restore");
		DrawNameList(snapshot.restoreFailures);
	}

	void DrawLocationTransitions(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("LocationTransitions")) {
			DrawField("Global duration", std::format("{:.2f}s", snapshot.globalTransitionSeconds));
			DrawField("Pause-aware clock", std::format("{:.3f}", snapshot.transitionTime));
			DrawField("Last tick",
				snapshot.lastTransitionTick < 0.0f ? std::string(kMissingValue) :
													 std::format("{:.3f} ({:.3f} ago)", snapshot.lastTransitionTick,
														 snapshot.transitionTime - snapshot.lastTransitionTick));
			DrawField("Active transitions", std::format("{}", snapshot.locationTransitions.size()));
			DrawField("Feature batches", std::format("{}", snapshot.transitionBatchCount));
			DrawFlag("Batches dirty", snapshot.transitionBatchesDirty);
			ImGui::EndTable();
		}

		ImGui::Spacing();
		if (snapshot.locationTransitions.empty()) {
			Util::Text::Disabled("No transitions are currently easing.");
		} else if (ImGui::BeginTable("ActiveTransitions", 8, kDebugTableFlags)) {
			for (const auto* header :
				{ "Feature", "Path", "Setting", "Start", "Current", "Target", "Progress", "Direction" })
				ImGui::TableSetupColumn(header);
			ImGui::TableHeadersRow();
			for (const auto& transition : snapshot.locationTransitions) {
				ImGui::TableNextRow();
				for (const auto* column : { &transition.feature, &transition.path, &transition.key }) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(column->empty() ? kMissingValue : column->c_str());
				}
				ImGui::TableNextColumn();
				ImGui::Text("%.4f", transition.startValue);
				ImGui::TableNextColumn();
				ImGui::Text("%.4f", transition.currentValue);
				ImGui::TableNextColumn();
				ImGui::Text("%.4f", transition.targetValue);
				ImGui::TableNextColumn();
				ImGui::Text("%.0f%% of %.2fs", transition.progress * 100.0f, transition.duration);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(transition.restoreAtEnd ? "restoring" : "applying");
			}
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Features failing to apply transitions");
		DrawNameList(snapshot.transitionApplyFailures);
	}

	void DrawPresets(const std::optional<SceneSettingsManager::PresetMetadata>& preset)
	{
		if (!preset) {
			Util::Text::Disabled("No active preset pack.");
			return;
		}
		if (!ImGui::BeginTable("Presets", 3, kDebugTableFlags))
			return;
		for (const auto* header : { "Name", "Version", "File" })
			ImGui::TableSetupColumn(header);
		ImGui::TableHeadersRow();
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(preset->name.c_str());
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(preset->version.c_str());
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(preset->path.string().c_str());
		ImGui::EndTable();
	}

	void DrawResolvedSettings(const DebugSnapshot& snapshot)
	{
		if (snapshot.resolvedSettings.empty()) {
			Util::Text::Disabled("No settings are currently overridden.");
			return;
		}

		for (size_t index = 0; index < snapshot.resolvedSettings.size(); ++index) {
			const auto& setting = snapshot.resolvedSettings[index];
			ImGui::PushID(static_cast<int>(index));
			const auto header = std::format("{}{}.{}: {} (base {})",
				setting.feature,
				setting.path.empty() ? "" : std::format(".{}", setting.path),
				setting.key, setting.applied, setting.baseline);
			if (ImGui::TreeNode(header.c_str())) {
				if (ImGui::BeginTable("PeriodValues", SceneSettingsManager::kPeriodCount + 1, kDebugTableFlags)) {
					ImGui::TableSetupColumn("Layer");
					DrawPeriodHeaders();
					DrawPeriodValues("Time of day", setting.timeOfDayValues);
					DrawPeriodValues("Current weather", setting.currentWeatherValues);
					DrawPeriodValues("Previous weather", setting.previousWeatherValues);
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}

	/** @brief Every resolver section, sampled only while the Debug header is open. */
	void DrawDebug(const SceneSettingsManager& manager)
	{
		const auto snapshot = manager.GetDebugSnapshot();
		ImGui::TextWrapped("Live debug view of the scene resolver. Sampled every frame while this section is open.");
		ImGui::Indent();
		if (ImGui::CollapsingHeader("Scene Context", ImGuiTreeNodeFlags_DefaultOpen))
			DrawSceneContext(snapshot);
		if (ImGui::CollapsingHeader("Time of Day", ImGuiTreeNodeFlags_DefaultOpen))
			DrawTimeOfDay(snapshot);
		if (ImGui::CollapsingHeader("Weather", ImGuiTreeNodeFlags_DefaultOpen))
			DrawWeather(snapshot);
		if (ImGui::CollapsingHeader("Resolver State"))
			DrawResolverState(snapshot);
		if (ImGui::CollapsingHeader("Location Transitions", ImGuiTreeNodeFlags_DefaultOpen))
			DrawLocationTransitions(snapshot);
		if (ImGui::CollapsingHeader("Applied Settings", ImGuiTreeNodeFlags_DefaultOpen))
			DrawResolvedSettings(snapshot);
		if (ImGui::CollapsingHeader("Presets"))
			DrawPresets(manager.GetActivePresetMetadata());
		if (ImGui::CollapsingHeader("Scene Type Entries"))
			DrawLayers(snapshot.sceneLayers);
		if (ImGui::CollapsingHeader("Weather Entries"))
			DrawLayers(snapshot.weatherLayers);
		if (ImGui::CollapsingHeader("Location Entries"))
			DrawLayers(snapshot.locationLayers);
		ImGui::Unindent();
	}

	/** @brief Buttons into the CS Editor pages that author scene settings, which only open in game. */
	void DrawEditorShortcuts()
	{
		if (Icons::LabeledButton("##SceneManagerOpenEditor", Icons::TI(ICON_TI_BRUSH),
				T("feature.scene_manager.open_editor", "Open Scene Manager")))
			SceneSettingsUI::OpenSceneManagerPage();
		Util::AddTooltip(T("feature.scene_manager.open_editor_tooltip",
			"Open the CS Editor's Scene Manager page, to author time of day and interior settings."));

		ImGui::SameLine();
		if (Icons::LabeledButton("##SceneManagerOpenLocations", Icons::TI(ICON_TI_MAP_PIN),
				T("feature.scene_manager.open_locations", "Locations")))
			SceneSettingsUI::OpenLocationsPage();
		Util::AddTooltip(T("feature.scene_manager.open_locations_tooltip", "Open the CS Editor's Locations page, to add places to override."));

		ImGui::SameLine();
		{
			Util::DisableGuard disable(!ScenePresetExport::CanExport());
			if (Icons::LabeledButton("##SceneManagerExportPreset", SceneActionIcons::kExport,
					T("cs_editor.export_preset", "Export Preset..."))) {
				CSEditor::OpenEditorWindow();
				// The export dialog is drawn by the editor window, so it only opens once the editor has.
				if (EditorWindow::GetSingleton()->open)
					ScenePresetExport::Open();
			}
		}
		Util::AddTooltip(ScenePresetExport::GetSummary(), Util::kTooltipWhenDisabled);
	}

	/** @brief What applies now with links into the CS Editor, or a hint while no save is loaded. */
	void DrawApplyingNow()
	{
		if (EditorWindow::CanBeOpen()) {
			SceneLayerHeader::DrawSummary();
			DrawEditorShortcuts();
			ImGui::SameLine();
		} else {
			Util::Text::WrappedDisabled("%s",
				T("feature.scene_manager.not_in_game", "Load a save to see what applies here and to open the CS Editor."));
		}
		PresetsPageRenderer::DrawOpenButton("##SceneManagerOpenPresets",
			T("feature.scene_manager.open_presets_tooltip", "Apply scene presets on the Presets page."));
	}

	/** @brief Reset button on the line of the control before it. @return Whether it was clicked. */
	bool DrawResetButton(const char* label, const char* tooltip)
	{
		ImGui::SameLine();
		const bool clicked = Util::WarningButton(label);
		Util::AddTooltip(tooltip);
		return clicked;
	}
}

std::pair<std::string, std::vector<std::string>> SceneManager::GetFeatureSummary()
{
	return {
		T("feature.scene_manager.description",
			"Applies selected Community Shaders settings by interior, time of day, weather, and location."),
		{
			T("feature.scene_manager.key_feature_1", "Blends exterior settings across time of day and weather transitions"),
			T("feature.scene_manager.key_feature_2", "Applies interior settings separately from exterior settings"),
			T("feature.scene_manager.key_feature_3", "Supports location and cell overrides with per-setting precedence"),
		},
	};
}

void SceneManager::DrawSettings()
{
	Util::DrawSectionHeader(T("feature.scene_manager.applying_now", "Applying Now"), false, false);
	DrawApplyingNow();

	Util::DrawSectionHeader(T("feature.scene_manager.transitions", "Transitions"), false, false);
	const auto drawTransitionControl = [this](TransitionControl control, const char* label, const char* format,
										   ImGuiSliderFlags flags, const char* tooltip, const char* resetLabel) {
		const auto& spec = GetTransitionControlSpec(control);
		auto value = GetTransitionControl(control);
		if (ImGui::SliderFloat(label, &value, spec.min, spec.max, format, ImGuiSliderFlags_AlwaysClamp | flags))
			SetTransitionControl(control, value, true);
		// A drag held still would otherwise outlive the debounce and write the file mid-gesture.
		if (ImGui::IsItemActive())
			HoldDeferredSceneChanges();
		Util::AddTooltip(tooltip);
		if (GetUserTransitionControl(control) &&
			DrawResetButton(resetLabel, T("feature.scene_manager.transition_reset_tooltip",
											"Drop your value and use the active preset's, or the default when it sets none.")))
			SetTransitionControl(control, std::nullopt);
	};

	drawTransitionControl(TransitionControl::TimeOfDayHours,
		T("feature.scene_manager.time_of_day_transition", "Time of Day Transition"), "%.2f h", ImGuiSliderFlags_None,
		T("feature.scene_manager.time_of_day_transition_tooltip",
			"Hours spent blending between time of day periods, kept inside the sky's own colour transitions.\n"
			"0 switches between periods instantly.\n"
			"The active preset supplies this until you set your own."),
		T("feature.scene_manager.time_of_day_transition_reset", "Reset##TimeOfDayTransition"));
	drawTransitionControl(TransitionControl::LocationSeconds,
		T("feature.scene_manager.location_transition", "Location Transition"), "%.1f s", ImGuiSliderFlags_Logarithmic,
		T("feature.scene_manager.location_transition_tooltip",
			"Seconds location overrides take to ease in and out when you move between places.\n"
			"A single setting can override this from its own transition field in the CS Editor.\n"
			"The active preset supplies this until you set your own."),
		T("feature.scene_manager.location_transition_reset", "Reset##LocationTransition"));
	drawTransitionControl(TransitionControl::WeatherScale,
		T("feature.scene_manager.weather_transition_length", "Weather Transition Length"), "%.2fx", ImGuiSliderFlags_Logarithmic,
		T("feature.scene_manager.weather_transition_length_tooltip",
			"Multiplies how long every weather takes to blend into the next, keeping each weather's own pacing.\n"
			"A longer transition also delays a queued weather change, which waits for the running one to finish.\n"
			"The active preset supplies this until you set your own."),
		T("feature.scene_manager.weather_transition_length_reset", "Reset##WeatherTransitionLength"));
	drawTransitionControl(TransitionControl::Ease,
		T("feature.scene_manager.transition_ease", "Transition Easing"), "%.2f", ImGuiSliderFlags_None,
		T("feature.scene_manager.transition_ease_tooltip",
			"Softens the start and end of time of day, location and weather transitions, sky included.\n"
			"0 keeps a constant-speed blend.\n"
			"The active preset supplies this until you set your own."),
		T("feature.scene_manager.transition_ease_reset", "Reset##TransitionEase"));

	const bool overwritesOpen = ImGui::CollapsingHeader(T("feature.scene_manager.overwrites.title", "Feature Overwrites"), ImGuiTreeNodeFlags_DefaultOpen);
	Util::AddTooltip(T("feature.scene_manager.overwrites.title_tooltip",
		"Files that set feature settings at startup: your own exports, files shipped by mods, and those of applied preset packs."));
	if (overwritesOpen)
		FeatureOverwritesPanel::Draw();
	if (Util::ShowDebugSections() && ImGui::CollapsingHeader(T("feature.scene_manager.debug", "Debug")))
		DrawDebug(*this);
}

void SceneManager::SetupResources()
{
	LoadAll();
}

void SceneManager::DataLoaded()
{
	SceneSettingsManager::OnDataLoaded();
	MenuOpenCloseEventHandler::Register();
}

void SceneManager::Update()
{
	SceneSettingsManager::Update();
}
