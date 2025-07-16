#pragma once

#include "Menu.h"
#include "OverlayFeature.h"
#include "PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Utils/PerfUtils.h"
#include <Windows.h>
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <variant>

// Forward declarations
struct DrawCallRow;

// Special shader type enum for summary rows
enum class SpecialShaderType
{
	Total = -1,
	Other = -2
};

// Constants for special draw call values
static constexpr int kDrawCallsNotApplicable = -1;  // Special value to indicate draw calls are not applicable

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

// Legend and configuration structures
struct ColumnLegend
{
	std::string header;
	Util::ColoredTextLines tooltip;
};

struct ABTestLegends
{
	ColumnLegend shaderType;
	ColumnLegend aAvg;
	ColumnLegend bAvg;
	ColumnLegend delta;
	ColumnLegend aMedian;
	ColumnLegend bMedian;
	ColumnLegend medianDelta;
};

struct DrawCallLegends
{
	ColumnLegend shaderType;
	ColumnLegend drawCalls;
	ColumnLegend frameTime;
	ColumnLegend costPerCall;
	ColumnLegend testFrameTime;
	ColumnLegend testCostPerCall;
};

struct ColumnConfig
{
	std::string header;
	std::function<void(const DrawCallRow&, int colIdx)> cellRender;
	std::function<bool(const DrawCallRow&, const DrawCallRow&, bool)> sortFunc;
	std::function<void()> headerTooltip;
};

struct PerformanceOverlay : OverlayFeature
{
	static PerformanceOverlay* GetSingleton()
	{
		static PerformanceOverlay singleton;
		return &singleton;
	}

	// ============================================================================
	// VIRTUAL OVERRIDES (Feature.h interface)
	// ============================================================================
	std::string GetName() override { return "Performance Overlay"; }
	std::string GetShortName() override { return "PerformanceOverlay"; }
	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }
	virtual bool IsInMenu() const override { return true; }
	bool IsOverlayVisible() const override { return settings.ShowInOverlay; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	virtual void DrawSettings() override;
	virtual void DataLoaded() override;
	void DrawOverlay() override;

	// ============================================================================
	// CORE PERFORMANCE DISPLAY FUNCTIONS
	// ============================================================================
	void DrawFPS();
	void DrawVRAM();

	// ============================================================================
	// A/B TESTING FUNCTIONS
	// ============================================================================
	void DrawABTestSection(const std::vector<DrawCallRow>& allRows, bool showCollapsibleSections);
	void DrawABTestResultsTable();
	void DrawABTestStatisticalValidity(const Menu::ThemeSettings& theme, const ABTestAggregator& aggregator) const;
	void ConvertABTestResultsToRows(const std::vector<AggregatedDrawCallStats>& results, std::vector<DrawCallRow>& mainRows, std::vector<DrawCallRow>& summaryRows) const;
	ABTestLegends BuildABTestLegends(const Menu::ThemeSettings& theme) const;
	std::vector<ColumnConfig> BuildABTestResultsTableColumns(const Menu::ThemeSettings& theme, const ABTestLegends& legends) const;
	static ABTestAggregator& GetABTestAggregator();

	// ============================================================================
	// TABLE BUILDING AND RENDERING FUNCTIONS
	// ============================================================================
	void DrawDrawCallsTable(const std::vector<DrawCallRow>& mainRows, const std::vector<DrawCallRow>& summaryRows);
	DrawCallLegends BuildDrawCallLegends(const Menu::ThemeSettings& theme, bool anyTestData, PerformanceOverlay* overlay) const;
	std::vector<ColumnConfig> BuildDrawCallTableColumns(const Menu::ThemeSettings& theme, const DrawCallLegends& legends, bool anyTestData, PerformanceOverlay* overlay);
	std::pair<std::vector<DrawCallRow>, std::vector<DrawCallRow>> BuildDrawCallRows() const;
	std::function<void(int, int, const DrawCallRow&)> CreateTableRowHandler(const std::vector<ColumnConfig>& columns, PerformanceOverlay* overlay);

	// ============================================================================
	// EVENT HANDLING FUNCTIONS
	// ============================================================================
	void HandleShaderToggle(const DrawCallRow& row, bool wasEnabled);
	void HandleTotalRowToggle();

	// ============================================================================
	// TEST DATA MANAGEMENT FUNCTIONS
	// ============================================================================
	void UpdateShaderTestData(int shaderType, float frameTime, float costPerCall);
	void UpdateAllShaderTestData();
	void UpdateShaderTestDataEntry(int shaderType, float frameTime, float costPerCall, float percent = 0.0f);
	void UpdateSummaryTestData(float smoothedFrameTime, float otherFrameTime, float otherPercent, float totalCostPerCall);
	std::string GetTestDataTooltip();

	// ============================================================================
	// PERFORMANCE OVERLAY STATE MANAGEMENT
	// ============================================================================
	class PerfOverlayState
	{
	private:
		// Frame time history buffers
		std::vector<float> frameTimeHistory;
		std::vector<float> postFGFrameTimeHistory;

		// State flags
		bool initialized = false;
		bool hasGraphs = false;
		bool isFrameGenerationActive = false;

		// History indices
		int frameTimeHistoryIndex = 0;
		int postFGFrameTimeHistoryIndex = 0;

		// Performance counters
		int64_t frequency;
		int64_t lastFrameCounter;
		int64_t currentFrameCounter;

		// Current frame metrics
		float frameTimeMs = 0.0f;
		float fps = 0.0f;
		float postFGFrameTimeMs = 0.0f;
		float postFGFps = 0.0f;

		// Smoothed metrics
		float smoothFps = 0.0f;
		float smoothFrameTimeMs = 0.0f;
		float postFGSmoothFps = 0.0f;
		float postFGSmoothFrameTimeMs = 0.0f;

		// Update timing using QueryPerformanceCounter
		float updateTimer = 0.0f;
		LARGE_INTEGER overlayTimingFrequency = {0};
		LARGE_INTEGER lastUpdateTime = {0};

		// Min/max tracking
		float minFrameTime = 1000.0f;
		float maxFrameTime = 0.0f;
		float smoothedMinFrameTime = 0.0f;
		float smoothedMaxFrameTime = 50.0f;

		// Display settings
		float textScale = 1.0f;

	public:
		// Performance threshold constants
		static constexpr float kSmoothingFactor = 0.15f;             // Smoothing factor: 0.1f = slow, 0.3f = fast.
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
		static constexpr float kDefaultFrameTimeMs = 16.67f;         // ms - Default frame time (60 FPS)

		// Getters for read-only access
		bool IsInitialized() const { return initialized; }
		bool HasGraphs() const { return hasGraphs; }
		bool IsFrameGenerationActive() const { return isFrameGenerationActive; }

		float GetFrameTimeMs() const { return frameTimeMs; }
		float GetFps() const { return fps; }
		float GetPostFGFrameTimeMs() const { return postFGFrameTimeMs; }
		float GetPostFGFps() const { return postFGFps; }
		float GetSmoothFps() const { return smoothFps; }
		float GetSmoothFrameTimeMs() const { return smoothFrameTimeMs; }
		float GetPostFGSmoothFps() const { return postFGSmoothFps; }
		float GetPostFGSmoothFrameTimeMs() const { return postFGSmoothFrameTimeMs; }
		float GetUpdateTimer() const { return updateTimer; }
		float GetMinFrameTime() const { return minFrameTime; }
		float GetMaxFrameTime() const { return maxFrameTime; }
		float GetSmoothedMinFrameTime() const { return smoothedMinFrameTime; }
		float GetSmoothedMaxFrameTime() const { return smoothedMaxFrameTime; }
		float GetTextScale() const { return textScale; }

		int64_t GetFrequency() const { return frequency; }
		int64_t GetLastFrameCounter() const { return lastFrameCounter; }
		int64_t GetCurrentFrameCounter() const { return currentFrameCounter; }
		int GetFrameTimeHistoryIndex() const { return frameTimeHistoryIndex; }
		int GetPostFGFrameTimeHistoryIndex() const { return postFGFrameTimeHistoryIndex; }

		// Non-const getters for use with system functions that require pointers
		int64_t& GetFrequencyRef() { return frequency; }
		int64_t& GetLastFrameCounterRef() { return lastFrameCounter; }
		int64_t& GetCurrentFrameCounterRef() { return currentFrameCounter; }

		const std::vector<float>& GetFrameTimeHistory() const { return frameTimeHistory; }
		const std::vector<float>& GetPostFGFrameTimeHistory() const { return postFGFrameTimeHistory; }
		const LARGE_INTEGER& GetLastUpdateTime() const { return lastUpdateTime; }
		LARGE_INTEGER& GetLastUpdateTimeRef() { return lastUpdateTime; }
		LARGE_INTEGER& GetOverlayTimingFrequencyRef() { return overlayTimingFrequency; }

		// Non-const getters for history vectors that need modification
		std::vector<float>& GetFrameTimeHistoryRef() { return frameTimeHistory; }
		std::vector<float>& GetPostFGFrameTimeHistoryRef() { return postFGFrameTimeHistory; }

		// Setters for controlled state modification
		void SetInitialized(bool value) { initialized = value; }
		void SetHasGraphs(bool value) { hasGraphs = value; }
		void SetFrameGenerationActive(bool value) { isFrameGenerationActive = value; }
		void SetFrameTimeMs(float value) { frameTimeMs = value; }
		void SetFps(float value) { fps = value; }
		void SetPostFGFrameTimeMs(float value) { postFGFrameTimeMs = value; }
		void SetPostFGFps(float value) { postFGFps = value; }
		void SetSmoothFps(float value) { smoothFps = value; }
		void SetSmoothFrameTimeMs(float value) { smoothFrameTimeMs = value; }
		void SetPostFGSmoothFps(float value) { postFGSmoothFps = value; }
		void SetPostFGSmoothFrameTimeMs(float value) { postFGSmoothFrameTimeMs = value; }
		void SetUpdateTimer(float value) { updateTimer = value; }
		void SetMinFrameTime(float value) { minFrameTime = value; }
		void SetMaxFrameTime(float value) { maxFrameTime = value; }
		void SetSmoothedMinFrameTime(float value) { smoothedMinFrameTime = value; }
		void SetSmoothedMaxFrameTime(float value) { smoothedMaxFrameTime = value; }
		void SetTextScale(float value) { textScale = value; }
		void SetFrequency(int64_t value) { frequency = value; }
		void SetLastFrameCounter(int64_t value) { lastFrameCounter = value; }
		void SetCurrentFrameCounter(int64_t value) { currentFrameCounter = value; }
		void SetFrameTimeHistoryIndex(int value) { frameTimeHistoryIndex = value; }
		void SetPostFGFrameTimeHistoryIndex(int value) { postFGFrameTimeHistoryIndex = value; }
		void SetLastUpdateTime(const LARGE_INTEGER& value) { lastUpdateTime = value; }

		// Buffer management methods
		void ResizeFrameTimeHistory(size_t size, float defaultValue = 0.0f) { frameTimeHistory.resize(size, defaultValue); }
		void ResizePostFGFrameTimeHistory(size_t size, float defaultValue = 0.0f) { postFGFrameTimeHistory.resize(size, defaultValue); }

		// Methods that modify state
		float CalculateTextScale();
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

	// ============================================================================
	// SETTINGS STRUCTURE
	// ============================================================================
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
	// ============================================================================
	// PRIVATE DATA STRUCTURES
	// ============================================================================
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

	// A/B testing settings diff data
	std::vector<SettingsDiffEntry> settingsDiff;
	bool settingsDiffLoaded = false;

	// Test data management
	void CaptureTestData();
	void ClearTestData();
	TestDataSource testDataSource = TestDataSource::None;
	std::chrono::steady_clock::time_point testDataLastUpdated;
	std::unordered_map<int, TestData> testData;
};