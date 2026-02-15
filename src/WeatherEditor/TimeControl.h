#pragma once

/// Centralized time control for the weather editor.
/// Owns all pause/resume state and provides reusable UI widgets.
class TimeControl
{
public:
	static TimeControl* GetSingleton()
	{
		static TimeControl singleton;
		return &singleton;
	}

	static constexpr float kVanillaTimeScale = 20.0f;
	static constexpr float kGameHourMin = 0.0f;
	static constexpr float kGameHourMax = 23.99f;
	static constexpr float kTimeScaleMin = 0.1f;
	static constexpr float kTimeScaleMax = 4000.0f;

	void PauseTime();
	void ResumeTime();
	void TogglePause();
	void ResetTimeScale();
	bool IsPaused() const { return paused; }
	float GetSavedTimeScale() const { return savedTimeScale; }

	/// Call once per frame to handle sleep/wait menu and external state sync.
	void Update();

	/// Draw a game-hour slider. Returns true if the slider was rendered.
	bool DrawGameHourSlider(const char* label = "Game Time", const char* format = "%.2f");

	/// Draw the full time controls section (pause button, game time, timescale).
	void DrawFullControls();

private:
	bool paused = false;
	float savedTimeScale = kVanillaTimeScale;
	float timeScaleSlider = kVanillaTimeScale;

	bool wasRestoredForWait = false;
	bool wasPausedBeforeWait = false;

	bool IsSleepWaitOpen() const;
	void SyncExternalState();
	void HandleSleepWait();
};
