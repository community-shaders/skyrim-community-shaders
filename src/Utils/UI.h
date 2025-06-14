#pragma once

namespace Util
{

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
}  // namespace Util
