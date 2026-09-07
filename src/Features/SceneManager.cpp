#include "SceneManager.h"

#include <imgui.h>

#include "SceneSettingsManager.h"
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

	void DrawField(const char* label, const std::string& value)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(value.empty() ? kMissingValue : value.c_str());
	}

	void DrawFlag(const char* label, bool value)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();
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
			DrawField("Cell form ID", std::format("{:08X}", snapshot.cellId));
			DrawField("Location", snapshot.locationName);
			DrawField("Location form ID", std::format("{:08X}", snapshot.locationId));
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
			DrawField("Current weather form ID", std::format("{:08X}", snapshot.weather.currentWeatherId));
			DrawField("Previous weather", snapshot.previousWeatherName);
			DrawField("Previous weather form ID", std::format("{:08X}", snapshot.weather.previousWeatherId));
			DrawField("Transition lerp", std::format("{:.4f}", snapshot.weather.lerp));
			DrawField("Current at last resolve",
				std::format("{:08X}", snapshot.lastWeather.currentWeatherId));
			DrawField("Previous at last resolve",
				std::format("{:08X}", snapshot.lastWeather.previousWeatherId));
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
			DrawField("Cell at last resolve", std::format("{:08X}", snapshot.lastCellId));
			DrawField("Location at last resolve", std::format("{:08X}", snapshot.lastLocationId));
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Features paused by the user");
		DrawNameList(snapshot.pausedFeatures);

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
	const auto snapshot = GetDebugSnapshot();

	ImGui::TextWrapped("Live debug view of the scene resolver. Sampled every frame while this panel is open.");
	ImGui::Spacing();

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
	if (ImGui::CollapsingHeader("Scene Type Entries"))
		DrawLayers(snapshot.sceneLayers);
	if (ImGui::CollapsingHeader("Weather Entries"))
		DrawLayers(snapshot.weatherLayers);
	if (ImGui::CollapsingHeader("Location Entries"))
		DrawLayers(snapshot.locationLayers);
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
