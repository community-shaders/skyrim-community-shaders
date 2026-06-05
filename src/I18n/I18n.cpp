#include "I18n.h"

#include <Windows.h>
#include <fstream>
#include <regex>

#include "Utils/FileSystem.h"

namespace
{
	/** Validates a locale code against a strict pattern to prevent path traversal. */
	bool IsValidLocaleCode(const std::string& locale)
	{
		// Allow: 2-3 letter language, optional underscore + 2-4 letter region
		// Examples: "en", "zh_CN", "pt_BR", "ja", "kok_IN"
		static const std::regex pattern(R"(^[a-zA-Z]{2,3}(_[a-zA-Z]{2,4})?$)");
		return std::regex_match(locale, pattern);
	}
}

void I18n::Init()
{
	std::unique_lock lock(mutex_);

	DiscoverLocales();

	// Always load English as the fallback
	if (!LoadLocaleInto("en", fallback_)) {
		logger::info(
			"[I18n] en.json not found or empty. "
			"Inline defaults from T(key, default) will be used.");
	}

	// Determine which locale to use.
	// The saved locale preference is read from SettingsUser.json by State::Load()
	// and forwarded here via SetLocale() before or after Init().
	// If currentLocale_ was already set (via an early SetLocale call), honour it.
	if (currentLocale_ != "en") {
		std::unordered_map<std::string, std::string> loaded;
		if (LoadLocaleInto(currentLocale_, loaded)) {
			strings_ = std::move(loaded);
		} else {
			logger::warn("[I18n] Could not load locale '{}', falling back to English.",
				currentLocale_);
			currentLocale_ = "en";
		}
	}

	logger::info("[I18n] Initialized. Locale: {}  |  {} available locale(s)  |  {} fallback keys",
		currentLocale_, availableLocales_.size(), fallback_.size());
}

const char* I18n::Get(std::string_view key, const char* defaultText) const
{
	std::string keyStr(key);

	// Fast path: try under shared lock (concurrent readers OK)
	{
		std::shared_lock lock(mutex_);

		// 1. Try current locale
		if (!strings_.empty()) {
			auto it = strings_.find(keyStr);
			if (it != strings_.end()) {
				return it->second.c_str();
			}
		}

		// 2. Try English fallback (from en.json)
		{
			auto it = fallback_.find(keyStr);
			if (it != fallback_.end()) {
				return it->second.c_str();
			}
		}

		// 3. Check if already cached
		{
			auto it = defaultCache_.find(keyStr);
			if (it != defaultCache_.end()) {
				return it->second;
			}
		}
	}

	// Slow path: need to insert into defaultCache_ — acquire exclusive lock
	{
		std::unique_lock lock(mutex_);

		// Double-check after acquiring exclusive lock (another thread may have inserted)
		auto it = defaultCache_.find(keyStr);
		if (it != defaultCache_.end()) {
			return it->second;
		}

		// Store string in deque (pointer-stable: deque never invalidates on push_back)
		defaultStorage_.emplace_back(defaultText ? std::string(defaultText) : keyStr);
		const char* ptr = defaultStorage_.back().c_str();
		defaultCache_.emplace(keyStr, ptr);
		return ptr;
	}
}

std::string I18n::Format(std::string_view key,
	const std::unordered_map<std::string, std::string>& args,
	const char* defaultText) const
{
	const char* raw = Get(key, defaultText);
	return SubstitutePlaceholders(std::string(raw), args);
}

std::string I18n::GetCurrentLocale() const
{
	std::shared_lock lock(mutex_);
	return currentLocale_;
}

void I18n::SetLocale(const std::string& locale)
{
	std::unique_lock lock(mutex_);

	if (locale == currentLocale_)
		return;

	if (!IsValidLocaleCode(locale)) {
		logger::warn("[I18n] Rejected invalid locale code: '{}'", locale);
		return;
	}

	if (locale == "en") {
		// English uses fallback_ directly; no need for strings_
		strings_.clear();
		defaultCache_.clear();
		defaultStorage_.clear();
		currentLocale_ = "en";
		logger::info("[I18n] Switched to English (en).");
		return;
	}

	std::unordered_map<std::string, std::string> loaded;
	if (LoadLocaleInto(locale, loaded)) {
		strings_ = std::move(loaded);
		defaultCache_.clear();
		defaultStorage_.clear();
		currentLocale_ = locale;
		logger::info("[I18n] Switched to locale '{}'.", locale);
	} else {
		logger::warn("[I18n] Failed to load locale '{}'. Staying on '{}'.",
			locale, currentLocale_);
	}
}

std::vector<std::pair<std::string, std::string>> I18n::GetAvailableLocales() const
{
	std::shared_lock lock(mutex_);
	return availableLocales_;
}

void I18n::Reload()
{
	std::unique_lock lock(mutex_);

	fallback_.clear();
	strings_.clear();
	defaultCache_.clear();
	defaultStorage_.clear();
	availableLocales_.clear();

	DiscoverLocales();
	LoadLocaleInto("en", fallback_);

	if (currentLocale_ != "en") {
		std::unordered_map<std::string, std::string> loaded;
		if (LoadLocaleInto(currentLocale_, loaded)) {
			strings_ = std::move(loaded);
		} else {
			logger::warn(
				"[I18n] Reload: could not load locale '{}', "
				"falling back to English.",
				currentLocale_);
			currentLocale_ = "en";
		}
	}

	logger::info("[I18n] Reloaded. Locale: {}  |  {} fallback keys",
		currentLocale_, fallback_.size());
}

// ─── Private ─────────────────────────────────────────────────────────────────

void I18n::DiscoverLocales()
{
	auto translationsPath = Util::PathHelpers::GetTranslationsPath();

	if (!std::filesystem::exists(translationsPath)) {
		logger::info("[I18n] Translations directory not found: {}",
			translationsPath.string());
		// At minimum, register English as available
		availableLocales_.emplace_back("en", "English");
		return;
	}

	for (const auto& entry : std::filesystem::directory_iterator(translationsPath)) {
		if (!entry.is_regular_file())
			continue;
		auto ext = entry.path().extension().string();
		// Case-insensitive extension check
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		if (ext != ".json")
			continue;

		auto locale = entry.path().stem().string();
		std::string displayName = locale;  // default to code

		// Try to read _meta.language for a friendly display name
		try {
			std::ifstream f(entry.path());
			if (f.is_open()) {
				nlohmann::json j;
				f >> j;
				if (j.contains("_meta") && j["_meta"].contains("language")) {
					displayName = j["_meta"]["language"].get<std::string>();
				}
			}
		} catch (const std::exception& e) {
			logger::warn("[I18n] Error reading metadata from {}: {}",
				entry.path().string(), e.what());
		}

		availableLocales_.emplace_back(locale, displayName);
	}

	// Sort with "en" (English) first, then alphabetically by display name
	std::sort(availableLocales_.begin(), availableLocales_.end(),
		[](const auto& a, const auto& b) {
			if (a.first == b.first)
				return false;
			if (a.first == "en")
				return true;
			if (b.first == "en")
				return false;
			if (a.second != b.second)
				return a.second < b.second;
			return a.first < b.first;
		});

	if (availableLocales_.empty()) {
		availableLocales_.emplace_back("en", "English");
	}
}

bool I18n::LoadLocaleInto(const std::string& locale,
	std::unordered_map<std::string, std::string>& target) const
{
	auto filePath = GetLocaleFilePath(locale);

	std::ifstream f(filePath);
	if (!f.is_open()) {
		logger::info("[I18n] Locale file not found: {}", filePath.string());
		return false;
	}

	try {
		nlohmann::json j;
		f >> j;

		if (!j.is_object()) {
			logger::warn("[I18n] Locale file is not a JSON object: {}", filePath.string());
			return false;
		}

		size_t count = 0;
		for (auto& [key, value] : j.items()) {
			// Skip the metadata block
			if (key == "_meta")
				continue;

			if (value.is_string()) {
				target[key] = value.get<std::string>();
				++count;
			}
		}

		logger::info("[I18n] Loaded {} keys from '{}'", count, filePath.string());
		return true;
	} catch (const nlohmann::json::parse_error& e) {
		logger::error("[I18n] JSON parse error in {}: {}", filePath.string(), e.what());
		return false;
	} catch (const std::exception& e) {
		logger::error("[I18n] Error loading {}: {}", filePath.string(), e.what());
		return false;
	}
}

std::filesystem::path I18n::GetLocaleFilePath(const std::string& locale) const
{
	return Util::PathHelpers::GetTranslationsPath() / (locale + ".json");
}

std::string I18n::SubstitutePlaceholders(const std::string& tmpl,
	const std::unordered_map<std::string, std::string>& args)
{
	if (args.empty())
		return tmpl;

	std::string result;
	result.reserve(tmpl.size());

	size_t i = 0;
	while (i < tmpl.size()) {
		if (tmpl[i] == '{') {
			auto closePos = tmpl.find('}', i + 1);
			if (closePos != std::string::npos) {
				auto name = tmpl.substr(i + 1, closePos - i - 1);
				auto it = args.find(name);
				if (it != args.end()) {
					result += it->second;
					i = closePos + 1;
					continue;
				}
			}
		}
		result += tmpl[i];
		++i;
	}

	return result;
}

std::string I18n::DetectSystemLocale() const
{
	// Get the Windows UI language (LANGID = primary + sublanguage)
	LANGID langId = GetUserDefaultUILanguage();
	WORD primary = PRIMARYLANGID(langId);
	WORD sub = SUBLANGID(langId);

	// Map Windows LANG_* constants to our locale codes
	std::string detected;
	switch (primary) {
	case LANG_CHINESE:
		// Distinguish Simplified vs Traditional
		if (sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE) {
			detected = "zh_CN";
		} else {
			detected = "zh_TW";
		}
		break;
	case LANG_JAPANESE:
		detected = "ja";
		break;
	case LANG_KOREAN:
		detected = "ko";
		break;
	case LANG_GERMAN:
		detected = "de";
		break;
	case LANG_FRENCH:
		detected = "fr";
		break;
	case LANG_SPANISH:
		detected = "es";
		break;
	case LANG_PORTUGUESE:
		if (sub == SUBLANG_PORTUGUESE_BRAZILIAN) {
			detected = "pt_BR";
		} else {
			detected = "pt";
		}
		break;
	case LANG_RUSSIAN:
		detected = "ru";
		break;
	case LANG_ITALIAN:
		detected = "it";
		break;
	case LANG_POLISH:
		detected = "pl";
		break;
	case LANG_TURKISH:
		detected = "tr";
		break;
	case LANG_THAI:
		detected = "th";
		break;
	case LANG_VIETNAMESE:
		detected = "vi";
		break;
	case LANG_UKRAINIAN:
		detected = "uk";
		break;
	default:
		detected = "en";
		break;
	}

	// Check if the detected locale has a translation file available
	std::shared_lock lock(mutex_);
	for (const auto& [code, name] : availableLocales_) {
		if (code == detected) {
			return detected;
		}
	}

	// Try matching just the primary language prefix (e.g. "zh" matches "zh_CN")
	std::string prefix = detected.substr(0, 2);
	for (const auto& [code, name] : availableLocales_) {
		if (code.starts_with(prefix)) {
			return code;
		}
	}

	return "en";
}
