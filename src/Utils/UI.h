#pragma once
#include <algorithm>
#include <cfloat>  // For FLT_MAX
#include <functional>
#include <imgui.h>
#include <string>
#include <vector>
#include <windows.h>  // For WPARAM and virtual key constants

#include "../Menu/Fonts.h"

// Forward declarations
struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ImVec2;
class Menu;
class Feature;

#define BUFFER_VIEWER_NODE(a_value, a_scale)                                                                 \
	if (ImGui::TreeNode(#a_value)) {                                                                         \
		ImGui::Image(a_value->srv.get(), { a_value->desc.Width * a_scale, a_value->desc.Height * a_scale }); \
		ImGui::TreePop();                                                                                    \
	}

#define BUFFER_VIEWER_NODE_BULLET(a_value, a_scale) \
	ImGui::BulletText(#a_value);                    \
	ImGui::Image(a_value->srv.get(), { a_value->desc.Width * a_scale, a_value->desc.Height * a_scale });

#define ADDRESS_NODE(a_value)                                                                        \
	if (ImGui::Button(#a_value)) {                                                                   \
		ImGui::SetClipboardText(std::format("{0:x}", reinterpret_cast<uintptr_t>(a_value)).c_str()); \
	}                                                                                                \
	if (ImGui::IsItemHovered())                                                                      \
		ImGui::SetTooltip(std::format("Copy {} Address to Clipboard", #a_value).c_str());

namespace Util
{
	/**
	 * Represents a single line and its color for any colored text rendering (tooltips, legends, etc.).
	 */
	struct ColoredTextLine
	{
		std::string text;
		ImVec4 color;
	};
	using ColoredTextLines = std::vector<ColoredTextLine>;

	// Text rendering constants
	constexpr float DefaultHeaderTextScale = 1.5f;  // Larger scale for header text to improve readability

	/**
	 * Usage:
	 * if (auto _tt = Util::HoverTooltipWrapper()){
	 *     ImGui::Text("What the tooltip says.");
	 * }
	*/
	class HoverTooltipWrapper
	{
	private:
		bool hovered;

	public:
		HoverTooltipWrapper();
		~HoverTooltipWrapper();
		inline operator bool() { return hovered; }
	};

	/**
	 * Usage:
	 * {
	 *      auto _ = DisableGuard(disableThis);
	 *      ... Some settings ...
	 * }
	*/
	class DisableGuard
	{
	private:
		bool disable;

	public:
		DisableGuard(bool disable);
		~DisableGuard();
	};

	/**
	 * RAII wrapper for styled ImGui buttons that automatically applies and restores styling.
	 * Use this to ensure consistent button styling without forgetting to pop styles.
	 */
	class StyledButtonWrapper
	{
	public:
		/**
		 * Creates a styled button wrapper with custom colors.
		 * @param normalColor Color when button is not hovered/pressed
		 * @param hoveredColor Color when button is hovered
		 * @param activeColor Color when button is pressed
		 */
		StyledButtonWrapper(const ImVec4& normalColor, const ImVec4& hoveredColor, const ImVec4& activeColor);

		/**
		 * Destructor automatically pops the applied styles
		 */
		~StyledButtonWrapper();

		// Delete copy and move operations to prevent double pops
		StyledButtonWrapper(const StyledButtonWrapper&) = delete;
		StyledButtonWrapper& operator=(const StyledButtonWrapper&) = delete;
		StyledButtonWrapper(StyledButtonWrapper&&) = delete;
		StyledButtonWrapper& operator=(StyledButtonWrapper&&) = delete;

	private:
		int m_pushedStyles;
	};

	/**
	 * Button with simple flash feedback (matches action icon hover effect style)
	 * @param label Button text
	 * @param size Button size (optional)
	 * @param flashDurationMs How long to show flash effect in milliseconds (default 200ms)
	 * @return True if the button was clicked
	 */
	bool ButtonWithFlash(const char* label, const ImVec2& size = ImVec2(0, 0), int flashDurationMs = 200);

	/**
	 * Clean, minimalist toggle switch for feature enable/disable state
	 * @param label Label text to display next to the toggle
	 * @param enabled Reference to the boolean state to toggle
	 * @param size Toggle size (optional, defaults to automatic sizing)
	 * @return True if the toggle state was changed
	 */
	bool FeatureToggle(const char* label, bool* enabled, const ImVec2& size = ImVec2(0, 0));

	/**
	 * RAII wrapper for creating collapsible UI sections.
	 * Automatically handles the TreeNode creation, styling, and cleanup.
	 */
	class SectionWrapper
	{
	public:
		/**
		 * Creates a section wrapper for organizing UI content.
		 * @param title The section title
		 * @param description Optional description text shown below the title
		 * @param titleColor Color for the section title
		 * @param isVisible Whether the section should be visible (used for conditional sections)
		 */
		SectionWrapper(const char* title, const char* description = nullptr,
			const ImVec4& titleColor = ImVec4(1, 1, 1, 1), bool isVisible = true);

		/**
		 * Destructor automatically closes the TreeNode if it was opened
		 */
		~SectionWrapper();

		/**
		 * Conversion operator to check if section should be drawn
		 */
		operator bool() const;

		// Delete copy and move operations to prevent double pops
		SectionWrapper(const SectionWrapper&) = delete;
		SectionWrapper& operator=(const SectionWrapper&) = delete;
		SectionWrapper(SectionWrapper&&) = delete;
		SectionWrapper& operator=(SectionWrapper&&) = delete;

	private:
		bool m_shouldDraw;
		bool m_treeNodeOpened;
	};

	/**
	 * Color utilities for contrast and readability
	 */
	namespace ColorUtils
	{
		/**
		 * Calculates the relative luminance of a color according to WCAG guidelines
		 * @param color ImVec4 color to calculate luminance for
		 * @return Luminance value between 0.0 (darkest) and 1.0 (brightest)
		 */
		float CalculateLuminance(const ImVec4& color);

		/**
		 * Determines the appropriate text color (black or white) for maximum contrast
		 * against the given background color
		 * @param backgroundColor Background color to test against
		 * @param threshold Luminance threshold for switching (default 0.5)
		 * @return Black color for light backgrounds, white color for dark backgrounds
		 */
		ImVec4 GetContrastingTextColor(const ImVec4& backgroundColor, float threshold = 0.5f);

		/**
		 * Calculates contrast ratio between two colors according to WCAG guidelines
		 * @param color1 First color
		 * @param color2 Second color
		 * @return Contrast ratio (1.0 = no contrast, 21.0 = maximum contrast)
		 */
		float CalculateContrastRatio(const ImVec4& color1, const ImVec4& color2);

		/**
		 * Adjusts a background color to ensure contrast with text
		 * Darkens light backgrounds or lightens dark backgrounds to prevent same-color-on-same-color issues
		 * @param backgroundColor Background color to adjust (modified in place)
		 * @param textLuminance Luminance of the text color
		 * @param luminanceThreshold Threshold for determining light vs dark (default 0.5)
		 * @param darkenFactor Multiplier for darkening light backgrounds (default 0.4 = 60% darker)
		 * @param lightenOffset Additive offset for lightening dark backgrounds (default 0.3 = 30% brighter)
		 */
		void AdjustBackgroundForTextContrast(ImVec4& backgroundColor, float textLuminance,
			float luminanceThreshold = 0.5f, float darkenFactor = 0.4f, float lightenOffset = 0.3f);

		/**
		 * Adjusts a text color to ensure sufficient contrast against a background
		 * @param textColor The desired text color (semantic color)
		 * @param backgroundColor The background color to contrast against
		 * @param minimumRatio Minimum acceptable contrast ratio (default 3.0)
		 * @return Adjusted text color with sufficient contrast
		 */
		ImVec4 AdjustColorForContrast(const ImVec4& textColor, const ImVec4& backgroundColor, float minimumRatio = 3.0f);

		/**
		 * Creates a selectable item with automatic contrast-aware text coloring
		 * @param label Text to display
		 * @param selected Whether the item is currently selected
		 * @param flags Selectable flags (optional)
		 * @param size Size of the selectable area (optional)
		 * @return True if the selectable was clicked
		 */
		bool ContrastSelectable(const char* label, bool selected, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));

		/**
		 * Creates a selectable item with contrast-adjusted semantic text coloring
		 * Preserves the intent of semantic colors while ensuring adequate contrast
		 * @param label Text to display
		 * @param selected Whether the item is currently selected
		 * @param semanticTextColor The desired semantic color (will be adjusted for contrast)
		 * @param flags Selectable flags (optional)
		 * @param size Size of the selectable area (optional)
		 * @return True if the selectable was clicked
		 */
		bool ContrastSelectableWithColor(const char* label, bool selected, const ImVec4& semanticTextColor, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));
	}

	bool PercentageSlider(const char* label, float* data, float lb = 0.f, float ub = 100.f, const char* format = "%.1f %%");
	ImVec2 GetNativeViewportSizeScaled(float scale);

	// Icon loading functions
	// `device` must remain alive for the SRV lifetime. Caller owns *out_srv and must `Release()` it.
	bool LoadTextureFromFile(ID3D11Device* device,
		const char* filename,
		ID3D11ShaderResourceView** out_srv,
		ImVec2& out_size);
	bool InitializeMenuIcons(Menu* menu);

	// Text rendering helpers for clearer title text
	// These functions modify ImGui rendering state and should be called within ImGui context
	ImVec2 DrawSharpText(const char* text, bool alignToPixelGrid = true, float scale = 1.0f);
	ImVec2 DrawAlignedTextWithLogo(ID3D11ShaderResourceView* logoTexture, const ImVec2& logoSize, const char* text, float textScale = DefaultHeaderTextScale);

	/**
	 * Draws a custom styled collapsible category header with lines extending from both sides
	 * @param categoryName The name of the category to display
	 * @param isExpanded Reference to the expansion state
	 * @param categoryCount Number of features in the category
	 * @return true if the expansion state was toggled
	 */
	bool DrawCategoryHeader(const char* categoryName, bool& isExpanded, int categoryCount);

	/**
	 * Draws a custom styled section header with lines extending from both sides
	 * @param sectionName The name of the section to display
	 * @param useWhiteText Whether to use white text (for differentiation)
	 * @param isCollapsible Whether the header should be collapsible
	 * @param isExpanded Reference to the expansion state (only used if collapsible)
	 * @return true if the expansion state was toggled (only relevant if collapsible)
	 */
	bool DrawSectionHeader(const char* sectionName, bool useWhiteText = false, bool isCollapsible = true, bool* isExpanded = nullptr);

	/**
	 * Configuration for color-coded value display with flexible thresholds and colors.
	 * Supports variable number of thresholds and corresponding colors.
	 */
	struct ColorCodedValueConfig
	{
		struct ThresholdColor
		{
			float threshold;
			ImVec4 color;

			ThresholdColor(float t, const ImVec4& c) :
				threshold(t), color(c) {}
		};

		std::vector<ThresholdColor> thresholds;  // Thresholds in ascending order with their colors
		const char* format = "%.1f%%";           // Printf-style format string for the value
		const char* tooltipText = nullptr;       // Optional tooltip text
		bool sameLine = true;                    // Whether to put value on same line as label

		// Helper methods for common patterns (implemented in UI.cpp to avoid header dependencies)
		// Use when higher values indicate problems/danger (intensity, errors, warnings)
		static ColorCodedValueConfig HighIsBad(float low, float med, float high);
		// Use when higher values indicate good things (performance, quality, progress)
		static ColorCodedValueConfig HighIsGood(float low, float med, float high);
	};
	/**
	 * Color-codes a value based on flexible thresholds and displays it with optional tooltip.
	 * Common pattern for showing status values (percentages, intensities, etc.) with color feedback.
	 *
	 * @param label The label to display next to the value.
	 * @param valueToCheck The numeric value to use for color-coding (compared to thresholds).
	 * @param valueStr The string to display (can be formatted, units, or descriptive text).
	 * @param config The configuration for thresholds, colors, formatting, and tooltip.
	 * @param useBullet If true (default), use ImGui::BulletText for the label; if false, use ImGui::Text.
	 */
	void DrawColorCodedValue(
		const std::string& label,
		float valueToCheck,
		const std::string& valueStr,
		const ColorCodedValueConfig& config,
		bool useBullet = true);

	/**
	 * @brief Draws a multi-line tooltip with optional per-line coloring.
	 *
	 * IMPORTANT: This function should only be called from within a tooltip context
	 * (e.g., from within a HoverTooltipWrapper or BeginTooltip/EndTooltip block).
	 * Do not call this function directly without proper tooltip context.
	 *
	 * @param lines The lines of text to display in the tooltip (as std::vector<std::string>).
	 * @param colors Optional per-line colors (if empty, default color is used for all lines).
	 */
	void DrawMultiLineTooltip(const std::vector<std::string>& lines, const std::vector<ImVec4>& colors = {});

	/**
	 * @brief Draws a multi-line tooltip with optional per-line coloring.
	 *
	 * Expects a vector of {text, color} pairs. Should be called from within a tooltip context.
	 */
	void DrawColoredMultiLineTooltip(const ColoredTextLines& lines);

	// Table sort function for string columns
	using TableSortFunc = std::function<bool(const std::string&, const std::string&, bool)>;
	using TableCellRenderFunc = std::function<void(int row, int col, const std::string& value)>;

	/**
	 * Renders a sortable ImGui table for string tables (vector<vector<string>>).
	 * Always sorts a copy if sorting is needed. Never modifies the input.
	 * @param table_id Unique ImGui table ID.
	 * @param headers Column headers.
	 * @param rows Table data, each row is a vector of strings.
	 * @param sortColumn Default sort column index.
	 * @param ascending Default sort direction.
	 * @param customSorts Vector of custom comparator functions, one per column (nullptr for default string sort).
	 *        Each function should compare two strings and return true if the first should come before the second.
	 * @param cellRender Optional cell renderer function for custom cell rendering. Signature: (row, col, value)
	 */
	void ShowSortedStringTableStrings(
		const char* table_id,
		const std::vector<std::string>& headers,
		const std::vector<std::vector<std::string>>& rows,
		size_t sortColumn = 0,
		bool ascending = true,
		const std::vector<TableSortFunc>& customSorts = {},
		TableCellRenderFunc cellRender = nullptr);

	/**
	 * Renders a sortable ImGui table for custom row types (vector<T>), sorts in-place.
	 * @tparam T The row type. Must be copyable and compatible with the provided cellRender and customSorts functions.
	 * @param table_id Unique ImGui table ID.
	 * @param headers Column headers.
	 * @param rows Table data, each row is of type T (will be sorted in-place).
	 * @param sortColumn Default sort column index.
	 * @param ascending Default sort direction.
	 * @param customSorts Vector of custom comparator functions, one per column.
	 *        Each function should compare two rows and return true if the first should come before the second.
	 * @param cellRender Function to render a cell: (rowIdx, colIdx, const T& row).
	 * @param footerRows Optional static footer rows (not sorted, rendered after main rows).
	 * @param outerSize Optional outer size for the table (0,0 = auto-size).
	 */
	template <typename T>
	void ShowSortedStringTableCustom(
		const char* table_id,
		const std::vector<std::string>& headers,
		std::vector<T>& rows,
		size_t sortColumn,
		bool ascending,
		const std::vector<std::function<bool(const T&, const T&, bool)>>& customSorts,
		std::function<void(int, int, const T&)> cellRender,
		const std::vector<T>& footerRows = {},
		const ImVec2& outerSize = ImVec2(0, 0))
	{
		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		ImVec2 tableSize = outerSize;
		if (outerSize.y == 0.0f) {
			size_t totalRows = rows.size() + footerRows.size();
			tableSize.y = ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>((totalRows < 15) ? totalRows : 15) + 1.2f);
		}
		if (ImGui::BeginTable(table_id, static_cast<int>(headers.size()), flags, tableSize)) {
			// Set up columns with content-based sizing
			for (size_t i = 0; i < headers.size(); ++i) {
				ImGui::TableSetupColumn(headers[i].c_str());
			}
			ImGui::TableHeadersRow();

			// Interactive sorting
			int sortCol = static_cast<int>(sortColumn);
			bool sortAsc = ascending;
			if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
				if (sortSpecs->SpecsCount > 0) {
					sortCol = sortSpecs->Specs->ColumnIndex;
					sortAsc = sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending;
				}
			}
			if (sortCol >= 0 && static_cast<size_t>(sortCol) < headers.size()) {
				if (sortCol < static_cast<int>(customSorts.size()) && customSorts[sortCol]) {
					auto cmp = customSorts[sortCol];
					std::sort(rows.begin(), rows.end(), [sortCol, sortAsc, &cmp](const T& a, const T& b) {
						return cmp(a, b, sortAsc);
					});
				}
			}

			// Render main (sorted) rows
			for (size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
				const auto& row = rows[rowIdx];
				ImGui::TableNextRow();
				for (size_t col = 0; col < headers.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					if (cellRender) {
						cellRender(static_cast<int>(rowIdx), static_cast<int>(col), row);
					}
				}
			}

			// Add separator between main rows and footer rows if there are footer rows
			if (!footerRows.empty() && !rows.empty()) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Separator();
			}

			// Render static footer rows (not sorted)
			for (size_t rowIdx = 0; rowIdx < footerRows.size(); ++rowIdx) {
				const auto& row = footerRows[rowIdx];
				ImGui::TableNextRow();
				for (size_t col = 0; col < headers.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					if (cellRender) {
						cellRender(static_cast<int>(rows.size() + rowIdx), static_cast<int>(col), row);
					}
				}
			}
			ImGui::EndTable();
		}
	}

	/**
	 * Renders a sortable and filterable ImGui table for custom row types.
	 * Extends ShowSortedStringTableCustom with filtering capabilities including
	 * substring highlighting and column-specific search.
	 * @tparam T The row type. Must be copyable and compatible with the provided functions.
	 * @param table_id Unique ImGui table ID.
	 * @param headers Column headers.
	 * @param originalRows Original table data (not modified - filtering creates a copy).
	 * @param sortColumn Default sort column index.
	 * @param ascending Default sort direction.
	 * @param customSorts Vector of custom comparator functions, one per column.
	 *        Each function should compare two rows and return true if the first should come before the second.
	 * @param cellRender Function to render a cell: (rowIdx, colIdx, const T& row, const std::string& filterText).
	 *        The filterText parameter enables substring highlighting in the cell renderer.
	 * @param filterText Reference to filter text string (modified by the input field).
	 * @param searchColumn Reference to search column index (0 = All Columns, 1+ = specific column).
	 * @param getFilterableFields Function that extracts filterable strings from a row for each column.
	 *        Should return a vector of strings, one per column, used for filtering.
	 * @param scrollOnFilterChange If true, scrolls to top when filter changes (default: true).
	 */
	template <typename T>
	void ShowFilteredStringTableCustom(
		const char* table_id,
		const std::vector<std::string>& headers,
		const std::vector<T>& originalRows,
		size_t sortColumn,
		bool ascending,
		const std::vector<std::function<bool(const T&, const T&, bool)>>& customSorts,
		std::function<void(int, int, const T&, const std::string&)> cellRender,
		std::string& filterText,
		int& searchColumn,
		std::function<std::vector<std::string>(const T&)> getFilterableFields,
		bool scrollOnFilterChange = true)
	{
		// Filter controls
		static std::string previousFilterText = "";
		char filterBuffer[256] = { 0 };
		strncpy_s(filterBuffer, filterText.c_str(), sizeof(filterBuffer) - 1);

		ImGui::InputText("Filter", filterBuffer, IM_ARRAYSIZE(filterBuffer));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Filter shaders by the selected column. Case-insensitive.");
		}

		// Create search column options
		std::vector<std::string> searchOptions = { "All Columns" };
		for (const auto& col : headers) {
			searchOptions.push_back(col);
		}
		std::vector<const char*> searchOptionsCStr;
		for (const auto& option : searchOptions) {
			searchOptionsCStr.push_back(option.c_str());
		}

		ImGui::Combo("Search In", &searchColumn, searchOptionsCStr.data(), static_cast<int>(searchOptionsCStr.size()));

		// Filter rows based on search column and filter text
		std::vector<T> filteredRows;
		std::string currentFilterText(filterBuffer);
		filterText = currentFilterText;  // Update the reference

		if (currentFilterText.empty()) {
			filteredRows = originalRows;
		} else {
			std::string filterLower = currentFilterText;
			std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

			for (const auto& row : originalRows) {
				bool passesFilter = false;
				auto filterableFields = getFilterableFields(row);

				if (searchColumn == 0) {  // All Columns
					for (const auto& field : filterableFields) {
						std::string fieldLower = field;
						std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), ::tolower);
						if (fieldLower.find(filterLower) != std::string::npos) {
							passesFilter = true;
							break;
						}
					}
				} else {  // Specific column (searchColumn is 1-indexed for columns)
					int columnIndex = searchColumn - 1;
					if (columnIndex >= 0 && static_cast<size_t>(columnIndex) < filterableFields.size()) {
						std::string fieldLower = filterableFields[columnIndex];
						std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), ::tolower);
						passesFilter = fieldLower.find(filterLower) != std::string::npos;
					}
				}

				if (passesFilter) {
					filteredRows.push_back(row);
				}
			}
		}

		// Handle filter change scrolling
		bool filterChanged = (currentFilterText != previousFilterText);
		if (filterChanged && scrollOnFilterChange) {
			ImGui::SetScrollHereY(0.5f);  // Keep the table visible when filter changes
			previousFilterText = currentFilterText;
		}

		// Constrain table height to prevent infinite scrolling appearance
		ImGui::BeginChild("ShaderTableContainer", ImVec2(0, 400), true, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 2));  // Tighter cell padding for better fit

		// Use the existing sorted table function
		ShowSortedStringTableCustom<T>(
			table_id,
			headers,
			filteredRows,
			sortColumn,
			ascending,
			customSorts,
			[&cellRender, &currentFilterText](int rowIdx, int colIdx, const T& row) {
				if (cellRender) {
					cellRender(rowIdx, colIdx, row, currentFilterText);
				}
			});

		ImGui::PopStyleVar();  // CellPadding
		ImGui::EndChild();
	}
	/**
	 * @brief Compares two version strings (e.g., "1.2.3") numerically.
	 * @param a First version string.
	 * @param b Second version string.
	 * @param ascending True for ascending, false for descending.
	 * @return True if a < b (or a > b if ascending is false).
	 */
	bool VersionStringLess(const std::string& a, const std::string& b, bool ascending = true);

	/**
	 * @brief TableSortFunc for version strings, using VersionStringLess.
	 */
	bool VersionSortComparator(const std::string& a, const std::string& b, bool ascending);

	// A standard string comparator for use with ShowSortedStringTable
	bool StringSortComparator(const std::string& a, const std::string& b, bool ascending);
	void RenderTextWithHighlights(const std::string& text, const std::string& searchTerm, ImVec4 highlightColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

	// Performance overlay formatting and color helpers
	ImVec4 GetThresholdColor(float value, float good, float warn, ImVec4 goodColor, ImVec4 warnColor, ImVec4 badColor);

	// Search functionality
	/**
	 * @brief Checks if a feature matches the search query.
	 * Searches both the feature's short name and display name.
	 * @param feat The feature to check
	 * @param searchQuery The search query string
	 * @return True if the feature matches the search query
	 */
	bool FeatureMatchesSearch(Feature* feat, const std::string& searchQuery);

	/**
	 * @brief Draws the feature search bar with magnifying glass icon.
	 * @param searchString Reference to the search string to modify
	 * @param availableWidth The available width for the search bar
	 */
	void DrawFeatureSearchBar(std::string& searchString, float availableWidth = 0.0f);

	/**
	 * Provides access to theme-aware UI colors for consistent styling.
	 * These functions return colors from the active theme's StatusPalette,
	 * ensuring consistency with the overall application theme.
	 */
	namespace Colors
	{
		/**
		 * Get theme-appropriate colors for timer/countdown displays.
		 * @return Theme colors: Good=SuccessColor, Warning=Warning, Critical=Error
		 */
		ImVec4 GetTimerGood();      // Green - good/safe status (from theme SuccessColor)
		ImVec4 GetTimerWarning();   // Orange - warning status (from theme Warning)
		ImVec4 GetTimerCritical();  // Red - critical/error status (from theme Error)

		/**
		 * Get standard theme UI colors for consistent theming.
		 * @return Theme colors from StatusPalette
		 */
		ImVec4 GetDefault();   // White - default text (from theme Text)
		ImVec4 GetSuccess();   // Green - success/positive (from theme SuccessColor)
		ImVec4 GetWarning();   // Orange - warning (from theme Warning)
		ImVec4 GetError();     // Red - error/negative (from theme Error)
		ImVec4 GetInfo();      // Blue - informational (from theme InfoColor)
		ImVec4 GetDisabled();  // Gray - disabled items (from theme Disable)
	}

	/**
	 * @brief Input handling utilities for ImGui integration
	 *
	 * This namespace provides input mapping functions for converting between different
	 * input systems (Windows Virtual Keys, DirectInput, ImGui) and generating
	 * human-readable key representations for UI display.
	 *
	 * These utilities were extracted from Menu.cpp to improve reusability and
	 * separation of concerns. They are designed to be stateless and thread-safe.
	 */
	namespace Input
	{
		/**
		 * @brief Converts Windows virtual key codes to ImGui key codes
		 *
		 * Translates Windows input events from the VK_* constant format to ImGui's
		 * ImGuiKey enum format for proper input handling in ImGui interfaces.
		 * Supports the full range of keyboard keys, function keys, numpad, and
		 * special keys (modifiers, navigation, etc.).
		 *
		 * @param vkKey Windows virtual key code (VK_* constants from winuser.h)
		 * @return Corresponding ImGuiKey value, or ImGuiKey_None if unmapped
		 *
		 * @note This function handles all standard keyboard keys including:
		 *       - Alphanumeric keys (A-Z, 0-9)
		 *       - Function keys (F1-F12)
		 *       - Modifier keys (Shift, Ctrl, Alt, Windows)
		 *       - Navigation keys (arrows, page up/down, home/end)
		 *       - Numpad keys and operations
		 *       - Special OEM keys (punctuation, brackets, etc.)
		 *
		 * @example
		 * @code
		 * ImGuiKey key = Util::Input::VirtualKeyToImGuiKey(VK_SPACE);
		 * if (key != ImGuiKey_None) {
		 *     ImGui::GetIO().AddKeyEvent(key, true);
		 * }
		 * @endcode
		 */
		ImGuiKey VirtualKeyToImGuiKey(WPARAM vkKey);

		/**
		 * @brief Converts DirectInput key codes to Windows virtual key codes
		 *
		 * Translates DirectInput device key codes (DIK_* constants) to standard
		 * Windows virtual key codes (VK_* constants). This is particularly useful
		 * for handling input from DirectInput devices and normalizing them to
		 * the Windows input system.
		 *
		 * @param dikKey DirectInput key code (DIK_* constants from dinput.h)
		 * @return Corresponding Windows virtual key code, or original dikKey if unmapped
		 *
		 * @note This function handles common DirectInput keys including:
		 *       - Arrow keys and navigation
		 *       - Numpad keys and operations
		 *       - Modifier keys (Alt, Ctrl, Windows)
		 *       - Special keys (Delete, Insert, Home, End, Page Up/Down)
		 *
		 * @note For unmapped keys, the function returns the original dikKey value
		 *       as a fallback, allowing for pass-through behavior.
		 *
		 * @example
		 * @code
		 * uint32_t vkKey = Util::Input::DIKToVK(DIK_LEFTARROW);
		 * // vkKey will be VK_LEFT
		 * @endcode
		 */
		uint32_t DIKToVK(uint32_t dikKey);

		/**
		 * @brief Converts key codes to human-readable string representations
		 *
		 * Provides localized, user-friendly key names for display in UI elements
		 * such as settings panels, tooltips, and configuration dialogs. The strings
		 * are suitable for direct display to users.
		 *
		 * @param key Virtual key code to convert (0-255 range)
		 * @return Human-readable key name string, or empty string if key >= 256
		 *
		 * @note Key names include proper formatting and descriptions:
		 *       - "Left Mouse", "Right Mouse", "Middle Mouse" for mouse buttons
		 *       - "Numpad 0", "Numpad +", "Numpad Enter" for numpad keys
		 *       - "Left Shift", "Right Ctrl" for specific modifier keys
		 *       - "Page Up", "Page Down" instead of "Prior", "Next"
		 *       - "Left Arrow", "Up Arrow" for navigation keys
		 *
		 * @note Returns empty string for invalid key codes (>= 256) to prevent
		 *       buffer overrun and provide safe fallback behavior.
		 *
		 * @example
		 * @code
		 * const char* keyName = Util::Input::KeyIdToString(VK_SPACE);
		 * // keyName will be "Space"
		 *
		 * const char* mouseName = Util::Input::KeyIdToString(VK_LBUTTON);
		 * // mouseName will be "Left Mouse"
		 * @endcode
		 */
		const char* KeyIdToString(uint32_t key);
	}

	/**
	 * @brief Renders a table cell with automatic text highlighting and optional tooltip/fallback.
	 * Convenience function for table cell renderers that combines text rendering with highlighting,
	 * tooltips, and fallback text for empty content.
	 * @param text The text to render in the cell (if empty, uses fallbackText)
	 * @param filterText The search filter text for highlighting
	 * @param tooltipText Optional tooltip text (if provided, shows on hover)
	 * @param fallbackText Text to show if primary text is empty (default: "--")
	 * @param highlightColor Color for highlighting (default: yellow)
	 * @param enableWrapping Whether to enable text wrapping for multi-line content (default: true)
	 * @param textColor Optional text color override (default: use default text color)
	 */
	inline void RenderTableCell(const std::string& text, const std::string& filterText,
		const std::string& tooltipText = "", const char* fallbackText = nullptr,
		ImVec4 highlightColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f), bool enableWrapping = true,
		ImVec4 textColor = ImVec4(0, 0, 0, 0))
	{
		const std::string& displayText = text.empty() && fallbackText ? std::string(fallbackText) : text;

		// Apply custom text color if provided
		if (textColor.w > 0.0f) {
			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		}

		// Enable text wrapping for the cell content
		if (enableWrapping) {
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		}

		RenderTextWithHighlights(displayText, filterText, highlightColor);

		if (enableWrapping) {
			ImGui::PopTextWrapPos();
		}

		if (!tooltipText.empty() && ImGui::IsItemHovered()) {
			if (auto _tt = HoverTooltipWrapper()) {
				ImGui::Text("%s", tooltipText.c_str());
			}
		}

		// Pop text color if we pushed one
		if (textColor.w > 0.0f) {
			ImGui::PopStyleColor();
		}
	}

	/**
	 * @brief Configuration for a table column (text-only, click handling is row-level)
	 */
	template <typename T>
	struct TableColumnConfig
	{
		std::string header;
		std::string tooltip;
		std::function<std::string(const T&)> getValue;
	};

	/**
	 * @brief Represents different types of input events that can occur on table rows
	 */
	enum class TableInputEventType
	{
		MouseClick,
		MouseDoubleClick,
		KeyPress,
		ContextMenu
	};

	/**
	 * @brief Configuration for a specific input event handler
	 * @tparam T The row type
	 */
	template <typename T>
	struct TableInputEvent
	{
		TableInputEventType type;
		int mouseButton = 0;                     // For mouse events (0=left, 1=right, 2=middle)
		ImGuiKey key = ImGuiKey_None;            // For keyboard events
		std::string label;                       // Display label for context menus
		std::function<void(const T&)> callback;  // Action to perform
		bool enabled = true;                     // Whether this event is currently enabled

		TableInputEvent(TableInputEventType t, std::function<void(const T&)> cb,
			const std::string& lbl = "", int btn = 0, ImGuiKey k = ImGuiKey_None) :
			type(t), mouseButton(btn), key(k), label(lbl), callback(cb) {}
	};

	/**
	 * @brief Manages the state and logic for table filtering
	 * @tparam T The row type
	 */
	template <typename T>
	struct TableFilterState
	{
		std::string filterText;
		int searchColumn = 0;  // 0 = All Columns, 1+ = specific column
		std::function<std::vector<std::string>(const T&)> getFilterableFields;

		TableFilterState(std::function<std::vector<std::string>(const T&)> fieldsFunc) :
			getFilterableFields(fieldsFunc) {}

		/**
		 * @brief Apply filtering to the original rows and return filtered results
		 */
		std::vector<T> ApplyFilter(const std::vector<T>& originalRows) const
		{
			if (filterText.empty()) {
				return originalRows;
			}

			std::vector<T> filteredRows;
			std::string filterLower = filterText;
			std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

			for (const auto& row : originalRows) {
				bool passesFilter = false;
				auto filterableFields = getFilterableFields(row);

				if (searchColumn == 0) {  // All Columns
					for (const auto& field : filterableFields) {
						std::string fieldLower = field;
						std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), ::tolower);
						if (fieldLower.find(filterLower) != std::string::npos) {
							passesFilter = true;
							break;
						}
					}
				} else {  // Specific column (searchColumn is 1-indexed for columns)
					int columnIndex = searchColumn - 1;
					if (columnIndex >= 0 && static_cast<size_t>(columnIndex) < filterableFields.size()) {
						std::string fieldLower = filterableFields[columnIndex];
						std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), ::tolower);
						passesFilter = fieldLower.find(filterLower) != std::string::npos;
					}
				}

				if (passesFilter) {
					filteredRows.push_back(row);
				}
			}

			return filteredRows;
		}

		/**
		 * @brief Render the filter UI controls
		 */
		void RenderControls(const std::vector<std::string>& columnHeaders)
		{
			char filterBuffer[256] = { 0 };
			strncpy_s(filterBuffer, filterText.c_str(), sizeof(filterBuffer) - 1);

			ImGui::InputText("Filter", filterBuffer, IM_ARRAYSIZE(filterBuffer));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Filter table by the selected column. Case-insensitive.");
			}

			// Create search column options
			std::vector<std::string> searchOptions = { "All Columns" };
			for (const auto& col : columnHeaders) {
				searchOptions.push_back(col);
			}
			std::vector<const char*> searchOptionsCStr;
			for (const auto& option : searchOptions) {
				searchOptionsCStr.push_back(option.c_str());
			}

			ImGui::Combo("Search In", &searchColumn, searchOptionsCStr.data(), static_cast<int>(searchOptionsCStr.size()));

			// Update filter text from buffer
			filterText = filterBuffer;
		}
	};

	/**
	 * @brief Enhanced filtered table with general input event support and theme integration
	 * @tparam T The row type
	 * @param table_id Unique ImGui table ID
	 * @param columns Column configurations (text-only, click handling is row-level)
	 * @param originalRows Original table data (not modified - filtering creates a copy)
	 * @param sortColumn Default sort column index
	 * @param ascending Default sort direction
	 * @param customSorts Vector of custom comparator functions, one per column
	 * @param filterState Filter state management
	 * @param inputEvents Vector of input event handlers for row interactions
	 * @param getRowTooltip Optional function to get tooltip for entire row
	 * @param getRowBgColor Optional function to get background color for row (for highlighting blocked/disabled items)
	 * @param getRowTextColor Optional function to get text color for row (for highlighting blocked/disabled items)
	 * @param tableHeight Maximum height for the table container (0 = auto)
	 */
	template <typename T>
	void ShowInteractiveTable(
		const char* table_id,
		const std::vector<TableColumnConfig<T>>& columns,
		const std::vector<T>& originalRows,
		size_t sortColumn,
		bool ascending,
		const std::vector<std::function<bool(const T&, const T&, bool)>>& customSorts,
		TableFilterState<T>& filterState,
		const std::vector<TableInputEvent<T>>& inputEvents = {},
		std::function<std::string(const T&)> getRowTooltip = nullptr,
		std::function<ImVec4(const T&)> getRowBgColor = nullptr,
		std::function<ImVec4(const T&)> getRowTextColor = nullptr,
		float tableHeight = 400.0f)
	{
		// Render filter controls
		filterState.RenderControls([&]() {
			std::vector<std::string> headers;
			for (const auto& col : columns) {
				headers.push_back(col.header);
			}
			return headers;
		}());

		// Apply filtering
		auto filteredRows = filterState.ApplyFilter(originalRows);

		// Handle filter change scrolling
		static std::string previousFilterText = "";
		bool filterChanged = (filterState.filterText != previousFilterText);
		if (filterChanged) {
			ImGui::SetScrollHereY(0.5f);
			previousFilterText = filterState.filterText;
		}

		// Constrain table height to prevent infinite scrolling appearance
		std::string containerId = std::string(table_id) + "_Container";
		ImGui::BeginChild(containerId.c_str(), ImVec2(0, tableHeight), true, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 2));

		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable(table_id, static_cast<int>(columns.size()), flags)) {
			// Set up columns
			for (size_t i = 0; i < columns.size(); ++i) {
				ImGui::TableSetupColumn(columns[i].header.c_str());
			}
			ImGui::TableHeadersRow();

			// Interactive sorting
			int sortCol = static_cast<int>(sortColumn);
			bool sortAsc = ascending;
			if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
				if (sortSpecs->SpecsCount > 0) {
					sortCol = sortSpecs->Specs->ColumnIndex;
					sortAsc = sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending;
				}
			}
			if (sortCol >= 0 && static_cast<size_t>(sortCol) < columns.size()) {
				if (sortCol < static_cast<int>(customSorts.size()) && customSorts[sortCol]) {
					auto cmp = customSorts[sortCol];
					std::sort(filteredRows.begin(), filteredRows.end(), [sortCol, sortAsc, &cmp](const T& a, const T& b) {
						return cmp(a, b, sortAsc);
					});
				}
			}

			// Render rows with input event support
			for (size_t rowIdx = 0; rowIdx < filteredRows.size(); ++rowIdx) {
				const auto& row = filteredRows[rowIdx];
				ImGui::TableNextRow();

				// Set custom row background color if provided (for blocked/disabled items)
				if (getRowBgColor) {
					ImVec4 bgColor = getRowBgColor(row);
					if (bgColor.w > 0.0f) {  // Only set if color has alpha > 0
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(bgColor));
					}
				}

				// Render all columns first to establish proper row layout
				for (size_t col = 0; col < columns.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					const auto& column = columns[col];

					// All columns are now text-only with highlighting
					std::string value = column.getValue(row);
					ImVec4 textColor = getRowTextColor ? getRowTextColor(row) : ImVec4(0, 0, 0, 0);
					Util::RenderTableCell(value, filterState.filterText, column.tooltip, nullptr, ImVec4(1.0f, 1.0f, 0.0f, 1.0f), true, textColor);
				}

				// Now create the invisible button that covers the entire rendered row
				// Get the position after all cells are rendered
				ImVec2 rowMin = ImGui::GetItemRectMin();
				ImVec2 rowMax = ImGui::GetItemRectMax();

				// Find the actual row boundaries by checking all columns
				float minY = FLT_MAX;
				float maxY = -FLT_MAX;
				float minX = FLT_MAX;
				float maxX = -FLT_MAX;

				for (size_t col = 0; col < columns.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					ImVec2 cellMin = ImGui::GetItemRectMin();
					ImVec2 cellMax = ImGui::GetItemRectMax();

					minX = std::min(minX, cellMin.x);
					maxX = std::max(maxX, cellMax.x);
					minY = std::min(minY, cellMin.y);
					maxY = std::max(maxY, cellMax.y);
				}

				ImVec2 rowStartPos = ImVec2(minX, minY);
				ImVec2 rowSize = ImVec2(maxX - minX, maxY - minY);

				// Position the button absolutely over the rendered row
				ImGui::SetCursorScreenPos(rowStartPos);
				ImGui::PushID(static_cast<int>(rowIdx));

				std::string buttonId = "##row_" + std::to_string(rowIdx);
				ImGui::InvisibleButton(buttonId.c_str(), rowSize);

				// Handle input events on the invisible button
				for (const auto& event : inputEvents) {
					if (!event.enabled)
						continue;

					bool shouldTrigger = false;
					switch (event.type) {
					case TableInputEventType::MouseClick:
						shouldTrigger = ImGui::IsItemClicked() && event.mouseButton == 0;  // Left click
						break;
					case TableInputEventType::MouseDoubleClick:
						shouldTrigger = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(event.mouseButton);
						break;
					case TableInputEventType::KeyPress:
						shouldTrigger = ImGui::IsItemFocused() && ImGui::IsKeyPressed(event.key);
						break;
					case TableInputEventType::ContextMenu:
						if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(event.mouseButton)) {
							std::string popupId = "row_context_" + std::to_string(rowIdx);
							ImGui::OpenPopup(popupId.c_str());
						}
						break;
					}

					if (shouldTrigger && event.callback) {
						event.callback(row);
					}
				}

				// Render context menus
				for (const auto& event : inputEvents) {
					if (event.type == TableInputEventType::ContextMenu) {
						std::string popupId = "row_context_" + std::to_string(rowIdx);
						if (ImGui::BeginPopup(popupId.c_str())) {
							if (ImGui::MenuItem(event.label.c_str()) && event.callback) {
								event.callback(row);
							}
							ImGui::EndPopup();
						}
					}
				}

				// Row tooltip
				if (getRowTooltip && ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						std::string tooltip = getRowTooltip(row);
						ImGui::Text("%s", tooltip.c_str());
					}
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::PopStyleVar();
		ImGui::EndChild();
	}
}  // namespace Util
