#include "SceneCopyModal.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "../../I18n/I18n.h"
#include "../EditorWindow.h"
#include "SceneSettingsContextRules.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	using SceneContextId = SceneSettingsManager::SceneContextId;
	using SceneContextType = SceneSettingsManager::SceneContextType;
	using CopyCandidate = SceneSettingsManager::CopyCandidate;
	using CopyConflictPolicy = SceneSettingsManager::CopyConflictPolicy;
	using CopyRejection = SceneSettingsManager::CopyRejection;
	using CopySource = SceneSettingsManager::CopySource;
	using LocationTargetType = SceneSettingsManager::LocationTargetType;
	using TimeOfDayPeriod = SceneSettingsManager::TimeOfDayPeriod;
	using WeatherFlag = RE::TESWeather::WeatherDataFlag;
	using PeriodSet = std::bitset<SceneSettingsManager::kPeriodCount>;

	/// Fixed ID behind the translated title, so the popup survives a language switch.
	constexpr const char* kPopupId = "###SceneCopyModal";

	/// Sizes in font heights, so the modal scales with the UI font.
	constexpr float kModalWidthEm = 40.0f;
	constexpr float kListHeightEm = 14.0f;
	constexpr float kDetailsHeightEm = 10.0f;
	constexpr float kSearchWidthEm = 10.0f;

	constexpr ImGuiTableFlags kListTableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;
	constexpr ImGuiTableFlags kDetailsTableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
	/// Rows keep the modal open, span the table, and let the tick box drawn over them take its own clicks.
	constexpr ImGuiSelectableFlags kRowFlags = ImGuiSelectableFlags_NoAutoClosePopups |
	                                           ImGuiSelectableFlags_SpanAllColumns |
	                                           ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick;

	constexpr std::array kContextTypes{ SceneContextType::TimeOfDay, SceneContextType::Weather,
		SceneContextType::Location, SceneContextType::Interior };
	constexpr std::array kWeatherClasses{ WeatherFlag::kPleasant, WeatherFlag::kCloudy, WeatherFlag::kRainy,
		WeatherFlag::kSnow };

	/// A page that has not asked for this long has been closed; evicting on size would drop pages still in use.
	constexpr int kSourceCheckRetentionFrames = 120;

	/** @brief Which side of the copy the page is on: From copies into it, To copies out of it. */
	enum class CopyDirection
	{
		From,
		To
	};

	/** @brief One scene (a weather, a location, the interior or the time of day) with the sets it resolves. */
	struct SceneRow
	{
		SceneContextId scene;
		std::string name;
		std::vector<CopySource> sets;
		size_t settingCount = 0;
		std::uint32_t weatherFlags = 0;
		bool flat = false;
		bool authored = false;
		bool current = false;

		/** @brief The set a period addresses: the flat set for any period, or null when the scene has none. */
		const CopySource* FindSet(TimeOfDayPeriod period) const
		{
			if (flat)
				return &sets.front();
			const auto it = std::ranges::find(sets, period, [](const auto& set) { return set.context.period; });
			return it == sets.end() ? nullptr : &*it;
		}
	};

	/** @brief Filters outlive the modal, so reopening it lands where the user left off. */
	struct Filters
	{
		CopyDirection direction = CopyDirection::To;
		std::optional<SceneContextType> type;
		std::optional<WeatherFlag> weatherClass;
		std::optional<LocationTargetType> locationType;
	};
	Filters filters;

	/** @brief One set a copy writes into, with the name the summary shows for it. */
	struct Destination
	{
		SceneContextId context;
		std::string name;

		bool operator==(const Destination&) const = default;
	};

	/** @brief What Copy would run right now: one source into every destination. */
	struct CopyPlan
	{
		SceneContextId source;
		std::string sourceName;
		std::vector<Destination> destinations;

		bool operator==(const CopyPlan&) const = default;
	};

	/** @brief One destination's dry-run rows, kept for the details table. */
	struct DestinationCandidates
	{
		std::string name;
		std::vector<CopyCandidate> candidates;
	};

	/** @brief Setting counts across every destination, grouped the way the copy lands them. */
	struct CopySummary
	{
		size_t willCopy = 0;
		size_t alreadySet = 0;
		size_t droppedTotal = 0;
		std::map<CopyRejection, size_t> dropped;
		std::vector<DestinationCandidates> details;
	};

	/** @brief One opening of the modal. Only the page that opened it draws it: a popup opened under one
	 *  page's ID stack cannot be drawn under another's. */
	struct Session
	{
		SceneContextId page;
		std::string pageName;
		std::string search;
		/// Keyed by scene, so a ticked row stays ticked while a filter hides it.
		std::set<SceneContextId> ticked;
		/// From's source, and To's target while nothing is ticked.
		std::optional<SceneContextId> cursor;
		PeriodSet periods;
		int cursorStep = 0;
		bool scrollToCursor = false;
		bool showUnauthored = false;
		bool focusSearch = false;
		bool active = false;
		bool pendingOpen = false;
		/// Enter commits only from the search box or a row, never from a chip or button it would also press.
		bool enterCommits = false;
		SceneSettingsManager::RevisionCache<std::vector<SceneRow>> rows;
		SceneSettingsManager::RevisionCache<CopySummary> summary;
		CopyPlan summaryPlan;
	};
	Session session;

	/** @brief One line of the list: a section heading, a scene, or the row that reveals hidden scenes. */
	struct ListLine
	{
		enum class Kind
		{
			Header,
			Scene,
			ShowUnauthored
		};
		Kind kind = Kind::Scene;
		const char* text = nullptr;
		const SceneRow* row = nullptr;
	};

	/** @brief The lines the list draws this frame, and how many unauthored scenes it left out. */
	struct VisibleList
	{
		std::vector<ListLine> lines;
		size_t hiddenUnauthored = 0;
	};

	/** @brief One page's cached answer to HasSources. */
	struct SourceCheck
	{
		SceneSettingsManager::RevisionCache<bool> hasSources;
		int lastUsedFrame = 0;
	};
	std::map<SceneContextId, SourceCheck> sourceChecks;

	/** @brief Heading a scene's type is listed under. */
	const char* GetContextTypeLabel(SceneContextType type)
	{
		switch (type) {
		case SceneContextType::Interior:
			return T(TKEY("scene_page_group_interior"), "Interior");
		case SceneContextType::Weather:
			return T(TKEY("scene_page_group_weather"), "Weather");
		case SceneContextType::Location:
			return T(TKEY("scene_page_group_location"), "Location");
		default:
			return T(TKEY("scene_page_group_time_of_day"), "Time of Day");
		}
	}

	/** @brief Why one candidate is not going to be copied. Empty when it is. */
	const char* GetRejectionText(CopyRejection rejection)
	{
		switch (rejection) {
		case CopyRejection::NotInCatalog:
			return T(TKEY("scene_page_copy_reject_catalog"), "Not a setting a scene can override.");
		case CopyRejection::NotBlendable:
			return T(TKEY("scene_page_copy_reject_blendable"), "Only blendable values can vary by time of day or weather.");
		case CopyRejection::NotAllowedInLayer:
			return T(TKEY("scene_page_copy_reject_layer"), "This page cannot hold this setting.");
		case CopyRejection::ValueRejected:
			return T(TKEY("scene_page_copy_reject_value"), "The value is not valid here.");
		case CopyRejection::BlockedByOverwrite:
			return T(TKEY("scene_page_copy_reject_overwrite"), "A mod override already holds this setting.");
		case CopyRejection::GroupCompanionRejected:
			return T(TKEY("scene_page_copy_reject_companion"), "Another part of the same control cannot be copied.");
		default:
			return "";
		}
	}

	/** @brief Chip label for a weather classification flag. */
	const char* GetWeatherClassLabel(WeatherFlag flag)
	{
		switch (flag) {
		case WeatherFlag::kPleasant:
			return T("feature.cs_editor.pleasant", "Pleasant");
		case WeatherFlag::kCloudy:
			return T("feature.cs_editor.cloudy", "Cloudy");
		case WeatherFlag::kRainy:
			return T("feature.cs_editor.rainy", "Rainy");
		default:
			return T("feature.cs_editor.snow", "Snow");
		}
	}

	/** @brief One details-table row: the setting, then its value, its old -> new change, or why it is dropped. */
	void DrawCandidateRow(const CopyCandidate& candidate)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		if (candidate.compatible)
			ImGui::TextUnformatted(candidate.displayName.c_str());
		else
			Util::Text::Disabled("%s", candidate.displayName.c_str());

		ImGui::TableNextColumn();
		const auto value = candidate.value.dump();
		if (!candidate.compatible) {
			// No arrow: nothing is going to happen to this row.
			Util::Text::Disabled("%s", value.c_str());
			ImGui::SameLine();
			Util::Text::Warning("%s", GetRejectionText(candidate.rejection));
		} else if (candidate.conflicts) {
			ImGui::Text("%s -> %s", candidate.destinationValue->dump().c_str(), value.c_str());
		} else {
			ImGui::TextUnformatted(value.c_str());
		}
	}

	/** @brief A scene's identity, shared by all of its period sets. */
	SceneContextId GetSceneOf(SceneContextId context)
	{
		context.period = TimeOfDayPeriod::Count;
		return context;
	}

	/** @brief A set's name without its period. The time of day has no scene name, so its type stands in. */
	std::string GetSceneName(const CopySource& set)
	{
		if (set.context.type == SceneContextType::TimeOfDay)
			return GetContextTypeLabel(SceneContextType::TimeOfDay);
		if (set.context.period == TimeOfDayPeriod::Count)
			return set.displayName;
		const auto suffix = std::format(" / {}", SceneSettingsContextRules::GetCopyPeriodName(set.context.period));
		assert(set.displayName.ends_with(suffix));
		return set.displayName.substr(0, set.displayName.size() - suffix.size());
	}

	/** @brief The weather's classification flags for the class chips; zero for any other scene. */
	std::uint32_t GetWeatherFlags(const SceneContextId& scene)
	{
		if (scene.type != SceneContextType::Weather)
			return 0;
		const auto* weather = RE::TESForm::LookupByID<RE::TESWeather>(scene.weatherId);
		return weather ? weather->data.flags.underlying() : 0;
	}

	/** @brief Groups the manager's per-period sets into one row per scene, keeping the manager's order. */
	std::vector<SceneRow> BuildRows(const std::vector<CopySource>& sets)
	{
		std::vector<SceneRow> rows;
		std::map<SceneContextId, size_t> rowIndices;
		for (const auto& set : sets) {
			const auto scene = GetSceneOf(set.context);
			const auto [indexIt, inserted] = rowIndices.try_emplace(scene, rows.size());
			if (inserted)
				rows.push_back({ .scene = scene,
					.name = GetSceneName(set),
					.weatherFlags = GetWeatherFlags(scene),
					.flat = set.context.period == TimeOfDayPeriod::Count });
			auto& row = rows[indexIt->second];
			row.sets.push_back(set);
			row.settingCount = std::max(row.settingCount, set.settingCount);
			row.authored |= set.authored;
			row.current |= set.current;
		}
		for (auto& row : rows)
			std::ranges::sort(row.sets, {}, [](const auto& set) { return set.context.period; });
		return rows;
	}

	/** @brief Whether a row passes the chips and the search. The authored filter is applied by the caller. */
	bool MatchesFilters(const SceneRow& row)
	{
		if (filters.type && row.scene.type != *filters.type)
			return false;
		if (filters.type == SceneContextType::Weather && filters.weatherClass &&
			!(row.weatherFlags & std::to_underlying(*filters.weatherClass)))
			return false;
		if (filters.type == SceneContextType::Location && filters.locationType &&
			row.scene.locationType != *filters.locationType)
			return false;
		return Util::StringMatchesSearch(row.name, session.search);
	}

	/** @brief The lines the list draws: current scenes pinned first, the rest after, then the way to reveal hidden ones. */
	VisibleList BuildVisibleList(const std::vector<SceneRow>& rows)
	{
		VisibleList list;
		const bool authoredOnly = filters.direction == CopyDirection::To && !session.showUnauthored;
		std::vector<const SceneRow*> currentRows;
		std::vector<const SceneRow*> otherRows;
		for (const auto& row : rows) {
			if (!MatchesFilters(row))
				continue;
			// A current scene stays listed with nothing authored: it is the likeliest target.
			if (row.current)
				currentRows.push_back(&row);
			else if (authoredOnly && !row.authored)
				++list.hiddenUnauthored;
			else
				otherRows.push_back(&row);
		}

		const auto addSection = [&](const char* heading, const std::vector<const SceneRow*>& sectionRows) {
			if (heading && !sectionRows.empty())
				list.lines.push_back({ .kind = ListLine::Kind::Header, .text = heading });
			for (const auto* row : sectionRows)
				list.lines.push_back({ .kind = ListLine::Kind::Scene, .row = row });
		};
		// Headings only earn their space once there is a pinned section to tell apart.
		const bool pinned = !currentRows.empty();
		addSection(pinned ? T(TKEY("scene_copy_current"), "Current") : nullptr, currentRows);
		addSection(pinned ? T(TKEY("filter_all"), "All") : nullptr, otherRows);
		if (list.hiddenUnauthored != 0)
			list.lines.push_back({ .kind = ListLine::Kind::ShowUnauthored });
		return list;
	}

	/** @brief Applies this frame's arrow steps and keeps the cursor on a visible row.
	 *  @return The cursor's line index, or -1 when no row is visible. */
	int UpdateCursor(const VisibleList& list)
	{
		std::vector<int> sceneLines;
		for (int index = 0; index < static_cast<int>(list.lines.size()); ++index)
			if (list.lines[index].kind == ListLine::Kind::Scene)
				sceneLines.push_back(index);
		const int step = std::exchange(session.cursorStep, 0);
		if (sceneLines.empty()) {
			session.cursor.reset();
			return -1;
		}

		const auto cursorIt = std::ranges::find_if(sceneLines,
			[&](int index) { return list.lines[index].row->scene == session.cursor; });
		// A cursor the filters hid snaps to the top match, so typing then Enter picks it.
		const int position = cursorIt == sceneLines.end() ?
		                         0 :
		                         std::clamp(static_cast<int>(cursorIt - sceneLines.begin()) + step, 0,
									 static_cast<int>(sceneLines.size()) - 1);
		const int line = sceneLines[position];
		session.cursor = list.lines[line].row->scene;
		session.scrollToCursor = step != 0;
		return line;
	}

	/** @brief Turns Up/Down in the search box into cursor steps, so the list is driven without leaving it. */
	int OnSearchHistory(ImGuiInputTextCallbackData* data)
	{
		*static_cast<int*>(data->UserData) += data->EventKey == ImGuiKey_UpArrow ? -1 : 1;
		return 0;
	}

	/** @brief A toggle drawn as a text-sized selectable, so a row of them reads as one control. */
	bool Chip(const char* label, bool selected)
	{
		return ImGui::Selectable(label, selected, ImGuiSelectableFlags_NoAutoClosePopups, ImGui::CalcTextSize(label));
	}

	/** @brief An "All" chip, then one chip per option; picking one narrows the filter to it. */
	template <typename Option, size_t Count, typename Label>
	void DrawFilterChips(const char* id, std::optional<Option>& filter, const std::array<Option, Count>& options,
		Label label)
	{
		ImGui::PushID(id);
		if (Chip(T(TKEY("filter_all"), "All"), !filter))
			filter.reset();
		for (const auto option : options) {
			ImGui::SameLine();
			ImGui::PushID(static_cast<int>(option));
			if (Chip(label(option), filter == option))
				filter = option;
			ImGui::PopID();
		}
		ImGui::PopID();
	}

	/** @brief Clears the picks and seeds the periods: the page's own, else every period for a flat page copying out. */
	void ResetSelection()
	{
		session.ticked.clear();
		session.cursor.reset();
		session.periods.reset();
		if (session.page.period != TimeOfDayPeriod::Count)
			session.periods.set(static_cast<size_t>(session.page.period));
		else if (filters.direction == CopyDirection::To)
			session.periods.set();
	}

	/** @brief The period From copies out of: the ticked one if the row holds it, else the row's first set. */
	TimeOfDayPeriod GetSourcePeriod(const SceneRow& row)
	{
		for (const auto period : SceneSettingsManager::kPeriods)
			if (session.periods.test(static_cast<size_t>(period)) && row.FindSet(period))
				return period;
		return row.sets.front().context.period;
	}

	/** @brief The copy the current picks describe; no destinations when nothing is picked. */
	CopyPlan BuildPlan(const std::vector<SceneRow>& rows)
	{
		const auto findRow = [&](const SceneContextId& scene) -> const SceneRow* {
			const auto it = std::ranges::find(rows, scene, &SceneRow::scene);
			return it == rows.end() ? nullptr : &*it;
		};

		if (filters.direction == CopyDirection::From) {
			const auto* row = session.cursor ? findRow(*session.cursor) : nullptr;
			if (!row)
				return {};
			const auto* set = row->FindSet(GetSourcePeriod(*row));
			assert(set);
			return { set->context, set->displayName, { { session.page, session.pageName } } };
		}

		CopyPlan plan{ .source = session.page, .sourceName = session.pageName };
		// With nothing ticked the highlighted row is the target, so a single copy needs no tick.
		const auto targets = session.ticked.empty() && session.cursor ? std::set{ *session.cursor } : session.ticked;
		for (const auto& scene : targets) {
			const auto* row = findRow(scene);
			if (!row)
				continue;
			for (const auto& set : row->sets)
				if (row->flat || session.periods.test(static_cast<size_t>(set.context.period)))
					plan.destinations.push_back({ set.context, set.displayName });
		}
		return plan;
	}

	/** @brief Dry-runs the plan. Mirrors CopySettingsToContext: a control lands, skips or drops as a whole. */
	CopySummary BuildSummary(const CopyPlan& plan)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		CopySummary summary;
		for (const auto& destination : plan.destinations) {
			auto candidates = manager->GetCopyCandidates(plan.source, destination.context);
			std::map<SceneSettingsContextRules::CopyGroupKey, std::vector<const CopyCandidate*>> groups;
			for (const auto& candidate : candidates)
				groups[SceneSettingsContextRules::GetCopyGroupKey(candidate.setting)].push_back(&candidate);
			for (const auto& [groupKey, group] : groups) {
				if (std::ranges::any_of(group, [](const auto* candidate) { return !candidate->compatible; })) {
					for (const auto* candidate : group)
						++summary.dropped[candidate->rejection];
					summary.droppedTotal += group.size();
				} else if (std::ranges::any_of(group, [](const auto* candidate) { return candidate->conflicts; })) {
					summary.alreadySet += group.size();
				} else {
					summary.willCopy += group.size();
				}
			}
			summary.details.push_back({ destination.name, std::move(candidates) });
		}
		return summary;
	}

	/** @brief Commits the plan as one batch and reports what landed in a toast. */
	void RunCopy(const CopyPlan& plan, CopyConflictPolicy policy)
	{
		const auto destinations =
			plan.destinations | std::views::transform(&Destination::context) | std::ranges::to<std::vector>();
		const auto result = SceneSettingsManager::GetSingleton()->CopySettingsBatch(plan.source, destinations, policy);
		auto copied = result.copied;
		auto overwritten = result.overwritten;
		// Dropped rows count as skipped; the summary already said why they were dropped.
		auto skipped = result.skipped + result.incompatible;
		EditorWindow::GetSingleton()->ShowNotification(
			std::vformat(T(TKEY("scene_page_copy_result"), "{} copied, {} overwritten, {} skipped"),
				std::make_format_args(copied, overwritten, skipped)),
			result.Changed() ? Util::Colors::GetSuccess() : Util::Colors::GetWarning());
	}

	/** @brief The From / To switch. Switching refetches the list and starts the picks over. */
	void DrawDirectionChips()
	{
		const auto drawChip = [](const char* label, const char* tooltip, CopyDirection direction) {
			if (Chip(label, filters.direction == direction) && filters.direction != direction) {
				filters.direction = direction;
				session.rows = {};
				ResetSelection();
			}
			Util::AddTooltip(tooltip);
		};
		drawChip(T(TKEY("scene_page_copy_from"), "From"),
			T(TKEY("scene_page_copy_from_tooltip"), "Copies another context's settings into this page."),
			CopyDirection::From);
		ImGui::SameLine();
		drawChip(T(TKEY("scene_page_copy_to"), "To"),
			T(TKEY("scene_page_copy_to_tooltip"), "Copies this page's settings into another context."),
			CopyDirection::To);
	}

	/** @brief Search, type chips and, in To, the authored-only toggle. */
	void DrawFilterRow()
	{
		if (std::exchange(session.focusSearch, false))
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * kSearchWidthEm);
		ImGui::InputTextWithHint("##search", T("ui.search", "Search..."), &session.search,
			ImGuiInputTextFlags_CallbackHistory, OnSearchHistory, &session.cursorStep);
		session.enterCommits = ImGui::IsItemFocused();
		ImGui::SameLine();
		DrawFilterChips("type", filters.type, kContextTypes, GetContextTypeLabel);

		// From lists only scenes that hold settings, so there is nothing for the toggle to hide.
		if (filters.direction == CopyDirection::To) {
			ImGui::SameLine();
			bool authoredOnly = !session.showUnauthored;
			if (ImGui::Checkbox(T(TKEY("scene_copy_authored_only"), "Authored only"), &authoredOnly))
				session.showUnauthored = !authoredOnly;
			Util::AddTooltip(T(TKEY("scene_copy_authored_only_tooltip"),
				"Hides weathers and locations that hold no settings yet."));
		}
	}

	/** @brief The narrower chips the Weather and Location types offer. */
	void DrawContextRow()
	{
		if (filters.type == SceneContextType::Weather)
			DrawFilterChips("weatherClass", filters.weatherClass, kWeatherClasses, GetWeatherClassLabel);
		else if (filters.type == SceneContextType::Location)
			DrawFilterChips("locationType", filters.locationType, SceneSettingsManager::kLocationTargetTypes,
				SceneSettingsContextRules::GetCopyLocationTypeName);
	}

	/** @brief Leaves a row-wide selectable for the name column: the next cell in To, beside it in From. */
	void EnterNameColumn(bool to)
	{
		if (to)
			ImGui::TableNextColumn();
		else
			ImGui::SameLine(0.0f, 0.0f);
		ImGui::AlignTextToFramePadding();
	}

	/** @brief One scene: click highlights it, the tick box adds it to To's targets, double-click commits.
	 *  @return Whether a double-click asked to commit. */
	bool DrawSceneRow(const SceneRow& row, bool isCursor, bool to)
	{
		const bool pressed = Util::TableRowSelectable("##row", isCursor, kRowFlags);
		session.enterCommits |= ImGui::IsItemFocused();
		const bool doubleClicked = pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		if (pressed)
			session.cursor = row.scene;
		if (isCursor && session.scrollToCursor)
			ImGui::SetScrollHereY();

		if (to) {
			ImGui::SameLine(0.0f, 0.0f);
			bool ticked = session.ticked.contains(row.scene);
			if (ImGui::Checkbox("##tick", &ticked)) {
				if (ticked)
					session.ticked.insert(row.scene);
				else
					session.ticked.erase(row.scene);
			}
		}
		EnterNameColumn(to);
		ImGui::TextUnformatted(row.name.c_str());

		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		Util::TextUnformattedDisabled(GetContextTypeLabel(row.scene.type));

		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%zu", row.settingCount);
		if (row.flat) {
			ImGui::SameLine();
			Util::TextUnformattedDisabled(T(TKEY("scene_copy_flat"), "flat"));
		}
		return doubleClicked;
	}

	/** @brief Draws the list and takes its clicks. @return Whether a double-click asked to commit. */
	bool DrawList(const VisibleList& list, int cursorLine)
	{
		const bool to = filters.direction == CopyDirection::To;
		if (!ImGui::BeginTable("##rows", to ? 4 : 3, kListTableFlags,
				ImVec2(0.0f, ImGui::GetFontSize() * kListHeightEm)))
			return false;
		if (to)
			ImGui::TableSetupColumn("##tick", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("##type", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("##count", ImGuiTableColumnFlags_WidthFixed);

		if (list.lines.empty()) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (to)
				ImGui::TableNextColumn();
			Util::TextUnformattedDisabled(T(TKEY("scene_copy_empty"), "Nothing matches."));
		}

		bool commitRequested = false;
		// Every line is frame-high, which the clipper's uniform-height assumption needs.
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(list.lines.size()));
		if (cursorLine >= 0)
			clipper.IncludeItemByIndex(cursorLine);
		while (clipper.Step()) {
			for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
				const auto& line = list.lines[index];
				ImGui::PushID(index);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				switch (line.kind) {
				case ListLine::Kind::Header:
					if (to)
						ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					Util::TextUnformattedDisabled(line.text);
					break;
				case ListLine::Kind::ShowUnauthored:
					{
						if (Util::TableRowSelectable("##showUnauthored", false, kRowFlags))
							session.showUnauthored = true;
						EnterNameColumn(to);
						auto hiddenCount = list.hiddenUnauthored;
						ImGui::TextUnformatted(std::vformat(T(TKEY("scene_copy_show_unauthored"),
																"{} without settings, show all"),
							std::make_format_args(hiddenCount))
												   .c_str());
						break;
					}
				case ListLine::Kind::Scene:
					commitRequested |= DrawSceneRow(*line.row, index == cursorLine, to);
					break;
				}
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
		return commitRequested;
	}

	/** @brief The shared period row: which periods receive in To, which one is copied out of in From. */
	void DrawPeriodChips(const SceneRow* cursorRow)
	{
		const bool from = filters.direction == CopyDirection::From;
		// From shows the period it will really copy, which falls back when the source lacks the ticked one.
		const auto sourcePeriod = from && cursorRow && !cursorRow->flat ?
		                              std::optional{ GetSourcePeriod(*cursorRow) } :
		                              std::nullopt;
		ImGui::PushID("periods");
		ImGui::TextUnformatted(T(TKEY("scene_copy_periods"), "Periods:"));
		{
			// A flat source has one set for every period, so there is nothing to pick.
			const Util::DisableGuard flatGuard(from && (!cursorRow || cursorRow->flat));
			for (const auto period : SceneSettingsManager::kPeriods) {
				const auto bit = static_cast<size_t>(period);
				ImGui::SameLine();
				ImGui::PushID(static_cast<int>(bit));
				const Util::DisableGuard missingGuard(from && cursorRow && !cursorRow->FindSet(period));
				const bool selected = sourcePeriod ? *sourcePeriod == period : session.periods.test(bit);
				if (Chip(SceneSettingsContextRules::GetCopyPeriodName(period), selected)) {
					if (from)
						session.periods.reset();
					session.periods.set(bit, from || !session.periods.test(bit));
				}
				ImGui::PopID();
			}
		}
		if (!from) {
			ImGui::SameLine();
			if (Chip(T(TKEY("filter_all"), "All"), session.periods.all()))
				session.periods.set();
		}
		ImGui::PopID();
	}

	/** @brief The plan in words, its counts, one warning per drop reason, and the collapsible details table. */
	void DrawSummary(const CopyPlan& plan, const CopySummary& summary)
	{
		if (plan.destinations.empty()) {
			Util::TextUnformattedDisabled(T(TKEY("scene_copy_pick"), "Pick a context to copy."));
			return;
		}

		auto destinationCount = plan.destinations.size();
		const auto destinationName = destinationCount == 1 ?
		                                 plan.destinations.front().name :
		                                 std::vformat(T(TKEY("scene_copy_contexts"), "{} contexts"),
											 std::make_format_args(destinationCount));
		ImGui::TextWrapped("%s", std::vformat(T(TKEY("scene_page_copy_intro"), "Copying {} into {}."),
									 std::make_format_args(plan.sourceName, destinationName))
									 .c_str());
		ImGui::TextUnformatted(std::vformat(T(TKEY("scene_copy_summary"), "{} will copy, {} already set, {} dropped"),
			std::make_format_args(summary.willCopy, summary.alreadySet, summary.droppedTotal))
								   .c_str());
		for (const auto& [rejection, count] : summary.dropped) {
			const char* reason = GetRejectionText(rejection);
			Util::Text::Warning("%s", std::vformat(T(TKEY("scene_copy_dropped_reason"), "{} dropped: {}"),
										  std::make_format_args(count, reason))
										  .c_str());
		}

		if (!ImGui::TreeNode("##details", "%s", T(TKEY("scene_copy_details"), "Details")))
			return;
		if (ImGui::BeginTable("##changes", 2, kDetailsTableFlags,
				ImVec2(0.0f, ImGui::GetFontSize() * kDetailsHeightEm))) {
			ImGui::TableSetupColumn(T(TKEY("scene_page_copy_column_setting"), "Setting"));
			ImGui::TableSetupColumn(T(TKEY("scene_page_copy_column_change"), "Change"));
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			for (const auto& destination : summary.details) {
				if (summary.details.size() > 1) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					Util::TextUnformattedDisabled(destination.name.c_str());
				}
				for (const auto& candidate : destination.candidates)
					DrawCandidateRow(candidate);
			}
			ImGui::EndTable();
		}
		ImGui::TreePop();
	}

	/** @brief Copy, Overwrite and Cancel. @return The policy to run, if the user chose one. */
	std::optional<CopyConflictPolicy> DrawButtons(const CopySummary& summary, bool commitRequested)
	{
		std::optional<CopyConflictPolicy> decision;
		const bool canCopy = summary.willCopy != 0;
		ImGui::BeginDisabled(!canCopy);
		if (ImGui::Button(T(TKEY("scene_page_copy"), "Copy")))
			decision = CopyConflictPolicy::SkipExisting;
		ImGui::EndDisabled();
		Util::AddTooltip(T(TKEY("scene_copy_copy_tooltip"),
							 "Copies what is not already set. Enter or a double-click does the same."),
			Util::kTooltipWhenDisabled);

		if (summary.alreadySet != 0) {
			ImGui::SameLine();
			if (Util::WarningButton(T(TKEY("scene_page_copy_overwrite"), "Overwrite existing")))
				decision = CopyConflictPolicy::OverwriteExisting;
			Util::AddTooltip(T(TKEY("scene_copy_overwrite_tooltip"),
				"Also replaces the values already set at the destination."));
		}

		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("cancel"), "Cancel"))) {
			ImGui::CloseCurrentPopup();
			return std::nullopt;
		}

		// Keyboard and double-click never overwrite: replacing values always takes a deliberate click.
		const bool enterPressed = session.enterCommits &&
		                          (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
		if (!decision && canCopy && (enterPressed || commitRequested))
			decision = CopyConflictPolicy::SkipExisting;
		return decision;
	}

	/** @brief The modal's body for one frame, from the direction switch down to the buttons. */
	void DrawContents()
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			// The editor ignores Escape only while a popup is open, which this close is about to end.
			if (auto* editor = EditorWindow::GetSingleton(); editor->open)
				editor->suppressNextEditorEscape = true;
			ImGui::CloseCurrentPopup();
			return;
		}

		DrawDirectionChips();
		DrawFilterRow();
		DrawContextRow();

		auto* manager = SceneSettingsManager::GetSingleton();
		const auto revision = manager->GetEntryPresentationRevision();
		const auto& rows = session.rows.Get(revision, [&] {
			return BuildRows(filters.direction == CopyDirection::From ? manager->GetCopySources(session.page) :
			                                                            manager->GetCopyDestinations(session.page));
		});
		const auto list = BuildVisibleList(rows);
		const bool commitRequested = DrawList(list, UpdateCursor(list));

		const auto cursorIt = session.cursor ? std::ranges::find(rows, *session.cursor, &SceneRow::scene) : rows.end();
		DrawPeriodChips(cursorIt == rows.end() ? nullptr : &*cursorIt);
		ImGui::Spacing();

		// A double-click commits alongside whatever is ticked, so its row joins this plan without staying ticked.
		assert(!commitRequested || session.cursor);
		const bool tickAdded = commitRequested && filters.direction == CopyDirection::To &&
		                       session.ticked.insert(*session.cursor).second;
		const auto plan = BuildPlan(rows);
		if (tickAdded)
			session.ticked.erase(*session.cursor);
		const auto& summary = session.summary.Get(
			revision,
			[&] {
				session.summaryPlan = plan;
				return BuildSummary(plan);
			},
			plan != session.summaryPlan);
		DrawSummary(plan, summary);
		ImGui::Spacing();

		if (const auto decision = DrawButtons(summary, commitRequested)) {
			RunCopy(plan, *decision);
			ImGui::CloseCurrentPopup();
		}
	}
}

void SceneCopyModal::Open(const SceneContextId& page)
{
	session = { .page = page,
		.pageName = SceneSettingsManager::GetSingleton()->GetSceneContextDisplayName(page),
		.focusSearch = true,
		.active = true,
		.pendingOpen = true };
	ResetSelection();
}

void SceneCopyModal::Draw(const SceneContextId& page)
{
	if (!session.active || session.page != page)
		return;

	const auto title = std::format("{}{}", T(TKEY("scene_page_copy_title"), "Copy settings"), kPopupId);
	if (std::exchange(session.pendingOpen, false))
		ImGui::OpenPopup(title.c_str());

	const float width = ImGui::GetFontSize() * kModalWidthEm;
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
	bool open = true;
	if (auto popup = Util::CenteredPopupModal(title.c_str(), &open))
		DrawContents();
	if (!ImGui::IsPopupOpen(title.c_str()))
		session.active = false;
}

bool SceneCopyModal::HasSources(const SceneContextId& page)
{
	auto* manager = SceneSettingsManager::GetSingleton();
	const auto frame = ImGui::GetFrameCount();
	std::erase_if(sourceChecks, [&](const auto& entry) {
		return entry.first != page && frame - entry.second.lastUsedFrame > kSourceCheckRetentionFrames;
	});
	auto& check = sourceChecks[page];
	check.lastUsedFrame = frame;
	return check.hasSources.Get(manager->GetEntryPresentationRevision(),
		[&] { return !manager->GetCopySources(page).empty(); });
}

#undef I18N_KEY_PREFIX
