#include "Fonts.h"

#include "../Globals.h"
#include "../Utils/FileSystem.h"
#include "ThemeManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace MenuFonts
{
	namespace
	{
		constexpr size_t RoleIndex(Menu::FontRole role)
		{
			return static_cast<size_t>(role);
		}

		const Menu::ThemeSettings::FontRoleSettings& GetDefaultRoleInternal(Menu::FontRole role)
		{
			static const Menu::ThemeSettings defaults{};
			return defaults.FontRoles[RoleIndex(role)];
		}

		std::string NormalizeFontFilePath(const std::string& path)
		{
			if (path.empty()) {
				return {};
			}
			std::filesystem::path asPath(path);
			auto normalized = asPath.generic_string();
			while (!normalized.empty() && (normalized.front() == '/' || normalized.front() == '\\')) {
				normalized.erase(normalized.begin());
			}
			return normalized;
		}

		void DeriveFamilyAndStyle(FontRoleSettings& role)
		{
			if (role.File.empty()) {
				return;
			}
			std::filesystem::path relative(role.File);
			if (role.Family.empty()) {
				auto parent = relative.parent_path();
				if (!parent.empty()) {
					role.Family = parent.begin()->string();
				} else {
					auto stem = relative.stem().string();
					auto split = stem.find_first_of("-_ ");
					role.Family = split != std::string::npos ? stem.substr(0, split) : stem;
				}
			}
			if (role.Style.empty()) {
				auto stem = relative.stem().string();
				auto split = stem.find_first_of("-_ ");
				if (split != std::string::npos && split + 1 < stem.size()) {
					role.Style = stem.substr(split + 1);
				} else {
					role.Style = "Regular";
				}
			}
		}

		void ApplyRoleDefaults(FontRoleSettings& target, Menu::FontRole role)
		{
			const auto& defaults = GetDefaultRoleInternal(role);
			if (target.File.empty()) {
				target.File = defaults.File;
			}
			if (target.SizeScale <= 0.f) {
				target.SizeScale = defaults.SizeScale;
			}
			if (target.Family.empty()) {
				target.Family = defaults.Family;
			}
			if (target.Style.empty()) {
				target.Style = defaults.Style;
			}
		}
	}

	void NormalizeFontRoles(Menu::ThemeSettings& theme, bool themeProvidedFontRoles)
	{
		if (!themeProvidedFontRoles && !theme.FontName.empty()) {
			theme.FontRoles[RoleIndex(Menu::FontRole::Body)].File = NormalizeFontFilePath(theme.FontName);
		}

		for (size_t i = 0; i < static_cast<size_t>(Menu::FontRole::Count); ++i) {
			Menu::FontRole role = static_cast<Menu::FontRole>(i);
			auto& settings = theme.FontRoles[i];
			settings.File = NormalizeFontFilePath(settings.File);
			ApplyRoleDefaults(settings, role);
			DeriveFamilyAndStyle(settings);
			settings.SizeScale = std::clamp(settings.SizeScale, 0.1f, 4.0f);
		}

		if (theme.FontRoles[RoleIndex(Menu::FontRole::Body)].File.empty()) {
			ApplyRoleDefaults(theme.FontRoles[RoleIndex(Menu::FontRole::Body)], Menu::FontRole::Body);
		}

		if (!theme.FontName.empty()) {
			theme.FontName = NormalizeFontFilePath(theme.FontName);
		}

		if (theme.FontName.empty()) {
			theme.FontName = theme.FontRoles[RoleIndex(Menu::FontRole::Body)].File;
		}
	}

	const FontRoleSettings& GetDefaultRole(Menu::FontRole role)
	{
		return GetDefaultRoleInternal(role);
	}

	FontRoleGuard::FontRoleGuard(FontRole role)
	{
		Menu* menuInstance = globals::menu;
		if (!menuInstance) {
			menuInstance = Menu::GetSingleton();
		}
		if (menuInstance) {
			font_ = menuInstance->GetFont(role);
			if (font_) {
				ImGui::PushFont(font_, font_->LegacySize);
			}
		}
	}

	FontRoleGuard::~FontRoleGuard()
	{
		if (font_) {
			ImGui::PopFont();
		}
	}

	TabBarPaddingGuard::TabBarPaddingGuard(FontRole tabFontRole)
	{
		// Get the font that will be used for tabs
		ImFont* tabFont = globals::menu->GetFont(tabFontRole);
		ImFont* bodyFont = globals::menu->GetFont(FontRole::Body);

		if (tabFont && bodyFont) {
			float fontScale = tabFont->LegacySize / bodyFont->LegacySize;

			// Only scale if the tab font is noticeably larger
			if (fontScale > 1.05f) {
				ImGuiStyle& style = ImGui::GetStyle();
				originalPadding_ = style.FramePadding;

				// Scale padding proportionally to font size
				style.FramePadding.x *= fontScale;
				style.FramePadding.y *= fontScale;

				scaled_ = true;
			}
		}
	}

	TabBarPaddingGuard::~TabBarPaddingGuard()
	{
		if (scaled_) {
			ImGuiStyle& style = ImGui::GetStyle();
			style.FramePadding = originalPadding_;
		}
	}

	bool BeginTabItemWithFont(const char* label, FontRole role, ImGuiTabItemFlags flags)
	{
		// Push the font for this role
		FontRoleGuard guard(role);

		// Simply begin the tab item - padding adjustments should be handled
		// by the tab bar wrapper, not individual tab items
		return ImGui::BeginTabItem(label, nullptr, flags);
	}

	std::string BuildFontSignature(const Menu::ThemeSettings& theme, float baseFontSize)
	{
		std::string signature;
		signature.reserve(256);
		for (size_t i = 0; i < static_cast<size_t>(Menu::FontRole::Count); ++i) {
			Menu::FontRole role = static_cast<Menu::FontRole>(i);
			const auto& roleSettings = theme.FontRoles[i];
			float scaledSize = baseFontSize * roleSettings.SizeScale;
			scaledSize = std::clamp(scaledSize, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE);
			float roundedSize = std::round(scaledSize);
			signature += std::format("{}|{}|{:.2f};", Menu::GetFontRoleKey(role), roleSettings.File, roundedSize);
		}
		signature += std::format("base|{:.2f};", std::round(baseFontSize));
		return signature;
	}
}  // namespace MenuFonts

namespace Util
{
	// Security: Validate that a path stays within an allowed directory
	bool IsPathWithinDirectory(const std::filesystem::path& basePath, const std::filesystem::path& testPath)
	{
		try {
			// Canonicalize both paths to resolve all symlinks and .. sequences
			auto canonicalBase = std::filesystem::canonical(basePath);
			auto canonicalTest = std::filesystem::weakly_canonical(testPath);

			// Check if test path is a subpath of base path
			auto [baseIt, testIt] = std::mismatch(
				canonicalBase.begin(), canonicalBase.end(),
				canonicalTest.begin(), canonicalTest.end());

			return baseIt == canonicalBase.end();
		} catch (const std::filesystem::filesystem_error&) {
			// If canonicalization fails, reject the path
			return false;
		}
	}

	namespace
	{
		// Security: Sanitize user input to prevent path traversal
		std::string SanitizeFontPath(const std::string& input)
		{
			std::string sanitized = input;

			// Remove any leading path separators or traversal sequences
			while (!sanitized.empty() &&
				   (sanitized.front() == '/' || sanitized.front() == '\\' ||
					   sanitized.starts_with("../") || sanitized.starts_with("..\\") ||
					   sanitized.starts_with("./") || sanitized.starts_with(".\\"))) {
				if (sanitized.starts_with("../") || sanitized.starts_with("..\\")) {
					sanitized = sanitized.substr(3);
				} else if (sanitized.starts_with("./") || sanitized.starts_with(".\\")) {
					sanitized = sanitized.substr(2);
				} else {
					sanitized = sanitized.substr(1);
				}
			}

			// Replace all instances of .. in the path
			size_t pos = 0;
			while ((pos = sanitized.find("..", pos)) != std::string::npos) {
				sanitized.replace(pos, 2, "");
			}

			return sanitized;
		}

		std::string NormalizeRelativeFontPath(const std::filesystem::path& root, const std::filesystem::path& absolute)
		{
			auto relative = absolute.lexically_relative(root);
			auto normalized = relative.generic_string();
			while (!normalized.empty() && (normalized.front() == '/' || normalized.front() == '\\')) {
				normalized.erase(normalized.begin());
			}
			return normalized;
		}

		std::string ToLowerCopy(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		// Font width variants that should be part of the style, not the family name
		bool IsWidthVariant(const std::string& token)
		{
			static const std::vector<std::string> widthVariants = {
				"condensed", "narrow", "compressed", "compact",
				"extended", "expanded", "wide",
				"ultracompressed", "ultracondensed", "ultraexpanded"
			};
			std::string lower = ToLowerCopy(token);
			return std::find(widthVariants.begin(), widthVariants.end(), lower) != widthVariants.end();
		}

		std::string ExtractFamilyName(const std::filesystem::path& relativePath)
		{
			if (relativePath.has_parent_path()) {
				auto it = relativePath.begin();
				if (it != relativePath.end()) {
					return it->string();
				}
			}
			auto stem = relativePath.stem().string();

			// Split by delimiters and take tokens before width variants
			std::vector<std::string> tokens;
			std::string token;
			for (char c : stem) {
				if (c == '-' || c == '_' || c == ' ') {
					if (!token.empty()) {
						tokens.push_back(token);
						token.clear();
					}
				} else {
					token += c;
				}
			}
			if (!token.empty()) {
				tokens.push_back(token);
			}

			// Find first width variant and take everything before it
			std::string family;
			for (const auto& t : tokens) {
				if (IsWidthVariant(t)) {
					break;
				}
				if (!family.empty()) {
					family += " ";
				}
				family += t;
			}

			// Fallback to simple split
			if (family.empty()) {
				auto pos = stem.find_first_of("-_ ");
				family = (pos != std::string::npos && pos > 0) ? stem.substr(0, pos) : stem;
			}

			return family;
		}

		std::string ExtractStyleName(const std::filesystem::path& relativePath, const std::string& family)
		{
			std::string stem = relativePath.stem().string();
			std::string lowerStem = ToLowerCopy(stem);
			std::string lowerFamily = ToLowerCopy(family);

			// Remove family prefix if present
			if (!lowerFamily.empty()) {
				std::string hyphen = lowerFamily + "-";
				std::string underscore = lowerFamily + "_";
				std::string space = lowerFamily + " ";
				if (lowerStem.rfind(hyphen, 0) == 0) {
					stem = stem.substr(family.size() + 1);
				} else if (lowerStem.rfind(underscore, 0) == 0) {
					stem = stem.substr(family.size() + 1);
				} else if (lowerStem.rfind(space, 0) == 0) {
					stem = stem.substr(family.size() + 1);
				}
			}

			// Parse remaining tokens and build style
			std::vector<std::string> tokens;
			std::string token;
			for (char c : stem) {
				if (c == '-' || c == '_' || c == ' ') {
					if (!token.empty()) {
						tokens.push_back(token);
						token.clear();
					}
				} else {
					token += c;
				}
			}
			if (!token.empty()) {
				tokens.push_back(token);
			}

			// Build style from all tokens
			std::string style;
			for (const auto& t : tokens) {
				if (!style.empty()) {
					style += " ";
				}
				style += t;
			}

			if (style.empty() || ToLowerCopy(style) == lowerFamily) {
				style = "Regular";
			}
			return style;
		}

		std::string ToDisplayLabel(const std::string& token)
		{
			if (token.empty()) {
				return "Regular";
			}
			std::string display;
			display.reserve(token.size() + 4);
			char prev = '\0';
			for (char c : token) {
				if (c == '-' || c == '_') {
					if (!display.empty() && display.back() != ' ') {
						display.push_back(' ');
					}
					prev = ' ';
					continue;
				}
				if (!display.empty() && std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(c))) {
					display.push_back(' ');
				}
				display.push_back(c);
				prev = c;
			}
			if (!display.empty()) {
				display[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(display[0])));
			}
			return display;
		}

		// Helper function to format any font filename into a user-friendly display name
		std::string FormatFontDisplayName(const std::string& filename)
		{
			if (filename.empty()) {
				return "Unknown";
			}

			std::filesystem::path filePath(filename);
			std::string stem = filePath.stem().string();

			if (stem.empty()) {
				return "Unknown";
			}

			// Remove common font file prefixes if present
			std::vector<std::string> prefixes = { "Font-", "Font_", "TTF-", "TTF_" };
			for (const auto& prefix : prefixes) {
				if (stem.size() > prefix.size() &&
					ToLowerCopy(stem.substr(0, prefix.size())) == ToLowerCopy(prefix)) {
					stem = stem.substr(prefix.size());
					break;
				}
			}

			return ToDisplayLabel(stem);
		}

		int StyleRank(const std::string& style)
		{
			std::string lower = ToLowerCopy(style);
			struct WeightRank
			{
				const char* token;
				int rank;
			};
			static constexpr WeightRank weights[] = {
				{ "thin", 0 },
				{ "hairline", 0 },
				{ "extra light", 1 },
				{ "extralight", 1 },
				{ "light", 2 },
				{ "regular", 3 },
				{ "book", 3 },
				{ "normal", 3 },
				{ "medium", 4 },
				{ "semibold", 5 },
				{ "demibold", 5 },
				{ "bold", 6 },
				{ "extrabold", 7 },
				{ "heavy", 7 },
				{ "black", 8 }
			};
			int rank = 9;
			for (const auto& weight : weights) {
				if (lower.find(weight.token) != std::string::npos) {
					rank = weight.rank;
					break;
				}
			}
			if (lower.find("italic") != std::string::npos || lower.find("oblique") != std::string::npos) {
				rank += 1;
			}
			return rank;
		}
	}  // namespace

	namespace Fonts
	{
		// Performance: Cache font catalog to avoid repeated filesystem scans
		static std::optional<Catalog> cachedCatalog;
		static std::mutex catalogMutex;

		Catalog DiscoverFontCatalog(bool forceRefresh)
		{
			std::lock_guard<std::mutex> lock(catalogMutex);

			// Return cached catalog if available and not forcing refresh
			if (!forceRefresh && cachedCatalog.has_value()) {
				logger::debug("DiscoverFontCatalog: Using cached catalog ({} families)",
					cachedCatalog->families.size());
				return *cachedCatalog;
			}

			Catalog catalog;
			try {
				auto fontsPath = Util::PathHelpers::GetFontsPath();
				logger::debug("DiscoverFontCatalog: Scanning fonts directory: {}", fontsPath.string());

				if (!std::filesystem::exists(fontsPath)) {
					logger::warn("DiscoverFontCatalog: Fonts directory does not exist: {}", fontsPath.string());
					cachedCatalog = catalog;  // Cache empty result
					return catalog;
				}

				std::unordered_map<std::string, size_t> familyIndex;

				for (const auto& entry : std::filesystem::recursive_directory_iterator(fontsPath)) {
					if (!entry.is_regular_file()) {
						continue;
					}

					auto extension = entry.path().extension().string();
					std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
					if (extension != ".ttf" && extension != ".otf") {
						continue;
					}

					std::string relativePath;
					try {
						relativePath = NormalizeRelativeFontPath(fontsPath, entry.path());
					} catch (const std::exception& e) {
						logger::warn("DiscoverFontCatalog: Unable to relativize '{}': {}", entry.path().string(), e.what());
						continue;
					}

					std::filesystem::path relPath(relativePath);
					std::string family = ExtractFamilyName(relPath);
					if (family.empty()) {
						family = relPath.stem().string();
					}
					std::string style = ExtractStyleName(relPath, family);
					std::string familyKey = ToLowerCopy(family);
					size_t familyIdx;
					auto found = familyIndex.find(familyKey);
					if (found == familyIndex.end()) {
						FamilyInfo info;
						info.name = family;
						info.displayName = ToDisplayLabel(family);
						familyIdx = catalog.families.size();
						catalog.families.push_back(std::move(info));
						familyIndex.emplace(familyKey, familyIdx);
					} else {
						familyIdx = found->second;
					}

					auto& familyInfo = catalog.families[familyIdx];
					auto styleIt = std::find_if(familyInfo.styles.begin(), familyInfo.styles.end(), [&](const StyleInfo& existing) {
						return _stricmp(existing.style.c_str(), style.c_str()) == 0;
					});
					if (styleIt == familyInfo.styles.end()) {
						StyleInfo styleInfo;
						styleInfo.style = style.empty() ? "Regular" : style;
						styleInfo.displayName = ToDisplayLabel(styleInfo.style);
						styleInfo.file = relativePath;
						styleInfo.family = familyInfo.name;
						familyInfo.styles.push_back(std::move(styleInfo));
					} else {
						logger::debug("DiscoverFontCatalog: Duplicate style '{}' for family '{}', keeping first entry.", style, familyInfo.name);
					}
				}

				std::sort(catalog.families.begin(), catalog.families.end(), [](const FamilyInfo& a, const FamilyInfo& b) {
					return _stricmp(a.displayName.c_str(), b.displayName.c_str()) < 0;
				});

				for (auto& family : catalog.families) {
					std::sort(family.styles.begin(), family.styles.end(), [](const StyleInfo& a, const StyleInfo& b) {
						int rankA = StyleRank(a.style);
						int rankB = StyleRank(b.style);
						if (rankA != rankB) {
							return rankA < rankB;
						}
						return _stricmp(a.displayName.c_str(), b.displayName.c_str()) < 0;
					});
				}

				logger::info("DiscoverFontCatalog: Found {} font families", catalog.families.size());
				cachedCatalog = catalog;  // Cache the result
			} catch (const std::exception& e) {
				logger::error("DiscoverFontCatalog: Exception occurred while scanning fonts: {}", e.what());
				cachedCatalog = catalog;  // Cache even on error to avoid repeated failures
			}

			return catalog;
		}

		// Convenience overload that uses cached catalog by default
		Catalog DiscoverFontCatalog()
		{
			return DiscoverFontCatalog(false);
		}
	}  // namespace Fonts

	std::vector<std::string> DiscoverFonts()
	{
		std::vector<std::string> fonts;
		auto catalog = Fonts::DiscoverFontCatalog();
		for (const auto& family : catalog.families) {
			for (const auto& style : family.styles) {
				fonts.push_back(style.file);
			}
		}
		return fonts;
	}

	bool ValidateFont(const std::string& fontName)
	{
		if (fontName.empty()) {
			return false;
		}

		try {
			auto fontsPath = Util::PathHelpers::GetFontsPath();

			// Security: Sanitize input to prevent path traversal
			std::string sanitizedName = SanitizeFontPath(fontName);
			if (sanitizedName.empty()) {
				logger::warn("ValidateFont: Rejected potentially malicious path: {}", fontName);
				return false;
			}

			std::filesystem::path relative(sanitizedName);
			std::filesystem::path directPath = fontsPath / relative;

			// Security: Verify the resolved path stays within fonts directory
			if (!IsPathWithinDirectory(fontsPath, directPath)) {
				logger::warn("ValidateFont: Path traversal attempt detected: {}", fontName);
				return false;
			}

			auto isValidExtension = [](const std::filesystem::path& candidate) {
				auto extension = candidate.extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
				return extension == ".ttf" || extension == ".otf";
			};

			if (std::filesystem::exists(directPath) && std::filesystem::is_regular_file(directPath) && isValidExtension(directPath)) {
				return true;
			}

			// Performance: Use cached catalog for case-insensitive search instead of scanning filesystem
			auto catalog = Fonts::DiscoverFontCatalog();  // Uses cache
			std::string targetNormalized = ToLowerCopy(relative.generic_string());
			std::string targetFilename = ToLowerCopy(relative.filename().string());

			for (const auto& family : catalog.families) {
				for (const auto& style : family.styles) {
					std::string fileLower = ToLowerCopy(style.file);
					std::string filenameLower = ToLowerCopy(std::filesystem::path(style.file).filename().string());

					if (fileLower == targetNormalized || filenameLower == targetFilename) {
						return true;
					}
				}
			}
		} catch (const std::exception& e) {
			logger::error("ValidateFont: Exception occurred while validating font '{}': {}", fontName, e.what());
		}

		return false;
	}
}  // namespace Util
