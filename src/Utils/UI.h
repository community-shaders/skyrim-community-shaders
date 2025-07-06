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

	class PerformanceOverlay
	{
	public:
		static float CalcFrameTime(uint64_t timeElapsed, uint64_t frequency)
		{
			return 1000.0f * (float)timeElapsed / (float)frequency;
		}

		static float CalcFPS(float frameTimeMs)
		{
			return 1000.0f / frameTimeMs;
		}
	};
	extern PerformanceOverlay performanceOverlay;

	/**
	 * @brief Draws a multi-line tooltip with optional per-line coloring.
	 * @param lines The lines of text to display in the tooltip (as std::vector<std::string>).
	 * @param colors Optional per-line colors (if empty, default color is used for all lines).
	 */
	void DrawMultiLineTooltip(const std::vector<std::string>& lines, const std::vector<ImVec4>& colors = {});

	/**
	 * @brief Comparator function type for table sorting.
	 *
	 * Should return true if the first value should come before the second, given the sort direction.
	 * @param a First string value (cell content).
	 * @param b Second string value (cell content).
	 * @param ascending True if sorting ascending, false for descending.
	 * @return True if a should come before b, false otherwise.
	 */
	using TableSortFunc = std::function<bool(const std::string&, const std::string&, bool)>;

	/**
	 * @brief Sorts table rows by the specified column using a default string comparison.
	 * @param rows The table data (vector of rows).
	 * @param column The column index to sort by.
	 * @param ascending True for ascending, false for descending.
	 */
	void SortTableRowsByColumn(std::vector<std::vector<std::string>>& rows, size_t column, bool ascending = true);

	/**
	 * @brief Renders a sortable ImGui table with arbitrary columns and per-column custom sorting.
	 *
	 * @param table_id Unique ImGui table ID.
	 * @param headers Column headers.
	 * @param rows Table data, each row is a vector of strings.
	 * @param sortColumn Default sort column index.
	 * @param ascending Default sort direction.
	 * @param customSorts Vector of custom comparator functions, one per column (nullptr for default string sort).
	 */
	void ShowSortedStringTable(
		const char* table_id,
		const std::vector<std::string>& headers,
		std::vector<std::vector<std::string>> rows,
		size_t sortColumn = 0,
		bool ascending = true,
		const std::vector<TableSortFunc>& customSorts = {});

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
	extern const TableSortFunc VersionSortComparator;
}  // namespace Util
