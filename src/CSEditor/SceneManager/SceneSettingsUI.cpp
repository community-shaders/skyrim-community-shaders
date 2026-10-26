#include "SceneSettingsUI.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <format>
#include <list>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "../../I18n/I18n.h"
#include "../Browser/BrowserWidgets.h"
#include "../EditorWindow.h"
#include "../FeatureListPicker.h"
#include "../Weather/WeatherWidget.h"
#include "Features/CSEditor.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/LocationTargetIcons.h"
#include "Menu/Icons/helpers/SceneActionIcons.h"
#include "SceneFeatureReplica.h"
#include "SceneLayerHeader.h"
#include "ScenePageToolbar.h"
#include "SceneSettingsManager.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	using TimeOfDayPeriod = SceneSettingsManager::TimeOfDayPeriod;
	constexpr int kPeriodCount = SceneSettingsManager::kPeriodCount;

	/// Game-hour delta that counts as a deliberate scrub. Deltas below it accumulate rather than
	/// reset the baseline, so time running at any timescale still re-couples the bar.
	constexpr float kScrubEpsilon = 1e-3f;

	/// The objects window nests its list inside the category list, so it reads as a sub-level.
	constexpr float kNestedFeatureFontScale = 0.85f;

	/// Name and editor ID share the slack; the type and the trailing actions are fixed and narrow.
	constexpr float kLocationAddColumnWidth = 32.0f;
	/// Search fields on the locations page, at the 1080p baseline.
	constexpr float kLocationSearchWidth = 320.0f;
	/// The user's list gets a filter once it holds more than this many places.
	constexpr size_t kAuthoredFilterThreshold = 8;
	/// Glyphs differ in advance, so each sits centred in a box this many font sizes wide to keep names aligned.
	constexpr float kLocationIconBoxScale = 1.4f;
	/// Rows the picker shows before it scrolls.
	constexpr float kLocationPickerVisibleRows = 8.0f;
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
		LocationColumnHere,
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

	/// Each panel keeps its own selection: several can be on screen at once.
	std::string weatherSelectedFeature;
	std::string panelSelectedFeature;
	std::string weatherFeatureSearch;

	/// A location the user opened for editing. Locations are not forms in the widget system, so the
	/// editor tracks its own windows instead of going through Widget.
	struct LocationWindow
	{
		SceneSettingsManager::LocationTarget target;
		std::string selectedFeature;
		std::string featureSearch;
		bool open = true;
		bool pendingFocus = false;
	};
	/// A list because a drawn window can open another mid-iteration (greyed-setting navigation).
	std::list<LocationWindow> locationWindows;

	struct LocationPickerState
	{
		char search[256]{};
		/// Catalog indices passing the search, rebuilt only when the query changes.
		std::vector<size_t> matches;
		bool matchesValid = false;
		/// Index into kLocationTargetTypes to browse, or -1 for every type.
		int typeFilter = -1;
	};
	LocationPickerState locationPicker;

	char authoredLocationSearch[256]{};
	Util::ConfirmationPopup locationRemoveConfirmation;
	/// The place the open remove confirmation is about.
	std::optional<SceneSettingsManager::LocationTarget> pendingLocationRemoval;

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

	/// Labels for the period segmented control, filled in period order.
	std::array<const char*, kPeriodCount> GetPeriodLabels()
	{
		std::array<const char*, kPeriodCount> labels{};
		for (int period = 0; period < kPeriodCount; ++period)
			labels[period] = GetPeriodLabel(period);
		return labels;
	}

	/// The context one page edits: the selected period's set while editing periods, else the flat set.
	SceneSettingsManager::SceneContextId ResolvePageContext(
		const SceneSettingsManager::SceneContextId& baseContext, TimeOfDayPeriod period, bool periodEditing)
	{
		auto context = baseContext;
		context.period = periodEditing && SceneSettingsManager::IsPeriodicContext(context.type) ?
		                     period :
		                     TimeOfDayPeriod::Count;
		assert(context.type != SceneSettingsManager::SceneContextType::TimeOfDay || context.period != TimeOfDayPeriod::Count);
		return context;
	}

	/// Whether a page edits one period at a time. The Scene Manager panel has no flat mode, so it
	/// always does, except indoors where the aperiodic interior layer takes the panel over. Weather
	/// and location pages follow the saved set their scene has active.
	bool ResolvePeriodEditing(const SceneSettingsManager::SceneContextId& baseContext, bool sceneManagerPanel)
	{
		if (sceneManagerPanel)
			return !interiorEnabled;
		return SceneSettingsManager::GetSingleton()->IsSceneTimeOfDayEnabled(baseContext);
	}

	/// Resolves which period is currently active, updating the follow/pin state as a side effect.
	/// Call exactly once per panel per frame: running the scrub/pin logic twice would double-apply it.
	int ResolveActivePeriod(bool editing)
	{
		const int live = static_cast<int>(SceneSettingsManager::GetCurrentPeriod());

		if (editing) {
			const float hour = SceneSettingsManager::GetCurrentGameHour();
			if (std::abs(hour - periodBar.lastHour) > kScrubEpsilon) {
				periodBar.selected = -1;
				periodBar.lastHour = hour;
			}
		} else if (periodBar.selected < 0) {
			// Pin the follow state so a flat (all-day) bar stops reacting to time entirely.
			periodBar.selected = live;
		}

		return periodBar.selected < 0 ? live : periodBar.selected;
	}

	/// Pins the bar to a period and jumps game time into its middle so the scene matches the edit.
	void PinPeriodAndJump(int period)
	{
		periodBar.selected = period;
		periodBar.lastHour = SceneSettingsManager::GetSingleton()->GetPeriodMidHour(static_cast<TimeOfDayPeriod>(period));
		SceneSettingsManager::SetGameHour(periodBar.lastHour);
	}

	/// Turns period editing on for this scene and lands on the chosen period.
	void EnableTimeOfDayEditing(const SceneSettingsManager::SceneContextId& baseContext, int period)
	{
		SceneSettingsManager::GetSingleton()->SetSceneTimeOfDayEnabled(baseContext, true);
		PinPeriodAndJump(period);
	}

	/// Returns to the flat all-day set; both sets are kept on disk.
	void DisableTimeOfDayEditing(const SceneSettingsManager::SceneContextId& baseContext)
	{
		SceneSettingsManager::GetSingleton()->SetSceneTimeOfDayEnabled(baseContext, false);
		periodBar.selected = static_cast<int>(SceneSettingsManager::GetCurrentPeriod());
		periodBar.lastHour = SceneSettingsManager::GetCurrentGameHour();
	}

	/// Compact chip that exits period editing back to one all-day settings set.
	bool DrawAllDayChip()
	{
		const char* label = T(TKEY("period_bar_all_day"), "All day");
		const bool clicked = ImGui::Button(label);
		Util::AddTooltip(T(TKEY("period_bar_all_day_tooltip"),
			"Use one set of settings for the whole day instead of separate period sets. "
			"Period settings are kept, so turning Time of Day back on restores them."));
		return clicked;
	}

	struct PeriodBarResult
	{
		int active = 0;
		bool editing = false;
	};

	/// Draws the period segmented control and returns the period/mode the panel below should edit.
	/// Weather and location pages fold Time of Day enable into the first period click, and expose
	/// an All day chip to leave period mode. The Scene Manager panel has no flat mode: outdoors it
	/// always edits periods, and indoors the bar yields to the interior layer indicator.
	PeriodBarResult DrawPeriodBarRow(const SceneSettingsManager::SceneContextId& baseContext, bool editing,
		bool sceneManagerPanel, int active)
	{
		const bool interior = sceneManagerPanel && interiorEnabled;
		const bool canToggleTimeOfDay = !sceneManagerPanel && !interior;
		const int live = static_cast<int>(SceneSettingsManager::GetCurrentPeriod());
		const auto labels = GetPeriodLabels();

		ImGui::BeginDisabled(interior);
		// Flat mode keeps the bar clickable but muted, so the current period still reads as "all day".
		const bool muted = canToggleTimeOfDay && !editing;
		// Dot the live period only when editing and the selection has left it.
		const int marked = editing ? live : -1;
		if (Util::SegmentedControl("PeriodBar", labels.data(), kPeriodCount, active, marked, muted)) {
			if (canToggleTimeOfDay && !editing) {
				EnableTimeOfDayEditing(baseContext, active);
				editing = true;
			} else {
				PinPeriodAndJump(active);
			}
		}
		ImGui::EndDisabled();

		char periodStatus[256];
		if (interior)
			std::snprintf(periodStatus, sizeof(periodStatus), "%s",
				T(TKEY("period_bar_interior"), "Time of day editing is unavailable indoors."));
		else if (!editing)
			std::snprintf(periodStatus, sizeof(periodStatus), "%s",
				T(TKEY("period_bar_off"),
					"One set of settings applies all day. Click a period to edit settings for that time of day."));
		else if (periodBar.selected < 0)
			std::snprintf(periodStatus, sizeof(periodStatus), "%s",
				T(TKEY("period_bar_following"),
					"Following the time of day. Click a period to jump to it and edit it on its own."));
		else
			std::snprintf(periodStatus, sizeof(periodStatus),
				T(TKEY("period_bar_manual"), "Editing %s. Scrub the time of day to follow it again."), labels[active]);
		Util::AddTooltip(periodStatus, Util::kTooltipWhenDisabled);

		if (canToggleTimeOfDay && editing) {
			// Kept close: this row also carries the page toolbar.
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
			if (DrawAllDayChip()) {
				DisableTimeOfDayEditing(baseContext);
				editing = false;
				active = periodBar.selected;
			}
		}

		if (sceneManagerPanel) {
			// Kept close for the same reason as the All day chip.
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
			// Indicator only: interior always follows the cell the player is actually in, so
			// toggling it by hand would silently do nothing when the layer can't resolve.
			ImGui::BeginDisabled();
			ImGui::Checkbox(T(TKEY("interior_toggle"), "Interior"), &interiorEnabled);
			ImGui::EndDisabled();
			Util::AddTooltip(T(TKEY("interior_toggle_tooltip"),
								 "Shows whether interior settings are being edited. Follows the cell the player is in."),
				Util::kTooltipWhenDisabled);
		}

		return { active, editing };
	}

	/// One header row for every panel with a period bar: the periods on the left and the page
	/// actions right-aligned beside them. The actions draw after the bar so they act on the period
	/// picked this frame, and wrap to their own row when the panel is too narrow for both.
	/// @return The context the page below edits.
	SceneSettingsManager::SceneContextId DrawPeriodAndActionsRow(
		const SceneSettingsManager::SceneContextId& baseContext, bool sceneManagerPanel)
	{
		const bool editing = ResolvePeriodEditing(baseContext, sceneManagerPanel);
		const int active = ResolveActivePeriod(editing);

		// A period click can enable Time of Day this frame; apply that before resolving the page.
		const auto bar = DrawPeriodBarRow(baseContext, editing, sceneManagerPanel, active);
		periodEditingThisFrame |= bar.editing;
		const auto context = ResolvePageContext(baseContext, static_cast<TimeOfDayPeriod>(bar.active), bar.editing);

		ImGui::SameLine();
		ScenePageToolbar::Draw(context);
		return context;
	}

	/// Scene-capable features, resolved once: loaded features and the catalog are fixed after boot.
	/// Transitionable-only is the weather and time-of-day set; the rest also covers interior and location.
	const std::vector<Feature*>& GetSceneFeatures(bool transitionableOnly)
	{
		auto build = [](const std::vector<std::string>& names) {
			std::vector<Feature*> features;
			features.reserve(names.size());
			for (const auto& name : names) {
				if (auto* feature = Feature::FindFeatureByShortName(name))
					features.push_back(feature);
			}
			return features;
		};
		static const std::vector<Feature*> transitionable = build(SceneSettingsManager::GetExteriorRelevantFeatureNames());
		static const std::vector<Feature*> all = build(SceneSettingsManager::GetLocationRelevantFeatureNames());
		return transitionableOnly ? transitionable : all;
	}

	/// The context the Scene Manager panel edits: the interior layer indoors, else time of day.
	SceneSettingsManager::SceneContextId GetSceneManagerPanelContext()
	{
		return {
			.type = interiorEnabled ? SceneSettingsManager::SceneContextType::Interior :
			                          SceneSettingsManager::SceneContextType::TimeOfDay,
			.period = interiorEnabled ? TimeOfDayPeriod::Count : TimeOfDayPeriod::Dawn,
		};
	}

	/// Whether a context holds any entry for a feature, in any of its periods.
	bool ContextHasFeature(const SceneSettingsManager::SceneContextId& context, const std::string& featureShortName)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		return manager && std::ranges::any_of(manager->GetContextEntries(context),
							  [&](const auto& entry) { return entry.featureShortName == featureShortName; });
	}

	/// The shared feature column, dotting the features this page already holds settings for.
	void DrawFeatureList(std::string& selected, const std::vector<Feature*>& features,
		const SceneSettingsManager::SceneContextId& context, std::string* search)
	{
		if (features.empty()) {
			selected.clear();
			Util::Text::WrappedSecondary("%s",
				T(TKEY("scene_feature_list_empty"), "No loaded feature exposes scene settings."));
			return;
		}

		if (std::ranges::none_of(features, [&](Feature* feature) { return feature->GetShortName() == selected; }))
			selected = features.front()->GetShortName();

		FeatureListPicker::Draw(features, selected,
			{
				.search = search,
				.marker = [&context](const Feature& feature) {
					const bool holdsSettings = ContextHasFeature(context, const_cast<Feature&>(feature).GetShortName());
					return holdsSettings ? FeatureListPicker::Marker::Filled : FeatureListPicker::Marker::None; },
				.markerTooltip = [](const Feature&, FeatureListPicker::Marker) { return T(TKEY("scene_feature_has_settings"), "Has settings on this page."); },
			});
	}

	/// Period bar and actions, the layer header, and the replicated feature UI bound to one scene context.
	void DrawPanel(const std::string& selectedFeature, const SceneSettingsManager::SceneContextId& baseContext,
		bool withPeriodBar = true, bool sceneManagerPanel = false)
	{
		auto context = baseContext;
		if (withPeriodBar) {
			// The Scene Manager panel names the weather it layers over on a row of its own.
			if (sceneManagerPanel)
				EditorWindow::GetSingleton()->DrawActiveWeatherIndicator(false);
			context = DrawPeriodAndActionsRow(baseContext, sceneManagerPanel);
		} else {
			// A page without the bar has no toggle row to share, so the actions get a row of their own.
			ScenePageToolbar::Draw(context);
		}
		// Which layer this page writes to and what an edit there does, the same header Base Settings shows.
		SceneLayerHeader::DrawScene(context, selectedFeature);
		ImGui::Separator();

		if (selectedFeature.empty())
			return;
		SceneFeatureReplica::Draw(selectedFeature, context);
	}

	/// Feature column beside the panel body, split by a divider the user can drag.
	/// Transitionable features are the weather set; the rest also covers interior and location.
	void DrawFeatureLayout(std::string& selectedFeature, std::string& featureSearch, bool transitionableOnly,
		bool withPeriodBar, const SceneSettingsManager::SceneContextId& baseContext)
	{
		if (!ImGui::BeginTable("SceneFeatureLayout", 2, FeatureListPicker::kLayoutFlags))
			return;

		ImGui::TableSetupColumn("##Features", ImGuiTableColumnFlags_WidthFixed, FeatureListPicker::kColumnWidth * Util::GetUIScale());
		ImGui::TableSetupColumn("##Body", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();

		// Each column scrolls on its own, so a long feature list never drags the panel with it.
		ImGui::TableSetColumnIndex(0);
		if (ImGui::BeginChild("##SceneFeatureList"))
			DrawFeatureList(selectedFeature, GetSceneFeatures(transitionableOnly), baseContext, &featureSearch);
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(1);
		if (ImGui::BeginChild("##SceneFeatureBody"))
			DrawPanel(selectedFeature, baseContext, withPeriodBar);
		ImGui::EndChild();

		ImGui::EndTable();
	}

	const char* GetLocationTypeLabel(SceneSettingsManager::LocationTargetType type)
	{
		switch (type) {
		case SceneSettingsManager::LocationTargetType::Worldspace:
			return T(TKEY("location_type_worldspace"), "Worldspace");
		case SceneSettingsManager::LocationTargetType::LocationType:
			return T(TKEY("location_type_location_type"), "Location Type");
		case SceneSettingsManager::LocationTargetType::Region:
			return T(TKEY("location_type_region"), "Region");
		case SceneSettingsManager::LocationTargetType::Cell:
			return T(TKEY("location_type_cell"), "Cell");
		default:
			return T(TKEY("location_type_location"), "Location");
		}
	}

	LocationTargetIcons::TargetView MakeLocationTargetView(const SceneSettingsManager::LocationTarget& target)
	{
		return {
			.category = static_cast<LocationTargetIcons::Category>(target.type),
			.editorId = target.editorId,
			.formId = target.formId,
			.formKey = target.formKey,
		};
	}

	Icons::GlyphRef GetLocationTargetIcon(const SceneSettingsManager::LocationTarget& target)
	{
		return LocationTargetIcons::Resolve(MakeLocationTargetView(target));
	}

	/// The type glyph leading a row's name, muted so the name stays the thing read first.
	void DrawLocationTypeIcon(const SceneSettingsManager::LocationTarget& target)
	{
		const Icons::GlyphRef icon = GetLocationTargetIcon(target);
		const float box = ImGui::GetFontSize() * kLocationIconBoxScale;
		const float offset = std::max(0.0f, (box - Icons::CalcGlyphSize(icon).x) * 0.5f);
		const float start = ImGui::GetCursorPosX();
		ImGui::SetCursorPosX(start + offset);
		{
			Icons::FontGuard font(icon);
			Util::Text::Secondary("%s", icon.utf8);
		}
		ImGui::SameLine(start + box);
	}

	bool IsSameLocationTarget(const SceneSettingsManager::LocationTarget& lhs, const SceneSettingsManager::LocationTarget& rhs)
	{
		return lhs.type == rhs.type && lhs.formKey == rhs.formKey;
	}

	/// Opens a location's editor window, focusing the existing one rather than opening a second.
	LocationWindow& OpenLocationWindow(const SceneSettingsManager::LocationTarget& target)
	{
		auto existing = std::ranges::find_if(locationWindows, [&](const auto& window) {
			return IsSameLocationTarget(window.target, target);
		});
		if (existing != locationWindows.end()) {
			existing->open = true;
			existing->pendingFocus = true;
			return *existing;
		}
		return locationWindows.emplace_back(LocationWindow{ .target = target });
	}

	/// Wide enough for the longest type label plus the header's sort arrow, so it never runs into the action.
	float GetLocationTypeColumnWidth()
	{
		float width = 0.0f;
		for (const auto type : SceneSettingsManager::kLocationTargetTypes)
			width = std::max(width, ImGui::CalcTextSize(GetLocationTypeLabel(type)).x);
		return width + ImGui::GetFontSize();
	}

	/// Both location tables identify a target the same way; the user's list adds where-you-are and its actions.
	void SetupLocationColumns(float actionWidth, bool withHere)
	{
		ImGui::TableSetupColumn(T(TKEY("location_column_name"), "Name"), ImGuiTableColumnFlags_WidthStretch, 0.0f, LocationColumnName);
		ImGui::TableSetupColumn(T(TKEY("location_column_editor_id"), "Editor ID"), ImGuiTableColumnFlags_WidthStretch, 0.0f, LocationColumnEditorId);
		ImGui::TableSetupColumn(T(TKEY("location_column_type"), "Type"), ImGuiTableColumnFlags_WidthFixed, GetLocationTypeColumnWidth(), LocationColumnType);
		if (withHere)
			ImGui::TableSetupColumn(T(TKEY("location_column_here"), "Here"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
				ImGui::CalcTextSize(T(TKEY("location_column_here"), "Here")).x + ImGui::GetStyle().CellPadding.x, LocationColumnHere);
		ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, actionWidth, LocationColumnAction);
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
		ImGui::AlignTextToFramePadding();
		Util::Text::Secondary("%s", GetLocationIdentityText(target).c_str());
		if (target.editorId.empty())
			Util::AddTooltip(T(TKEY("location_form_key_tooltip"), "The game exposes no editor ID for this place, so its form key is shown."));

		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		Util::Text::Secondary("%s", GetLocationTypeLabel(target.type));
	}

	/// What an add button promises, or why it is greyed once the place is listed.
	const char* GetLocationAddTooltip(bool authored)
	{
		return authored ? T(TKEY("location_already_added"), "Already on your list.") :
		                  T(TKEY("location_add_tooltip"), "Add to your locations");
	}

	/// Trailing icon action for an addable row: quiet until hovered, a check once the place is listed.
	bool DrawLocationAddButton(bool authored)
	{
		const Icons::GlyphRef icon = authored ? SceneActionIcons::kAdded : SceneActionIcons::kAdd;
		const float padding = ImGui::GetStyle().FramePadding.x * 0.5f;
		const float width = Icons::CalcGlyphSize(icon).x + padding * 2.0f;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { padding, ImGui::GetStyle().FramePadding.y });
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		auto _style = Util::TransparentIconButtonStyle();
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetSecondary());
		ImGui::BeginDisabled(authored);
		const bool clicked = Icons::Button("##add", icon);
		ImGui::EndDisabled();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
		return clicked;
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

	/// One row of a table the user adds targets from, disabled once the target is on their list.
	void DrawLocationAddRow(SceneSettingsManager& manager, const SceneSettingsManager::LocationTarget& target)
	{
		ImGui::TableNextRow();
		ImGui::PushID(target.formKey.c_str());

		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		DrawLocationTypeIcon(target);
		ImGui::TextUnformatted(target.name.c_str());
		DrawLocationDetailColumns(target);

		ImGui::TableNextColumn();
		const bool authored = manager.IsLocationTargetAuthored(target.type, target.formKey);
		if (DrawLocationAddButton(authored))
			manager.AddLocationTarget(target);
		Util::AddTooltip(GetLocationAddTooltip(authored), Util::kTooltipWhenDisabled);

		ImGui::PopID();
	}

	using BrowserUI::SameLineIfFits;

	/// The chain the player is standing in, outermost first, as a breadcrumb of addable chips.
	void DrawLocationChain()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		if (!manager)
			return;
		const auto& targets = manager->GetCurrentLocationTargets();
		if (targets.empty()) {
			Util::Text::WrappedSecondary("%s",
				T(TKEY("location_chain_unavailable"), "Waiting for the player's location."));
			return;
		}

		const Icons::GlyphRef separator = Icons::TI(ICON_TI_CHEVRON_RIGHT);
		const float separatorWidth = Icons::CalcGlyphSize(separator).x + ImGui::GetStyle().ItemSpacing.x;
		const ImVec4 added = Util::Colors::GetSuccess();
		const ImVec4 iconColor = Util::Colors::GetSecondary();
		for (size_t i = 0; i < targets.size(); ++i) {
			const auto& target = targets[i];
			ImGui::PushID(static_cast<int>(i));
			const bool authored = manager->IsLocationTargetAuthored(target.type, target.formKey);
			const Icons::GlyphRef icon = GetLocationTargetIcon(target);
			const float width = BrowserUI::MeasureChip(target.name.c_str(), icon.IsValid()) + ImGui::GetStyle().ItemInnerSpacing.x +
			                    BrowserUI::IconButtonSize();

			// The separator only sits between chips that share a line.
			if (i > 0) {
				ImGui::SameLine();
				if (ImGui::GetContentRegionAvail().x >= separatorWidth + width) {
					ImGui::AlignTextToFramePadding();
					{
						Icons::FontGuard font(separator);
						Util::Text::Disabled("%s", separator.utf8);
					}
					ImGui::SameLine();
				} else {
					ImGui::NewLine();
				}
			}

			if (BrowserUI::Chip("##link", target.name.c_str(), icon, &iconColor, authored ? &added : nullptr)) {
				if (authored)
					OpenLocationWindow(target);
				else
					manager->AddLocationTarget(target);
			}
			Util::AddTooltip(std::format("{} \xC2\xB7 {}\n{}", GetLocationIdentityText(target), GetLocationTypeLabel(target.type),
				authored ? T(TKEY("location_chain_open_tooltip"), "On your list. Click to edit its settings.") :
						   T(TKEY("location_add_tooltip"), "Add to your locations"))
					.c_str());

			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::BeginDisabled(authored);
			if (BrowserUI::IconButton("##add", authored ? SceneActionIcons::kAdded : SceneActionIcons::kAdd,
					GetLocationAddTooltip(authored),
					false, ImGui::GetColorU32(authored ? Util::Colors::GetSuccess() : Util::Colors::GetAccent())))
				manager->AddLocationTarget(target);
			ImGui::EndDisabled();
			ImGui::PopID();
		}
	}

	bool LocationTargetMatchesSearch(const SceneSettingsManager::LocationTarget& target, const std::string& query)
	{
		if (Util::StringMatchesSearch(target.name, query) ||
			Util::StringMatchesSearch(GetLocationIdentityText(target), query) ||
			Util::StringMatchesSearch(GetLocationTypeLabel(target.type), query))
			return true;
		if (target.type == SceneSettingsManager::LocationTargetType::LocationType) {
			if (const auto label = LocationTypeIcons::LookupLocationTypeLabel(target.editorId); !label.empty())
				return Util::StringMatchesSearch(std::string(label), query);
		}
		return false;
	}

	/// Every place the game defines, searchable or browsable by type, so targets away from the player can be added too.
	void DrawLocationPicker()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		if (!manager)
			return;
		const auto& catalog = manager->GetLocationCatalog();
		const float scale = Util::GetUIScale();

		bool changed = BrowserUI::SearchField("##LocationSearch", locationPicker.search, IM_ARRAYSIZE(locationPicker.search),
			T(TKEY("location_search"), "Search by name, editor ID, or type..."),
			std::min(ImGui::GetContentRegionAvail().x, kLocationSearchWidth * scale), false);

		const ImVec4 iconColor = Util::Colors::GetSecondary();
		const ImVec4 accent = Util::Colors::GetAccent();
		const auto& types = SceneSettingsManager::kLocationTargetTypes;
		for (int index = 0; index < static_cast<int>(types.size()); ++index) {
			ImGui::PushID(index);
			const char* label = GetLocationTypeLabel(types[index]);
			const Icons::GlyphRef icon = GetLocationTargetIcon(SceneSettingsManager::LocationTarget{ .type = types[index] });
			SameLineIfFits(BrowserUI::MeasureChip(label, icon.IsValid()));
			const bool active = locationPicker.typeFilter == index;
			if (BrowserUI::Chip("##type", label, icon, &iconColor, active ? &accent : nullptr)) {
				locationPicker.typeFilter = active ? -1 : index;
				changed = true;
			}
			Util::AddTooltip(T(TKEY("location_type_chip_tooltip"), "Browse every place of this type."));
			ImGui::PopID();
		}

		const std::string query = locationPicker.search;
		// The catalog runs to thousands of forms, so it is filtered only when the query or type changes.
		if (changed || !locationPicker.matchesValid) {
			locationPicker.matches.clear();
			for (size_t index = 0; index < catalog.size(); ++index) {
				const auto& target = catalog[index];
				if (locationPicker.typeFilter >= 0 && target.type != types[locationPicker.typeFilter])
					continue;
				if (query.empty() || LocationTargetMatchesSearch(target, query))
					locationPicker.matches.push_back(index);
			}
			locationPicker.matchesValid = true;
		}

		// Nothing to list until the user narrows the catalog, so it does not bury the rest of the page.
		if (query.empty() && locationPicker.typeFilter < 0) {
			Util::Text::WrappedDisabled("%s", I18n::GetSingleton()->Format(TKEY("location_search_hint"),
																	  { { "count", std::to_string(catalog.size()) } },
																	  "Type to search {count} places, or pick a type to browse them.")
												  .c_str());
			return;
		}
		if (locationPicker.matches.empty()) {
			Util::Text::WrappedSecondary("%s", T(TKEY("location_search_empty"), "No places match the search."));
			return;
		}

		const float rows = std::min(static_cast<float>(locationPicker.matches.size()) + 1.0f, kLocationPickerVisibleRows);
		const ImVec2 tableSize{ 0.0f, ImGui::GetFrameHeightWithSpacing() * rows };
		{
			BrowserUI::RowShadeScope shade;
			if (ImGui::BeginTable("LocationCatalog", 4, kLocationTableFlags | ImGuiTableFlags_ScrollY, tableSize)) {
				ImGui::TableSetupScrollFreeze(0, 1);
				SetupLocationColumns(kLocationAddColumnWidth * scale, false);
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(locationPicker.matches.size()));
				while (clipper.Step())
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
						DrawLocationAddRow(*manager, catalog[locationPicker.matches[row]]);
				ImGui::EndTable();
			}
		}
		Util::Text::Disabled("%s", I18n::GetSingleton()->Format(TKEY("location_result_count"),
														   { { "shown", std::to_string(locationPicker.matches.size()) }, { "total", std::to_string(catalog.size()) } },
														   "{shown} of {total} places")
									   .c_str());
	}

	/// Asks before dropping a location, since its authored settings go with it.
	void RequestLocationRemoval(const SceneSettingsManager::LocationTarget& target)
	{
		pendingLocationRemoval = target;
		locationRemoveConfirmation.title = T(TKEY("location_remove_title"), "Remove location?");
		locationRemoveConfirmation.message = I18n::GetSingleton()->Format(TKEY("location_remove_message"), { { "place", target.name } },
			"Remove {place} from your list? The settings authored for it are deleted too.");
		locationRemoveConfirmation.confirmLabel = T(TKEY("remove"), "Remove");
		locationRemoveConfirmation.cancelLabel = T(TKEY("cancel"), "Cancel");
		locationRemoveConfirmation.Request();
	}

	/// The user's list: double-click or the pen to edit a location, the bin to drop it and its settings.
	void DrawAuthoredLocations()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		if (!manager)
			return;
		auto targets = manager->GetAuthoredLocationTargets();
		if (targets.empty()) {
			Util::Text::WrappedSecondary("%s", T(TKEY("location_list_empty"),
												   "No locations yet. Add one from where you are standing, or search for any place."));
			return;
		}

		// A filter only earns its row once the list is long enough to need one.
		if (targets.size() > kAuthoredFilterThreshold) {
			BrowserUI::SearchField("##AuthoredSearch", authoredLocationSearch, IM_ARRAYSIZE(authoredLocationSearch),
				T(TKEY("location_filter_list"), "Filter your list..."),
				std::min(ImGui::GetContentRegionAvail().x, kLocationSearchWidth * Util::GetUIScale()), false);
			if (const std::string query = authoredLocationSearch; !query.empty())
				std::erase_if(targets, [&query](const auto& target) { return !LocationTargetMatchesSearch(target, query); });
		}

		const auto& here = manager->GetCurrentLocationTargets();
		const float button = BrowserUI::IconButtonSize();
		const float actionWidth = button * 2.0f + ImGui::GetStyle().ItemSpacing.x;
		{
			BrowserUI::RowShadeScope shade;
			if (ImGui::BeginTable("AuthoredLocations", 5, kAuthoredLocationTableFlags)) {
				SetupLocationColumns(actionWidth, true);
				// The list is rebuilt from the manager each frame, so it is re-sorted each frame too.
				SortLocationTargets(targets);

				for (const auto& target : targets) {
					ImGui::TableNextRow();
					ImGui::PushID(target.formKey.c_str());

					ImGui::TableNextColumn();
					const bool opened = std::ranges::any_of(locationWindows, [&](const auto& window) {
						return window.open && IsSameLocationTarget(window.target, target);
					});
					const ImVec2 cellStart = ImGui::GetCursorScreenPos();
					if (Util::TableRowSelectable("##row", opened,
							ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap) &&
						ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						OpenLocationWindow(target);
					ImGui::SetCursorScreenPos(cellStart);
					ImGui::AlignTextToFramePadding();
					DrawLocationTypeIcon(target);
					ImGui::TextUnformatted(target.name.c_str());
					DrawLocationDetailColumns(target);

					ImGui::TableNextColumn();
					if (std::ranges::any_of(here, [&](const auto& link) { return IsSameLocationTarget(link, target); })) {
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
						Util::DrawInlineIndicatorDot(ImGui::GetColorU32(Util::Colors::GetSuccess()), true);
						Util::AddTooltip(T(TKEY("location_here_tooltip"), "You are here: its settings apply now."));
					}

					ImGui::TableNextColumn();
					ImGui::SetNextItemAllowOverlap();
					if (BrowserUI::IconButton("##edit", Icons::TI(ICON_TI_PENCIL), T(TKEY("location_edit_tooltip"), "Edit its settings"), opened))
						OpenLocationWindow(target);
					ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
					ImGui::SetNextItemAllowOverlap();
					if (BrowserUI::IconButton("##remove", SceneActionIcons::kDelete,
							T(TKEY("location_remove_tooltip"), "Drops the location from the list along with the settings authored for it."),
							false, BrowserUI::DestructiveIconColor()))
						RequestLocationRemoval(target);

					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}

		// Removal mutates the manager's map, so it runs after the rows are submitted.
		if (locationRemoveConfirmation.Draw() && pendingLocationRemoval) {
			const auto target = *pendingLocationRemoval;
			std::erase_if(locationWindows, [&](const auto& window) { return IsSameLocationTarget(window.target, target); });
			manager->RemoveLocationTarget(target.type, target.formKey);
			pendingLocationRemoval.reset();
		} else if (!locationRemoveConfirmation.IsOpen()) {
			pendingLocationRemoval.reset();
		}
	}

	/** @brief Opens the CS Editor on one of its browser categories, by stable id. */
	void OpenEditorCategory(const char* id)
	{
		CSEditor::OpenEditorWindow();
		if (auto* editorWindow = EditorWindow::GetSingleton(); editorWindow->open)
			editorWindow->SelectCategory(id);
	}

	/** @brief Selects a feature on a page, keeping its current selection when none is given. */
	void SelectFeature(std::string& selection, const std::string& featureShortName)
	{
		if (!featureShortName.empty())
			selection = featureShortName;
	}
}

UndoHistory::Target SceneSettingsUI::MakeUndoTarget()
{
	using Snapshot = SceneSettingsManager::UserSettingsSnapshot;
	UndoHistory::Target target{ .key = "scene", .label = T(TKEY("scene_undo_label"), "Scene Manager") };
	target.capture = []() -> UndoHistory::SnapshotPtr {
		auto* manager = SceneSettingsManager::GetSingleton();
		return manager ? UndoHistory::Capture(manager->CaptureUserSettings()) : nullptr;
	};
	target.restore = [](const UndoHistory::Snapshot& snapshot) {
		if (auto* manager = SceneSettingsManager::GetSingleton())
			manager->RestoreUserSettings(UndoHistory::Get<Snapshot>(snapshot));
	};
	return target;
}

/** @brief Registers the scene settings with the editor's undo for a frame that draws scene UI. */
static void WatchSceneSettingsForUndo()
{
	EditorWindow::GetSingleton()->undoHistory.Watch(SceneSettingsUI::MakeUndoTarget());
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
	WatchSceneSettingsForUndo();
	DrawFeatureLayout(weatherSelectedFeature, weatherFeatureSearch, true, true, context);
}

void SceneSettingsUI::DrawSceneManagerPanel()
{
	// DrawPanel below draws the active-weather indicator itself, above the period and actions row,
	// since this is the only caller that passes sceneManagerPanel = true.

	// The interior layer takes the panel over indoors, and it has no periods.
	SyncSceneToggles();
	WatchSceneSettingsForUndo();
	DrawPanel(panelSelectedFeature, GetSceneManagerPanelContext(), true, true);
}

void SceneSettingsUI::DrawSceneManagerCategoryFeatures()
{
	ImGui::Separator();

	ImGui::Indent();
	ImGui::SetWindowFontScale(kNestedFeatureFontScale);
	DrawFeatureList(panelSelectedFeature, GetSceneFeatures(false), GetSceneManagerPanelContext(), nullptr);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Unindent();
}

void SceneSettingsUI::DrawLocationBrowser()
{
	WatchSceneSettingsForUndo();
	EditorWindow::GetSingleton()->DrawActiveWeatherIndicator(false);

	Util::Explainer(T(TKEY("location_browser_intro_label"), "How locations resolve"),
		T(TKEY("location_browser_intro"),
			"Locations resolve last, so they win over interior, time of day, and weather. Narrower places win over broader ones: "
			"a cell over its location, a location over its region, location types, and worldspace."));

	// The user's own list comes first: it is what they come back to edit.
	ImGui::Spacing();
	BrowserUI::SectionLabel(T(TKEY("location_list_title"), "Your locations"));
	Util::AddTooltip(T(TKEY("location_list_tooltip"), "Double-click a location to edit its settings."));
	DrawAuthoredLocations();

	ImGui::Spacing();
	BrowserUI::SectionLabel(T(TKEY("location_add_from_here"), "Add from where you are"));
	Util::AddTooltip(T(TKEY("location_add_from_here_tooltip"),
		"The places the player is standing in, outermost first. Where listed places overlap, the narrowest one wins."));
	DrawLocationChain();

	ImGui::Spacing();
	BrowserUI::SectionLabel(T(TKEY("location_add_any"), "Add any place"));
	Util::AddTooltip(T(TKEY("location_add_any_tooltip"),
		"Every place the game defines, so you can add one without travelling there."));
	DrawLocationPicker();
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
		// LocType / category glyph left of the title (same pattern as weather popouts).
		const Icons::GlyphRef typeIcon = GetLocationTargetIcon(window.target);
		const auto title = std::format("{} ({})###SceneLocation_{}", window.target.name,
			GetLocationTypeLabel(window.target.type), window.target.formKey);
		const bool visible = Util::BeginWithCustomHeader(title.c_str(), &window.open, nullptr,
			ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags,
			typeIcon ?
				[typeIcon](ImVec2 iconMin, float iconSize) {
					Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), iconMin, ImVec2(iconSize, iconSize),
						typeIcon, ImGui::GetColorU32(ImGuiCol_Text));
				} :
				std::function<void(ImVec2, float)>{});
		UpdateWidgetTypeSize(kLocationWidgetType);
		if (visible) {
			WatchSceneSettingsForUndo();
			EditorWindow::GetSingleton()->ClaimTimePeriodSet(EditorWindow::TimePeriodSet::Scene);
			const SceneSettingsManager::SceneContextId context{
				.type = SceneSettingsManager::SceneContextType::Location,
				.period = TimeOfDayPeriod::Count,
				.locationType = window.target.type,
				.locationFormKey = window.target.formKey,
			};
			// Both sets take location features; a per-period set greys whatever cannot blend.
			DrawFeatureLayout(window.selectedFeature, window.featureSearch, false, true, context);
		}
		ImGui::End();
	}

	std::erase_if(locationWindows, [](const auto& window) { return !window.open; });
}

void SceneSettingsUI::OpenSceneContext(const SceneSettingsManager::SceneContextId& context,
	const std::string& featureShortName)
{
	CSEditor::OpenEditorWindow();
	auto* editorWindow = EditorWindow::GetSingleton();
	if (!editorWindow->open)
		return;

	// Pinned without moving the clock, so a mid-blend jump lands on the incoming period as it is now.
	if (context.period != TimeOfDayPeriod::Count)
		periodBar = { static_cast<int>(context.period), SceneSettingsManager::GetCurrentGameHour() };

	switch (context.type) {
	case SceneSettingsManager::SceneContextType::Location:
		{
			const auto& targets = SceneSettingsManager::GetSingleton()->GetCurrentLocationTargets();
			const auto target = std::ranges::find_if(targets, [&](const auto& candidate) {
				return candidate.type == context.locationType && candidate.formKey == context.locationFormKey;
			});
			if (target != targets.end())
				SelectFeature(OpenLocationWindow(*target).selectedFeature, featureShortName);
			break;
		}
	case SceneSettingsManager::SceneContextType::Weather:
		for (const auto& widget : editorWindow->weatherWidgets)
			if (widget->form && widget->form->GetFormID() == context.weatherId) {
				static_cast<WeatherWidget*>(widget.get())->OpenSceneManagerTab();
				SelectFeature(weatherSelectedFeature, featureShortName);
				break;
			}
		break;
	default:
		editorWindow->SelectCategory("Scene Manager");
		SelectFeature(panelSelectedFeature, featureShortName);
		break;
	}
}

bool SceneSettingsUI::LayerListsFeature(SceneSettingsManager::SceneContextType layer, const std::string& featureShortName)
{
	const bool transitionableOnly = layer == SceneSettingsManager::SceneContextType::Weather;
	return std::ranges::any_of(GetSceneFeatures(transitionableOnly),
		[&](Feature* feature) { return feature->GetShortName() == featureShortName; });
}

std::optional<SceneSettingsManager::SceneContextId> SceneSettingsUI::ResolveCurrentLayer(
	SceneSettingsManager::SceneContextType layer, const std::string& featureShortName)
{
	return LayerListsFeature(layer, featureShortName) ? ResolveCurrentContext(layer) : std::nullopt;
}

std::optional<SceneSettingsManager::SceneContextId> SceneSettingsUI::ResolveCurrentContext(SceneSettingsManager::SceneContextType layer)
{
	using enum SceneSettingsManager::SceneContextType;
	auto* manager = SceneSettingsManager::GetSingleton();
	if (!manager)
		return std::nullopt;

	switch (layer) {
	case TimeOfDay:
		return SceneSettingsManager::SceneContextId{ .type = TimeOfDay, .period = SceneSettingsManager::GetCurrentPeriod() };
	case Interior:
		return SceneSettingsManager::SceneContextId{ .type = Interior };
	case Weather:
		{
			auto* sky = globals::game::sky;
			if (!sky || !sky->currentWeather)
				return std::nullopt;
			return SceneSettingsManager::SceneContextId{ .type = Weather, .weatherId = sky->currentWeather->GetFormID() };
		}
	default:
		{
			// Outermost first, so the last listed link is the narrowest: the one whose settings win here.
			const auto& chain = manager->GetCurrentLocationTargets();
			const auto narrowest = std::ranges::find_if(chain | std::views::reverse, [&](const auto& target) {
				return manager->IsLocationTargetAuthored(target.type, target.formKey);
			});
			if (narrowest == (chain | std::views::reverse).end())
				return std::nullopt;
			return SceneSettingsManager::SceneContextId{
				.type = Location,
				.locationType = narrowest->type,
				.locationFormKey = narrowest->formKey,
			};
		}
	}
}

void SceneSettingsUI::OpenLocationsPage()
{
	OpenEditorCategory("Locations");
}

void SceneSettingsUI::OpenSceneManagerPage()
{
	OpenEditorCategory("Scene Manager");
}

bool SceneSettingsUI::OpenCurrentLocationForSetting(const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey)
{
	CSEditor::OpenEditorWindow();
	auto* editorWindow = EditorWindow::GetSingleton();
	auto* manager = SceneSettingsManager::GetSingleton();
	if (!editorWindow || !editorWindow->open || !manager)
		return false;

	const auto warn = [editorWindow](const char* message) {
		editorWindow->ShowNotification(message, Util::Colors::GetWarning(), EditorWindow::kLongNotificationDuration);
	};

	const auto& targets = manager->GetCurrentLocationTargets();
	if (targets.empty()) {
		warn(T(TKEY("scene_override_location_unknown"),
			"Can't tell where you are yet. Step into the world, then try again."));
		return false;
	}

	// Flat Location is the layer that accepts non-blendable settings Weather/TOD grey out.
	if (!SceneSettingsManager::IsSettingAllowedForType(SceneSettingsManager::SceneType::Location,
			featureShortName, settingPath, settingKey, false)) {
		warn(T(TKEY("scene_override_location_unsupported"),
			"This place can't hold that setting: Location scenes don't support it either."));
		return false;
	}

	// Chain is outermost-first; the last link is the place you're actually standing in.
	const auto& target = targets.back();
	const bool alreadyAuthored = manager->IsLocationTargetAuthored(target.type, target.formKey);
	if (!alreadyAuthored && !manager->AddLocationTarget(target)) {
		warn(T(TKEY("scene_override_location_add_failed"),
			"Couldn't add an override for where you are. Check the log."));
		return false;
	}

	const SceneSettingsManager::SceneContextId locationContext{
		.type = SceneSettingsManager::SceneContextType::Location,
		.period = TimeOfDayPeriod::Count,
		.locationType = target.type,
		.locationFormKey = target.formKey,
	};
	// Period sets require blendable values; turn Time of Day off so this setting is editable here.
	bool turnedOffTod = false;
	if (manager->IsSceneTimeOfDayEnabled(locationContext)) {
		manager->SetSceneTimeOfDayEnabled(locationContext, false);
		turnedOffTod = true;
	}

	auto& window = OpenLocationWindow(target);
	window.selectedFeature = featureShortName;
	window.pendingFocus = true;

	if (!alreadyAuthored) {
		editorWindow->ShowNotification(
			I18n::GetSingleton()->Format("cs_editor.scene_override_location_added",
				{ { "place", target.name } },
				"Added {place}. Edit this setting here."),
			Util::Colors::GetInfo());
	} else if (turnedOffTod) {
		editorWindow->ShowNotification(
			I18n::GetSingleton()->Format("cs_editor.scene_override_location_flat",
				{ { "place", target.name } },
				"Opened {place}. Time of Day is off here so this setting can be edited."),
			Util::Colors::GetInfo(), EditorWindow::kLongNotificationDuration);
	}

	return true;
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
