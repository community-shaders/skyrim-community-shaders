#pragma once

#include <deque>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/**
 * Internationalization (i18n) engine for Community Shaders.
 *
 * Loads flat-JSON translation files from the Translations/ directory.
 * Supports runtime language switching, named placeholder formatting ({name}),
 * and automatic fallback to English when a key is missing in the current locale.
 *
 * ## Developer workflow
 *
 * Use the two-argument T() form everywhere in source code:
 *
 *     ImGui::Text("%s", T("menu.home.welcome", "Welcome to Community Shaders"));
 *
 * The second argument is the English default. It serves as:
 *   1. The fallback text when no translation file is loaded
 *   2. The source for the extraction script (tools/extract-i18n.py) which
 *      auto-generates en.json from source code
 *
 * Developer only touches ONE file (the source code). en.json is auto-generated.
 * Translators work from en.json on Weblate/GitHub and never touch source code.
 *
 * ## String ownership
 *
 * The internal maps own all std::string values, so Get() returns stable
 * const char* pointers suitable for ImGui APIs as long as SetLocale() /
 * Reload() is not called concurrently.
 *
 * ## Thread safety
 *
 * Init/SetLocale/Reload acquire an exclusive lock;
 * Get/Format acquire a shared lock. Concurrent reads are safe.
 */
class I18n
{
public:
	static I18n* GetSingleton()
	{
		static I18n singleton;
		return &singleton;
	}

	/**
	 * Initialize the i18n system.
	 * Discovers available locales, loads the saved locale (or falls back to "en"),
	 * and populates the English fallback table.
	 */
	void Init();

	/**
	 * Get a translated string by key, with an inline English default.
	 *
	 * Lookup order:
	 *   1. Current locale map
	 *   2. English fallback map (from en.json)
	 *   3. Inline default text (if provided)
	 *   4. The key itself (last resort)
	 *
	 * @param key          Dot-notation key, e.g. "menu.home.welcome"
	 * @param defaultText  English default text (used as fallback AND as source
	 *                     for the extraction script). Pass nullptr to skip.
	 * @return Pointer to a null-terminated string owned by this object.
	 *         Valid until the next SetLocale() or Reload() call.
	 */
	const char* Get(std::string_view key, const char* defaultText = nullptr) const;

	/**
	 * Get a translated string with named placeholder substitution.
	 * Placeholders use {name} syntax, e.g. "Welcome {version}".
	 *
	 * @param key          Translation key
	 * @param args         Map of placeholder names to replacement values
	 * @param defaultText  English default (optional, same role as in Get)
	 * @return  Fully substituted string (caller owns the std::string)
	 */
	std::string Format(std::string_view key,
		const std::unordered_map<std::string, std::string>& args,
		const char* defaultText = nullptr) const;

	/** @return Current locale code, e.g. "en", "zh_CN" */
	std::string GetCurrentLocale() const;

	/**
	 * Switch to a different locale at runtime.
	 * @param locale  Locale code matching a JSON filename (without extension)
	 */
	void SetLocale(const std::string& locale);

	/**
	 * @return List of (locale_code, display_name) pairs for all discovered locales.
	 *         Display names come from each file's _meta.language field.
	 */
	std::vector<std::pair<std::string, std::string>> GetAvailableLocales() const;

	/** Reload translation files from disk (useful during development). */
	void Reload();

	/**
	 * Detect the system UI language and return the best matching available locale.
	 * Uses Windows GetUserDefaultUILanguage() to determine the system language,
	 * then matches against available translation files.
	 *
	 * @return Best matching locale code (e.g. "zh_CN", "ja", "de"), or "en" if no match.
	 */
	std::string DetectSystemLocale() const;

private:
	I18n() = default;

	/** Scan the Translations/ directory for *.json files and populate availableLocales_. */
	void DiscoverLocales();

	/**
	 * Load all key-value pairs from a locale JSON file into the target map.
	 * Skips the "_meta" object.
	 * @param locale  Locale code
	 * @param target  Map to populate
	 * @return true if loaded successfully
	 */
	bool LoadLocaleInto(const std::string& locale,
		std::unordered_map<std::string, std::string>& target) const;

	/** Resolve the filesystem path for a given locale code. */
	std::filesystem::path GetLocaleFilePath(const std::string& locale) const;

	/** Perform {placeholder} substitution on a raw template string. */
	static std::string SubstitutePlaceholders(const std::string& tmpl,
		const std::unordered_map<std::string, std::string>& args);

	mutable std::shared_mutex mutex_;

	std::string currentLocale_ = "en";

	// Current locale strings (may be empty for "en" — we just use fallback_)
	std::unordered_map<std::string, std::string> strings_;

	// English fallback (always loaded from en.json)
	std::unordered_map<std::string, std::string> fallback_;

	// (locale_code, display_name) — discovered from Translations/*.json
	std::vector<std::pair<std::string, std::string>> availableLocales_;

	// Cache of inline defaults and missing keys — ensures pointer stability.
	// Uses deque for string storage (deque never invalidates pointers on push_back)
	// and unordered_map for O(1) lookup by key.
	// Mutable because it's populated lazily from const Get().
	mutable std::deque<std::string> defaultStorage_;
	mutable std::unordered_map<std::string, const char*> defaultCache_;
};

// ─── Convenience free function ───────────────────────────────────────────────

/**
 * Get a translated string. Two usage patterns:
 *
 *   // Preferred: inline default — developer only touches this one file
 *   T("menu.home.welcome", "Welcome to Community Shaders")
 *
 *   // Key-only: falls back to en.json, then the key itself
 *   T("menu.home.welcome")
 *
 * The inline default serves double duty:
 *   1. Runtime fallback when no translation file has the key
 *   2. Source text for tools/extract-i18n.py to auto-generate en.json
 */
inline const char* T(std::string_view key, const char* defaultText = nullptr)
{
	return I18n::GetSingleton()->Get(key, defaultText);
}

// ─── Scoped prefix macro ─────────────────────────────────────────────────────

/**
 * TKEY(suffix) — Compile-time key prefix concatenation.
 *
 * Define I18N_KEY_PREFIX at the top of a file, then use TKEY("suffix")
 * to avoid repeating the full dotted path at every call site.
 *
 * Example (in GrassLighting.cpp):
 *
 *     #define I18N_KEY_PREFIX "feature.grass_lighting."
 *
 *     ImGui::SliderFloat(T(TKEY("glossiness"), "Glossiness"), ...);
 *     //                   → T("feature.grass_lighting.glossiness", "Glossiness")
 *
 *     ImGui::Text("%s", T(TKEY("sss_tooltip"),
 *         "Subsurface Scattering amount.\n"
 *         "Models light transport through the surface."));
 *
 * At end of file (to avoid prefix leaking to other translation units):
 *
 *     #undef I18N_KEY_PREFIX
 *
 * C++ adjacent string literals ("a" "b" → "ab") do the concatenation
 * at compile time — zero runtime cost.
 */
#define TKEY(suffix) I18N_KEY_PREFIX suffix
