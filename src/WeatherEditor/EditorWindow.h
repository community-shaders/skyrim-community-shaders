#pragma once

#include "Buffer.h"

#include "Weather/CellLightingWidget.h"
#include "Weather/ImageSpaceWidget.h"
#include "Weather/LensFlareWidget.h"
#include "Weather/LightingTemplateWidget.h"
#include "Weather/PrecipitationWidget.h"
#include "Weather/ReferenceEffectWidget.h"
#include "Weather/VolumetricLightingWidget.h"
#include "Weather/WeatherWidget.h"
#include "WeatherUtils.h"
#include "Widget.h"

#include <unordered_map>

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

	// Owned by EditorWindow, created in Draw(), released in destructor
	Texture2D* tempTexture = nullptr;

	// Widget collections owned by EditorWindow, created in SetupResources(), released in destructor
	std::vector<std::unique_ptr<Widget>> weatherWidgets;
	std::vector<std::unique_ptr<Widget>> lightingTemplateWidgets;
	std::vector<std::unique_ptr<Widget>> imageSpaceWidgets;
	std::vector<std::unique_ptr<Widget>> volumetricLightingWidgets;
	std::vector<std::unique_ptr<Widget>> precipitationWidgets;
	std::vector<std::unique_ptr<Widget>> lensFlareWidgets;
	std::vector<std::unique_ptr<Widget>> referenceEffectWidgets;
	std::vector<std::unique_ptr<Widget>> artObjectWidgets;
	std::vector<std::unique_ptr<Widget>> effectShaderWidgets;

	// Owned by EditorWindow, created on demand in ShowObjectsWindow(), released in destructor
	std::unique_ptr<CellLightingWidget> currentCellLightingWidget;

	// Weather locking for editing
	RE::TESWeather* lockedWeather = nullptr;
	bool weatherLockActive = false;

	// Time control constants
	static constexpr float kVanillaTimeScale = 20.0f;
	static constexpr float kGameHourMax = 23.99f;
	static constexpr float kTimeScaleMin = 0.1f;
	static constexpr float kTimeScaleMax = 4000.0f;

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

	// Time controls
	void PauseTime();
	void ResumeTime();
	inline void TogglePause() { timePaused ? ResumeTime() : PauseTime(); }
	void ResetTimeScale();
	bool IsTimePaused() const { return timePaused; }

	/// Call once per frame — handles sleep/wait menu and external state sync.
	void UpdateTimeState();

	/// Draw a game-hour slider. Returns true if calendar is valid.
	bool DrawGameHourSlider(const char* label = "Game Time", const char* format = "%.2f");

	/// Draw the full time controls panel (pause, game time, timescale).
	void DrawTimeControls();

	// Check if ESC key should close the editor (no popups open)
	bool ShouldHandleEscapeKey() const;

	void DisableVanityCamera();
	void RestoreVanityCamera();

	// Undo system
	struct UndoState
	{
		Widget* widget;
		json settings;
		std::string widgetId;
	};
	std::vector<UndoState> undoStack;
	static const size_t maxUndoStates = 50;

	void PushUndoState(Widget* widget);
	void PerformUndo();
	bool CanUndo() const { return !undoStack.empty(); }

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
		bool useTextButtons = false;
		bool enableInheritFromParent = false;
		float editorUIScale = 1.0f;
		std::vector<std::string> favoriteWidgets;
		std::map<std::string, std::vector<std::string>> recentWidgets;
		int maxRecentWidgets = 10;
		bool rememberOpenWidgets = true;
		std::vector<std::string> lastOpenWidgets;

		// Palette settings
		struct PaletteColorEntry
		{
			float r, g, b;
			int useCount = 0;
			float lastUsedTime = 0.0f;
			bool isFavorite = false;
		};
		struct PaletteValueEntry
		{
			std::string name;
			float value;
			int useCount = 0;
			float lastUsedTime = 0.0f;
			bool isFavorite = false;
		};
		struct PaletteFavoriteColor
		{
			bool hasValue = false;
			float r = 0.0f, g = 0.0f, b = 0.0f;
		};
		std::vector<PaletteColorEntry> paletteColors;
		std::vector<PaletteValueEntry> paletteValues;
		std::array<PaletteFavoriteColor, 10> paletteFavorites;
	};

	Settings settings;

	void Save();
	void AddToRecent(const std::string& widgetId, const std::string& category);
	void ToggleFavorite(const std::string& widgetId);
	bool IsFavorite(const std::string& widgetId) const;
	void SaveSessionWidgets();
	void RestoreSessionWidgets();

	// Navigation helpers for weather-controlled settings
	void OpenWeatherFeatureSetting(RE::TESWeather* weather, const std::string& featureName, const std::string& settingName);

	~EditorWindow();

private:
	friend class Widget;

	void SaveAll();
	void SaveSettings();
	void LoadSettings();
	void ShowSettingsWindow();
	void Load();
	json j;
	std::string settingsFilename = "EditorSettings";
	bool showSettingsWindow = false;
	std::string settingsSelectedCategory = "Flags";

	// Widget focus tracking for Ctrl+W
	Widget* lastFocusedWidget = nullptr;

	// Time control state
	bool timePaused = false;
	float savedTimeScale = kVanillaTimeScale;
	float timeScaleSlider = kVanillaTimeScale;
	bool wasRestoredForWait = false;
	bool wasPausedBeforeWait = false;

	// Sorting state
	enum class SortColumn
	{
		None,
		EditorID,
		FormID,
		File,
		Status,
		JsonAttachment
	};
	SortColumn currentSortColumn = SortColumn::None;
	bool sortAscending = true;

	Widget* pendingDeleteWidget = nullptr;
	bool pendingDeletePopupRequested = false;

	void OnWidgetJsonAttachmentChanged(Widget* widget);
	std::unordered_map<Widget*, bool> jsonAttachmentCache;
	void RefreshJsonAttachmentCache(const std::vector<Widget*>& widgets);
	bool HasCachedJsonAttachment(Widget* widget) const;
	void InvalidateJsonAttachmentCache(Widget* widget = nullptr);
};