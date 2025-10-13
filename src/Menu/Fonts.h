#pragma once

#include "../Menu.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace MenuFonts
{
	using FontRole = Menu::FontRole;
	using FontRoleSettings = Menu::ThemeSettings::FontRoleSettings;

	void NormalizeFontRoles(Menu::ThemeSettings& theme, bool themeProvidedFontRoles);
	const FontRoleSettings& GetDefaultRole(FontRole role);
	std::string BuildFontSignature(const Menu::ThemeSettings& theme, float baseFontSize);
}

namespace Util
{
	namespace Fonts
	{
		struct StyleInfo
		{
			std::string style;
			std::string displayName;
			std::string file;
			std::string family;
		};

		struct FamilyInfo
		{
			std::string name;
			std::string displayName;
			std::vector<StyleInfo> styles;
		};

		struct Catalog
		{
			std::vector<FamilyInfo> families;

			const FamilyInfo* FindFamily(const std::string& name) const;
			const StyleInfo* FindStyle(const std::string& family, const std::string& style) const;
		};

		Catalog DiscoverFontCatalog();
		Catalog DiscoverFontCatalog(bool forceRefresh);  // Explicit refresh control
		std::string FormatFontDisplayName(const std::string& filename);
	}

	std::vector<std::string> DiscoverFonts();
	bool ValidateFont(const std::string& fontName);
	
	// Security: Path validation helpers
	bool IsPathWithinDirectory(const std::filesystem::path& basePath, const std::filesystem::path& testPath);
}

inline const Util::Fonts::FamilyInfo* Util::Fonts::Catalog::FindFamily(const std::string& name) const
{
	auto it = std::find_if(families.begin(), families.end(), [&](const FamilyInfo& info) {
		return _stricmp(info.name.c_str(), name.c_str()) == 0;
	});
	return it != families.end() ? &(*it) : nullptr;
}

inline const Util::Fonts::StyleInfo* Util::Fonts::Catalog::FindStyle(const std::string& family, const std::string& style) const
{
	const FamilyInfo* familyInfo = FindFamily(family);
	if (!familyInfo) {
		return nullptr;
	}
	auto it = std::find_if(familyInfo->styles.begin(), familyInfo->styles.end(), [&](const StyleInfo& info) {
		return _stricmp(info.style.c_str(), style.c_str()) == 0;
	});
	return it != familyInfo->styles.end() ? &(*it) : nullptr;
}
