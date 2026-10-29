#include "SceneManager.h"

#include <imgui.h>

#include "../EditorWindow.h"
#include "FeatureOverwritesPanel.h"
#include "Features/CSEditor.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/SceneActionIcons.h"
#include "Menu/MenuIcons.h"
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
			Util::Text::Success(T("cs_editor.debug.yes", "yes"));
		else
			Util::Text::Disabled(T("cs_editor.debug.no", "no"));
	}

	std::string FormatGameHour(float hour)
	{
		const auto wholeHours = static_cast<int>(hour);
		const auto minutes = static_cast<int>((hour - static_cast<float>(wholeHours)) * 60.0f);
		return std::format("{:.4f} ({:02d}:{:02d})", hour, wholeHours, minutes);
	}

	void DrawPeriodHeaders()
	{
		for (int period = 0; period < SceneSettingsManager::kPeriodCount; ++period)
			ImGui::TableSetupColumn(SceneSettingsManager::GetPeriodDisplayName(static_cast<SceneSettingsManager::TimeOfDayPeriod>(period)));
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
			Util::Text::Disabled(T("cs_editor.debug.no_entries", "No entries"));
			return;
		}
		if (!ImGui::BeginTable(id, 8, kDebugTableFlags))
			return;

		for (const auto* header : { T("cs_editor.debug.feature", "Feature"), T("cs_editor.debug.path", "Path"), T("cs_editor.debug.setting", "Setting"), T("cs_editor.debug.value", "Value"), T("cs_editor.debug.period", "Period"), T("cs_editor.debug.transition", "Transition"), T("cs_editor.debug.source", "Source"), T("cs_editor.debug.state", "State") })
			ImGui::TableSetupColumn(header);
		ImGui::TableHeadersRow();

		for (const auto& entry : entries) {
			ImGui::TableNextRow();
			const std::string featureLabel = SceneSettingsManager::GetFeatureDisplayName(entry.feature);
			for (const auto* column : { &featureLabel, &entry.path, &entry.key, &entry.value, &entry.period }) {
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(column->empty() ? kMissingValue : column->c_str());
			}
			ImGui::TableNextColumn();
			if (entry.transitionSeconds)
				ImGui::Text("%.2fs", *entry.transitionSeconds);
			else
				Util::Text::Disabled(T("cs_editor.debug.global", "global"));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(entry.overwrite ? T("cs_editor.debug.overwrite", "overwrite") : T("cs_editor.debug.user", "user"));
			ImGui::TableNextColumn();
			if (entry.resolvable)
				Util::Text::Success(T("cs_editor.debug.resolvable", "resolvable"));
			else if (entry.paused)
				Util::Text::Warning(T("cs_editor.debug.paused", "paused"));
			else if (!entry.active)
				Util::Text::Warning(T("cs_editor.debug.feature_paused", "feature paused"));
			else
				Util::Text::Disabled(T("cs_editor.debug.not_resolvable", "not resolvable"));
		}
		ImGui::EndTable();
	}

	void DrawLayers(const std::vector<DebugLayer>& layers)
	{
		if (layers.empty()) {
			Util::Text::Disabled(T("cs_editor.debug.none", "None"));
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
			Util::Text::Disabled(T("cs_editor.debug.none", "None"));
			return;
		}
		for (const auto& name : names)
			ImGui::BulletText("%s", SceneSettingsManager::GetFeatureDisplayName(name).c_str());
	}

	void DrawSceneContext(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("SceneContext")) {
			DrawFlag(T("cs_editor.debug.player_and_cell_ready", "Player and cell ready"), snapshot.playerReady);
			DrawFlag(T("cs_editor.debug.interior", "Interior"), snapshot.interior);
			DrawFlag(T("cs_editor.debug.main_or_loading_menu_open", "Main or loading menu open"), snapshot.menuOpen);
			DrawField(T("cs_editor.debug.cell", "Cell"), snapshot.cellName);
			DrawField(T("cs_editor.debug.cell_editor_id", "Cell editor ID"), snapshot.cellEditorId);
			DrawFormIdField(T("cs_editor.debug.cell_form_id", "Cell form ID"), snapshot.cellId);
			DrawField(T("cs_editor.debug.location", "Location"), snapshot.locationName);
			DrawFormIdField(T("cs_editor.debug.location_form_id", "Location form ID"), snapshot.locationId);
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted(T("cs_editor.debug.location_targets_applied_in_order_later_", "Location targets (applied in order, later targets win)"));
		if (snapshot.locationTargets.empty()) {
			Util::Text::Disabled(T("cs_editor.debug.none", "None"));
			return;
		}
		if (ImGui::BeginTable("LocationTargets", 4, kDebugTableFlags)) {
			for (const auto* header : { T("cs_editor.debug.type", "Type"), T("cs_editor.debug.name", "Name"), T("cs_editor.debug.form_key", "Form key"), T("cs_editor.debug.coc", "COC") })
				ImGui::TableSetupColumn(header);
			ImGui::TableHeadersRow();
			for (const auto& target : snapshot.locationTargets) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(SceneSettingsManager::GetLocationTargetTypeDisplayName(target.type));
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
			DrawField(T("cs_editor.debug.game_hour", "Game hour"), FormatGameHour(snapshot.gameHour));
			DrawField(T("cs_editor.debug.current_period", "Current period"), SceneSettingsManager::GetPeriodDisplayName(snapshot.period));
			DrawField(T("cs_editor.debug.hour_at_last_resolve", "Hour at last resolve"),
				snapshot.lastHour < 0.0f ? std::string(kMissingValue) : FormatGameHour(snapshot.lastHour));
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted(T("cs_editor.debug.live_period_weights", "Live period weights"));
		DrawPeriodWeights("LiveWeights", snapshot.timeOfDayFactors);

		ImGui::Spacing();
		ImGui::TextUnformatted(T("cs_editor.debug.weights_used_by_the_last_resolve", "Weights used by the last resolve"));
		DrawPeriodWeights("ResolvedWeights", snapshot.blendFactors);
	}

	void DrawWeather(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("Weather")) {
			DrawField(T("cs_editor.debug.current_weather", "Current weather"), snapshot.currentWeatherName);
			DrawFormIdField(T("cs_editor.debug.current_weather_form_id", "Current weather form ID"), snapshot.weather.currentWeatherId);
			DrawField(T("cs_editor.debug.previous_weather", "Previous weather"), snapshot.previousWeatherName);
			DrawFormIdField(T("cs_editor.debug.previous_weather_form_id", "Previous weather form ID"), snapshot.weather.previousWeatherId);
			DrawField(T("cs_editor.debug.transition_lerp", "Transition lerp"), std::format("{:.4f}", snapshot.weather.lerp));
			DrawFormIdField(T("cs_editor.debug.current_at_last_resolve", "Current at last resolve"), snapshot.lastWeather.currentWeatherId);
			DrawFormIdField(T("cs_editor.debug.previous_at_last_resolve", "Previous at last resolve"), snapshot.lastWeather.previousWeatherId);
			DrawField(T("cs_editor.debug.lerp_at_last_resolve", "Lerp at last resolve"), std::format("{:.4f}", snapshot.lastWeather.lerp));
			ImGui::EndTable();
		}
		if (snapshot.interior)
			Util::Text::Disabled(T("cs_editor.debug.weather_is_not_blended_while_in_an_inter", "Weather is not blended while in an interior."));
	}

	void DrawResolverState(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("ResolverState")) {
			DrawFlag(T("cs_editor.debug.scene_data_loaded", "Scene data loaded"), snapshot.dataLoaded);
			DrawFlag(T("cs_editor.debug.weather_data_loaded", "Weather data loaded"), snapshot.weatherDataLoaded);
			DrawFlag(T("cs_editor.debug.location_data_loaded", "Location data loaded"), snapshot.locationDataLoaded);
			DrawFlag(T("cs_editor.debug.game_data_ready", "Game data ready"), snapshot.gameDataReady);
			DrawFlag(T("cs_editor.debug.resolver_suspended", "Resolver suspended"), snapshot.resolverSuspended);
			DrawFlag(T("cs_editor.debug.resolver_dirty", "Resolver dirty"), snapshot.resolverDirty);
			DrawFlag(T("cs_editor.debug.active_entry_cache_dirty", "Active entry cache dirty"), snapshot.activeEntryCacheDirty);
			DrawFlag(T("cs_editor.debug.has_active_scene_entries", "Has active scene entries"), snapshot.hasActiveSceneEntries);
			DrawFlag(T("cs_editor.debug.deferred_save_pending", "Deferred save pending"), snapshot.deferredSceneChangesPending);
			DrawField(T("cs_editor.debug.scene_layer_suspend_depth", "Scene layer suspend depth"), std::format("{}", snapshot.sceneLayerSuspendDepth));
			DrawFlag(T("cs_editor.debug.interior_at_last_resolve", "Interior at last resolve"), snapshot.lastInterior);
			DrawFormIdField(T("cs_editor.debug.cell_at_last_resolve", "Cell at last resolve"), snapshot.lastCellId);
			DrawFormIdField(T("cs_editor.debug.location_at_last_resolve", "Location at last resolve"), snapshot.lastLocationId);
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted(T("cs_editor.debug.features_failing_to_apply", "Features failing to apply"));
		DrawNameList(snapshot.applyFailures);

		ImGui::Spacing();
		ImGui::TextUnformatted(T("cs_editor.debug.features_failing_to_restore", "Features failing to restore"));
		DrawNameList(snapshot.restoreFailures);
	}

	void DrawLocationTransitions(const DebugSnapshot& snapshot)
	{
		if (BeginFieldTable("LocationTransitions")) {
			DrawField(T("cs_editor.debug.global_duration", "Global duration"), std::format("{:.2f}s", snapshot.globalTransitionSeconds));
			DrawField(T("cs_editor.debug.pause_aware_clock", "Pause-aware clock"), std::format("{:.3f}", snapshot.transitionTime));
			DrawField(T("cs_editor.debug.last_tick", "Last tick"),
				snapshot.lastTransitionTick < 0.0f ? std::string(kMissingValue) :
													 std::format("{:.3f} ({:.3f} ago)", snapshot.lastTransitionTick,
														 snapshot.transitionTime - snapshot.lastTransitionTick));
			DrawField(T("cs_editor.debug.active_transitions", "Active transitions"), std::format("{}", snapshot.locationTransitions.size()));
			DrawField(T("cs_editor.debug.feature_batches", "Feature batches"), std::format("{}", snapshot.transitionBatchCount));
			DrawFlag(T("cs_editor.debug.batches_dirty", "Batches dirty"), snapshot.transitionBatchesDirty);
			ImGui::EndTable();
		}

		ImGui::Spacing();
		if (snapshot.locationTransitions.empty()) {
			Util::Text::Disabled(T("cs_editor.debug.no_transitions_are_currently_easing", "No transitions are currently easing."));
		} else if (ImGui::BeginTable("ActiveTransitions", 8, kDebugTableFlags)) {
			for (const auto* header :
				{ T("cs_editor.debug.feature", "Feature"), T("cs_editor.debug.path", "Path"), T("cs_editor.debug.setting", "Setting"), T("cs_editor.debug.start", "Start"), T("cs_editor.debug.current", "Current"), T("cs_editor.debug.target", "Target"), T("cs_editor.debug.progress", "Progress"), T("cs_editor.debug.direction", "Direction") })
				ImGui::TableSetupColumn(header);
			ImGui::TableHeadersRow();
			for (const auto& transition : snapshot.locationTransitions) {
				ImGui::TableNextRow();
				const std::string featureLabel = SceneSettingsManager::GetFeatureDisplayName(transition.feature);
				for (const auto* column : { &featureLabel, &transition.path, &transition.key }) {
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
				ImGui::TextUnformatted(transition.restoreAtEnd ? T("cs_editor.debug.restoring", "restoring") : T("cs_editor.debug.applying", "applying"));
			}
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted(T("cs_editor.debug.features_failing_to_apply_transitions", "Features failing to apply transitions"));
		DrawNameList(snapshot.transitionApplyFailures);
	}

	void DrawPresets(const std::optional<SceneSettingsManager::PresetMetadata>& preset)
	{
		if (!preset) {
			Util::Text::Disabled(T("cs_editor.debug.no_active_preset_pack", "No active preset pack."));
			return;
		}
		if (!ImGui::BeginTable("Presets", 3, kDebugTableFlags))
			return;
		for (const auto* header : { T("cs_editor.debug.name", "Name"), T("cs_editor.debug.version", "Version"), T("cs_editor.debug.file", "File") })
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
			Util::Text::Disabled(T("cs_editor.debug.no_settings_are_currently_overridden", "No settings are currently overridden."));
			return;
		}

		for (size_t index = 0; index < snapshot.resolvedSettings.size(); ++index) {
			const auto& setting = snapshot.resolvedSettings[index];
			ImGui::PushID(static_cast<int>(index));
			const auto header = std::format("{}{}.{}: {} (base {})",
				SceneSettingsManager::GetFeatureDisplayName(setting.feature),
				setting.path.empty() ? "" : std::format(".{}", setting.path),
				setting.key, setting.applied, setting.baseline);
			if (ImGui::TreeNode(header.c_str())) {
				if (ImGui::BeginTable("PeriodValues", SceneSettingsManager::kPeriodCount + 1, kDebugTableFlags)) {
					ImGui::TableSetupColumn(T("cs_editor.debug.layer", "Layer"));
					DrawPeriodHeaders();
					DrawPeriodValues(T("cs_editor.debug.time_of_day", "Time of day"), setting.timeOfDayValues);
					DrawPeriodValues(T("cs_editor.debug.current_weather", "Current weather"), setting.currentWeatherValues);
					DrawPeriodValues(T("cs_editor.debug.previous_weather", "Previous weather"), setting.previousWeatherValues);
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
		ImGui::TextWrapped(T("cs_editor.debug.live_debug_view_of_the_scene_resolver_sa", "Live debug view of the scene resolver. Sampled every frame while this section is open."));
		ImGui::Indent();
		if (ImGui::CollapsingHeader(T("cs_editor.debug.scene_context", "Scene Context"), ImGuiTreeNodeFlags_DefaultOpen))
			DrawSceneContext(snapshot);
		if (ImGui::CollapsingHeader(T("cs_editor.debug.time_of_day_header", "Time of Day"), ImGuiTreeNodeFlags_DefaultOpen))
			DrawTimeOfDay(snapshot);
		if (ImGui::CollapsingHeader(T("cs_editor.debug.weather", "Weather"), ImGuiTreeNodeFlags_DefaultOpen))
			DrawWeather(snapshot);
		if (ImGui::CollapsingHeader(T("cs_editor.debug.resolver_state", "Resolver State")))
			DrawResolverState(snapshot);
		if (ImGui::CollapsingHeader(T("cs_editor.debug.location_transitions", "Location Transitions"), ImGuiTreeNodeFlags_DefaultOpen))
			DrawLocationTransitions(snapshot);
		if (ImGui::CollapsingHeader(T("cs_editor.debug.applied_settings", "Applied Settings"), ImGuiTreeNodeFlags_DefaultOpen))
			DrawResolvedSettings(snapshot);
		if (ImGui::CollapsingHeader(T("cs_editor.debug.presets", "Presets")))
			DrawPresets(manager.GetActivePresetMetadata());
		if (ImGui::CollapsingHeader(T("cs_editor.debug.scene_type_entries", "Scene Type Entries")))
			DrawLayers(snapshot.sceneLayers);
		if (ImGui::CollapsingHeader(T("cs_editor.debug.weather_entries", "Weather Entries")))
			DrawLayers(snapshot.weatherLayers);
		if (ImGui::CollapsingHeader(T("cs_editor.debug.location_entries", "Location Entries")))
			DrawLayers(snapshot.locationLayers);
		ImGui::Unindent();
	}

	/** @brief Buttons into the CS Editor pages that author scene settings, which only open in game. */
	void DrawEditorShortcuts()
	{
		if (Icons::LabeledButton("##SceneManagerOpenEditor", Icons::TI(MenuIcons::kCSEditor),
				T("feature.scene_manager.open_editor", "Open Scene Manager")))
			SceneSettingsUI::OpenSceneManagerPage();
		Util::AddTooltip(T("feature.scene_manager.open_editor_tooltip",
			"Open the CS Editor's Scene Manager page, to author time of day and interior settings."));

		ImGui::SameLine();
		if (Icons::LabeledButton("##SceneManagerOpenLocations", Icons::TI(MenuIcons::kLocations),
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
