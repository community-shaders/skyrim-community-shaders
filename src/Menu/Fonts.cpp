#include "Fonts.h"

#include "ThemeManager.h"
#include "../Utils/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
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
	namespace
	{
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

		std::string ExtractFamilyName(const std::filesystem::path& relativePath)
		{
			if (relativePath.has_parent_path()) {
				auto it = relativePath.begin();
				if (it != relativePath.end()) {
					return it->string();
				}
			}
			auto stem = relativePath.stem().string();
			auto pos = stem.find_first_of("-_ ");
			return (pos != std::string::npos && pos > 0) ? stem.substr(0, pos) : stem;
		}

		std::string ExtractStyleName(const std::filesystem::path& relativePath, const std::string& family)
		{
			std::string stem = relativePath.stem().string();
			std::string lowerStem = ToLowerCopy(stem);
			std::string lowerFamily = ToLowerCopy(family);
			if (!lowerFamily.empty()) {
				std::string hyphen = lowerFamily + "-";
				std::string underscore = lowerFamily + "_";
				if (lowerStem.rfind(hyphen, 0) == 0) {
					stem = stem.substr(family.size() + 1);
				} else if (lowerStem.rfind(underscore, 0) == 0) {
					stem = stem.substr(family.size() + 1);
				}
			}

			auto pos = stem.find_first_of("-_ ");
			std::string style = (pos != std::string::npos) ? stem.substr(pos + 1) : stem;
			if (ToLowerCopy(style) == lowerFamily) {
				style = "Regular";
			}
			if (style.empty()) {
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
		Catalog DiscoverFontCatalog()
		{
			Catalog catalog;
			try {
				auto fontsPath = Util::PathHelpers::GetFontsPath();
				logger::debug("DiscoverFontCatalog: Scanning fonts directory: {}", fontsPath.string());

				if (!std::filesystem::exists(fontsPath)) {
					logger::warn("DiscoverFontCatalog: Fonts directory does not exist: {}", fontsPath.string());
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
			} catch (const std::exception& e) {
				logger::error("DiscoverFontCatalog: Exception occurred while scanning fonts: {}", e.what());
			}

			return catalog;
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
			std::filesystem::path relative(fontName);
			std::filesystem::path directPath = fontsPath / relative;

			auto isValidExtension = [](const std::filesystem::path& candidate) {
				auto extension = candidate.extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
				return extension == ".ttf" || extension == ".otf";
			};

			if (std::filesystem::exists(directPath) && std::filesystem::is_regular_file(directPath) && isValidExtension(directPath)) {
				return true;
			}

			std::string targetNormalized = ToLowerCopy(relative.generic_string());
			std::string targetFilename = ToLowerCopy(relative.filename().string());

			for (const auto& entry : std::filesystem::recursive_directory_iterator(fontsPath)) {
				if (!entry.is_regular_file()) {
					continue;
				}
				if (!isValidExtension(entry.path())) {
					continue;
				}

				std::string relativePath = NormalizeRelativeFontPath(fontsPath, entry.path());
				std::string relativeLower = ToLowerCopy(relativePath);
				std::string filenameLower = ToLowerCopy(entry.path().filename().string());

				if (relativeLower == targetNormalized || filenameLower == targetFilename) {
					return true;
				}
			}
		} catch (const std::exception& e) {
			logger::error("ValidateFont: Exception occurred while validating font '{}': {}", fontName, e.what());
		}

		return false;
	}
}  // namespace Util
