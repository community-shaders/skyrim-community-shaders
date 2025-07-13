#pragma once

#include "../Utils/PerfUtils.h"
#include "OverlayFeature.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <variant>

// Forward declarations
struct DrawCallRow;
class ABTestAggregator;

// Special shader type enum for summary rows
enum class SpecialShaderType
{
	Total = -1,
	Other = -2
};

struct DrawCallRow
{
	std::string label;
	int shaderType;  // Use int for consistency with the rest of the codebase
	int drawCalls;
	float frameTime;
	float percent;
	float costPerCall;
	std::string tooltip;
	bool enabled;
	std::optional<float> testFrameTime;
	std::optional<float> testCostPerCall;
};

struct ShaderRow
{
	std::string label;
	int type;
	std::string tooltip;
};

enum DrawCallTableColumn
{
	Col_Label,
	Col_DrawCalls,
	Col_FrameTime,
	Col_Percent,
	Col_CostPerCall,
	Col_TestFrameTime,
	Col_TestCostPerCall,
	Col_Count
};

struct PerformanceOverlay : OverlayFeature
{
	static PerformanceOverlay* GetSingleton()
	{
		static PerformanceOverlay singleton;
		return &singleton;
	}

	// Virtual overrides in Feature.h order
	std::string GetName() override { return "Performance Overlay"; }
	std::string GetShortName() override { return "PerformanceOverlay"; }

	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }
	virtual bool IsInMenu() const override { return true; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	virtual void DrawSettings() override;

	virtual void DataLoaded() override;

	// Core performance display functions
	void DrawFPS();
	void DrawVRAM();

	// Private helper for table rendering
	void DrawDrawCallsTable(const std::vector<DrawCallRow>& mainRows, const std::vector<DrawCallRow>& summaryRows);

	// Private helper for A/B testing section
	void DrawABTestSection(const std::vector<DrawCallRow>& allRows, bool showCollapsibleSections);
	void DrawABTestResultsTable();

	// Test data management
	static void UpdateShaderTestData(int shaderType, float frameTime, float costPerCall);
	static std::string GetTestDataTooltip();

public:
	static void UpdateAllShaderTestData();

	// A/B Test aggregator access
	static ABTestAggregator& GetABTestAggregator();

	// Test data management helpers
	static void UpdateShaderTestDataEntry(int shaderType, float frameTime, float costPerCall, float percent = 0.0f);
	static void UpdateSummaryTestData(float smoothedFrameTime, float otherFrameTime, float otherPercent, float totalCostPerCall);

	/**
	 * @brief Builds the main and summary rows for the performance overlay table.
	 *
	 * @return A pair of vectors: (mainRows, summaryRows).
	 *         - mainRows: One row per shader type, with live and (if present) test data.
	 *         - summaryRows: 'Other' and 'Total' rows, with live and (if present) test data.
	 */
	std::pair<std::vector<DrawCallRow>, std::vector<DrawCallRow>> BuildDrawCallRows() const;

	// Performance overlay state management
	class PerfOverlayState
	{
	public:
		std::vector<float> frameTimeHistory;
		std::vector<float> postFGFrameTimeHistory;
		bool initialized = false;
		bool hasGraphs = false;
		int frameTimeHistoryIndex = 0;
		int postFGFrameTimeHistoryIndex = 0;
		bool isFrameGenerationActive = false;
		int64_t frequency;
		int64_t lastFrameCounter;
		int64_t currentFrameCounter;
		float frameTimeMs = 0.0f;
		float fps = 0.0f;
		float postFGFrameTimeMs = 0.0f;
		float postFGFps = 0.0f;
		float smoothFps = 0.0f;
		float smoothFrameTimeMs = 0.0f;
		float postFGSmoothFps = 0.0f;
		float postFGSmoothFrameTimeMs = 0.0f;
		float updateTimer = 0.0f;
		float minFrameTime = 1000.0f;
		float maxFrameTime = 0.0f;
		float smoothedMinFrameTime = 0.0f;
		float smoothedMaxFrameTime = 50.0f;
		float textScale = 1.0f;
		static constexpr float kSmoothingFactor = 0.15f;  // Smoothing factor: 0.1f = slow, 0.3f = fast.
		std::chrono::steady_clock::time_point lastUpdateTime;

		// Performance threshold constants
		static constexpr float kFrameTimeGoodThreshold = 2.0f;       // ms - Good performance threshold
		static constexpr float kFrameTimeWarningThreshold = 5.0f;    // ms - Warning performance threshold
		static constexpr float kCostPerCallGoodThreshold = 0.05f;    // ms/call - Good cost per call threshold
		static constexpr float kCostPerCallWarningThreshold = 0.2f;  // ms/call - Warning cost per call threshold
		static constexpr float kMicrosecondThreshold = 0.01f;        // ms - Threshold for showing microseconds
		static constexpr float kPercentDisplayThreshold = 0.01f;     // Minimum percent difference to display
		static constexpr float kGraphSpreadMultiplier = 2.0f;        // Standard deviation multiplier for graph range
		static constexpr float kGraphMinSpread = 2.0f;               // ms - Minimum graph spread
		static constexpr float kGraphMaxSpread = 20.0f;              // ms - Maximum graph spread
		static constexpr float kFrameGenerationMultiplier = 2.0f;    // Frame generation doubles frame rate
		static constexpr float kMaxUpdateInterval = 2.0f;            // seconds - Maximum update interval
		static constexpr float kDefaultWindowPadding = 10.0f;        // pixels - Default window padding
		static constexpr float kLabelPadding = 100.0f;               // pixels - Padding for labels
		static constexpr float kDrawCallsTableWidth = 600.0f;        // pixels - Draw calls table width
		static constexpr float kVRAMSectionWidth = 300.0f;           // pixels - VRAM section width
		static constexpr float kWindowBorderPadding = 20.0f;         // pixels - Window border padding

		float SetTextScale();
		/**
		* @brief Updates all runtime state related to the performance overlay graph.
		*
		* This function synchronizes the frame time history buffer, tracks min/max frame times,
		* and computes the normalized Y-axis range for the frame time graph using statistical analysis.
		*
		* Steps performed:
		*   1. Resizes the frameTimeHistory buffer if the user has changed the setting.
		*   2. Inserts the latest frame time into the circular history buffer.
		*   3. Updates instantaneous min/max frame time values, with full rescans if necessary.
		*   4. Calculates the average (mean) and standard deviation of frame times in the buffer.
		*   5. Sets the graph Y-axis range to be centered on the average, with a spread of ±2 standard deviations,
		*      clamped to user-friendly minimum and maximum values.
		*   6. Smooths the min/max Y-axis values for visual stability using exponential smoothing.
		*
		*
		* No parameters; uses settings from the singleton.
		*/
		void UpdateGraphValues();
		void UpdateFrameTimeHistorySizes();
		void UpdateMinFrameTime();
		void UpdateMaxFrameTime();
		void UpdateFGFrameTime();
		void DrawPostFGFrameTimeGraph();
	};

	PerfOverlayState perfOverlayState;

	// Implement OverlayFeature interface
	void DrawOverlay() override;
	bool IsOverlayVisible() const override { return settings.ShowInOverlay; }

	// Settings structure
	struct PerfOverlaySettings
	{
		bool ShowInOverlay = true;  // was: Enabled
		bool ShowDrawCalls = true;
		bool ShowVRAM = true;
		bool ShowFPS = true;
		bool ShowPreFGFrameTimeGraph = true;
		bool ShowPostFGFrameTimeGraph = true;
		float UpdateInterval = 0.5f;
		int FrameHistorySize = 120;                       // Default 120 frames = 2s @ 60fps. Clamped using static values to prevent config file values going outside of slider bounds.
		static constexpr int kMinFrameHistorySize = 60;   // 60 frames = 1s @ 60fps. Reasonable minimum.
		static constexpr int kMaxFrameHistorySize = 480;  // 480 frames = 10s @ 60fps or 2s @ 240fps. Reasonable maximum.
		enum class TextSize
		{
			Small,
			Medium,
			Large
		};
		TextSize Size = TextSize::Medium;

		float BackgroundOpacity = 0.5f;
		bool ShowBorder = true;
		ImVec2 Position = ImVec2(10.f, 10.f);
		bool PositionSet = false;
	};

public:
	PerfOverlaySettings settings;

private:
	/**
	 * @brief Captures test data for the performance overlay table.
	 *
	 * This function is responsible for updating the static test data used for A/B comparison and manual shader toggling.
	 *
	 * - In A/B Test Mode (Variant B): If ABTestingManager is enabled and using test config, this function continuously captures
	 *   test data for all shader types, as well as the 'Other' and 'Total' summary rows, every frame.
	 * - In Manual Shader Toggle mode: If any shader is disabled, this function captures test data for the disabled
	 *   shaders and summary rows at the moment of disabling, and keeps it until cleared.
	 * - Test data is NOT cleared when shaders are re-enabled; it is only cleared by the 'Clear Test Data' button
	 *   or if all shaders are disabled (rare edge case).
	 *
	 * Side effects:
	 * - Updates s_testData, s_testDataSource, and s_testDataLastUpdated.
	 */
	void CaptureTestData();

	/**
	 * @brief Clears all captured test data and resets the test data source.
	 *
	 * This should be called when the user clicks the 'Clear Test Data' button,
	 * or in rare cases where all shaders are disabled.
	 *
	 * Side effects:
	 * - Empties s_testData and resets s_testDataSource.
	 */
	void ClearTestData();

	struct TestData
	{
		float frameTime;
		float costPerCall;
		float percent;
	};

	enum class TestDataSource
	{
		None,
		ABTest_VariantB,
		ManualShaderToggle
	};

	static TestDataSource s_testDataSource;
	static std::chrono::steady_clock::time_point s_testDataLastUpdated;
	static std::unordered_map<int, TestData> s_testData;
};