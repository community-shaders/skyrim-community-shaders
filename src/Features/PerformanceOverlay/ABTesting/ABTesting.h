#pragma once
#include "ABTestAggregator.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <vector>

// A/B Testing Manager - handles the overall A/B testing system
class ABTestingManager
{
public:
	static ABTestingManager* GetSingleton();

	// Configuration
	void SetTestInterval(uint32_t interval);
	uint32_t GetTestInterval() const { return testInterval; }
	bool IsEnabled() const { return abTestingEnabled; }
	bool IsUsingTestConfig() const { return usingTestConfig; }

	// State management
	void Enable();
	void Disable();
	void Update();  // Called each frame to handle timing and switching

	// UI
	void DrawSettingsUI();  // The A/B test interval slider
	void DrawOverlayUI();   // The "Variant X: Y seconds left" overlay

	// Access to aggregator
	ABTestAggregator& GetAggregator() { return aggregator; }

private:
	uint32_t testInterval = 0;
	bool abTestingEnabled = false;
	bool usingTestConfig = false;
	std::chrono::high_resolution_clock::time_point lastTestSwitch;
	ABTestAggregator aggregator;
};