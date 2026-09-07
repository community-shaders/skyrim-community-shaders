#include "ScenePageToolbar.h"

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "Menu.h"
#include "ScenePresetExport.h"
#include "SceneTransitionField.h"
#include "Utils/Format.h"
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
	using PeriodScope = SceneSettingsManager::PeriodScope;

	/// Keeps the toolbar off the window's scrollbar, like the widget gutter does.
	constexpr float kRightMargin = 8.0f;

	/// The preview lists every candidate, so it scrolls rather than growing past the screen.
	constexpr float kPreviewWidth = 520.0f;
	constexpr float kPreviewHeight = 300.0f;
	constexpr ImGuiTableFlags kPreviewTableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

	constexpr const char* kCopyPopupId = "##ScenePageCopy";

	/// Height the Weather/Location leaf lists scroll within once they outgrow it, most notably To's
	/// weather list, which offers every loaded weather form rather than only the ones already authored.
	constexpr float kCopyListHeight = 260.0f;

	/// Font sizes ImGui reserves past a menu row's label for its check mark or submenu arrow.
	constexpr float kMenuMarkWidthRatio = 1.2f;

	/// Source/destination enumeration walks every weather period and every location, so each direction is
	/// cached until the entries it counts change. The dropdown recomputes on open, so a stale count never picks.
	struct CopyListCache
	{
		SceneSettingsManager::RevisionCache<std::vector<CopySource>> entries;
		int lastUsedFrame = 0;
	};
	std::map<SceneContextId, CopyListCache> sourceCaches;
	std::map<SceneContextId, CopyListCache> destinationCaches;
	/// A page that has not drawn for this long has been closed or scrolled out of the tree. Evicting on
	/// size instead would drop caches pages are still using, which costs a rebuild every frame.
	constexpr int kSourceCacheRetentionFrames = 120;

	/// A copy whose conflicts the user has yet to resolve. One flow at a time, keyed by its page.
	struct CopyFlow
	{
		SceneContextId source;
		SceneContextId destination;
		std::string sourceName;
		std::vector<CopyCandidate> candidates;
		PeriodScope periodScope = PeriodScope::ActivePeriod;
		bool active = false;
		bool pendingOpen = false;
	};
	CopyFlow copyFlow;

	/// A confirmation one page asked for. Clear belongs to its page and Load Preset is global, but
	/// every page offers both, so each is keyed to the page that asked and only that page draws it.
	struct PageConfirmation
	{
		void Request(const SceneContextId& a_page)
		{
			context = a_page;
			requested = true;
			popup.Request();
		}

		/** @brief Runs a_confirmed on the requesting page once accepted, and drops the request as
		 *  soon as the popup is gone, however it went. */
		template <typename Action>
		void Draw(const SceneContextId& a_page, Action&& a_confirmed)
		{
			if (!requested || context != a_page)
				return;
			if (popup.Draw())
				a_confirmed();
			else if (popup.IsOpen())
				return;
			requested = false;
		}

		Util::ConfirmationPopup popup;
		SceneContextId context;
		bool requested = false;
	};
	PageConfirmation clearConfirmation;
	PageConfirmation loadPresetConfirmation;

	/// Shared cache/eviction logic for both copy directions; only what fetches the list differs.
	template <typename Fetch>
	const std::vector<CopySource>& GetCachedCopyList(
		std::map<SceneContextId, CopyListCache>& caches, const SceneContextId& context, bool forceRefresh, Fetch fetch)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto revision = manager->GetEntryPresentationRevision();
		const auto frame = ImGui::GetFrameCount();
		std::erase_if(caches, [&](const auto& entry) {
			return entry.first != context && frame - entry.second.lastUsedFrame > kSourceCacheRetentionFrames;
		});

		auto& cache = caches[context];
		cache.lastUsedFrame = frame;
		return cache.entries.Get(
			revision, [&] { return fetch(manager, context); }, forceRefresh);
	}

	/// Whether two contexts are different periods of the same otherwise-identical weather or global
	/// time-of-day context. With time of day off, a page already stands in for every period of its
	/// own context at once, so offering another of its periods as a source or destination would just
	/// copy the page into itself.
	bool IsSamePeriodicFamily(const SceneContextId& lhs, const SceneContextId& rhs)
	{
		if (lhs.type != rhs.type)
			return false;
		switch (lhs.type) {
		case SceneContextType::TimeOfDay:
			return true;
		case SceneContextType::Weather:
			return lhs.weatherId == rhs.weatherId;
		default:
			return false;
		}
	}

	std::vector<CopySource> FilterSamePeriodicFamily(
		std::vector<CopySource> entries, const SceneContextId& context, PeriodScope periodScope)
	{
		if (periodScope != PeriodScope::AllPeriods)
			return entries;
		std::erase_if(entries, [&](const auto& entry) { return IsSamePeriodicFamily(entry.context, context); });
		return entries;
	}

	/// Whether a cached list would survive the family filter, for a caller that only needs to know
	/// whether the popup has anything to offer rather than what.
	bool HasUsableEntries(const std::vector<CopySource>& entries, const SceneContextId& context,
		PeriodScope periodScope)
	{
		if (periodScope != PeriodScope::AllPeriods)
			return !entries.empty();
		return std::ranges::any_of(entries,
			[&](const auto& entry) { return !IsSamePeriodicFamily(entry.context, context); });
	}

	const std::vector<CopySource>& GetCachedSources(const SceneContextId& context, bool forceRefresh)
	{
		return GetCachedCopyList(sourceCaches, context, forceRefresh,
			[](auto* manager, const auto& ctx) { return manager->GetCopySources(ctx); });
	}

	const std::vector<CopySource>& GetCachedDestinations(const SceneContextId& context, bool forceRefresh)
	{
		return GetCachedCopyList(destinationCaches, context, forceRefresh,
			[](auto* manager, const auto& ctx) { return manager->GetCopyDestinations(ctx); });
	}

	std::vector<CopySource> GetCopySources(const SceneContextId& context, PeriodScope periodScope, bool forceRefresh)
	{
		return FilterSamePeriodicFamily(GetCachedSources(context, forceRefresh), context, periodScope);
	}

	std::vector<CopySource> GetCopyDestinations(
		const SceneContextId& context, PeriodScope periodScope, bool forceRefresh)
	{
		return FilterSamePeriodicFamily(GetCachedDestinations(context, forceRefresh), context, periodScope);
	}

	/// Heading the source list groups under, matching the type-first order GetCopySources returns.
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

	/// Why one candidate is not going to be copied. Empty when it is.
	const char* GetRejectionText(CopyRejection rejection)
	{
		switch (rejection) {
		case CopyRejection::NotInCatalog:
			return T(TKEY("scene_page_copy_reject_catalog"), "Not a setting a scene can override.");
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

	/// Width one text button occupies, so the toolbar can right-align before drawing anything.
	float ButtonWidth(const char* label)
	{
		return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	}

	void RunCopy(const SceneContextId& source, const SceneContextId& destination, CopyConflictPolicy policy,
		PeriodScope periodScope)
	{
		const auto result = SceneSettingsManager::GetSingleton()->CopySettingsAcrossPeriods(
			source, destination, policy, periodScope);
		auto copied = result.copied;
		auto overwritten = result.overwritten;
		// The preview already explained the incompatible rows; the toast only counts what landed.
		auto skipped = result.skipped + result.incompatible;
		EditorWindow::GetSingleton()->ShowNotification(
			std::vformat(T(TKEY("scene_page_copy_result"), "{} copied, {} overwritten, {} skipped"),
				std::make_format_args(copied, overwritten, skipped)),
			result.Changed() ? Util::Colors::GetSuccess() : Util::Colors::GetWarning());
	}

	/// Dry run first: a copy with nothing to overwrite needs no preview and runs on the click.
	void StartCopy(const SceneContextId& source, const SceneContextId& destination, const std::string& sourceName,
		PeriodScope periodScope)
	{
		auto candidates = SceneSettingsManager::GetSingleton()->GetCopyCandidates(source, destination, periodScope);
		if (std::ranges::none_of(candidates, [](const auto& candidate) { return candidate.conflicts; })) {
			RunCopy(source, destination, CopyConflictPolicy::SkipExisting, periodScope);
			return;
		}
		copyFlow = { .source = source,
			.destination = destination,
			.sourceName = sourceName,
			.candidates = std::move(candidates),
			.periodScope = periodScope,
			.active = true,
			.pendingOpen = true };
	}

	/// One weather form's periods, gathered so the scene picks once before the period does.
	struct WeatherGroup
	{
		std::string displayName;
		std::vector<const CopySource*> periods;
	};

	/// Type/scene tree the From and To submenus share, built once per popup draw from the flat,
	/// type-sorted CopySource list the manager returns.
	struct CopyTree
	{
		const CopySource* interior = nullptr;
		std::vector<const CopySource*> timeOfDay;
		std::vector<WeatherGroup> weather;
		std::vector<const CopySource*> location;
	};

	/// Weather display names are "<weather> / <period>"; the period is always the final segment.
	std::pair<std::string, std::string> SplitWeatherDisplayName(const std::string& displayName)
	{
		const auto separator = displayName.rfind(" / ");
		if (separator == std::string::npos)
			return { displayName, displayName };
		return { displayName.substr(0, separator), displayName.substr(separator + 3) };
	}

	CopyTree BuildCopyTree(std::span<const CopySource> entries)
	{
		CopyTree tree;
		std::map<RE::FormID, WeatherGroup> weatherGroups;
		for (const auto& entry : entries) {
			switch (entry.context.type) {
			case SceneContextType::Interior:
				tree.interior = &entry;
				break;
			case SceneContextType::Weather: {
				auto& group = weatherGroups[entry.context.weatherId];
				if (group.displayName.empty())
					group.displayName = SplitWeatherDisplayName(entry.displayName).first;
				group.periods.push_back(&entry);
				break;
			}
			case SceneContextType::Location:
				tree.location.push_back(&entry);
				break;
			default:  // TimeOfDay
				tree.timeOfDay.push_back(&entry);
				break;
			}
		}
		std::ranges::sort(tree.timeOfDay, {}, [](const auto* entry) { return entry->context.period; });
		tree.weather.reserve(weatherGroups.size());
		for (auto& [weatherId, group] : weatherGroups) {
			std::ranges::sort(group.periods, {}, [](const auto* entry) { return entry->context.period; });
			tree.weather.push_back(std::move(group));
		}
		std::ranges::sort(tree.weather, {}, &WeatherGroup::displayName);
		return tree;
	}

	/// Display names collide (two location forms can share a name), so scope menu entries by the
	/// context identity behind them to keep ImGui IDs distinct.
	void PushContextId(const SceneContextId& context)
	{
		ImGui::PushID(static_cast<int>(context.weatherId));
		ImGui::PushID(context.locationFormKey.c_str());
	}

	void PopContextId()
	{
		ImGui::PopID();
		ImGui::PopID();
	}

	/// The row every context is offered as: its name plus how much picking it would carry.
	std::string FormatCopyLabel(const std::string& label, size_t settingCount)
	{
		return std::format("{} ({})", label, settingCount);
	}

	void DrawCopyMenuItem(const std::string& label, size_t settingCount, const CopySource& entry,
		const std::function<void(const CopySource&)>& onPick)
	{
		PushContextId(entry.context);
		if (ImGui::MenuItem(FormatCopyLabel(label, settingCount).c_str()))
			onPick(entry);
		PopContextId();
	}

	/// How long a typed prefix survives without further typing before the next key starts a new search.
	constexpr double kTypeAheadResetSeconds = 1.0;

	/// Type-to-jump for the long leaf lists. Menus never read the character queue, so it is ours to
	/// consume: typed letters accumulate into a prefix the list scrolls to the first entry of.
	struct CopyTypeAhead
	{
		std::string prefix;
		double lastInputTime = 0.0;
		int lastFrame = -1;
		bool pendingJump = false;
		bool retryLastKey = false;

		/// Consumes this frame's typing. Call once inside the list, before drawing its entries.
		void BeginList()
		{
			const auto time = ImGui::GetTime();
			const auto frame = ImGui::GetFrameCount();
			// A skipped frame means a different list opened, so its search starts from nothing.
			if (frame != lastFrame + 1 || time - lastInputTime > kTypeAheadResetSeconds)
				prefix.clear();
			lastFrame = frame;

			pendingJump = std::exchange(retryLastKey, false);
			for (const auto character : ImGui::GetIO().InputQueueCharacters) {
				if (character < ' ' || character > '~')
					continue;
				prefix.push_back(static_cast<char>(character));
				lastInputTime = time;
				pendingJump = true;
			}
		}

		/// Scrolls to the first entry the typed prefix matches. Call right after drawing each entry.
		void ScrollToMatch(std::string_view name)
		{
			if (!pendingJump || name.size() < prefix.size())
				return;
			if (Util::IEquals(name.substr(0, prefix.size()), prefix)) {
				ImGui::SetScrollHereY(0.0f);
				pendingJump = false;
			}
		}

		/// Call once after the entries. A prefix that matched nothing was a fresh jump rather than a
		/// longer search, so the next frame retries with the last key alone.
		void EndList()
		{
			if (!pendingJump || prefix.size() < 2)
				return;
			prefix.erase(0, prefix.size() - 1);
			retryLastKey = true;
		}
	};
	CopyTypeAhead copyTypeAhead;

	/// Width a leaf list needs for its widest row. A child sizes itself from the width its parent has
	/// already resolved, and a menu resolves its width from what it drew, so a child left to fill the
	/// available width would pin the submenu it opens in to the minimum window width instead.
	float CalcCopyListWidth(std::span<const std::string> rowLabels)
	{
		const auto& style = ImGui::GetStyle();
		float widest = 0.0f;
		for (const auto& label : rowLabels)
			widest = std::max(widest, ImGui::CalcTextSize(label.c_str()).x);
		widest += style.ItemSpacing.x + ImGui::GetFontSize() * kMenuMarkWidthRatio;
		if (rowLabels.size() * ImGui::GetTextLineHeightWithSpacing() > kCopyListHeight * Util::GetUIScale())
			widest += style.ScrollbarSize;
		return widest;
	}

	/// Labels a leaf list's rows ahead of drawing them, since its child has to be sized from them first.
	template <typename Rows, typename Label>
	std::vector<std::string> CollectRowLabels(const Rows& rows, Label rowLabel)
	{
		std::vector<std::string> labels;
		labels.reserve(rows.size());
		for (const auto& row : rows)
			labels.push_back(rowLabel(row));
		return labels;
	}

	/// Scrollable wrapper for leaf lists that can still grow long: individual weather scenes, locations.
	/// Typing while one is open jumps to the first entry starting with what was typed.
	void DrawScrollableChild(std::span<const std::string> rowLabels, const std::function<void()>& drawItems)
	{
		const ImVec2 size(CalcCopyListWidth(rowLabels), kCopyListHeight * Util::GetUIScale());
		if (ImGui::BeginChild("##ScenePageCopyScroll", size)) {
			copyTypeAhead.BeginList();
			drawItems();
			copyTypeAhead.EndList();
		}
		ImGui::EndChild();
	}

	/// Type -> scene -> period, so From/To never dump every weather-period combination in one list.
	/// Interior and Location are aperiodic and stop one level short; Weather collapses to its scene
	/// alone when the scene's own TOD view is off, since every period then holds the same synced data.
	void DrawCopyTree(const CopyTree& tree, const std::function<void(const CopySource&)>& onPick)
	{
		if (tree.interior)
			DrawCopyMenuItem(GetContextTypeLabel(SceneContextType::Interior), tree.interior->settingCount,
				*tree.interior, onPick);

		if (!tree.timeOfDay.empty() && ImGui::BeginMenu(GetContextTypeLabel(SceneContextType::TimeOfDay))) {
			for (const auto* entry : tree.timeOfDay)
				DrawCopyMenuItem(entry->displayName, entry->settingCount, *entry, onPick);
			ImGui::EndMenu();
		}

		auto* manager = SceneSettingsManager::GetSingleton();
		if (!tree.weather.empty() && ImGui::BeginMenu(GetContextTypeLabel(SceneContextType::Weather))) {
			const auto rowLabels = CollectRowLabels(tree.weather, [](const auto& group) {
				return FormatCopyLabel(group.displayName, group.periods.front()->settingCount);
			});
			DrawScrollableChild(rowLabels, [&] {
				for (const auto& group : tree.weather) {
					const auto* firstPeriod = group.periods.front();
					if (!manager->IsWeatherShowTimeOfDay(firstPeriod->context.weatherId)) {
						DrawCopyMenuItem(group.displayName, firstPeriod->settingCount, *firstPeriod, onPick);
					} else {
						PushContextId(firstPeriod->context);
						if (ImGui::BeginMenu(group.displayName.c_str())) {
							for (const auto* entry : group.periods)
								DrawCopyMenuItem(SplitWeatherDisplayName(entry->displayName).second,
									entry->settingCount, *entry, onPick);
							ImGui::EndMenu();
						}
						PopContextId();
					}
					copyTypeAhead.ScrollToMatch(group.displayName);
				}
			});
			ImGui::EndMenu();
		}

		if (!tree.location.empty() && ImGui::BeginMenu(GetContextTypeLabel(SceneContextType::Location))) {
			const auto rowLabels = CollectRowLabels(tree.location, [](const auto* entry) {
				return FormatCopyLabel(entry->displayName, entry->settingCount);
			});
			DrawScrollableChild(rowLabels, [&] {
				for (const auto* entry : tree.location) {
					DrawCopyMenuItem(entry->displayName, entry->settingCount, *entry, onPick);
					copyTypeAhead.ScrollToMatch(entry->displayName);
				}
			});
			ImGui::EndMenu();
		}
	}

	void DrawCopyPopup(const SceneContextId& context, PeriodScope periodScope)
	{
		if (!ImGui::BeginPopup(kCopyPopupId))
			return;

		auto* manager = SceneSettingsManager::GetSingleton();
		const auto sources = GetCopySources(context, periodScope, false);
		// BeginMenu's own `enabled` param (not BeginDisabled) is required here: BeginDisabled only
		// greys out the visuals but doesn't stop the hover-to-open submenu logic, leaving an empty
		// submenu stuck open when the disabled entry is hovered.
		const bool fromOpen = ImGui::BeginMenu(T(TKEY("scene_page_copy_from"), "From"), !sources.empty());
		Util::AddTooltip(T(TKEY("scene_page_copy_from_tooltip"), "Copies another context's settings into this page."),
			Util::kTooltipWhenDisabled);
		if (fromOpen) {
			DrawCopyTree(BuildCopyTree(sources), [&](const CopySource& source) {
				StartCopy(source.context, context, source.displayName, periodScope);
				ImGui::CloseCurrentPopup();
			});
			ImGui::EndMenu();
		}

		const auto destinations = GetCopyDestinations(context, periodScope, false);
		const bool toOpen = ImGui::BeginMenu(T(TKEY("scene_page_copy_to"), "To"), !destinations.empty());
		Util::AddTooltip(T(TKEY("scene_page_copy_to_tooltip"), "Copies this page's settings into another context."),
			Util::kTooltipWhenDisabled);
		if (toOpen) {
			const auto sourceName = manager->GetSceneContextDisplayName(context);
			DrawCopyTree(BuildCopyTree(destinations), [&](const CopySource& destination) {
				StartCopy(context, destination.context, sourceName, periodScope);
				ImGui::CloseCurrentPopup();
			});
			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}

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

	/// Draws wherever the destination page's own toolbar happens to render, which may not be the page
	/// the user is currently looking at (e.g. copying to a period other than the one on screen). The
	/// flow is global, so this only needs to run once per frame even if several toolbars call it.
	void DrawCopyPreview()
	{
		if (!copyFlow.active)
			return;

		static int lastDrawnFrame = -1;
		const int frame = ImGui::GetFrameCount();
		if (lastDrawnFrame == frame)
			return;
		lastDrawnFrame = frame;

		const char* title = T(TKEY("scene_page_copy_title"), "Copy settings");
		if (copyFlow.pendingOpen) {
			ImGui::OpenPopup(title);
			copyFlow.pendingOpen = false;
		}

		auto* manager = SceneSettingsManager::GetSingleton();
		bool open = true;
		std::optional<CopyConflictPolicy> decision;
		if (auto popup = Util::CenteredPopupModal(title, &open)) {
			const auto destinationName = manager->GetSceneContextDisplayName(copyFlow.destination);
			ImGui::TextWrapped("%s", std::vformat(T(TKEY("scene_page_copy_intro"), "Copying {} into {}."),
										std::make_format_args(copyFlow.sourceName, destinationName))
										.c_str());

			size_t conflicting = 0;
			size_t rejected = 0;
			for (const auto& candidate : copyFlow.candidates) {
				conflicting += candidate.conflicts ? 1 : 0;
				rejected += candidate.compatible ? 0 : 1;
			}
			Util::Text::Disabled("%s",
				std::vformat(T(TKEY("scene_page_copy_counts"), "{} already set here, {} cannot be copied."),
					std::make_format_args(conflicting, rejected))
					.c_str());
			ImGui::Spacing();

			const float scale = Util::GetUIScale();
			if (ImGui::BeginTable("##ScenePageCopyPreview", 2, kPreviewTableFlags,
					ImVec2(kPreviewWidth * scale, kPreviewHeight * scale))) {
				ImGui::TableSetupColumn(T(TKEY("scene_page_copy_column_setting"), "Setting"));
				ImGui::TableSetupColumn(T(TKEY("scene_page_copy_column_change"), "Change"));
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();
				for (const auto& candidate : copyFlow.candidates)
					DrawCandidateRow(candidate);
				ImGui::EndTable();
			}

			ImGui::Spacing();
			if (ImGui::Button(T(TKEY("scene_page_copy_skip"), "Skip existing")))
				decision = CopyConflictPolicy::SkipExisting;
			Util::AddTooltip(T(TKEY("scene_page_copy_skip_tooltip"),
				"Copies only the settings this page does not already hold."));
			ImGui::SameLine();
			if (Util::WarningButton(T(TKEY("scene_page_copy_overwrite"), "Overwrite existing")))
				decision = CopyConflictPolicy::OverwriteExisting;
			Util::AddTooltip(T(TKEY("scene_page_copy_overwrite_tooltip"),
				"Replaces the values this page already holds with the ones listed above."));
			ImGui::SameLine();
			if (decision || ImGui::Button(T(TKEY("cancel"), "Cancel")))
				ImGui::CloseCurrentPopup();
		}

		if (decision)
			RunCopy(copyFlow.source, copyFlow.destination, *decision, copyFlow.periodScope);
		// The preview is discarded once its popup is gone, whether it was acted on or dismissed.
		if (!ImGui::IsPopupOpen(title))
			copyFlow = {};
	}
}

void ScenePageToolbar::Draw(const SceneContextId& context, SceneSettingsManager::PeriodScope periodScope)
{
	auto* manager = SceneSettingsManager::GetSingleton();
	if (!manager)
		return;

	const auto summary = manager->GetContextUserEntrySummary(context, periodScope);
	const bool hasEntries = summary.total != 0;
	// A mixed page pauses rather than resumes: the button is a way out of that state, not into it.
	const bool pauseTarget = !summary.AllPaused();

	const char* toggleLabel = pauseTarget ? T(TKEY("scene_page_pause_all"), "Pause All") :
	                                        T(TKEY("scene_page_resume_all"), "Resume All");
	const char* copyLabel = T(TKEY("scene_page_copy"), "Copy");
	const char* loadPresetLabel = T(TKEY("scene_page_load_preset"), "Load Preset");
	const char* exportLabel = T(TKEY("scene_page_export"), "Export");
	const char* clearLabel = T(TKEY("scene_page_clear"), "Clear");
	const char* transitionLabel = T(TKEY("scene_page_transition"), "Transition");

	const auto& style = ImGui::GetStyle();
	auto* menu = Menu::GetSingleton();
	const bool hasClearIcon = menu && menu->uiIcons.deleteSettings.texture;
	// An image button is the image plus the same frame padding, so a font-sized icon matches the row.
	const float clearIconSize = ImGui::GetFontSize();
	const float clearWidth = hasClearIcon ? clearIconSize + style.FramePadding.x * 2.0f : ButtonWidth(clearLabel);
	// The global duration only governs the location layer, so it is absent everywhere else.
	const bool hasTransitionField = context.type == SceneContextType::Location;
	const float transitionWidth = hasTransitionField ?
	                                  ImGui::CalcTextSize(transitionLabel).x + style.ItemInnerSpacing.x +
	                                      SceneTransitionField::GetWidth() + style.ItemSpacing.x :
	                                  0.0f;
	const float width = ButtonWidth(toggleLabel) + ButtonWidth(copyLabel) + ButtonWidth(loadPresetLabel) +
	                    ButtonWidth(exportLabel) + clearWidth + transitionWidth + style.ItemSpacing.x * 4.0f;
	const float margin = kRightMargin * Util::GetUIScale();
	if (const auto avail = ImGui::GetContentRegionAvail().x; avail > width + margin)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width - margin);

	ImGui::PushID("ScenePageToolbar");

	if (hasTransitionField) {
		// Bare text is top-aligned, which would float it above the framed row it labels.
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(transitionLabel);
		ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

		// The global has no layer above it, so it is always set: emptying restores the default
		// rather than clearing to nullopt, which the resolver could not use.
		std::optional<float> seconds = manager->GetLocationTransitionSeconds();
		if (SceneTransitionField::Draw("##ScenePageTransition", seconds,
				SceneSettingsManager::kDefaultLocationTransitionSeconds, true)) {
			manager->SetLocationTransitionSeconds(
				seconds.value_or(SceneSettingsManager::kDefaultLocationTransitionSeconds));
		}
		Util::AddTooltip(T(TKEY("scene_page_transition_tooltip"),
			"Seconds every value on this page takes to ease in and out when the location changes.\n"
			"A single setting can override this from its own transition field."));

		ImGui::SameLine();
	}

	ImGui::BeginDisabled(!hasEntries);
	if (ImGui::Button(toggleLabel))
		manager->SetContextEntriesPaused(context, pauseTarget, periodScope);
	ImGui::EndDisabled();
	Util::AddTooltip(pauseTarget ?
						 T(TKEY("scene_page_pause_all_tooltip"),
							 "Holds back every override on this page without losing its value.") :
						 T(TKEY("scene_page_resume_all_tooltip"), "Applies every override on this page again."),
		Util::kTooltipWhenDisabled);

	ImGui::SameLine();
	// The lists are rebuilt on open, so what they offer is never a frame behind the page.
	const bool hasSources = HasUsableEntries(GetCachedSources(context, false), context, periodScope);
	const bool hasDestinations = HasUsableEntries(GetCachedDestinations(context, false), context, periodScope);
	ImGui::BeginDisabled(!hasSources && !hasDestinations);
	if (ImGui::Button(copyLabel)) {
		GetCopySources(context, periodScope, true);
		GetCopyDestinations(context, periodScope, true);
		ImGui::OpenPopup(kCopyPopupId);
	}
	ImGui::EndDisabled();
	Util::AddTooltip(T(TKEY("scene_page_copy_tooltip"), "Copies settings between this page and another context."),
		Util::kTooltipWhenDisabled);
	DrawCopyPopup(context, periodScope);

	ImGui::SameLine();
	const bool hasUserLayer = manager->HasAnyUserEntries();
	ImGui::BeginDisabled(!hasUserLayer);
	if (ImGui::Button(loadPresetLabel)) {
		loadPresetConfirmation.popup.title = T(TKEY("scene_page_load_preset_title"), "Load preset");
		loadPresetConfirmation.popup.message = T(TKEY("scene_page_load_preset_message"),
			"Remove every setting you have authored, in every context, and let the installed preset drive the "
			"scene?\n\nSettings you removed from the preset come back too. This cannot be undone.");
		loadPresetConfirmation.popup.confirmLabel = loadPresetLabel;
		loadPresetConfirmation.popup.cancelLabel = T(TKEY("cancel"), "Cancel");
		loadPresetConfirmation.Request(context);
	}
	ImGui::EndDisabled();
	Util::AddTooltip(T(TKEY("scene_page_load_preset_tooltip"),
						  "Clears your own settings everywhere so the installed preset is what applies."),
		Util::kTooltipWhenDisabled);

	ImGui::SameLine();
	ImGui::BeginDisabled(!ScenePresetExport::CanExport());
	if (ImGui::Button(exportLabel))
		ScenePresetExport::Open(context);
	ImGui::EndDisabled();
	Util::AddTooltip(T(TKEY("scene_page_export_tooltip"),
						  "Writes every setting from every context out as an overwrite preset."),
		Util::kTooltipWhenDisabled);

	ImGui::SameLine();
	ImGui::BeginDisabled(!hasEntries);
	const bool clearClicked = hasClearIcon ?
	                              Util::ErrorImageButton("##ScenePageClear", menu->uiIcons.deleteSettings.texture,
	                                  ImVec2(clearIconSize, clearIconSize)) :
	                              Util::ErrorTextButton(clearLabel);
	if (clearClicked) {
		auto count = summary.total;
		auto pageName = manager->GetSceneContextDisplayName(context);
		clearConfirmation.popup.title = T(TKEY("scene_page_clear_title"), "Clear page");
		clearConfirmation.popup.message = std::vformat(T(TKEY("scene_page_clear_message"),
														  "Remove all {} settings from {}? Mod overrides are left alone.\n\n"
														  "Settings you removed come back too."),
			std::make_format_args(count, pageName));
		clearConfirmation.popup.confirmLabel = clearLabel;
		clearConfirmation.popup.cancelLabel = T(TKEY("cancel"), "Cancel");
		clearConfirmation.Request(context);
	}
	ImGui::EndDisabled();
	Util::AddTooltip(T(TKEY("scene_page_clear_tooltip"), "Removes every override this page holds."),
		Util::kTooltipWhenDisabled);

	clearConfirmation.Draw(context, [&] { manager->ClearContextEntries(context, periodScope); });
	loadPresetConfirmation.Draw(context, [manager] {
		// An export earlier this session left the mod layer holding what was on disk before it.
		manager->ReloadOverwrites();
		manager->ClearAllUserEntries();
	});
	DrawCopyPreview();
	ScenePresetExport::Draw(context);

	ImGui::PopID();
}

#undef I18N_KEY_PREFIX
