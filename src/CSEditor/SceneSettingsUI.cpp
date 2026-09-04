#include "SceneSettingsUI.h"

#include <algorithm>
#include <cstring>
#include <format>

#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "Menu.h"
#include "SceneFeatureReplica.h"
#include "ScenePageToolbar.h"
#include "SceneSettingsManager.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include "WeatherUtils.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	using TimeOfDayPeriod = SceneSettingsManager::TimeOfDayPeriod;
	constexpr int kPeriodCount = SceneSettingsManager::kPeriodCount;

	/// Game-hour delta that counts as a deliberate scrub. Deltas below it accumulate rather than
	/// reset the baseline, so time running at any timescale still re-couples the bar.
	constexpr float kScrubEpsilon = 1e-3f;

	/// Period labels are centered so the row reads as one segmented control.
	constexpr ImVec2 kSegmentTextAlign{ 0.5f, 0.5f };

	constexpr ImGuiTableFlags kPeriodBarFlags =
		ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV;

	/// Starting width of the weather tab's feature column at the baseline font size; the user can drag it.
	constexpr float kFeatureListWidth = 180.0f;

	/// The divider doubles as the resize grip, so the feature column and the panel share one border.
	constexpr ImGuiTableFlags kFeatureLayoutFlags =
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;

	/// The objects window nests its list inside the category list, so it reads as a sub-level.
	constexpr float kNestedFeatureFontScale = 0.85f;

	/// Name and editor ID share the slack; the type and the trailing action are fixed and narrow.
	constexpr float kLocationTypeColumnWidth = 90.0f;
	constexpr float kLocationAddColumnWidth = 60.0f;
	constexpr float kLocationRemoveColumnWidth = 40.0f;
	/// Matches the weather list's JSON delete icon, which sits a little inside the row height.
	constexpr float kRemoveIconScale = 0.85f;
	/// Outlined like the weather editor's lists, which frame their rows the same way.
	constexpr ImGuiTableFlags kLocationTableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
	/// Only the user's list sorts: the chain's order is its hierarchy, outermost first.
	constexpr ImGuiTableFlags kAuthoredLocationTableFlags = kLocationTableFlags | ImGuiTableFlags_Sortable;

	/// Identifies the clicked header in the sort specs, which go by user ID rather than position.
	enum LocationColumnId
	{
		LocationColumnName,
		LocationColumnEditorId,
		LocationColumnType,
		LocationColumnAction
	};

	/// Location windows share their size with each other, like the form widgets do per type.
	constexpr const char* kLocationWidgetType = "SceneLocation";

	/// One bar shared by every panel: it tracks live time, which is global.
	struct PeriodBarState
	{
		int selected = -1;       // -1 follows the live period
		float lastHour = -1.0f;  // game hour the coupling was last judged against
	};
	PeriodBarState periodBar;

	/// Weather pages are flat until the user asks for periods; the bar only takes over time, and
	/// pauses the game, once they do. The Scene Manager panel has no flat mode and never shows this.
	bool weatherTimeOfDayEnabled = false;

	/// The Scene Manager panel edits interior and time of day, and only one of them resolves at a
	/// time, so it follows the player instead of being matched to the cell by hand.
	bool interiorEnabled = false;
	bool lastInterior = false;

	/// Re-arms the interior layer on a cell transition.
	void SyncSceneToggles()
	{
		const bool interior = Util::IsInterior();
		if (interior != lastInterior) {
			lastInterior = interior;
			interiorEnabled = interior;
		}
	}

	/// A selectable feature, with its label built once because the list is fixed after boot.
	struct FeatureListEntry
	{
		std::string shortName;
		std::string label;
	};

	/// Each panel keeps its own selection: several can be on screen at once.
	std::string weatherSelectedFeature;
	std::string panelSelectedFeature;

	/// A location the user opened for editing. Locations are not forms in the widget system, so the
	/// editor tracks its own windows instead of going through Widget.
	struct LocationWindow
	{
		SceneSettingsManager::LocationTarget target;
		std::string selectedFeature;
		bool open = true;
		bool pendingFocus = false;
	};
	std::vector<LocationWindow> locationWindows;

	// Latch driving the automatic time pause: raised by any page editing a period this frame.
	bool periodEditingThisFrame = false;
	bool wasEditingTimeOfDay = false;
	bool pausedByPanel = false;

	const char* GetPeriodLabel(int period)
	{
		switch (static_cast<TimeOfDayPeriod>(period)) {
		case TimeOfDayPeriod::Dawn:
			return T(TKEY("tod_dawn"), "Dawn");
		case TimeOfDayPeriod::Sunrise:
			return T(TKEY("tod_sunrise"), "Sunrise");
		case TimeOfDayPeriod::Day:
			return T(TKEY("tod_day"), "Day");
		case TimeOfDayPeriod::Sunset:
			return T(TKEY("tod_sunset"), "Sunset");
		case TimeOfDayPeriod::Dusk:
			return T(TKEY("tod_dusk"), "Dusk");
		default:
			return T(TKEY("tod_night"), "Night");
		}
	}

	/// The context one page edits: the base context, narrowed to the period the bar has selected.
	SceneSettingsManager::SceneContextId ResolvePageContext(
		const SceneSettingsManager::SceneContextId& baseContext, TimeOfDayPeriod period)
	{
		auto context = baseContext;
		// The interior and location layers are aperiodic, so the bar informs the view but not the context.
		if (SceneSettingsManager::IsPeriodicContext(context.type))
			context.period = period;
		return context;
	}

	/// Whether a page edits one period at a time. The Scene Manager panel has no flat mode, so it
	/// always does, except indoors where the aperiodic interior layer takes the panel over.
	bool ResolvePeriodEditing(bool sceneManagerPanel)
	{
		return sceneManagerPanel ? !interiorEnabled : weatherTimeOfDayEnabled;
	}

	/// How much of a page an action owns: a flat periodic page writes every period at once, so the
	/// page-wide actions have to cover every period too.
	SceneSettingsManager::PeriodScope ResolvePeriodScope(
		const SceneSettingsManager::SceneContextId& context, bool periodEditing)
	{
		return SceneSettingsManager::IsPeriodicContext(context.type) && !periodEditing ?
		           SceneSettingsManager::PeriodScope::AllPeriods :
		           SceneSettingsManager::PeriodScope::ActivePeriod;
	}

	/// Draws the period bar and the page toolbar, and returns the period the panel below it should
	/// edit. The Scene Manager panel is the only place both layers meet, so it is the only one that
	/// shows the interior indicator, and it takes the toggle's place there.
	int DrawPeriodBar(const SceneSettingsManager::SceneContextId& baseContext, bool editing,
		bool sceneManagerPanel)
	{
		const bool interior = sceneManagerPanel && interiorEnabled;
		const int live = static_cast<int>(SceneSettingsManager::GetCurrentPeriod());

		if (editing) {
			const float hour = SceneSettingsManager::GetCurrentGameHour();
			if (std::abs(hour - periodBar.lastHour) > kScrubEpsilon) {
				periodBar.selected = -1;
				periodBar.lastHour = hour;
			}
		} else if (periodBar.selected < 0) {
			// Pin the follow state so a disabled bar stops reacting to time entirely.
			periodBar.selected = live;
		}

		const int active = periodBar.selected < 0 ? live : periodBar.selected;

		ImGui::BeginDisabled(!editing);
		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, kSegmentTextAlign);
		if (ImGui::BeginTable("PeriodBar", kPeriodCount, kPeriodBarFlags)) {
			for (int period = 0; period < kPeriodCount; ++period) {
				ImGui::TableNextColumn();
				// Colour the live period only when the selection has left it, so the bar always
				// shows where time actually is.
				const bool marksLive = editing && period == live && period != active;
				if (marksLive)
					ImGui::PushStyleColor(ImGuiCol_Text, Menu::GetSingleton()->GetTheme().StatusPalette.CurrentHotkey);
				if (ImGui::Selectable(GetPeriodLabel(period), period == active)) {
					periodBar.selected = period;
					// Jump time into the middle of the period so the scene shows what is being edited.
					// The baseline moves with it, or the scrub check would drop straight back to following.
					periodBar.lastHour = SceneSettingsManager::GetPeriodMidHour(static_cast<TimeOfDayPeriod>(period));
					SceneSettingsManager::SetGameHour(periodBar.lastHour);
				}
				if (marksLive)
					ImGui::PopStyleColor();
			}
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		ImGui::EndDisabled();

		if (sceneManagerPanel) {
			// Indicator only: interior always follows the cell the player is actually in, so
			// toggling it by hand would silently do nothing when the layer can't resolve.
			ImGui::BeginDisabled();
			ImGui::Checkbox(T(TKEY("interior_toggle"), "Interior"), &interiorEnabled);
			ImGui::EndDisabled();
			Util::AddTooltip(T(TKEY("interior_toggle_tooltip"),
				"Shows whether interior settings are being edited. Follows the cell the player is in."),
				Util::kTooltipWhenDisabled);
		} else {
			// Enabling re-couples the bar to live time, so it always lands on the current period.
			if (ImGui::Checkbox(T(TKEY("time_of_day_toggle"), "Time of Day"), &weatherTimeOfDayEnabled) && weatherTimeOfDayEnabled) {
				periodBar.selected = -1;
				periodBar.lastHour = SceneSettingsManager::GetCurrentGameHour();
			}
			Util::AddTooltip(T(TKEY("time_of_day_toggle_tooltip"),
				"Edit one period at a time instead of the whole page at once. Game time pauses while a period is being edited."),
				ImGuiHoveredFlags_DelayNormal);
		}

		ImGui::SameLine();
		const auto pageContext = ResolvePageContext(baseContext, static_cast<TimeOfDayPeriod>(active));
		ScenePageToolbar::Draw(pageContext, ResolvePeriodScope(pageContext, editing));

		if (interior)
			Util::Text::Disabled("%s", T(TKEY("period_bar_interior"), "Time of day editing is unavailable indoors."));
		else if (!editing)
			Util::Text::Disabled("%s", T(TKEY("period_bar_off"), "Time of day editing is off. The bar does not follow game time."));
		else if (periodBar.selected < 0)
			Util::Text::Disabled("%s", T(TKEY("period_bar_following"), "Following the time of day. Click a period to jump to it and edit it on its own."));
		else
			Util::Text::Disabled(T(TKEY("period_bar_manual"), "Editing %s. Scrub the time of day to follow it again."), GetPeriodLabel(active));

		return active;
	}

	/// Scene-capable features, resolved once: loaded features and the catalog are fixed after boot.
	/// Transitionable-only is the weather and time-of-day set; the rest also covers interior and location.
	const std::vector<FeatureListEntry>& GetFeatureEntries(bool transitionableOnly)
	{
		auto build = [](const std::vector<std::string>& names) {
			std::vector<FeatureListEntry> entries;
			entries.reserve(names.size());
			for (const auto& name : names)
				entries.push_back({ name, std::format("{}##{}", SceneSettingsManager::GetFeatureDisplayName(name), name) });
			return entries;
		};
		static const std::vector<FeatureListEntry> transitionable = build(SceneSettingsManager::GetExteriorRelevantFeatureNames());
		static const std::vector<FeatureListEntry> all = build(SceneSettingsManager::GetLocationRelevantFeatureNames());
		return transitionableOnly ? transitionable : all;
	}

	/// Draws the feature selectables and returns the feature the panel below should edit.
	const std::string& DrawFeatureList(std::string& selected, const std::vector<FeatureListEntry>& entries)
	{
		if (entries.empty()) {
			selected.clear();
			Util::Text::WrappedDisabled("%s",
				T(TKEY("scene_feature_list_empty"), "No loaded feature exposes scene settings."));
			return selected;
		}

		if (std::ranges::none_of(entries, [&](const auto& entry) { return entry.shortName == selected; }))
			selected = entries.front().shortName;

		for (const auto& entry : entries) {
			if (ImGui::Selectable(entry.label.c_str(), entry.shortName == selected))
				selected = entry.shortName;
		}
		return selected;
	}

	/// Period bar, intro, and the replicated feature UI bound to one scene context.
	void DrawPanel(const char* intro, const std::string& selectedFeature,
		const SceneSettingsManager::SceneContextId& baseContext, bool withPeriodBar = true,
		bool sceneManagerPanel = false)
	{
		auto context = baseContext;
		bool periodEditing = false;
		if (withPeriodBar) {
			periodEditing = ResolvePeriodEditing(sceneManagerPanel);
			periodEditingThisFrame |= periodEditing;
			const auto period = static_cast<TimeOfDayPeriod>(
				DrawPeriodBar(baseContext, periodEditing, sceneManagerPanel));
			context = ResolvePageContext(baseContext, period);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		} else {
			// A page without the bar has no toggle row to share, so the actions get a row of their own.
			ScenePageToolbar::Draw(context, ResolvePeriodScope(context, periodEditing));
		}
		ImGui::TextWrapped("%s", intro);
		// Greying covers both what no scene can hold and what only another scene type can.
		Util::Text::WrappedDisabled("%s", T(TKEY("scene_settings_greyed_note"),
			"Greyed settings cannot be overridden here. Some cannot be overridden by any scene; others need a different kind, such as a location override."));
		ImGui::Spacing();

		if (selectedFeature.empty())
			return;
		const bool perPeriod = periodEditing && SceneSettingsManager::IsPeriodicContext(context.type);
		SceneFeatureReplica::Draw(selectedFeature, context, perPeriod);
	}

	/// Feature column beside the panel body, split by a divider the user can drag.
	/// Transitionable features are the per-period set; the rest also covers interior and location.
	void DrawFeatureLayout(std::string& selectedFeature, bool transitionableOnly, const char* intro,
		bool withPeriodBar, const SceneSettingsManager::SceneContextId& baseContext)
	{
		if (!ImGui::BeginTable("SceneFeatureLayout", 2, kFeatureLayoutFlags))
			return;

		ImGui::TableSetupColumn("##Features", ImGuiTableColumnFlags_WidthFixed, kFeatureListWidth * Util::GetUIScale());
		ImGui::TableSetupColumn("##Body", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();

		// Each column scrolls on its own, so a long feature list never drags the panel with it.
		ImGui::TableSetColumnIndex(0);
		if (ImGui::BeginChild("##SceneFeatureList")) {
			ImGui::Text("%s", T(TKEY("scene_feature_list_title"), "Features"));
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			DrawFeatureList(selectedFeature, GetFeatureEntries(transitionableOnly));
		}
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(1);
		if (ImGui::BeginChild("##SceneFeatureBody"))
			DrawPanel(intro, selectedFeature, baseContext, withPeriodBar);
		ImGui::EndChild();

		ImGui::EndTable();
	}

	const char* GetLocationTypeLabel(SceneSettingsManager::LocationTargetType type)
	{
		switch (type) {
		case SceneSettingsManager::LocationTargetType::Region:
			return T(TKEY("location_type_region"), "Region");
		case SceneSettingsManager::LocationTargetType::Cell:
			return T(TKEY("location_type_cell"), "Cell");
		default:
			return T(TKEY("location_type_location"), "Location");
		}
	}

	/// Opens a location's editor window, focusing the existing one rather than opening a second.
	void OpenLocationWindow(const SceneSettingsManager::LocationTarget& target)
	{
		auto existing = std::ranges::find_if(locationWindows, [&](const auto& window) {
			return window.target.type == target.type && window.target.formKey == target.formKey;
		});
		if (existing != locationWindows.end()) {
			existing->open = true;
			existing->pendingFocus = true;
			return;
		}
		locationWindows.push_back({ .target = target });
	}

	/// Both location tables identify a target the same way; only the trailing action differs.
	void SetupLocationColumns(float actionWidth)
	{
		const float scale = Util::GetUIScale();
		ImGui::TableSetupColumn(T(TKEY("location_column_name"), "Name"), ImGuiTableColumnFlags_WidthStretch, 0.0f, LocationColumnName);
		ImGui::TableSetupColumn(T(TKEY("location_column_editor_id"), "Editor ID"), ImGuiTableColumnFlags_WidthStretch, 0.0f, LocationColumnEditorId);
		ImGui::TableSetupColumn(T(TKEY("location_column_type"), "Type"), ImGuiTableColumnFlags_WidthFixed, kLocationTypeColumnWidth * scale, LocationColumnType);
		ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, actionWidth * scale, LocationColumnAction);
		ImGui::TableHeadersRow();
	}

	/// The form key is the fallback identity for targets whose editor ID the game does not expose.
	const std::string& GetLocationIdentityText(const SceneSettingsManager::LocationTarget& target)
	{
		return target.editorId.empty() ? target.formKey : target.editorId;
	}

	/// Editor ID and type: what tells apart two links of a chain that share a display name.
	void DrawLocationDetailColumns(const SceneSettingsManager::LocationTarget& target)
	{
		ImGui::TableNextColumn();
		const auto& identity = GetLocationIdentityText(target);
		if (target.editorId.empty())
			Util::Text::Disabled("%s", identity.c_str());
		else
			ImGui::TextUnformatted(identity.c_str());

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(GetLocationTypeLabel(target.type));
	}

	/// Orders the user's list by the clicked header; ImGui owns the direction cycling and the arrow.
	void SortLocationTargets(std::vector<SceneSettingsManager::LocationTarget>& targets)
	{
		const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
		if (!specs || specs->SpecsCount <= 0)
			return;

		const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
		std::ranges::sort(targets, [&spec](const auto& lhs, const auto& rhs) {
			int comparison = 0;
			switch (spec.ColumnUserID) {
			case LocationColumnName:
				comparison = _stricmp(lhs.name.c_str(), rhs.name.c_str());
				break;
			case LocationColumnEditorId:
				comparison = _stricmp(GetLocationIdentityText(lhs).c_str(), GetLocationIdentityText(rhs).c_str());
				break;
			case LocationColumnType:
				comparison = _stricmp(GetLocationTypeLabel(lhs.type), GetLocationTypeLabel(rhs.type));
				break;
			default:
				break;
			}
			// The form key breaks ties: the sort is unstable, so equal keys would otherwise swap rows per frame.
			if (comparison == 0)
				comparison = _stricmp(lhs.formKey.c_str(), rhs.formKey.c_str());
			return spec.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0 : comparison > 0;
		});
	}

	/// The chain the player is standing in, outermost first, each link addable on its own.
	void DrawLocationChain()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		if (!manager)
			return;
		const auto& targets = manager->GetCurrentLocationTargets();
		if (targets.empty()) {
			Util::Text::WrappedDisabled("%s",
				T(TKEY("location_chain_unavailable"), "Waiting for the player's location."));
			return;
		}

		if (!ImGui::BeginTable("LocationChain", 4, kLocationTableFlags))
			return;

		SetupLocationColumns(kLocationAddColumnWidth);
		for (const auto& target : targets) {
			ImGui::TableNextRow();
			ImGui::PushID(target.formKey.c_str());

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(target.name.c_str());
			DrawLocationDetailColumns(target);

			ImGui::TableNextColumn();
			const bool authored = manager->IsLocationTargetAuthored(target.type, target.formKey);
			ImGui::BeginDisabled(authored);
			if (ImGui::SmallButton(T(TKEY("location_add"), "Add")))
				manager->AddLocationTarget(target);
			ImGui::EndDisabled();
			if (authored)
				Util::AddTooltip(T(TKEY("location_already_added"), "Already on your list."),
					Util::kTooltipWhenDisabled);

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	/// The editor's delete icon, matching the weather list's JSON removal action.
	bool DrawLocationRemoveButton()
	{
		// The row selectable spans every column, so the button has to claim the clicks over it.
		ImGui::SetNextItemAllowOverlap();

		auto* menu = globals::menu;
		if (!menu || !menu->uiIcons.deleteSettings.texture)
			return Util::ErrorTextButton(T(TKEY("remove"), "Remove"));

		const float iconSize = ImGui::GetFrameHeight() * kRemoveIconScale;
		return Util::ErrorImageButton("##remove", menu->uiIcons.deleteSettings.texture, { iconSize, iconSize });
	}

	/// The user's list: double-click a row to edit it, or drop it and its settings.
	void DrawAuthoredLocations()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		if (!manager)
			return;
		auto targets = manager->GetAuthoredLocationTargets();
		if (targets.empty()) {
			Util::Text::WrappedDisabled("%s", T(TKEY("location_list_empty"),
				"No locations yet. Add one from where you are standing."));
			return;
		}

		if (!ImGui::BeginTable("AuthoredLocations", 4, kAuthoredLocationTableFlags))
			return;

		SetupLocationColumns(kLocationRemoveColumnWidth);
		// The list is rebuilt from the manager each frame, so it is re-sorted each frame too.
		SortLocationTargets(targets);

		// Removal mutates the manager's map, so it waits until the rows are submitted.
		const SceneSettingsManager::LocationTarget* pendingRemoval = nullptr;
		for (const auto& target : targets) {
			ImGui::TableNextRow();
			ImGui::PushID(target.formKey.c_str());

			ImGui::TableNextColumn();
			const bool opened = std::ranges::any_of(locationWindows, [&](const auto& window) {
				return window.open && window.target.type == target.type && window.target.formKey == target.formKey;
			});
			if (Util::TableRowSelectable(target.name.c_str(), opened,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap) &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				OpenLocationWindow(target);
			DrawLocationDetailColumns(target);

			ImGui::TableNextColumn();
			if (DrawLocationRemoveButton())
				pendingRemoval = &target;
			Util::AddTooltip(T(TKEY("location_remove_tooltip"),
				"Drops the location from the list along with the settings authored for it."));

			ImGui::PopID();
		}
		ImGui::EndTable();

		if (pendingRemoval) {
			std::erase_if(locationWindows, [&](const auto& window) {
				return window.target.type == pendingRemoval->type && window.target.formKey == pendingRemoval->formKey;
			});
			manager->RemoveLocationTarget(pendingRemoval->type, pendingRemoval->formKey);
		}
	}
}

void SceneSettingsUI::DrawWeatherSceneTab(RE::FormID weatherId)
{
	if (weatherId == 0) {
		Util::Text::WrappedDisabled("%s",
			T(TKEY("scene_weather_unresolved"), "This weather has no form, so it cannot hold overrides."));
		return;
	}

	const SceneSettingsManager::SceneContextId context{
		.type = SceneSettingsManager::SceneContextType::Weather,
		.weatherId = weatherId,
	};
	DrawFeatureLayout(weatherSelectedFeature, true,
		T(TKEY("scene_manager_weather_intro"), "Settings overridden while this weather is active."), true, context);
}

void SceneSettingsUI::DrawSceneManagerPanel()
{
	EditorWindow::GetSingleton()->DrawActiveWeatherIndicator();

	// The interior layer takes the panel over indoors, and it has no periods.
	SyncSceneToggles();
	const bool interior = interiorEnabled;
	const SceneSettingsManager::SceneContextId context{
		.type = interior ? SceneSettingsManager::SceneContextType::Interior :
						   SceneSettingsManager::SceneContextType::TimeOfDay,
		.period = interior ? TimeOfDayPeriod::Count : TimeOfDayPeriod::Dawn,
	};

	DrawPanel(T(TKEY("scene_manager_panel_intro"), "Settings overridden by interior and time of day."),
		panelSelectedFeature, context, true, true);
}

void SceneSettingsUI::DrawSceneManagerCategoryFeatures()
{
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Indent();
	ImGui::SetWindowFontScale(kNestedFeatureFontScale);
	DrawFeatureList(panelSelectedFeature, GetFeatureEntries(false));
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Unindent();
}

void SceneSettingsUI::DrawLocationBrowser()
{
	EditorWindow::GetSingleton()->DrawActiveWeatherIndicator();

	ImGui::TextWrapped("%s", T(TKEY("location_browser_intro"),
		"Locations resolve last, so they win over interior, time of day, and weather. A cell wins over the locations that contain it."));
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("%s", T(TKEY("location_add_from_here"), "Add from where you are"));
	ImGui::Spacing();
	DrawLocationChain();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("%s", T(TKEY("location_list_title"), "Your locations"));
	Util::AddTooltip(T(TKEY("location_list_tooltip"), "Double-click a location to edit its settings."));
	ImGui::Spacing();
	DrawAuthoredLocations();
}

void SceneSettingsUI::DrawLocationWindows()
{
	for (auto& window : locationWindows) {
		if (!window.open)
			continue;

		if (window.pendingFocus) {
			ImGui::SetNextWindowFocus();
			window.pendingFocus = false;
		}

		SetupWidgetWindowDefaults(kLocationWidgetType);
		// The form key keeps the id stable while the visible name stays readable.
		const auto title = std::format("{} ({})###SceneLocation_{}", window.target.name,
			GetLocationTypeLabel(window.target.type), window.target.formKey);
		const bool visible = Util::BeginWithRoundedClose(title.c_str(), &window.open, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags);
		UpdateWidgetTypeSize(kLocationWidgetType);
		if (visible) {
			const SceneSettingsManager::SceneContextId context{
				.type = SceneSettingsManager::SceneContextType::Location,
				.period = TimeOfDayPeriod::Count,
				.locationType = window.target.type,
				.locationFormKey = window.target.formKey,
			};
			// Location settings are flat, so the window carries no period bar and no time selection.
			DrawFeatureLayout(window.selectedFeature, false,
				T(TKEY("scene_manager_location_intro"), "Settings overridden while the player is in this location."),
				false, context);
		}
		ImGui::End();
	}

	std::erase_if(locationWindows, [](const auto& window) { return !window.open; });
}

void SceneSettingsUI::SyncTimePause()
{
	// Time only stops for a panel that is actually editing a period.
	const bool editing = periodEditingThisFrame;
	periodEditingThisFrame = false;
	if (editing == wasEditingTimeOfDay)
		return;
	wasEditingTimeOfDay = editing;

	auto* editorWindow = EditorWindow::GetSingleton();
	if (editing) {
		// A pause the user set themselves is theirs to release, so only track our own.
		pausedByPanel = !editorWindow->IsTimePaused();
		if (pausedByPanel)
			editorWindow->PauseTime();
	} else if (pausedByPanel) {
		editorWindow->ResumeTime();
		pausedByPanel = false;
	}
}
