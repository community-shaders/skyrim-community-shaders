#pragma once

#include "Buffer.h"

#include "Weather/ImageSpaceWidget.h"
#include "Weather/LightingTemplateWidget.h"
#include "Weather/WeatherWidget.h"
#include "Weather/WorldSpaceWidget.h"
#include "WeatherUtils.h"
#include "Widget.h"

class EditorWindow
{
public:
	static EditorWindow* GetSingleton()
	{
		static EditorWindow singleton;
		return &singleton;
	}

	bool open = false;
	const static int maxRecordMarkers = 10;

	Texture2D* tempTexture;

	std::vector<Widget*> weatherWidgets;
	std::vector<Widget*> worldSpaceWidgets;
	std::vector<Widget*> lightingTemplateWidgets;
	std::vector<Widget*> imageSpaceWidgets;

	// Weather locking for editing
	RE::TESWeather* lockedWeather = nullptr;
	bool weatherLockActive = false;

	// Time pause for editing
	bool timePaused = false;
	float savedTimeScale = 1.0f;

	// Vanity camera control
	bool vanityCameraDisabled = false;
	float savedVanityCameraDelay = 180.0f;

	void ShowObjectsWindow();

	void ShowViewportWindow();

	void ShowWidgetWindow();

	void RenderUI();

	void SetupResources();

	void Draw();

	void LockWeather(RE::TESWeather* weather);
	void UnlockWeather();
	bool IsWeatherLocked() const { return weatherLockActive; }
	RE::TESWeather* GetLockedWeather() const { return lockedWeather; }

	void PauseTime();
	void ResumeTime();
	bool IsTimePaused() const { return timePaused; }

	void DisableVanityCamera();
	void RestoreVanityCamera();

	// Notification system
	struct Notification
	{
		std::string message;
		ImVec4 color;
		float startTime;
		float duration;
	};
	std::vector<Notification> notifications;

	void ShowNotification(const std::string& message, const ImVec4& color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f), float duration = 3.0f);
	void RenderNotifications();

	struct Settings
	{
		std::map<std::string, ImVec4> recordMarkers = {
			{ "To Do", { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ "In Progress", { 190.0f / 255.0f, 155.0f / 255.0f, 0.0f, 1.0f } },
			{ "Complete", { 0.0f, 130.0f / 255.0f, 0.0f, 1.0f } }
		};
		std::map<std::string, std::string> markedRecords;
		bool autoApplyChanges = true;
		bool suppressDeleteWarning = false;
		bool useTextButtons = false;
		std::vector<std::string> favoriteWidgets;
		std::vector<std::string> recentWidgets;
		int maxRecentWidgets = 10;
		bool rememberOpenWidgets = true;
		std::vector<std::string> lastOpenWidgets;
	};

	Settings settings;

	void Save();
	void AddToRecent(const std::string& widgetId);
	void ToggleFavorite(const std::string& widgetId);
	bool IsFavorite(const std::string& widgetId) const;
	void SaveSessionWidgets();
	void RestoreSessionWidgets();

private:
	void SaveAll();
	void SaveSettings();
	void LoadSettings();
	void ShowSettingsWindow();
	void Load();
	json j;
	std::string settingsFilename = "EditorSettings";
	bool showSettingsWindow = false;

	// Sorting state
	enum class SortColumn
	{
		None,
		EditorID,
		FormID,
		File,
		Status
	};
	SortColumn currentSortColumn = SortColumn::None;
	bool sortAscending = true;
};