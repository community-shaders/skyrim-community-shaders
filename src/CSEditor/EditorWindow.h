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
#include "LightEditor.h"
#include "WeatherUtils.h"
#include "Widget.h"

#include <unordered_map>

class EditorWindow
{
public:
	/** @brief Returns the global EditorWindow singleton instance. */
	static EditorWindow* GetSingleton()
	{
		static EditorWindow singleton;
		return &singleton;
	}

	// Preview modes for exploring the scene without the full editor UI
	enum class PreviewMode
	{
		None,              // Full editor UI visible
		FreeCamera,        // Flying free camera (tfc), input to game
		FreeCameraLocked,  // Camera locked in place, editor interactive
		PlayMode           // Normal gameplay, no scroll interception
	};

	bool open = false;
	PreviewMode previewMode = PreviewMode::None;
	const static int maxRecordMarkers = 10;

	// Owned by EditorWindow, created in Draw(), released in destructor
	Texture2D* tempTexture = nullptr;

	// Widget collections owned by EditorWindow, created in SetupResources(), released in destructor
	using WidgetVec = std::vector<std::unique_ptr<Widget>>;
	WidgetVec weatherWidgets;
	WidgetVec lightingTemplateWidgets;
	WidgetVec imageSpaceWidgets;
	WidgetVec volumetricLightingWidgets;
	WidgetVec precipitationWidgets;
	WidgetVec lensFlareWidgets;
	WidgetVec referenceEffectWidgets;

	/** @brief Returns references to all editable widget collections for centralized iteration. */
	std::array<WidgetVec*, 7> GetWidgetCollections()
	{
		return { &weatherWidgets, &lightingTemplateWidgets, &imageSpaceWidgets,
			&volumetricLightingWidgets, &precipitationWidgets, &lensFlareWidgets,
			&referenceEffectWidgets };
	}
	std::vector<std::unique_ptr<Widget>> artObjectWidgets;
	std::vector<std::unique_ptr<Widget>> effectShaderWidgets;

	// Owned by EditorWindow, created on demand in ShowObjectsWindow(), released in destructor
	std::unique_ptr<CellLightingWidget> currentCellLightingWidget;

	LightEditor lightEditor;

	// Weather locking for editing
	RE::TESWeather* lockedWeather = nullptr;
	bool weatherLockActive = false;

	/** @brief When true, resets all window positions/sizes on next frame (auto-cleared). */
	bool resetLayout = false;

	/** @brief Bottom Y of the viewport window, set during layout for palette positioning. */
	float viewportBottomY = 0.0f;

	// Time control constants
	static constexpr float kVanillaTimeScale = 20.0f;
	static constexpr float kGameHourMax = 23.99f;
	static constexpr float kTimeScaleMin = 0.1f;
	static constexpr float kTimeScaleMax = 4000.0f;
	static constexpr float kMenuBarSliderWidth = 400.0f;

	// Preview mode constants
	static constexpr float kDefaultFlySpeed = 10.0f;
	static constexpr float kMinFlySpeed = 1.0f;
	static constexpr float kMaxFlySpeed = 100.0f;
	static constexpr float kFlySpeedScrollStep = 2.0f;
	static constexpr float kToggleActiveAlpha = 0.6f;
	static constexpr float kToggleHoverAlpha = 0.8f;
	static constexpr float kInactiveHoverAlpha = 0.25f;

	// Preview mode state
	float flySpeed = kDefaultFlySpeed;
	ImVec2 savedMousePos = { -FLT_MAX, -FLT_MAX };

	/** @brief Enter a preview mode, configuring camera and input accordingly.
	 *  @param mode The preview mode to activate.
	 */
	void EnterPreviewMode(PreviewMode mode);

	/** @brief Exit the current preview mode and restore normal editor state. */
	void ExitPreviewMode();

	/** @brief Returns true if any preview mode is active. */
	bool IsInPreviewMode() const { return previewMode != PreviewMode::None; }

	/** @brief Returns true if the viewport window is hovered and no popup is blocking it. */
	bool IsViewportActive() const;

	/** @brief Returns true if the current preview mode uses a flying camera. */
	bool IsPreviewFlying() const { return previewMode == PreviewMode::FreeCamera || previewMode == PreviewMode::PlayMode; }

	/** @brief Returns the currently active preview mode. */
	PreviewMode GetPreviewMode() const { return previewMode; }

	/** @brief Toggle between free camera and locked free camera preview modes. */
	void ToggleFreeCameraLock();

	/**
	 * @brief Adjust the fly speed for free camera preview modes.
	 * @param scrollDelta Scroll wheel delta to apply as a speed adjustment.
	 */
	void AdjustFlySpeed(float scrollDelta);

	// Vanity camera control
	bool vanityCameraDisabled = false;
	float savedVanityCameraDelay = 180.0f;

	// Game HUD hiding (tm equivalent)
	bool gameMenusHidden = false;

	/** @brief Draw the Objects browser window listing all editable form widgets. */
	void ShowObjectsWindow();

	/** @brief Draw the game viewport preview window with render target display. */
	void ShowViewportWindow();

	/** @brief Draw all currently open widget editing windows. */
	void ShowWidgetWindow();

	/** @brief Render the full editor UI including menu bar, windows, and overlays. */
	void RenderUI();

	/** @brief Create widget instances for all game forms and load saved settings. */
	void SetupResources();

	/** @brief Top-level draw entry point called once per frame when the editor is open. */
	void Draw();

	/**
	 * @brief Lock the game to a specific weather for editing.
	 * @param weather The weather form to force active via Sky::ForceWeather.
	 */
	void LockWeather(RE::TESWeather* weather);

	/** @brief Unlock the weather, allowing natural weather progression to resume. */
	void UnlockWeather();

	/** @brief Returns true if a weather is currently locked for editing. */
	bool IsWeatherLocked() const { return weatherLockActive; }

	/** @brief Returns the currently locked weather form, or nullptr if none. */
	RE::TESWeather* GetLockedWeather() const { return lockedWeather; }

	// Time controls
	/** @brief Pause in-game time by setting the timescale to zero. */
	void PauseTime();

	/** @brief Resume in-game time by restoring the saved timescale. */
	void ResumeTime();

	/** @brief Toggle between paused and resumed time states. */
	inline void TogglePause() { timePaused ? ResumeTime() : PauseTime(); }

	/** @brief Reset the timescale to the vanilla default (20x). */
	void ResetTimeScale();

	/** @brief Returns true if in-game time is currently paused. */
	bool IsTimePaused() const { return timePaused; }

	/** @brief Call once per frame to handle sleep/wait menu and external time state sync. */
	void UpdateTimeState();

	/**
	 * @brief Draw a game-hour slider.
	 * @param label  Slider label text.
	 * @param format Printf-style format string for the time value.
	 * @return True if the game calendar is valid and the slider was drawn.
	 */
	bool DrawGameHourSlider(const char* label = "Game Time", const char* format = "%.2f");

	/** @brief Draw the full time controls panel (pause, game time, timescale). */
	void DrawTimeControls();

	/** @brief Returns true if ESC should close the editor (no popup open and none just consumed ESC this frame). */
	bool ShouldHandleEscapeKey();

	/** @brief Set by popup close-on-ESC to suppress the same key-up from also closing the editor. */
	bool suppressNextEditorEscape = false;

	/** @brief Returns true if the editor can be opened (game is loaded and not in main menu). */
	static bool CanBeOpen();

	/** @brief Disable Skyrim's vanity camera to prevent auto-rotation while editing. */
	void DisableVanityCamera();

	/** @brief Restore vanity camera to its previous delay setting. */
	void RestoreVanityCamera();

	/** @brief Hide the game HUD and menus (equivalent to the 'tm' console command). */
	void HideGameMenus();

	/** @brief Show the game HUD and menus, reversing a previous HideGameMenus call. */
	void ShowGameMenus();

	/** @brief Call every frame from the overlay renderer to track open/close transitions. */
	void UpdateOpenState();

	// Undo system
	struct UndoState
	{
		Widget* widget;
		json settings;
		std::string widgetId;
	};
	std::vector<UndoState> undoStack;
	static const size_t maxUndoStates = 50;

	/**
	 * @brief Capture the current settings of a widget and push them onto the undo stack.
	 * @param widget The widget whose settings to snapshot.
	 */
	void PushUndoState(Widget* widget);

	/** @brief Pop the most recent undo state and restore its widget settings. */
	void PerformUndo();

	/** @brief Returns true if the undo stack contains at least one entry. */
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

	/**
	 * @brief Display a temporary on-screen notification message.
	 * @param message The text to display.
	 * @param color   Text color (defaults to error red).
	 * @param duration How long the notification remains visible, in seconds.
	 */
	void ShowNotification(const std::string& message, const ImVec4& color = Util::Colors::GetError(), float duration = 3.0f);

	/** @brief Draw all active notifications and expire old ones. */
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
		bool showViewport = true;
		std::string selectedCategory = "Weather";

		// Per-widget-type window sizes (serialized as JSON for persistence)
		json widgetTypeSizes;

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

	/** @brief Save all editor settings and widget data to disk. */
	void Save();

	/**
	 * @brief Add a widget to the recent-usage list for its category.
	 * @param widgetId  The widget's editor ID.
	 * @param category  The category name (e.g. "Weather", "ImageSpace").
	 */
	void AddToRecent(const std::string& widgetId, const std::string& category);

	/**
	 * @brief Toggle the favorite status of a widget.
	 * @param widgetId The widget's editor ID.
	 */
	void ToggleFavorite(const std::string& widgetId);

	/**
	 * @brief Returns true if the given widget is marked as a favorite.
	 * @param widgetId The widget's editor ID.
	 */
	bool IsFavorite(const std::string& widgetId) const;

	/**
	 * @brief Navigate to and highlight a specific feature setting within a weather widget.
	 * @param weather     The weather form to open.
	 * @param featureName The feature tab name to select.
	 * @param settingName The setting ID to scroll to and highlight.
	 */
	void OpenWeatherFeatureSetting(RE::TESWeather* weather, const std::string& featureName, const std::string& settingName);

	/** @brief Destructor. Releases owned textures and widget resources. */
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

	// Objects window filter state
	enum class FilterColumn : int
	{
		All = 0,
		EditorID,
		FormID,
		File,
		Status,
		Count_  // Sentinel – must equal IM_ARRAYSIZE(kFilterColumnNames)
	};
	std::string m_selectedCategory = "Weather";
	std::string m_previousSelectedCategory = "Weather";
	char m_filterBuffer[256] = {};
	bool m_showOnlyFlagged = false;
	bool m_showOnlyFavorites = false;
	FilterColumn m_currentFilterColumn = FilterColumn::All;
	void ResetObjectsFilter();
	bool MatchesObjectFilter(Widget* w) const;
	static std::string ResolveEditorId(RE::TESForm* form, const WidgetVec& widgets);
};