#pragma once

#include "../Menu.h"
#include "FontAtlasState.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <optional>
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
	 * @brief RAII guard for pushing/popping an arbitrary ImGui font.
	 */
	class ImFontGuard
	{
	public:
		explicit ImFontGuard(ImFont* font);
		~ImFontGuard();

		ImFontGuard(const ImFontGuard&) = delete;
		ImFontGuard& operator=(const ImFontGuard&) = delete;

	private:
		bool pushed_ = false;
	};

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
		std::optional<ImFontGuard> guard_;
	};

	/**
	 * @brief RAII guard for tab bars that automatically scales padding for larger tab fonts
	 *
	 * Scales FramePadding when tab fonts are larger than body text to ensure proper
	 * tab bar height and separator positioning. Automatically restores original padding on destruction.
	 *
	 * Usage:
	 *   {
	 *       MenuFonts::TabBarPaddingGuard tabGuard(Menu::FontRole::Subheading);
	 *       if (Util::BeginPillTabBar("##MyTabs")) {
	 *           // Tab items...
	 *           Util::EndPillTabBar();
	 *       }
	 *   } // Padding automatically restored here
	 *   Prefer Util::BeginPillTabBar for new UI; TabBarPaddingGuard only affects classic ImGui tabs.
	 */
	class TabBarPaddingGuard
	{
	public:
		explicit TabBarPaddingGuard(FontRole tabFontRole);
		~TabBarPaddingGuard();

		TabBarPaddingGuard(const TabBarPaddingGuard&) = delete;
		TabBarPaddingGuard& operator=(const TabBarPaddingGuard&) = delete;

	private:
		ImVec2 originalPadding_;
		bool scaled_ = false;
	};

	/**
	 * @brief Begins a tab item with the specified font role for the label.
	 *
	 * Inside a Util::BeginPillTabBar, routes to Util::BeginPillTabItem (pill strip uses Body).
	 * Otherwise falls back to ImGui::BeginTabItem with a temporary FontRoleGuard on the label.
	 *
	 * @param label Tab label text
	 * @param role Font role to use for classic ImGui tab labels
	 * @param flags ImGui tab item flags
	 * @return true if the tab is selected and visible, false otherwise
	 */
	bool BeginTabItemWithFont(const char* label, FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None);

	/** @brief Ends a tab item opened via BeginTabItemWithFont / BeginPillTabItem / BeginTabItem. */
	void EndTabItem();

	/**
	 * @brief Loads catalog fonts into the current ImGui atlas for selector previews.
	 *
	 * Must be called after role fonts are added and before io.Fonts->Build().
	 * Preview fonts are baked at the given pixel size (typically matching body text) with a limited Latin glyph range.
	 */
	void AddPreviewFontsToAtlas(float previewFontSize);

	/** @brief Drops cached preview font pointers after io.Fonts->Clear(). */
	void InvalidatePreviewFonts();

	/**
	 * @brief Returns a preview font for a catalog file path, or nullptr if unavailable.
	 */
	[[nodiscard]] ImFont* GetPreviewFont(const std::string& file);
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

		[[nodiscard]] const StyleInfo* FindRegularStyle(const FamilyInfo& family);
		[[nodiscard]] int FindFamilyIndex(const Catalog& catalog, const std::string& familyName);
		[[nodiscard]] int FindStyleIndex(const FamilyInfo& family, const std::string& styleName);
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
