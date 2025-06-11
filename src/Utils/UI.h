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
