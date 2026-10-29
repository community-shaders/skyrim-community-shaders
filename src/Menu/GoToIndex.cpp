#include "Menu/GoToIndex.h"

#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "Features/Effects11.h"
#include "Features/Effects11/Editor/Effects11Editor.h"
#include "Features/ScreenshotFeature.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/AdvancedSettingsRenderer.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/IssuesPageRenderer.h"
#include "Menu/SettingReveal.h"
#include "Menu/SettingsPageRenderer.h"
#include "Menu/SettingsTabRenderer.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Util.h"
#include "Utils/FileSystem.h"
#include "Utils/SettingsCatalog.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <set>
#include <shellapi.h>
#include <span>
#include <unordered_map>

namespace GoToIndex
{
	namespace
	{
		namespace Catalog = SceneSettingsCatalog;

		constexpr const char* kSeparator = " > ";

		/// Settings entries of the catalog, cached for one I18n generation and set of loaded features.
		std::vector<Entry> g_settingEntries;
		std::uint64_t g_settingGeneration = 0;
		size_t g_settingLoadedCount = 0;
		bool g_settingsBuilt = false;

		/// Everything else, rebuilt on each Refresh().
		std::vector<Entry> g_entries;

		std::vector<std::string> g_recent;  ///< Entry IDs, newest first

		/// The next code point of `text` at `pos`, case-folded, so matching ignores case in every language.
		char32_t NextFolded(std::string_view text, size_t& pos)
		{
			return Util::FoldCodepoint(Util::NextCodepoint(text, pos));
		}

		/// Where `word` first appears in `target`, ignoring case, as a byte range starting on a code point.
		std::optional<std::pair<size_t, size_t>> FindFolded(std::string_view target, std::string_view word)
		{
			for (size_t start = 0; start < target.size(); Util::NextCodepoint(target, start)) {
				size_t t = start;
				size_t w = 0;
				bool same = true;
				while (same && w < word.size()) {
					if (t >= target.size())
						return std::nullopt;
					same = NextFolded(target, t) == NextFolded(word, w);
				}
				if (same)
					return std::pair{ start, t };
			}
			return std::nullopt;
		}

		bool IsAlnum(char c)
		{
			return std::isalnum(static_cast<unsigned char>(c)) != 0;
		}

		/// A word starts after a non-alphanumeric byte or at a lower-to-upper (camelCase) step.
		bool IsWordStart(std::string_view text, size_t i)
		{
			if (i == 0)
				return true;
			const char prev = text[i - 1];
			if (!IsAlnum(prev))
				return static_cast<unsigned char>(prev) < 0x80;
			return std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(text[i]));
		}

		std::string Join(const std::vector<std::string>& parts)
		{
			return parts | std::views::join_with(std::string_view(kSeparator)) | std::ranges::to<std::string>();
		}

		/// Drops the ':' keybinding labels end in, and any trailing space.
		std::string TrimLabel(std::string_view label)
		{
			return std::string(label.substr(0, label.find_last_not_of(" :") + 1));
		}

		void Navigate(std::string_view pageId)
		{
			globals::menu->SelectFeatureMenu(std::string(pageId));
		}

		void OpenSection(std::string sectionId)
		{
			globals::menu->GetSettings().LastSettingsSection = std::move(sectionId);
			Navigate(MenuPageIds::kSettings);
		}

		void AddPages(std::vector<Entry>& out)
		{
			const auto addPage = [&out](std::string_view pageId, const char* title) {
				Entry entry;
				entry.kind = Kind::Page;
				entry.id = "page:" + std::string(pageId);
				entry.title = title;
				entry.run = [page = std::string(pageId)] { Navigate(page); };
				out.push_back(std::move(entry));
			};
			addPage(MenuPageIds::kHome, T("menu.features.home", "Home"));
			addPage(MenuPageIds::kPresets, T("menu.features.presets", "Presets"));
			addPage(MenuPageIds::kSettings, T("menu.features.settings", "Settings"));
			addPage(MenuPageIds::kProfiling, T("menu.features.profiling", "Profiling"));
			if (IssuesPageRenderer::GetIssueCount() > 0)
				addPage(MenuPageIds::kIssues, T("menu.palette.page_issues", "Issues"));

			const std::string settings = T("menu.features.settings", "Settings");
			for (const auto& section : SettingsPageRenderer::GetSections()) {
				Entry entry;
				entry.kind = Kind::Page;
				entry.id = "section:" + std::string(section.id);
				entry.title = section.title;
				entry.breadcrumb = settings + kSeparator + section.group;
				entry.run = [id = std::string(section.id)] { OpenSection(id); };
				out.push_back(std::move(entry));
			}
		}

		/// The Settings page's own labels (Use Custom Shaders, Toggle Key, ...), from each renderer's search index.
		void AddSettingsPageLabels(std::vector<Entry>& out)
		{
			const std::string settings = T("menu.features.settings", "Settings");
			std::unordered_map<std::string_view, SettingsPageRenderer::SectionInfo> sections;
			for (const auto& section : SettingsPageRenderer::GetSections())
				sections.emplace(section.id, section);

			const auto add = [&](std::span<const SettingsSearchEntry> index) {
				for (const auto& item : index) {
					const auto section = sections.find(item.sectionId);
					if (section == sections.end())
						continue;
					const char* label = item.labelKey ? T(item.labelKey, item.fallback) : item.fallback;
					Entry entry;
					entry.kind = Kind::Setting;
					entry.id = std::format("label:{}:{}", item.sectionId, item.fallback);
					entry.title = TrimLabel(label);
					entry.breadcrumb = settings + kSeparator + section->second.group + kSeparator + section->second.title;
					entry.haystack = TrimLabel(item.fallback);  // English guides keep working in other locales
					entry.run = [id = std::string(item.sectionId)] { OpenSection(id); };
					out.push_back(std::move(entry));
				}
			};
			add(SettingsTabRenderer::GetSearchIndex());
			add(AdvancedSettingsRenderer::GetSearchIndex());
		}

		void AddFeatures(std::vector<Entry>& out)
		{
			const bool developerMode = globals::state->IsDeveloperMode();
			globals::state->PrimeUserEdits();  // answers every feature's Modified filter at once
			for (Feature* feature : Feature::GetFeatureList()) {
				const std::string shortName = feature->GetShortName();
				if (!feature->IsInMenu() || feature->IsHiddenUnreleased())
					continue;
				if (!feature->loaded && FeatureIssues::IsObsoleteFeature(shortName) && !developerMode)
					continue;

				const std::string category = FeatureListRenderer::TranslateCategory(feature->GetCategory());
				Entry entry;
				entry.kind = Kind::Feature;
				entry.id = "feature:" + shortName;
				entry.title = feature->GetDisplayName();
				entry.breadcrumb = category;
				// The English name too, so English guides keep working in other locales
				entry.haystack = shortName + " " + category + " " + feature->GetName();
				entry.filterBits = FeatureListRenderer::GetFilterBits(feature);
				if (!feature->loaded) {
					entry.dimmed = true;
					entry.breadcrumb += std::string(" ") + T("menu.palette.not_loaded", "(not loaded)");
				}
				entry.run = [shortName] { Navigate(shortName); };
				out.push_back(std::move(entry));
			}
		}

		void AddCatalogSettings()
		{
			const auto generation = I18n::GetSingleton()->GetGeneration();
			size_t loadedCount = 0;
			for (Feature* feature : Feature::GetFeatureList())
				loadedCount += feature->loaded ? 1 : 0;
			if (g_settingsBuilt && generation == g_settingGeneration && loadedCount == g_settingLoadedCount)
				return;

			g_settingEntries.clear();
			g_settingGeneration = generation;
			g_settingLoadedCount = loadedCount;
			g_settingsBuilt = true;

			std::unordered_map<std::string, Feature*> features;  // short name -> feature, or null when not loaded
			std::set<std::string> seen;                          // a multi-component control is one entry
			for (const auto& setting : Catalog::GetSettings()) {
				if (Catalog::HasFlag(setting.flags, Catalog::SettingFlag::Hidden) || setting.serializedComponent > 0)
					continue;

				const std::string shortName(setting.featureShortName);
				auto found = features.find(shortName);
				if (found == features.end()) {
					Feature* feature = Feature::FindFeatureByShortName(shortName);
					found = features.emplace(shortName, feature && feature->loaded && feature->IsInMenu() ? feature : nullptr).first;
				}
				if (!found->second)
					continue;

				auto path = Util::Settings::GetCatalogDisplayPath(setting);
				const std::string leaf = Util::Settings::GetCatalogLeafDisplayName(setting);
				if (leaf.empty())
					continue;

				std::vector<std::string> crumbs{ found->second->GetDisplayName() };
				crumbs.insert(crumbs.end(), path.begin(), path.end());
				Entry entry;
				entry.kind = Kind::Setting;
				entry.title = leaf;
				entry.breadcrumb = Join(crumbs);
				entry.id = std::format("setting:{}/{}/{}", shortName, setting.serializedPath, setting.serializedKey);
				if (!seen.insert(entry.id).second)
					continue;
				entry.haystack = shortName;
				entry.setting = &setting;
				entry.run = [shortName, &setting] {
					Navigate(shortName);
					SettingReveal::Request(setting);
				};
				g_settingEntries.push_back(std::move(entry));
			}
		}

		void AddCommands(std::vector<Entry>& out, const std::vector<MenuShortcuts::Shortcut>& shortcuts)
		{
			namespace Ids = MenuShortcuts::Ids;
			const auto& menuSettings = globals::menu->GetSettings();
			const auto addCommand = [&out](std::string_view id, const char* title, std::function<void()> run, std::string chord = {},
										std::string_view shortcutId = {}) {
				Entry entry;
				entry.kind = Kind::Command;
				entry.id = "cmd:" + std::string(id);
				entry.title = title;
				entry.chord = std::move(chord);
				entry.shortcutId = shortcutId;
				entry.run = std::move(run);
				out.push_back(std::move(entry));
			};
			const auto hint = [&shortcuts](std::string_view id) { return MenuShortcuts::GetHint(shortcuts, id); };
			const auto hotkey = [](const std::vector<InputCombo>& keys) {
				return keys.empty() ? std::string() : Util::Input::KeyIdToString(keys);
			};

			addCommand("save", T("menu.palette.cmd_save", "Save settings"), {}, hint(Ids::kSave), Ids::kSave);
			addCommand("restore", T("menu.palette.cmd_restore", "Restore saved settings..."),
				[] { Util::RequestRestoreSavedSettingsConfirmation(); });
			addCommand("clear_cache", T("menu.palette.cmd_clear_cache", "Clear shader cache..."),
				[] { Util::RequestClearShaderCacheConfirmation(); });
			if (EditorWindow::CanBeOpen())
				addCommand("cs_editor", T("menu.palette.cmd_cs_editor", "Open CS Editor"), {}, hint(Ids::kOpenEditor), Ids::kOpenEditor);
			if (globals::features::effects11.loaded)
				addCommand("effects11_editor", T("menu.palette.cmd_effects11_editor", "Open Effects 11 editor"), [] { Effects11Editor::GetSingleton().Open(true); }, hotkey(menuSettings.Effects11EditorKey));
			addCommand("overlay", T("menu.palette.cmd_overlay", "Toggle performance overlay"), [] { globals::menu->overlayVisible = !globals::menu->overlayVisible; }, hotkey(menuSettings.OverlayToggleKey));
			if (globals::features::screenshotFeature.loaded)
				addCommand("screenshot", T("menu.palette.cmd_screenshot", "Take screenshot"), [] { globals::features::screenshotFeature.captureRequested = true; }, hotkey(menuSettings.ScreenshotKey));
			addCommand("logs", T("menu.palette.cmd_logs", "Open logs folder"), [] {
				const auto logFolder = Util::PathHelpers::GetLogPath().parent_path();
				if (!logFolder.empty())
					ShellExecuteA(nullptr, "open", logFolder.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			});
			addCommand("help", T("menu.palette.cmd_help", "Keyboard shortcuts and help"), {}, hint(Ids::kHelp), Ids::kHelp);
		}

		/// Splits a query at spaces into its words.
		std::vector<std::string_view> SplitWords(std::string_view query)
		{
			std::vector<std::string_view> words;
			for (size_t start = 0; start < query.size();) {
				const auto end = query.find(' ', start);
				const auto word = query.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
				if (!word.empty())
					words.push_back(word);
				if (end == std::string_view::npos)
					break;
				start = end + 1;
			}
			return words;
		}

		/// Whether the feature filters leave an entry in; they narrow features only.
		bool PassesFeatureFilter(const Entry& entry)
		{
			namespace Filter = ::FeatureListFilter;
			if (entry.kind != Kind::Feature)
				return true;
			const auto filter = globals::menu->GetSettings().FeatureListFilter & Filter::kKnown;
			if (filter == 0)
				return true;
			// Like the list: with Unloaded on only unloaded features are candidates, otherwise only loaded ones
			if (((filter & Filter::kUnloaded) != 0) != ((entry.filterBits & Filter::kUnloaded) != 0))
				return false;
			return FeatureListRenderer::PassesFilter(entry.filterBits, filter);
		}

		/// Features in the order the feature list shows them: pinned first, then by name or by category.
		void SortFeatures(std::vector<const Entry*>& features)
		{
			const bool alphabetical = globals::menu->GetSettings().FeatureListSort == Menu::FeatureSortMode::Alphabetical;
			std::ranges::stable_sort(features, [alphabetical](const Entry* a, const Entry* b) {
				const bool pinnedA = (a->filterBits & FeatureListFilter::kFavorites) != 0;
				const bool pinnedB = (b->filterBits & FeatureListFilter::kFavorites) != 0;
				if (pinnedA != pinnedB)
					return pinnedA;
				if (!alphabetical && a->breadcrumb != b->breadcrumb)
					return a->breadcrumb < b->breadcrumb;
				return a->title < b->title;
			});
		}

		/// Sum of the best score of every query word, or -1 if any word matches nothing.
		int ScoreEntry(const std::vector<std::string_view>& words, const Entry& entry, const std::string& context)
		{
			int total = 0;
			for (const auto word : words) {
				const int inTitle = ScoreWord(word, entry.title);
				// Breadcrumb and extra text count for less than the title itself
				const int inContext = ScoreWord(word, context);
				const int best = std::max(inTitle, inContext >= 0 ? inContext / 2 : -1);
				if (best < 0)
					return -1;
				total += best;
			}
			return total;
		}
	}

	int ScoreWord(std::string_view word, std::string_view target)
	{
		if (word.empty())
			return 0;
		if (target.empty() || word.size() > target.size())
			return -1;

		constexpr int kBase = 100;
		constexpr int kSubstring = 1000;
		constexpr int kPrefix = 400;
		constexpr int kWordStart = 200;
		constexpr int kConsecutive = 15;
		constexpr int kSubsequenceWordStart = 30;
		constexpr int kLengthBonusCap = 80;  // Targets shorter than this earn the difference

		const int lengthBonus = std::max(0, kLengthBonusCap - static_cast<int>(target.size()));

		// Substring first: the strongest signal, and cheap
		if (const auto hit = FindFolded(target, word)) {
			const size_t pos = hit->first;
			int score = kSubstring - static_cast<int>(pos) + lengthBonus;
			if (pos == 0)
				score += kPrefix;
			if (IsWordStart(target, pos))
				score += kWordStart;
			return score;
		}

		// Subsequence: letters in order, rewarded for word starts and runs
		int score = kBase + lengthBonus;
		size_t ti = 0;
		bool previousMatched = false;
		for (size_t wi = 0; wi < word.size();) {
			const char32_t folded = NextFolded(word, wi);
			bool found = false;
			while (ti < target.size()) {
				const size_t at = ti;
				if (NextFolded(target, ti) != folded) {
					previousMatched = false;
					continue;
				}
				if (previousMatched)
					score += kConsecutive;
				if (IsWordStart(target, at))
					score += kSubsequenceWordStart;
				previousMatched = true;
				found = true;
				break;
			}
			if (!found)
				return -1;
		}
		return score;
	}

	void Refresh(const std::vector<MenuShortcuts::Shortcut>& shortcuts)
	{
		g_entries.clear();
		AddPages(g_entries);
		AddFeatures(g_entries);
		AddSettingsPageLabels(g_entries);
		AddCommands(g_entries, shortcuts);
		AddCatalogSettings();
	}

	std::vector<size_t> MatchPositions(std::string_view query, std::string_view title)
	{
		std::vector<size_t> positions;
		for (const auto word : SplitWords(query)) {
			if (word.size() > title.size())
				continue;
			if (const auto hit = FindFolded(title, word)) {
				for (size_t i = hit->first; i < hit->second; ++i)
					positions.push_back(i);
				continue;
			}
			// The same greedy subsequence walk ScoreWord scores
			size_t at = 0;
			std::vector<size_t> found;
			for (size_t wi = 0; wi < word.size();) {
				const char32_t q = NextFolded(word, wi);
				size_t next = at;
				while (at < title.size() && NextFolded(title, next) != q)
					at = next;
				if (at == title.size()) {
					found.clear();
					break;
				}
				found.push_back(at);
				at = next;
			}
			positions.insert(positions.end(), found.begin(), found.end());
		}
		std::ranges::sort(positions);
		positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
		std::erase_if(positions, [&](size_t i) { return static_cast<unsigned char>(title[i]) >= 0x80; });
		return positions;
	}

	Results Search(std::string_view query, std::optional<Kind> only, size_t limit)
	{
		Results results;
		const auto words = SplitWords(query);

		const auto forEach = [](auto&& fn) {
			for (const auto& entry : g_entries) {
				if (PassesFeatureFilter(entry))
					fn(entry);
			}
			for (const auto& entry : g_settingEntries)
				fn(entry);
		};
		const auto wanted = [&only](const Entry& entry) { return !only || *only == entry.kind; };

		if (words.empty()) {
			// Settings are too many to list: they wait for a query
			forEach([&](const Entry& entry) {
				if (entry.kind != Kind::Setting)
					results.counts[static_cast<size_t>(entry.kind)]++;
			});
			if (only == Kind::Setting)
				return results;
			if (!only) {
				// Recent picks, then the pages
				for (const auto& id : g_recent) {
					forEach([&](const Entry& entry) {
						if (entry.id == id && results.entries.size() < limit)
							results.entries.push_back(&entry);
					});
				}
				results.recent = results.entries.size();
			}
			std::vector<const Entry*> listed;
			forEach([&](const Entry& entry) {
				const bool inDefault = only ? entry.kind == *only : entry.kind == Kind::Page;
				if (inDefault && std::ranges::find(results.entries, &entry) == results.entries.end())
					listed.push_back(&entry);
			});
			if (only == Kind::Feature)
				SortFeatures(listed);
			for (const Entry* entry : listed) {
				if (results.entries.size() < limit)
					results.entries.push_back(entry);
			}
			return results;
		}

		struct Scored
		{
			int score;
			const Entry* entry;
		};
		std::vector<Scored> scored;
		forEach([&](const Entry& entry) {
			const std::string context = entry.breadcrumb + " " + entry.haystack;
			if (const int score = ScoreEntry(words, entry, context); score >= 0) {
				results.counts[static_cast<size_t>(entry.kind)]++;
				if (wanted(entry))
					scored.push_back({ score, &entry });
			}
		});
		std::ranges::stable_sort(scored, [](const Scored& a, const Scored& b) {
			if (a.score != b.score)
				return a.score > b.score;
			if (a.entry->kind != b.entry->kind)
				return a.entry->kind < b.entry->kind;
			return a.entry->title.size() < b.entry->title.size();
		});

		if (only) {
			for (size_t i = 0; i < scored.size() && i < limit; ++i)
				results.entries.push_back(scored[i].entry);
			return results;
		}

		// Every kind: groups ordered by their best hit, each trimmed, so the top result stays on top
		std::array<std::vector<const Entry*>, 4> groups;
		std::vector<Kind> order;
		for (const auto& hit : scored) {
			auto& group = groups[static_cast<size_t>(hit.entry->kind)];
			if (group.empty())
				order.push_back(hit.entry->kind);
			if (group.size() < ThemeManager::Constants::PALETTE_GROUP_MAX)
				group.push_back(hit.entry);
		}
		for (const Kind kind : order) {
			for (const Entry* entry : groups[static_cast<size_t>(kind)]) {
				if (results.entries.size() < limit)
					results.entries.push_back(entry);
			}
		}
		return results;
	}

	void RecordPick(const Entry& entry)
	{
		std::erase(g_recent, entry.id);
		g_recent.insert(g_recent.begin(), entry.id);
		if (g_recent.size() > ThemeManager::Constants::PALETTE_RECENT_COUNT)
			g_recent.resize(ThemeManager::Constants::PALETTE_RECENT_COUNT);
	}

	void Execute(const Entry& entry, const std::vector<MenuShortcuts::Shortcut>& shortcuts)
	{
		if (!entry.shortcutId.empty()) {
			const auto it = std::ranges::find(shortcuts, entry.shortcutId, &MenuShortcuts::Shortcut::id);
			if (it != shortcuts.end() && (!it->enabled || it->enabled()))
				it->run();
			return;
		}
		if (entry.run)
			entry.run();
	}
}
