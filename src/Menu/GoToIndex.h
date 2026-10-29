#pragma once

#include "Menu/MenuShortcuts.h"
#include "SceneSettingsCatalog.generated.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief The searchable index behind the "Go to..." palette: pages, Settings sections, features, catalog
 * settings and commands, plus the ranking that orders them for a query.
 *
 * Catalog settings are the bulk of the index and only change with the locale or the set of loaded features,
 * so they are cached; everything else is rebuilt each time the palette opens, which keeps command
 * availability and shortcut hints current.
 */
namespace GoToIndex
{
	/** @brief What an entry is. The order is also the tie-break priority: pages first, commands last. */
	enum class Kind : std::uint8_t
	{
		Page,
		Feature,
		Setting,
		Command
	};

	/** @brief One thing the palette can go to or run. */
	struct Entry
	{
		Kind kind = Kind::Page;
		std::string id;                                                  ///< Stable key, used to remember recent picks
		std::string title;                                               ///< Main line
		std::string breadcrumb;                                          ///< Where it lives, e.g. "Skylighting > Diffuse"
		std::string haystack;                                            ///< Extra searchable text (short name, category, English label)
		std::string chord;                                               ///< Shortcut shown on command rows; empty when unbound
		std::string_view shortcutId;                                     ///< MenuShortcuts ID a command runs through; empty for other entries
		bool dimmed = false;                                             ///< Drawn greyed, for features that are not loaded
		std::uint32_t filterBits = 0;                                    ///< FeatureListFilter bits a Feature matches; the list filters apply to features only
		const SceneSettingsCatalog::SettingMetadata* setting = nullptr;  ///< The catalog entry of a Setting
		std::function<void()> run;                                       ///< Navigates or runs the command; unused when shortcutId is set
	};

	/**
	 * @brief Brings the index up to date. Call when the palette opens.
	 * @param shortcuts The menu's shortcut table, for command chords.
	 */
	void Refresh(const std::vector<MenuShortcuts::Shortcut>& shortcuts);

	/** @brief What a search found. */
	struct Results
	{
		std::vector<const Entry*> entries;  ///< Pointers into the index, valid until the next Refresh(); best first
		std::array<size_t, 4> counts{};     ///< Matches per Kind (indexed by Kind), whatever scope was asked for
		size_t recent = 0;                  ///< With an empty query and no scope: how many leading entries are recent picks
		size_t Count(Kind kind) const { return counts[static_cast<size_t>(kind)]; }
		size_t Total() const { return counts[0] + counts[1] + counts[2] + counts[3]; }
	};

	/**
	 * @brief Entries that match a query, best first. Features are narrowed by the main menu's feature filters
	 * (Menu::Settings::FeatureListFilter), the same ones that narrow the feature list.
	 * @param query User text; words are matched independently and all must match. Empty lists recent picks, then pages
	 * (for one scope: every page, feature or command; settings only appear for a query).
	 * @param only Restricts the entries to one kind; empty lists every kind, grouped, with each group trimmed.
	 * @param limit Most entries to return.
	 */
	Results Search(std::string_view query, std::optional<Kind> only, size_t limit);

	/**
	 * @brief Byte offsets of the title characters a query matched, ascending. Only ASCII bytes are reported, so a
	 * run of unreported bytes never splits a multi-byte character.
	 */
	std::vector<size_t> MatchPositions(std::string_view query, std::string_view title);

	/**
	 * @brief Scores one target against a query word: case-insensitive, subsequence match with bonuses for a
	 * prefix, a word start and consecutive letters, and a small bonus for shorter targets.
	 * @return The score, or -1 when the word does not match.
	 */
	int ScoreWord(std::string_view word, std::string_view target);

	/** @brief Remembers an entry as a recent pick (in memory only, newest first). */
	void RecordPick(const Entry& entry);

	/**
	 * @brief Runs an entry.
	 * @param entry An entry from Search().
	 * @param shortcuts The menu's shortcut table, for entries that run through it.
	 */
	void Execute(const Entry& entry, const std::vector<MenuShortcuts::Shortcut>& shortcuts);
}
