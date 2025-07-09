#pragma once

#include "OverlayFeature.h"
#include <chrono>
#include <optional>
#include <unordered_map>

struct DrawCallRow
{
	std::string label;
	int shaderType;
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
	void DrawDrawCalls();
	void DrawVRAM();

	// Test data management
	static void UpdateShaderTestData(int shaderType, float frameTime, float costPerCall);
	static std::string GetTestDataTooltip();

public:
	static void UpdateAllShaderTestData();

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

		float SetTextScale();
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

	static ImVec4 GetPerformanceColor(float value, float lowThreshold, float highThreshold);
	static ImVec4 GetPerformanceStatusColor(int status);  // Use int for now if PerformanceStatus is not defined
	static bool RenderColorCodedMetric(const char* label, float value, float lowThreshold, float highThreshold, const char* format);
};