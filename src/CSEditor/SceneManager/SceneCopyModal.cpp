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
#include <tuple>
#include <utility>
#include <vector>

#include <imgui.h>

#include "../../I18n/I18n.h"
#include "../Browser/BrowserWidgets.h"
#include "../EditorWindow.h"
#include "Features/CSEditor.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/LocationTargetIcons.h"
#include "Menu/Icons/helpers/WeatherTypeIcons.h"
#include "Menu/MenuIcons.h"
#include "SceneSettingsContextRules.h"
#include "Utils/SettingsCatalog.h"
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
	constexpr float kModalWidthEm = 50.0f;
	constexpr float kPaneHeightEm = 17.0f;
	constexpr float kSearchWidthEm = 10.0f;
	constexpr float kPeriodDotPitchEm = 0.5f;
	constexpr float kPeriodDotRadiusEm = 0.16f;
	/// Large fonts would otherwise push the modal past the screen edge.
	constexpr float kMaxViewportWidthShare = 0.9f;
	/// The pick pane's share of the width; the review pane takes the rest.
	constexpr float kPickPaneShare = 0.56f;
	/// Route card fill and border alphas, matching the tinted chips.
	constexpr float kCardFillAlpha = 0.14f;
	constexpr float kCardBorderAlpha = 0.5f;

	constexpr size_t kSearchBufferSize = 256;

	constexpr ImGuiTableFlags kDetailsTableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

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

	/** @brief How one setting fares in a copy; also the order the summary chips list them in. */
	enum class CopyOutcome
	{
		Copy,
		AlreadySet,
		Dropped
	};
	constexpr std::array kOutcomes{ CopyOutcome::Copy, CopyOutcome::AlreadySet, CopyOutcome::Dropped };

	/** @brief A glyph and its colour, resolved when the rows are built rather than every frame. */
	struct TintedGlyph
	{
		Icons::GlyphRef glyph;
		ImVec4 color;
	};

	/** @brief One scene (a weather, a location, the interior or the time of day) with the sets it resolves. */
	struct SceneRow
	{
		SceneContextId scene;
		std::string name;
		TintedGlyph icon;
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

	/** @brief One details-table row: a setting that fares the same way in every destination it names. */
	struct DetailRow
	{
		std::string feature;
		std::string name;
		std::string value;
		/// The destination's current value, which Overwrite would replace.
		std::optional<std::string> previous;
		CopyRejection rejection = CopyRejection::None;
		CopyOutcome outcome = CopyOutcome::Copy;
		size_t destinationCount = 0;
		/// Newline-joined, ready for the tooltip.
		std::string destinationNames;
	};

	/** @brief Setting counts across every destination, grouped the way the copy lands them. */
	struct CopySummary
	{
		std::array<size_t, kOutcomes.size()> counts{};
		std::map<CopyRejection, size_t> dropped;
		/// Sorted by feature, so the table can head each run of rows with its feature.
		std::vector<DetailRow> details;

		size_t Count(CopyOutcome outcome) const { return counts[std::to_underlying(outcome)]; }
	};

	/** @brief One opening of the modal. Only the page that opened it draws it: a popup opened under one
	 *  page's ID stack cannot be drawn under another's. */
	struct Session
	{
		SceneContextId page;
		std::string pageName;
		TintedGlyph pageIcon;
		char search[kSearchBufferSize]{};
		/// Keyed by scene, so a ticked row stays ticked while a filter hides it.
		std::set<SceneContextId> ticked;
		/// From's source, and To's target while nothing is ticked.
		std::optional<SceneContextId> cursor;
		PeriodSet periods;
		int cursorStep = 0;
		bool scrollToCursor = false;
		bool showUnauthored = false;
		bool focusSearch = false;
		Util::DeferredModal modal;
		/// The swap button sits above the list, so it applies next frame rather than under rows already fetched.
		bool swapPending = false;
		/// Enter commits only from the search box or a row, never from a chip or button it would also press.
		bool enterCommits = false;
		/// The outcome chip narrowing the details table, if any.
		std::optional<CopyOutcome> detailsFilter;
		SceneSettingsManager::RevisionCache<std::vector<SceneRow>> rows;
		SceneSettingsManager::RevisionCache<CopySummary> summary;
		CopyPlan summaryPlan;
	};
	Session session;

	/** @brief One line of the list: a section heading, a scene, or the link that reveals hidden scenes. */
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

	using BrowserUI::ChipFace;
	using BrowserUI::FilterChip;
	using Util::Settings::FormatValue;

	/** @brief What one side of the preview names, and the glyph leading it. */
	struct PreviewSide
	{
		std::string label;
		TintedGlyph icon;
	};

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

	/** @brief Glyph standing for a whole scene type. */
	Icons::GlyphRef GetContextTypeIcon(SceneContextType type)
	{
		switch (type) {
		case SceneContextType::Interior:
			return Icons::TI(ICON_TI_HOME);
		case SceneContextType::Weather:
			return WeatherTypeIcons::kCloudy;
		case SceneContextType::Location:
			return Icons::TI(MenuIcons::kLocations);
		default:
			return Icons::TI(ICON_TI_CLOCK);
		}
	}

	/** @brief A scene's own glyph: its weather class or location type, else its type's glyph. */
	TintedGlyph GetSceneIcon(const SceneContextId& scene)
	{
		const ImVec4 secondary = Util::Colors::GetSecondary();
		switch (scene.type) {
		case SceneContextType::Weather:
			{
				auto* weather = RE::TESForm::LookupByID<RE::TESWeather>(scene.weatherId);
				return { WeatherTypeIcons::ResolveGlyph(weather), CSEditor::GetWeatherTypeColor(weather) };
			}
		case SceneContextType::Location:
			return { LocationTargetIcons::Resolve({ .category = static_cast<LocationTargetIcons::Category>(scene.locationType),
						 .formKey = scene.locationFormKey }),
				secondary };
		default:
			return { GetContextTypeIcon(scene.type), secondary };
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

	/** @brief Chip for a scene type. */
	ChipFace DescribeContextType(SceneContextType type)
	{
		return { GetContextTypeLabel(type), GetContextTypeIcon(type) };
	}

	/** @brief Chip for a weather classification flag, in the flag's colour. */
	ChipFace DescribeWeatherClass(WeatherFlag flag)
	{
		const auto face = [flag](const char* label, Icons::GlyphRef icon) {
			return ChipFace{ label, icon, CSEditor::GetWeatherFlagColor(flag) };
		};
		switch (flag) {
		case WeatherFlag::kPleasant:
			return face(T("feature.cs_editor.pleasant", "Pleasant"), WeatherTypeIcons::kPleasant);
		case WeatherFlag::kCloudy:
			return face(T("feature.cs_editor.cloudy", "Cloudy"), WeatherTypeIcons::kCloudy);
		case WeatherFlag::kRainy:
			return face(T("feature.cs_editor.rainy", "Rainy"), WeatherTypeIcons::kRain);
		default:
			return face(T("feature.cs_editor.snow", "Snow"), WeatherTypeIcons::kSnow);
		}
	}

	/** @brief Chip for a location target type. */
	ChipFace DescribeLocationType(LocationTargetType type)
	{
		return { SceneSettingsContextRules::GetCopyLocationTypeName(type),
			LocationTargetIcons::ResolveCategory(static_cast<LocationTargetIcons::Category>(type)) };
	}

	/** @brief A translated "{}" pattern filled with one count. */
	std::string FormatCount(const char* format, size_t count)
	{
		return std::vformat(format, std::make_format_args(count));
	}

	/** @brief The glyph an outcome's chip and rows carry, in the outcome's colour. */
	TintedGlyph GetOutcomeIcon(CopyOutcome outcome)
	{
		switch (outcome) {
		case CopyOutcome::Copy:
			return { Icons::TI(ICON_TI_CHECK), Util::Colors::GetSuccess() };
		case CopyOutcome::AlreadySet:
			return { Icons::TI(ICON_TI_REFRESH), Util::Colors::GetInfo() };
		default:
			return { Icons::TI(ICON_TI_X), Util::Colors::GetWarning() };
		}
	}

	/** @brief The translated "{}" pattern an outcome's chip counts with. */
	const char* GetOutcomeCountFormat(CopyOutcome outcome)
	{
		switch (outcome) {
		case CopyOutcome::Copy:
			return T(TKEY("scene_copy_will_copy"), "{} will copy");
		case CopyOutcome::AlreadySet:
			return T(TKEY("scene_copy_already_set"), "{} already set");
		default:
			return T(TKEY("scene_copy_dropped"), "{} dropped");
		}
	}

	/** @brief What happens to one details row, for its glyph's tooltip. */
	const char* GetDetailTooltip(const DetailRow& row)
	{
		switch (row.outcome) {
		case CopyOutcome::Copy:
			return T(TKEY("scene_copy_row_copy_tooltip"), "Copies into the destination.");
		case CopyOutcome::AlreadySet:
			return T(TKEY("scene_copy_row_already_set_tooltip"),
				"The destination already holds this. Only Overwrite replaces it.");
		default:
			return GetRejectionText(row.rejection);
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
					.icon = GetSceneIcon(scene),
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

	/** @brief The row for a scene, or null when the list no longer holds it. */
	const SceneRow* FindRow(const std::vector<SceneRow>& rows, const SceneContextId& scene)
	{
		const auto it = std::ranges::find(rows, scene, &SceneRow::scene);
		return it == rows.end() ? nullptr : &*it;
	}

	/** @brief Whether a row passes the chips and the search. The authored filter is applied by the caller. */
	bool MatchesFilters(const SceneRow& row, const std::string& query)
	{
		if (filters.type && row.scene.type != *filters.type)
			return false;
		if (filters.type == SceneContextType::Weather && filters.weatherClass &&
			!(row.weatherFlags & std::to_underlying(*filters.weatherClass)))
			return false;
		if (filters.type == SceneContextType::Location && filters.locationType &&
			row.scene.locationType != *filters.locationType)
			return false;
		return Util::StringMatchesSearch(row.name, query);
	}

	/** @brief The lines the list draws: current scenes pinned first, then one section per type in chip order, then
	 *  the way to reveal hidden ones. The headings stand in for a type column. */
	VisibleList BuildVisibleList(const std::vector<SceneRow>& rows, const std::string& query)
	{
		VisibleList list;
		const bool authoredOnly = filters.direction == CopyDirection::To && !session.showUnauthored;
		std::array<std::vector<const SceneRow*>, kContextTypes.size() + 1> sections;
		for (const auto& row : rows) {
			if (!MatchesFilters(row, query))
				continue;
			// A current scene stays listed with nothing authored: it is the likeliest target.
			if (row.current)
				sections.front().push_back(&row);
			else if (authoredOnly && !row.authored)
				++list.hiddenUnauthored;
			else
				sections[1 + static_cast<size_t>(std::ranges::find(kContextTypes, row.scene.type) - kContextTypes.begin())]
					.push_back(&row);
		}

		// Headings only earn their space once there are sections to tell apart.
		const bool headed = std::ranges::count_if(sections, [](const auto& section) { return !section.empty(); }) > 1;
		for (size_t index = 0; index < sections.size(); ++index) {
			if (headed && !sections[index].empty())
				list.lines.push_back({ .kind = ListLine::Kind::Header,
					.text = index == 0 ? T(TKEY("scene_copy_current"), "Current") : GetContextTypeLabel(kContextTypes[index - 1]) });
			for (const auto* row : sections[index])
				list.lines.push_back({ .kind = ListLine::Kind::Scene, .row = row });
		}
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

	/** @brief An "All" chip, then one chip per option, wrapping as needed; picking one narrows the filter to it. */
	template <typename Option, size_t Count, typename Describe>
	void DrawFilterChips(const char* id, std::optional<Option>& filter, const std::array<Option, Count>& options,
		Describe describe)
	{
		ImGui::PushID(id);
		if (FilterChip("##all", { T(TKEY("filter_all"), "All") }, !filter))
			filter.reset();
		for (const auto option : options) {
			const ChipFace face = describe(option);
			BrowserUI::SameLineIfFits(BrowserUI::MeasureChip(face.label, face.icon.IsValid()));
			ImGui::PushID(static_cast<int>(option));
			if (FilterChip("##chip", face, filter == option))
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
		if (filters.direction == CopyDirection::From) {
			const auto* row = session.cursor ? FindRow(rows, *session.cursor) : nullptr;
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
			const auto* row = FindRow(rows, scene);
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
		// A setting that fares alike in several destinations is one row, so its key holds everything the row shows.
		using RowKey = std::tuple<SceneSettingsManager::SettingIdentity, CopyOutcome, std::optional<std::string>, CopyRejection>;
		std::map<RowKey, size_t> rowIndices;
		for (const auto& destination : plan.destinations) {
			const auto candidates = manager->GetCopyCandidates(plan.source, destination.context);
			std::map<SceneSettingsContextRules::CopyGroupKey, std::vector<const CopyCandidate*>> groups;
			for (const auto& candidate : candidates)
				groups[SceneSettingsContextRules::GetCopyGroupKey(candidate.setting)].push_back(&candidate);
			for (const auto& [groupKey, group] : groups) {
				const auto outcome = std::ranges::any_of(group, std::logical_not{}, &CopyCandidate::compatible) ?
				                         CopyOutcome::Dropped :
				                     std::ranges::any_of(group, std::identity{}, &CopyCandidate::conflicts) ?
				                         CopyOutcome::AlreadySet :
				                         CopyOutcome::Copy;
				summary.counts[std::to_underlying(outcome)] += group.size();
				for (const auto* candidate : group) {
					if (outcome == CopyOutcome::Dropped)
						++summary.dropped[candidate->rejection];
					auto previous = outcome == CopyOutcome::AlreadySet && candidate->destinationValue ?
					                    std::optional{ FormatValue(*candidate->destinationValue) } :
					                    std::nullopt;
					const auto [indexIt, inserted] = rowIndices.try_emplace(
						RowKey{ candidate->setting, outcome, previous, candidate->rejection }, summary.details.size());
					if (inserted)
						summary.details.push_back({ .feature = SceneSettingsManager::GetFeatureDisplayName(
														candidate->setting.featureShortName),
							.name = candidate->displayName,
							.value = FormatValue(candidate->value),
							.previous = std::move(previous),
							.rejection = candidate->rejection,
							.outcome = outcome });
					auto& row = summary.details[indexIt->second];
					++row.destinationCount;
					if (!row.destinationNames.empty())
						row.destinationNames += '\n';
					row.destinationNames += destination.name;
				}
			}
		}
		std::ranges::stable_sort(summary.details, {}, &DetailRow::feature);
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

	/** @brief Flips which side the page is on. The list is refetched and the picks start over. */
	void SwapDirection()
	{
		filters.direction = filters.direction == CopyDirection::To ? CopyDirection::From : CopyDirection::To;
		session.rows = {};
		ResetSelection();
	}

	/** @brief Search and the type chips. */
	void DrawFilterRow()
	{
		if (std::exchange(session.focusSearch, false))
			ImGui::SetKeyboardFocusHere();
		BrowserUI::SearchField("##search", session.search, sizeof(session.search), T("ui.search", "Search..."),
			ImGui::GetFontSize() * kSearchWidthEm, true, ImGuiInputTextFlags_CallbackHistory, OnSearchHistory,
			&session.cursorStep);
		session.enterCommits = ImGui::IsItemFocused();
		ImGui::SameLine();
		DrawFilterChips("type", filters.type, kContextTypes, DescribeContextType);
	}

	/** @brief The narrower chips the Weather and Location types offer and, in To, the authored-only toggle. */
	void DrawContextRow(const std::vector<SceneRow>& rows, const std::string& query)
	{
		if (filters.type == SceneContextType::Weather)
			DrawFilterChips("weatherClass", filters.weatherClass, kWeatherClasses, DescribeWeatherClass);
		else if (filters.type == SceneContextType::Location)
			DrawFilterChips("locationType", filters.locationType, SceneSettingsManager::kLocationTargetTypes,
				DescribeLocationType);
		else  // Holds the row's height, so the list does not jump as the type changes.
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));

		// From lists only scenes that hold settings, so there is nothing for the toggle to hide.
		if (filters.direction != CopyDirection::To)
			return;
		const auto authoredCount = static_cast<int>(
			std::ranges::count_if(rows, [&](const SceneRow& row) { return row.authored && MatchesFilters(row, query); }));
		const float width = BrowserUI::MeasureToggleChip(authoredCount);
		BrowserUI::SameLineIfFits(width);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
		const auto tooltip = std::format("{}\n{}", T(TKEY("scene_copy_authored_only"), "Authored only"),
			T(TKEY("scene_copy_authored_only_tooltip"), "Hides weathers and locations that hold no settings yet."));
		if (BrowserUI::ToggleChip("##authoredOnly", BrowserUI::GlyphIcon(Icons::TI(ICON_TI_PENCIL)), authoredCount,
				!session.showUnauthored, Util::Colors::GetInfo(), tooltip.c_str()))
			session.showUnauthored = !session.showUnauthored;
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

	/** @brief A glyph in a font-sized box `height` tall, so the text after it lines up whatever the glyph's width. */
	void DrawTintedGlyph(const TintedGlyph& icon, float height)
	{
		const float box = ImGui::GetFontSize();
		const ImVec2 min = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(box, height));
		Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), ImVec2(min.x, min.y + (height - box) * 0.5f),
			ImVec2(box, box), icon.glyph, ImGui::GetColorU32(icon.color));
		ImGui::SameLine();
	}

	/** @brief One dot per period, filled where that period's set holds settings; rings when one set covers them all. */
	void DrawPeriodDots(const SceneRow& row)
	{
		const float pitch = ImGui::GetFontSize() * kPeriodDotPitchEm;
		const float radius = ImGui::GetFontSize() * kPeriodDotRadiusEm;
		const float height = ImGui::GetFrameHeight();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(pitch * SceneSettingsManager::kPeriodCount, height));

		const ImU32 flatColor = ImGui::GetColorU32(Util::Colors::GetInfo());
		const ImU32 setColor = ImGui::GetColorU32(Util::Colors::GetAccent());
		const ImU32 emptyColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		for (const auto period : SceneSettingsManager::kPeriods) {
			const auto index = static_cast<float>(static_cast<size_t>(period));
			const ImVec2 center(origin.x + pitch * (index + 0.5f), origin.y + height * 0.5f);
			if (row.flat) {
				Util::DrawIconCircle(center, radius, flatColor, false);
				continue;
			}
			const auto* set = row.FindSet(period);
			Util::DrawIconCircle(center, radius, set && set->settingCount != 0 ? setColor : emptyColor, true);
		}
		Util::AddTooltip(row.flat ? T(TKEY("scene_copy_dots_flat_tooltip"), "One set covers every period.") :
									T(TKEY("scene_copy_dots_tooltip"), "Filled periods hold settings."));
	}

	/** @brief One scene: click highlights it, the tick box adds it to To's targets, double-click commits.
	 *  @return Whether a double-click asked to commit. */
	bool DrawSceneRow(const SceneRow& row, bool isCursor, bool to)
	{
		if (row.current)
			BrowserUI::RowStripe(ImGui::GetColorU32(Util::Colors::GetSuccess()), BrowserUI::RowHeight());
		const bool pressed = Util::TableRowSelectable("##row", isCursor, BrowserUI::kTickRowFlags | ImGuiSelectableFlags_AllowDoubleClick);
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
		DrawTintedGlyph(row.icon, ImGui::GetFrameHeight());
		const char* live = T(TKEY("badge_live"), "LIVE");
		const float badgeWidth = row.current ? Util::MeasureBadgeWidth(live) + ImGui::GetStyle().ItemSpacing.x : 0.0f;
		BrowserUI::EllipsizedText(row.name.c_str(), std::max(1.0f, ImGui::GetContentRegionAvail().x - badgeWidth),
			ImGui::GetStyleColorVec4(ImGuiCol_Text));
		if (row.current)
			BrowserUI::InlineBadge(live, Util::Colors::GetSuccess());

		ImGui::TableNextColumn();
		DrawPeriodDots(row);

		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		Util::Text::Secondary("%zu", row.settingCount);
		Util::AddTooltip(T(TKEY("scene_copy_count_tooltip"), "Settings this context holds."));
		return doubleClicked;
	}

	/** @brief Draws the list and takes its clicks.
	 *  @param reservedHeight Height left below the list for the rows that follow it in the pane.
	 *  @return Whether a double-click asked to commit. */
	bool DrawList(const VisibleList& list, int cursorLine, float reservedHeight)
	{
		const bool to = filters.direction == CopyDirection::To;
		if (!BrowserUI::BeginListTable("##rows", to ? 4 : 3, ImVec2(0.0f, -reservedHeight), list.lines.empty(),
				T(TKEY("scene_copy_empty"), "Nothing matches.")))
			return false;

		if (to)
			ImGui::TableSetupColumn("##tick", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("##periods", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("##count", ImGuiTableColumnFlags_WidthFixed);

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
				if (line.kind != ListLine::Kind::Scene) {
					if (to)
						ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
				}
				switch (line.kind) {
				case ListLine::Kind::Header:
					BrowserUI::SectionLabel(line.text);
					break;
				case ListLine::Kind::ShowUnauthored:
					if (BrowserUI::Link("##showUnauthored",
							FormatCount(T(TKEY("scene_copy_show_unauthored"), "{} without settings, show all"),
								list.hiddenUnauthored)
								.c_str()))
						session.showUnauthored = true;
					break;
				case ListLine::Kind::Scene:
					commitRequested |= DrawSceneRow(*line.row, index == cursorLine, to);
					break;
				}
				ImGui::PopID();
			}
		}
		BrowserUI::EndListTable();
		return commitRequested;
	}

	/** @brief To's tick controls: tick every listed scene, and a count of the ticks, including hidden ones, that clears them. */
	void DrawSelectionFooter(const VisibleList& list)
	{
		auto sceneLines =
			list.lines | std::views::filter([](const ListLine& line) { return line.kind == ListLine::Kind::Scene; });
		const auto selectedShown = static_cast<size_t>(std::ranges::count_if(sceneLines,
			[](const ListLine& line) { return session.ticked.contains(line.row->scene); }));
		const auto action = BrowserUI::SelectionFooter(session.ticked.size(), selectedShown,
			T(TKEY("scene_copy_clear_ticks_tooltip"), "Unticks every scene, shown or hidden."));
		if (action == BrowserUI::SelectionAction::SelectShown)
			for (const auto& line : sceneLines)
				session.ticked.insert(line.row->scene);
		else if (action == BrowserUI::SelectionAction::Clear)
			session.ticked.clear();
	}

	/** @brief The shared period row: which periods receive in To, which one is copied out of in From. */
	void DrawPeriodChips(const SceneRow* cursorRow)
	{
		const bool from = filters.direction == CopyDirection::From;
		// From shows the period it will really copy, which falls back when the source lacks the ticked one.
		const auto sourcePeriod = from && cursorRow && !cursorRow->flat ?
		                              std::optional{ GetSourcePeriod(*cursorRow) } :
		                              std::nullopt;
		ImGui::AlignTextToFramePadding();
		Util::Text::Secondary("%s", T(TKEY("scene_copy_periods"), "Periods"));
		Util::AddTooltip(from ? T(TKEY("scene_copy_periods_from_tooltip"),
									"The period the settings are read from. A source with one set for every period has nothing to pick.") :
								T(TKEY("scene_copy_periods_to_tooltip"), "The periods that receive the settings."));
		ImGui::SameLine();
		ImGui::PushID("periods");
		{
			// A flat source has one set for every period, so there is nothing to pick.
			const Util::DisableGuard flatGuard(from && (!cursorRow || cursorRow->flat));
			for (const auto period : SceneSettingsManager::kPeriods) {
				const auto bit = static_cast<size_t>(period);
				if (bit != 0)
					ImGui::SameLine();
				ImGui::PushID(static_cast<int>(bit));
				const Util::DisableGuard missingGuard(from && cursorRow && !cursorRow->FindSet(period));
				const bool selected = sourcePeriod ? *sourcePeriod == period : session.periods.test(bit);
				if (FilterChip("##period", { SceneSettingsContextRules::GetCopyPeriodName(period) }, selected)) {
					if (from)
						session.periods.reset();
					session.periods.set(bit, from || !session.periods.test(bit));
				}
				ImGui::PopID();
			}
		}
		if (!from) {
			ImGui::SameLine();
			if (FilterChip("##all", { T(TKEY("filter_all"), "All") }, session.periods.all()))
				session.periods.set();
		}
		ImGui::PopID();
	}

	/** @brief To's destination side: one set by name, else how many sets across how many scenes. */
	PreviewSide DescribeDestinations(const CopyPlan& plan, const std::vector<SceneRow>& rows)
	{
		const auto& first = plan.destinations.front();
		const auto* firstRow = FindRow(rows, GetSceneOf(first.context));
		assert(firstRow);
		if (plan.destinations.size() == 1)
			return { first.name, firstRow->icon };

		auto contextCount = plan.destinations.size();
		const auto scenes = plan.destinations |
		                    std::views::transform([](const Destination& destination) { return GetSceneOf(destination.context); }) |
		                    std::ranges::to<std::set>();
		if (scenes.size() == 1)
			return { std::vformat(T(TKEY("scene_copy_scene_periods"), "{}, {} periods"),
						 std::make_format_args(firstRow->name, contextCount)),
				firstRow->icon };
		auto sceneCount = scenes.size();
		return { std::vformat(T(TKEY("scene_copy_contexts_in_scenes"), "{} contexts in {} scenes"),
					 std::make_format_args(contextCount, sceneCount)),
			{ Icons::TI(ICON_TI_STACK_2), Util::Colors::GetSecondary() } };
	}

	/** @brief The dimmed arrow pointing from what a copy reads to what it writes. */
	void DrawArrow()
	{
		const Icons::GlyphRef arrow = Icons::TI(ICON_TI_CHEVRON_RIGHT);
		Icons::FontGuard font(arrow);
		Util::Text::Disabled("%s", arrow.utf8);
	}

	/** @brief One end of the route: a caption over the side's glyph and name, or a hint while it is unpicked.
	 *  The page's own end is tinted in the accent, so it reads as the fixed side of the copy. */
	void DrawRouteCard(const char* caption, const std::optional<PreviewSide>& side, bool page, float width)
	{
		const auto& style = ImGui::GetStyle();
		const ImVec2 pad(style.FramePadding.x * 2.0f, style.FramePadding.y);
		const float height = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetTextLineHeight() + pad.y * 2.0f;
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 max(min.x + width, min.y + height);
		const ImVec4 tint = page ? Util::Colors::GetAccent() : ImGui::GetStyleColorVec4(ImGuiCol_Border);
		auto* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(min, max,
			page ? ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, kCardFillAlpha)) : ImGui::GetColorU32(ImGuiCol_FrameBg),
			style.FrameRounding);
		drawList->AddRect(min, max, ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, kCardBorderAlpha)), style.FrameRounding);

		ImGui::SetCursorScreenPos(ImVec2(min.x + pad.x, min.y + pad.y));
		Util::TextUnformattedDisabled(caption);
		ImGui::SetCursorScreenPos(ImVec2(min.x + pad.x, ImGui::GetCursorScreenPos().y));
		if (side) {
			DrawTintedGlyph(side->icon, ImGui::GetTextLineHeight());
			BrowserUI::EllipsizedText(side->label.c_str(), max.x - pad.x - ImGui::GetCursorScreenPos().x,
				ImGui::GetStyleColorVec4(ImGuiCol_Text));
		} else {
			Util::TextUnformattedDisabled(T(TKEY("scene_copy_pick_from_list"), "Pick from the list"));
		}
		// The card itself is the item, so the next widget lays out after it rather than after its text.
		ImGui::SetCursorScreenPos(min);
		ImGui::Dummy(ImVec2(width, height));
	}

	/** @brief From card, swap button, To card. The page fills its own end; the other end names what the list
	 *  picks. Shows the picks as of the frame's start: a click in the list below lands here next frame. */
	void DrawRoute(const CopyPlan& plan, const std::vector<SceneRow>& rows)
	{
		const bool from = filters.direction == CopyDirection::From;
		const std::optional<PreviewSide> page = PreviewSide{ session.pageName, session.pageIcon };
		std::optional<PreviewSide> other;
		if (!plan.destinations.empty() && from) {
			const auto* sourceRow = FindRow(rows, GetSceneOf(plan.source));
			assert(sourceRow);
			other = PreviewSide{ plan.sourceName, sourceRow->icon };
		} else if (!plan.destinations.empty()) {
			other = DescribeDestinations(plan, rows);
		}

		const float swapSize = BrowserUI::IconButtonSize();
		const float cardWidth = (ImGui::GetContentRegionAvail().x - swapSize - ImGui::GetStyle().ItemSpacing.x * 2.0f) * 0.5f;
		const float top = ImGui::GetCursorPosY();
		DrawRouteCard(from ? T(TKEY("scene_copy_from"), "From") : T(TKEY("scene_copy_from_page"), "From this page"),
			from ? other : page, !from, cardWidth);
		const float cardHeight = ImGui::GetItemRectSize().y;

		ImGui::SameLine();
		ImGui::SetCursorPosY(top + (cardHeight - swapSize) * 0.5f);
		const auto tooltip = std::format("{}\n{}", T(TKEY("scene_copy_swap"), "Swap direction"),
			T(TKEY("scene_copy_swap_tooltip"), "Switches between copying out of this page and copying into it. Starts the picks over."));
		if (BrowserUI::IconButton("##swap", Icons::TI(ICON_TI_ARROWS_EXCHANGE), tooltip.c_str(), false,
				ImGui::GetColorU32(ImGuiCol_Text)))
			session.swapPending = true;

		ImGui::SameLine();
		ImGui::SetCursorPosY(top);
		DrawRouteCard(from ? T(TKEY("scene_copy_to_page"), "To this page") : T(TKEY("scene_copy_to"), "To"),
			from ? page : other, from, cardWidth);
	}

	/** @brief One outcome's count as a chip that narrows the details to its rows; Dropped's tooltip lists why. */
	void DrawOutcomeChip(const CopySummary& summary, CopyOutcome outcome)
	{
		const auto count = summary.Count(outcome);
		const auto icon = GetOutcomeIcon(outcome);
		const bool active = session.detailsFilter == outcome;
		ImGui::PushID(static_cast<int>(outcome));
		{
			const Util::DisableGuard empty(count == 0);
			if (BrowserUI::Chip("##outcome", FormatCount(GetOutcomeCountFormat(outcome), count).c_str(), icon.glyph,
					&icon.color, active ? &icon.color : nullptr))
				session.detailsFilter = active ? std::nullopt : std::optional{ outcome };
		}
		ImGui::PopID();

		std::string tooltip;
		if (outcome == CopyOutcome::Dropped)
			for (const auto& [rejection, reasonCount] : summary.dropped) {
				const char* reason = GetRejectionText(rejection);
				tooltip += std::vformat(T(TKEY("scene_copy_dropped_reason"), "{} dropped: {}"),
					std::make_format_args(reasonCount, reason));
				tooltip += '\n';
			}
		tooltip += active ? T(TKEY("scene_copy_filter_clear_tooltip"), "Click to list every setting again.") :
		                    T(TKEY("scene_copy_filter_tooltip"), "Click to list only these in the details.");
		Util::AddTooltip(tooltip.c_str());
	}

	/** @brief One details row: outcome glyph and name, then the value, or the old value it replaces and the new.
	 *  @param merged Whether the plan has several destinations, so the row says how many it stands for. */
	void DrawDetailRow(const DetailRow& row, bool merged)
	{
		const char* tooltip = GetDetailTooltip(row);
		const bool dropped = row.outcome == CopyOutcome::Dropped;
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		DrawTintedGlyph(GetOutcomeIcon(row.outcome), ImGui::GetTextLineHeight());
		Util::AddTooltip(tooltip);
		if (dropped)
			Util::TextUnformattedDisabled(row.name.c_str());
		else
			ImGui::TextUnformatted(row.name.c_str());
		if (merged) {
			ImGui::SameLine();
			Util::Text::Secondary("×%zu", row.destinationCount);
			Util::AddTooltip(row.destinationNames.c_str());
		}

		ImGui::TableNextColumn();
		if (row.previous) {
			Util::TextUnformattedDisabled(row.previous->c_str());
			ImGui::SameLine();
			DrawArrow();
			ImGui::SameLine();
		}
		if (dropped) {
			Util::TextUnformattedDisabled(row.value.c_str());
			Util::AddTooltip(tooltip);
		} else {
			ImGui::TextUnformatted(row.value.c_str());
		}
	}

	/** @brief The chip-filtered rows under a heading per feature, filling the rest of the review pane. */
	void DrawDetails(const CopySummary& summary, size_t destinationCount)
	{
		if (destinationCount == 0) {
			BrowserUI::EmptyState(T(TKEY("scene_copy_pick"), "Pick a context to copy."),
				T(TKEY("scene_copy_pick_hint"), "Every setting it would write shows here first."));
			return;
		}
		if (!ImGui::BeginTable("##changes", 2, kDetailsTableFlags))
			return;
		ImGui::TableSetupColumn(T(TKEY("scene_page_copy_column_setting"), "Setting"));
		ImGui::TableSetupColumn(T(TKEY("scene_page_copy_column_change"), "Change"));
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();

		std::vector<const DetailRow*> rows;
		for (const auto& row : summary.details)
			if (!session.detailsFilter || row.outcome == *session.detailsFilter)
				rows.push_back(&row);
		for (const auto section : rows | std::views::chunk_by([](const DetailRow* lhs, const DetailRow* rhs) {
				 return lhs->feature == rhs->feature;
			 })) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			BrowserUI::SectionLabel(section.front()->feature.c_str());
			for (const auto* row : section)
				DrawDetailRow(*row, destinationCount > 1);
		}
		ImGui::EndTable();
	}

	/** @brief A chip per outcome count, then the details table they filter. */
	void DrawSummary(const CopySummary& summary, size_t destinationCount)
	{
		// A filter whose rows went away would leave the table empty with nothing to click to clear it.
		if (session.detailsFilter && summary.Count(*session.detailsFilter) == 0)
			session.detailsFilter.reset();
		for (const auto outcome : kOutcomes) {
			// Unlike the other two, a zero drop count is not worth a chip.
			if (outcome == CopyOutcome::Dropped && summary.Count(outcome) == 0)
				continue;
			if (outcome != kOutcomes.front())
				ImGui::SameLine();
			DrawOutcomeChip(summary, outcome);
		}
		DrawDetails(summary, destinationCount);
	}

	/** @brief The guide at the left; Copy and Overwrite, each with the count it would write, then Cancel at the right.
	 *  @return The policy to run, if the user chose one. */
	std::optional<CopyConflictPolicy> DrawFooter(const CopySummary& summary, bool commitRequested)
	{
		ImGui::AlignTextToFramePadding();
		Util::HelpMarker(T(TKEY("scene_copy_guide"),
			"Copies scene settings between this page and other contexts: times of day, interiors, weathers and "
			"locations.\n\n"
			"The arrows between From and To swap the direction: out of this page into the contexts you tick, or "
			"into this page from the one you pick.\n"
			"Search and type chips narrow the list. Authored only hides contexts that hold no settings yet.\n"
			"Periods: when copying out, the periods that receive the settings; when copying in, the period they are "
			"read from.\n"
			"The right side lists what happens to each setting before anything is written.\n"
			"Copy fills only settings not set yet; Overwrite also replaces values already set.\n\n"
			"Use it to start a new weather or location from one you already tuned. It changes only your own scene "
			"settings; export a preset to share them."));

		std::optional<CopyConflictPolicy> decision;
		const auto copyCount = summary.Count(CopyOutcome::Copy);
		const auto alreadySetCount = summary.Count(CopyOutcome::AlreadySet);
		const bool canCopy = copyCount != 0;
		const auto copyLabel = FormatCount(T(TKEY("scene_copy_copy_count"), "Copy {}"), copyCount) + "###copy";
		const auto overwriteLabel =
			FormatCount(T(TKEY("scene_copy_overwrite_count"), "Overwrite {}"), alreadySetCount) + "###overwrite";
		const char* cancelLabel = T(TKEY("cancel"), "Cancel");
		const auto& style = ImGui::GetStyle();
		float width = style.ItemSpacing.x * 2.0f;
		for (const char* label : { copyLabel.c_str(), overwriteLabel.c_str(), cancelLabel })
			width += ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;
		BrowserUI::RightAlign(width);

		ImGui::BeginDisabled(!canCopy);
		if (Util::SuccessButton(copyLabel.c_str()))
			decision = CopyConflictPolicy::SkipExisting;
		ImGui::EndDisabled();
		Util::AddTooltip(T(TKEY("scene_copy_copy_tooltip"),
							 "Copies what is not already set. Enter or a double-click does the same."),
			Util::kTooltipWhenDisabled);

		ImGui::SameLine();
		ImGui::BeginDisabled(alreadySetCount == 0);
		if (Util::WarningButton(overwriteLabel.c_str()))
			decision = CopyConflictPolicy::OverwriteExisting;
		ImGui::EndDisabled();
		Util::AddTooltip(T(TKEY("scene_copy_overwrite_tooltip"),
							 "Also replaces the values already set at the destination."),
			Util::kTooltipWhenDisabled);

		ImGui::SameLine();
		if (Util::Button(cancelLabel)) {
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

	/** @brief The modal's body for one frame: the route and filters across the top, the pick pane beside the
	 *  review pane, then the footer. */
	void DrawContents()
	{
		if (EditorWindow::ClosePopupOnEscape())
			return;
		if (std::exchange(session.swapPending, false))
			SwapDirection();

		auto* manager = SceneSettingsManager::GetSingleton();
		const auto revision = manager->GetEntryPresentationRevision();
		// Fetched after the swap, which drops the cached rows.
		const auto& rows = session.rows.Get(revision, [&] {
			return BuildRows(filters.direction == CopyDirection::From ? manager->GetCopySources(session.page) :
																		manager->GetCopyDestinations(session.page));
		});
		// Built from the search as it stood before this frame's typing, which lands next frame.
		const std::string query = session.search;
		const auto list = BuildVisibleList(rows, query);
		const int cursorLine = UpdateCursor(list);
		DrawRoute(BuildPlan(rows), rows);
		DrawFilterRow();

		const bool to = filters.direction == CopyDirection::To;
		const float paneHeight = ImGui::GetFontSize() * kPaneHeightEm;
		bool commitRequested = false;
		if (ImGui::BeginChild("##pick", ImVec2(ImGui::GetContentRegionAvail().x * kPickPaneShare, paneHeight),
				ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
			DrawContextRow(rows, query);
			// The list leaves room for the selection footer (To only) and the period row.
			commitRequested = DrawList(list, cursorLine, ImGui::GetFrameHeightWithSpacing() * (to ? 2.0f : 1.0f));
			if (to)
				DrawSelectionFooter(list);
			DrawPeriodChips(session.cursor ? FindRow(rows, *session.cursor) : nullptr);
		}
		ImGui::EndChild();

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

		Util::ToolbarDivider();
		if (ImGui::BeginChild("##review", ImVec2(0.0f, paneHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar))
			DrawSummary(summary, plan.destinations.size());
		ImGui::EndChild();

		ImGui::Separator();
		if (const auto decision = DrawFooter(summary, commitRequested)) {
			RunCopy(plan, *decision);
			ImGui::CloseCurrentPopup();
		}
	}
}

void SceneCopyModal::Open(const SceneContextId& page)
{
	session = { .page = page,
		.pageName = SceneSettingsManager::GetSingleton()->GetSceneContextDisplayName(page),
		.pageIcon = GetSceneIcon(page),
		.focusSearch = true };
	session.modal.Request();
	ResetSelection();
}

void SceneCopyModal::Draw(const SceneContextId& page)
{
	if (!session.modal.IsActive() || session.page != page)
		return;
	const auto title = std::format("{}{}", T(TKEY("scene_page_copy_title"), "Copy settings"), kPopupId);
	session.modal.Draw(title.c_str(),
		std::min(ImGui::GetFontSize() * kModalWidthEm, ImGui::GetMainViewport()->WorkSize.x * kMaxViewportWidthShare),
		DrawContents);
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
