#pragma once
#include <algorithm>
#include <functional>
#include <imgui.h>
#include <string>
#include <vector>

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
		const std::vector<T>& footerRows = {})
	{
		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable;
		if (ImGui::BeginTable(table_id, static_cast<int>(headers.size()), flags)) {
			for (const auto& header : headers)
				ImGui::TableSetupColumn(header.c_str());
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
}  // namespace Util
