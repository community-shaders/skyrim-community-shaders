
#pragma once

#include "Util.h"
#include "Utils/Form.h"
#include <array>
#include <string_view>

#include <initializer_list>
#include <type_traits>
#include <vector>

namespace WidgetUI
{
	// Search bar / dropdown
	constexpr int kSearchBufferSize = 256;
	constexpr float kSearchBarWidth = 200.0f;
	constexpr float kSearchDropdownWidth = 300.0f;
	constexpr size_t kSearchDropdownMaxResults = 5;
	constexpr float kSearchDropdownBgGray = 0.16f;

	// Search-result highlight pulse
	constexpr float kHighlightDurationSeconds = 2.0f;
	constexpr float kHighlightMaxAlpha = 0.3f;
	constexpr ImVec4 kHighlightFrameBg = ImVec4(0.3f, 0.6f, 1.0f, 1.0f);
	constexpr ImVec4 kHighlightFrameBgHovered = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);

	// "Force Weather" lock button colors
	constexpr ImVec4 kLockButtonColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
	constexpr ImVec4 kLockButtonHoverColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);

	// Inherit checkbox styling (dark frame, grey mark)
	constexpr ImVec4 kInheritCheckboxFrameBg = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
	constexpr ImVec4 kInheritCheckboxMark = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

	// Transparent icon button styling
	constexpr ImVec4 kIconButtonTransparent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	constexpr ImVec4 kIconButtonHover = ImVec4(0.8f, 0.8f, 0.8f, 0.25f);

	// Category section header text (light teal-blue)
	constexpr ImVec4 kCategoryHeaderColor = ImVec4(0.7f, 0.9f, 1.0f, 1.0f);

	// Help / hint text (light grey)
	constexpr ImVec4 kHelpTextColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

	// Feature override toggle button states (enabled = green, disabled falls back to GetDisabled())
	constexpr ImVec4 kOverrideEnabledButton = ImVec4(0.2f, 0.7f, 0.2f, 1.0f);
	constexpr ImVec4 kOverrideEnabledButtonHovered = ImVec4(0.3f, 0.8f, 0.3f, 1.0f);
	constexpr ImVec4 kOverrideEnabledButtonActive = ImVec4(0.1f, 0.6f, 0.1f, 1.0f);
	constexpr ImVec4 kOverrideDisabledButtonHovered = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
	constexpr ImVec4 kOverrideDisabledButtonActive = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

	// Icon button spacing
	constexpr float kIconButtonSpacing = 4.0f;
	constexpr float kIconButtonSizeRatio = 0.85f;
}

class Widget
{
public:
	RE::TESForm* form = nullptr;

	/** @brief Virtual destructor. */
	virtual ~Widget() {};

	static constexpr std::string_view kWeatherFolderName = "Weathers";
	static constexpr std::string_view kLightingTemplateFolderName = "Lighting Templates";
	static constexpr std::string_view kImageSpaceFolderName = "ImageSpaces";
	static constexpr std::string_view kVolumetricLightingFolderName = "Volumetric Lighting";
	static constexpr std::string_view kPrecipitationFolderName = "Precipitation";
	static constexpr std::string_view kVisualEffectsFolderName = "Visual Effects";
	static constexpr std::string_view kCellLightingFolderName = "Cell Lighting";
	static constexpr std::string_view kOtherEditorWidgetsFolderName = "Other Editor Widgets";

	static constexpr std::array kSaveFolderNames = {
		kWeatherFolderName,
		kLightingTemplateFolderName,
		kImageSpaceFolderName,
		kVolumetricLightingFolderName,
		kPrecipitationFolderName,
		kVisualEffectsFolderName,
		kCellLightingFolderName,
		kOtherEditorWidgetsFolderName
	};

	/** @brief Get the cached editor ID for this widget's form, retrying if using a fallback. */
	virtual std::string GetEditorID() const
	{
		// If using a fallback ID, retry getting the real EditorID
		if (isFallbackEditorID && form) {
			const char* editorID = form->GetFormEditorID();
			if (editorID && editorID[0] != '\0') {
				cachedEditorID = editorID;
				isFallbackEditorID = false;
				return editorID;
			}
		}
		return cachedEditorID;
	}

	/** @brief SPID-based key for file save/load operations (load-order-portable). */
	std::string GetSaveKey() const
	{
		return cachedSaveKey;
	}

	/** @brief Full path to this widget's save file. */
	std::string GetSaveFilePath() const;

	/** @brief Get the form ID as an 8-character hex string. */
	virtual std::string GetFormID() const
	{
		if (!form)
			return "00000000";
		return std::format("{:08X}", form->GetFormID());
	}

	/** @brief Get the plugin filename that owns this form. */
	virtual std::string GetFilename() const
	{
		if (!form)
			return "Invalid";
		if (auto file = form->GetFile())
			return std::format("{}", file->GetFilename());
		return "Generated";
	}

	/** @brief Cache the editor ID and save key from the form for fast subsequent lookups. */
	void CacheFormData()
	{
		if (!form) {
			cachedEditorID = "Invalid";
			cachedSaveKey = "Invalid";
			isFallbackEditorID = false;
			return;
		}

		// Cache the SPID-based save key (always load-order-portable)
		cachedSaveKey = Util::GetFormFileKey(form);

		// Try to resolve EditorID via shared utility
		std::string editorId = Util::GetFormEditorID(form);
		if (!editorId.empty()) {
			cachedEditorID = editorId;
			isFallbackEditorID = false;
			return;
		}

		// Fallback: type prefix + SPID key
		const char* prefix = [&]() -> const char* {
			switch (form->GetFormType()) {
			case RE::FormType::ImageSpace:
				return "IS";
			case RE::FormType::VolumetricLighting:
				return "VL";
			case RE::FormType::ShaderParticleGeometryData:
				return "Particle";
			case RE::FormType::LensFlare:
				return "LensFlare";
			case RE::FormType::ReferenceEffect:
				return "VisualEffect";
			default:
				return "Form";
			}
		}();
		cachedEditorID = std::format("{}_{}", prefix, cachedSaveKey);
		isFallbackEditorID = true;
	}

	/** @brief Draw this widget's ImGui editing interface. Must be implemented by subclasses. */
	virtual void DrawWidget() = 0;

	/** @brief Type name for widget-type-level state sharing (window size, etc.). */
	virtual const char* GetWidgetTypeName() const = 0;

	/** @brief Call instead of SetupWidgetWindowDefaults + ImGui::Begin. Tracks per-type window size. */
	bool BeginWidgetWindow();

	/** @brief Queue focus for the next BeginWidgetWindow call (use instead of SetWindowFocus on a not-yet-drawn window). */
	void RequestFocus() { m_pendingFocus = true; }

	bool open = false;

	/** @brief Returns true if this widget's window is currently open. */
	bool IsOpen() const
	{
		return open;
	}

	/**
	 * @brief Set the open/closed state of this widget's window.
	 * @param state True to open, false to close.
	 */
	void SetOpen(bool state = true)
	{
		open = state;
	}

	/** @brief Returns a window title with unique ImGui ID: "EditorID###FormID". */
	std::string GetWindowTitle() const
	{
		return std::format("{}###{}", GetEditorID(), GetFormID());
	}

	/** @brief Save the widget's current settings to its JSON file on disk. */
	void Save();

	/**
	 * @brief Load this widget's settings from its JSON file on disk.
	 * @param showNotification If true, display a notification on successful load.
	 */
	void Load(bool showNotification = true);

	/** @brief Returns true if a saved JSON file exists on disk for this widget. */
	bool HasSavedFile() const;

	/** @brief Delete this widget's saved JSON file from disk. */
	virtual void Delete();

	/** @brief Load widget-specific settings from the internal JSON object. Must be implemented by subclasses. */
	virtual void LoadSettings() = 0;

	/** @brief Save widget-specific settings to the internal JSON object. Must be implemented by subclasses. */
	virtual void SaveSettings() = 0;

	/** @brief Apply the current widget settings to the live game form. Must be implemented by subclasses. */
	virtual void ApplyChanges() = 0;

	/** @brief Revert widget settings to the last loaded state. Default implementation calls LoadSettings. */
	virtual void RevertChanges() { LoadSettings(); }

	/** @brief Returns true if the widget has unsaved modifications relative to its saved file. */
	virtual bool HasUnsavedChanges() const { return false; }

	/**
	 * @brief Reinitialize a weather form to apply form references that are only read at load time.
	 * @param weather The weather form to reinitialize.
	 */
	static void ForceWeatherReinit(RE::TESWeather* weather);

	/** @brief Reinitialize the current sky weather (use when the specific weather is unknown). */
	static void ForceCurrentWeatherReinit();

	/** @brief Override to suppress per-frame auto-apply and show a manual-apply warning in the header. */
	virtual bool RequiresManualApply() const { return false; }

	/**
	 * @brief Draw the common widget header with search bar and action buttons.
	 * @param searchId         ImGui ID for the search input.
	 * @param showApply        If true, show the Apply button.
	 * @param showSaveLoadRevert If true, show Save/Load/Revert buttons.
	 * @param showForceWeather If true, show the Force Weather lock button.
	 * @param weather          The weather form for the Force Weather button (required when showForceWeather is true).
	 */
	void DrawWidgetHeader(const char* searchId, bool showApply = true, bool showSaveLoadRevert = false, bool showForceWeather = false, RE::TESWeather* weather = nullptr);

	// Search functionality
	char searchBuffer[WidgetUI::kSearchBufferSize] = "";
	int deleteConfirmationFrame = -1;

	// Unified search dropdown + tab navigation + highlight helpers.
	// Widgets supply searchable entries via CollectSearchableSettings(); DrawSearchDropdown()
	// renders the matches and updates navigation state on selection.
	struct SearchResult
	{
		std::string displayName;
		std::string tabName;    // empty if widget has no tabs
		std::string settingId;  // id used for highlight matching
	};
	/** @brief Collect all searchable settings for the search dropdown. Override to provide entries. */
	virtual std::vector<SearchResult> CollectSearchableSettings() const { return {}; }

	/** @brief Render the search dropdown. Call immediately after DrawWidgetHeader(). */
	void DrawSearchDropdown();

	/**
	 * @brief Get tab flags for search-driven tab navigation.
	 *
	 * Returns ImGuiTabItemFlags_SetSelected if the given tab matches the pending
	 * navigation request, otherwise 0. Clears the override after the first tab is set.
	 * @param tabName The tab name to check against the pending navigation.
	 * @return ImGuiTabItemFlags_SetSelected or 0.
	 */
	int GetTabFlagsForOverride(const std::string& tabName);

	/**
	 * @brief Check if a setting matches the current search query.
	 *
	 * Returns true when no search is active, or when settingId appears in the
	 * current filtered results. Use DrawIfMatchesSearch() for simple controls.
	 * @param settingId The setting identifier to check.
	 * @return True if the setting should be visible.
	 */
	bool MatchesSearch(const std::string& settingId) const;

	/**
	 * @brief Check if any of the given setting IDs match the current search query.
	 * @param settingIds List of setting identifiers to check.
	 * @return True if any setting matches or no search is active.
	 */
	bool MatchesAnySearch(std::initializer_list<const char*> settingIds) const;

	/**
	 * @brief Draw a control only if its setting ID matches the current search.
	 * @tparam DrawFn Callable taking const char* settingId, returning bool.
	 * @param  settingId The setting identifier for search filtering.
	 * @param  draw      Drawing callback invoked with the setting ID.
	 * @return True if the control was drawn and returned true.
	 */
	template <class DrawFn>
	bool DrawIfMatchesSearch(const char* settingId, DrawFn draw)
	{
		return MatchesSearch(settingId) && draw(settingId);
	}

	/** @copydoc DrawIfMatchesSearch(const char*, DrawFn) */
	template <class DrawFn>
	bool DrawIfMatchesSearch(const std::string& settingId, DrawFn draw)
	{
		return MatchesSearch(settingId) && draw(settingId.c_str());
	}

	/**
	 * @brief Draw a section only if its setting ID matches the current search.
	 * @tparam DrawFn Callable taking const char* settingId, returning void.
	 * @param  settingId The setting identifier for search filtering.
	 * @param  draw      Drawing callback invoked with the setting ID.
	 */
	template <class DrawFn>
	void DrawSearchSectionIfMatches(const char* settingId, DrawFn draw)
	{
		if (MatchesSearch(settingId))
			draw(settingId);
	}

	/** @copydoc DrawSearchSectionIfMatches(const char*, DrawFn) */
	template <class DrawFn>
	void DrawSearchSectionIfMatches(const std::string& settingId, DrawFn draw)
	{
		if (MatchesSearch(settingId))
			draw(settingId.c_str());
	}

	/** @brief Returns true if a search is active or the user navigated from search, indicating sections should be expanded. */
	bool ShouldOpenSearchSection() const { return searchBuffer[0] != '\0' || navigatedFromSearch; }

	/**
	 * @brief Check if the given setting is currently highlighted by search navigation.
	 * @param settingId The setting identifier to check.
	 * @return True if this setting is within the animated highlight window.
	 */
	bool IsHighlighted(const std::string& settingId) const;

	/**
	 * @brief Push a pulsing highlight style for the next ImGui widget. Call PopHighlightStyle after.
	 * @param settingId The setting identifier being highlighted.
	 */
	void PushHighlightStyle(const std::string& settingId);

	/**
	 * @brief Pop the highlight style pushed by PushHighlightStyle.
	 * @param settingId The setting identifier that was highlighted.
	 */
	void PopHighlightStyle(const std::string& settingId);

	/**
	 * @brief Push highlight style only if the setting is currently highlighted.
	 * @param settingId The setting identifier to check and potentially highlight.
	 * @return True if highlight was pushed (caller must call PopHighlightIfNeeded).
	 */
	bool PushHighlightIfNeeded(const std::string& settingId)
	{
		if (!IsHighlighted(settingId))
			return false;
		PushHighlightStyle(settingId);
		return true;
	}

	/**
	 * @brief Pop highlight style if it was previously pushed.
	 * @param settingId The setting identifier.
	 * @param pushed    The return value from the corresponding PushHighlightIfNeeded call.
	 */
	void PopHighlightIfNeeded(const std::string& settingId, bool pushed)
	{
		if (pushed)
			PopHighlightStyle(settingId);
	}

	/**
	 * @brief Draw a control wrapped with automatic highlight push/pop.
	 * @tparam DrawFn Callable returning void or a value.
	 * @param  settingId The setting identifier for highlight matching.
	 * @param  draw      Drawing callback to invoke between push and pop.
	 * @return The return value of draw, if non-void.
	 */
	template <class DrawFn>
	auto DrawWithHighlight(const std::string& settingId, DrawFn draw)
	{
		const bool highlighted = PushHighlightIfNeeded(settingId);
		if constexpr (std::is_void_v<std::invoke_result_t<DrawFn>>) {
			draw();
			PopHighlightIfNeeded(settingId, highlighted);
		} else {
			auto result = draw();
			PopHighlightIfNeeded(settingId, highlighted);
			return result;
		}
	}

	/**
	 * @brief Draw a modal confirmation dialog for deleting this widget's saved data.
	 * @param popupId ImGui popup ID for the modal.
	 */
	void DrawDeleteConfirmationModal(const char* popupId = "DeleteConfirmation");

	json js = json();

protected:
	mutable std::string cachedEditorID;
	mutable std::string cachedSaveKey;
	mutable bool isFallbackEditorID = false;
	virtual void DrawMenu();
	std::string GetFolderName() const;

	// Cached dropdown position from DrawWidgetHeader so DrawSearchDropdown() can anchor below the search bar.
	ImVec2 searchDropdownAnchor{ 0.0f, 0.0f };
	ImVec2 searchInputMin{ 0.0f, 0.0f };
	ImVec2 searchInputMax{ 0.0f, 0.0f };

	// Whether the search result dropdown is currently visible.
	bool dropdownVisible = false;

	// Navigation / highlight state shared by the search dropdown.
	std::vector<SearchResult> searchResults;
	std::string activeTabOverride;
	std::string highlightedSetting;
	std::string highlightedDisplaySetting;
	float highlightStartTime = 0.0f;
	bool scrollToHighlighted = false;
	bool navigatedFromSearch = false;

	// Cache the last query searchResults was built for, so per-frame work is skipped
	// while the buffer is unchanged.
	std::string searchResultsForQuery;
	bool m_pendingFocus = false;

	void ClearSearchState(bool clearBuffer);
	void NavigateToSearchResult(const SearchResult& result);
};

/** @brief Lightweight widget for caching form data without full editing functionality. */
class SimpleFormWidget : public Widget
{
public:
	void DrawWidget() override {}
	const char* GetWidgetTypeName() const override { return ""; }
	void LoadSettings() override {}
	void SaveSettings() override {}
	void ApplyChanges() override {}
};
