#include "TimeControl.h"

#include "Globals.h"
#include "Util.h"

void TimeControl::PauseTime()
{
	if (paused)
		return;
	auto calendar = globals::game::calendar;
	if (calendar && calendar->timeScale) {
		savedTimeScale = calendar->timeScale->value;
		calendar->timeScale->value = 0.0f;
		paused = true;
		logger::info("Time paused (saved timescale: {})", savedTimeScale);
	}
}

void TimeControl::ResumeTime()
{
	if (!paused)
		return;
	auto calendar = globals::game::calendar;
	if (calendar && calendar->timeScale) {
		calendar->timeScale->value = savedTimeScale;
		paused = false;
		logger::info("Time resumed (timescale: {})", savedTimeScale);
	}
}

void TimeControl::TogglePause()
{
	paused ? ResumeTime() : PauseTime();
}

void TimeControl::ResetTimeScale()
{
	auto calendar = globals::game::calendar;
	if (!calendar || !calendar->timeScale)
		return;

	if (paused) {
		savedTimeScale = kVanillaTimeScale;
	} else {
		calendar->timeScale->value = kVanillaTimeScale;
	}
	timeScaleSlider = kVanillaTimeScale;
}

bool TimeControl::IsSleepWaitOpen() const
{
	auto ui = globals::game::ui;
	return ui && ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME);
}

void TimeControl::SyncExternalState()
{
	if (IsSleepWaitOpen())
		return;

	auto calendar = globals::game::calendar;
	if (!calendar || !calendar->timeScale)
		return;

	if (calendar->timeScale->value == 0.0f && !paused) {
		// External pause detected (e.g. console command) — keep sane restore value
		savedTimeScale = kVanillaTimeScale;
	} else if (calendar->timeScale->value > 0.0f && paused) {
		// External resume detected — clear stale pause flag
		paused = false;
	}
}

void TimeControl::HandleSleepWait()
{
	auto calendar = globals::game::calendar;
	if (!calendar || !calendar->timeScale)
		return;

	bool sleepWaitOpen = IsSleepWaitOpen();

	if (sleepWaitOpen && calendar->timeScale->value == 0.0f) {
		if (!wasRestoredForWait) {
			wasPausedBeforeWait = true;
			if (paused) {
				ResumeTime();
			} else {
				// Direct write: restore a sane timescale without touching pause state
				calendar->timeScale->value = std::max(savedTimeScale, kVanillaTimeScale);
			}
			wasRestoredForWait = true;
		}
	} else if (!sleepWaitOpen && wasRestoredForWait) {
		if (wasPausedBeforeWait && !paused)
			PauseTime();
		wasRestoredForWait = false;
		wasPausedBeforeWait = false;
	}
}

void TimeControl::Update()
{
	SyncExternalState();
	HandleSleepWait();
}

bool TimeControl::DrawGameHourSlider(const char* label, const char* format)
{
	auto calendar = globals::game::calendar;
	if (!calendar || !calendar->gameHour)
		return false;
	ImGui::SliderFloat(label, &calendar->gameHour->value, kGameHourMin, kGameHourMax, format);
	return true;
}

void TimeControl::DrawFullControls()
{
	auto calendar = globals::game::calendar;
	if (!calendar || !calendar->gameHour || !calendar->timeScale)
		return;

	// Row 1: Pause/Resume + Game Time slider
	if (ImGui::Button(paused ? "Resume Time" : "Pause Time", ImVec2(120, 0)))
		TogglePause();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Pause or resume game time progression");

	ImGui::SameLine();
	DrawGameHourSlider();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Adjust the current game time");

	// Sync slider with actual value
	if (paused) {
		timeScaleSlider = std::max(savedTimeScale, kTimeScaleMin);
	} else if (std::abs(calendar->timeScale->value - timeScaleSlider) > 0.01f) {
		timeScaleSlider = calendar->timeScale->value;
	}

	// Row 2: Reset Speed + TimeScale slider + current speed
	if (ImGui::Button("Reset Speed", ImVec2(120, 0)))
		ResetTimeScale();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Reset time speed to vanilla (%.1fx)", kVanillaTimeScale);

	ImGui::SameLine();

	ImGui::BeginDisabled(paused);
	if (ImGui::SliderFloat("##TimeScale", &timeScaleSlider, kTimeScaleMin, kTimeScaleMax,
			timeScaleSlider == kVanillaTimeScale ? "Vanilla Speed" : "", ImGuiSliderFlags_Logarithmic)) {
		calendar->timeScale->value = timeScaleSlider;
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::Text("%.1fx", calendar->timeScale->value);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Adjust how fast time passes (vanilla: %.1fx)", kVanillaTimeScale);
}
