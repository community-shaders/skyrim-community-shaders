#pragma once

#include "../Menu.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui.h>
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

	/**
	 * @brief RAII guard for pushing/popping ImGui fonts based on font roles
	 *
	 * Automatically pushes the specified font role on construction and pops it on destruction.
	 * This ensures proper font stack management even if exceptions occur.
	 *
	 * Usage:
	 *   {
	 *       MenuFonts::FontRoleGuard guard(Menu::FontRole::Heading);
	 *       ImGui::Text("This text uses the Heading font");
	 *   } // Font automatically popped here
	 */
	class FontRoleGuard
	{
	public:
		explicit FontRoleGuard(FontRole role);
		~FontRoleGuard();

		FontRoleGuard(const FontRoleGuard&) = delete;
		FontRoleGuard& operator=(const FontRoleGuard&) = delete;

		[[nodiscard]] ImFont* Get() const { return font_; }

	private:
		ImFont* font_ = nullptr;
	};

	/**
	 * @brief Begins an ImGui tab item with the specified font role
	 *
	 * Convenience wrapper that combines FontRoleGuard with ImGui::BeginTabItem.
	 * The font is automatically managed and will be popped when the tab ends.
	 *
	 * @param label Tab label text
	 * @param role Font role to use for the tab
	 * @param flags ImGui tab item flags
	 * @return true if the tab is selected and visible, false otherwise
	 */
	bool BeginTabItemWithFont(const char* label, FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None);
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
