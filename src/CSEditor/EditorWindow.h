#pragma once

#include "Buffer.h"

#include "Browser/BrowserState.h"
#include "FormEditSources.h"
#include "LightEditor.h"
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

#include <optional>
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
	bool showWeatherDebug = false;  // optional Weather Debug window, off by default (not persisted)
	bool widgetsHidden = false;     // action bar toggle: hides every window except the bar (not persisted)
	float widgetsFade = 1.0f;       // 1 = widgets shown, 0 = hidden; eased toward widgetsHidden each frame
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

	/** @brief Calls fn on every editable widget, the cell lighting widget included. */
	template <typename Fn>
	void ForEachWidget(Fn&& fn)
	{
		for (auto* collection : GetWidgetCollections())
			for (auto& widget : *collection)
				fn(*widget);
		if (currentCellLightingWidget)
			fn(*currentCellLightingWidget);
	}

	/** @brief Saves every widget with unsaved changes; clean widgets are left untouched. */
	void SaveUnsavedWidgets();

	/**
	 * @brief Returns true if any widget, or Scene Manager, has unsaved changes.
	 * @details Open widgets are checked every call since they are the ones being edited; the full
	 *          pass, which catches closed widgets changed by Load or Undo, runs on a short interval.
	 */
	bool HasUnsavedChanges();
	std::vector<std::unique_ptr<Widget>> artObjectWidgets;
	std::vector<std::unique_ptr<Widget>> effectShaderWidgets;

	// Owned by EditorWindow, created on demand in ShowObjectsWindow(), released in destructor
	std::unique_ptr<CellLightingWidget> currentCellLightingWidget;

	LightEditor lightEditor;

	/** @brief Tools that write into other mods' files; the editor's save never writes them. */
	std::array<ExternalTool*, 1> GetExternalTools() { return { &lightEditor }; }

	/** @brief Why a tool's unwritten changes need an answer. */
	enum class ToolPromptReason
	{
		Close,   ///< The editor is closing.
		Switch,  ///< The tool is moving on to something else, such as another light.
	};

	/**
	 * @brief Asks whether to write or discard a tool's unwritten changes before an action that would lose them.
	 * @param proceed The action; runs once the changes are written or discarded, and not on Cancel.
	 */
	void RequestToolPrompt(ExternalTool& tool, ToolPromptReason reason, std::function<void()> proceed);

	/** @brief When true, resets all window positions/sizes on next frame (auto-cleared). */
	bool resetLayout = false;

	/** @brief Bottom Y of the viewport window, set during layout for palette positioning. */
	float viewportBottomY = 0.0f;

	/** @brief Editor dockspace ID from the last frame, read by the action bar to find nodes docked against it. */
	ImGuiID dockSpaceId = 0;

	/** @brief Last frame's viewport collapse state, so Draw() can skip the framebuffer copy. */
	bool viewportCollapsed = false;

	// Time control constants
	static constexpr float kVanillaTimeScale = 20.0f;
	static constexpr float kGameHourMax = 23.99f;
	static constexpr float kTimeScaleMin = 0.1f;
	static constexpr float kTimeScaleMax = 4000.0f;
	static constexpr float kMenuBarSliderWidth = 400.0f;

	// Notification durations, in seconds; the long one suits messages that explain what to do next.
	static constexpr float kNotificationDuration = 3.0f;
	static constexpr float kLongNotificationDuration = 4.0f;

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

	// Game HUD hiding (tm equivalent)
	bool gameMenusHidden = false;

	/** @brief Draw the Objects browser window listing all editable form widgets. */
	void ShowObjectsWindow();

	/**
	 * @brief Open the Cell Lighting editor for a cell, reusing the open one when it is the same cell.
	 * @param cell The interior cell to edit.
	 * @param notify Show the "loaded" notification when a saved file is read.
	 */
	void OpenCellLighting(RE::TESObjectCELL* cell, bool notify);

	/** @brief Draw a compact "Active: <weather>" line matching the indicator atop other object categories.
	 *  @param drawTrailer Follow with a separator; pass false to keep adding to the same row. */
	void DrawActiveWeatherIndicator(bool drawTrailer = true);

	/** @brief Draw the game viewport preview window with render target display. */
	void ShowViewportWindow();

	/** @brief Draw all currently open widget editing windows. */
	void ShowWidgetWindow();

	/** @brief Lays out and draws every editor window below the action bar; skipped once the hide fade finishes. */
	void DrawEditorWindows(float scale, float actionBarBottom);

	/** @brief Render the full editor UI including menu bar, windows, and overlays. */
	void RenderUI();

	/** @brief Create widget instances for all game forms and load saved settings. */
	void SetupResources();

	/** @brief Reloads every built widget whose form is in @p keys from its resolved file, then refreshes the JSON markers. */
	void ReloadFormEdits(const FormEditSources::FormKeySet& keys);

	/** @brief Top-level draw entry point called once per frame when the editor is open. */
	void Draw();

	/**
	 * @brief Lock the game to a specific weather for editing.
	 * @param weather The weather form to force active and guard against engine weather changes.
	 */
	void LockWeather(RE::TESWeather* weather);

	/** @brief Unlock the weather, releasing the override so natural progression resumes. */
	void UnlockWeather();

	/** @brief Returns true if a weather is currently locked for editing. */
	bool IsWeatherLocked() const;

	/** @brief Returns the currently locked weather form, or nullptr if none. */
	RE::TESWeather* GetLockedWeather() const;

	/**
	 * @brief Returns true if the user explicitly locked the weather.
	 * @details The editor silently locks weather while open; that implicit lock does not count.
	 */
	bool IsWeatherLockedByUser() const;

	/**
	 * @brief Lock a weather on the user's behalf; unlike the editor's implicit lock it survives closing the editor.
	 * @param weather The weather form to lock.
	 */
	void LockWeatherForUser(RE::TESWeather* weather);

	/** @brief Release the user's lock: while the editor is open it falls back to the implicit lock, otherwise it unlocks. */
	void ReleaseUserWeatherLock();

	/** @brief Redirect the engine's weather-change call sites to the locked weather. Call once during plugin init. */
	static void InstallWeatherLockHooks();

	/** @brief Returns true if InstallWeatherLockHooks successfully redirected both call sets. */
	static bool AreWeatherLockHooksInstalled();

	/** @brief Repair the locked weather if the engine changed it or queued an override release. Call once per frame. */
	static void MaintainWeatherLock();

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

	/**
	 * @brief Restores time around menus the engine cannot complete with a zero timescale, and
	 * re-pauses once they close. Sleep/wait never finishes and fast travel hangs otherwise.
	 * @param a_needsRunningTime True while any such menu is open.
	 */
	void SetTimeRunningForMenu(bool a_needsRunningTime);

	/** @brief Drives the time guard off menu transitions, since the world render stops during them. */
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		/** @brief Subscribes the singleton handler to the UI menu event source. */
		static bool Register();
	};

	/**
	 * @brief Draw a game-hour slider.
	 * @param label  Slider label text.
	 * @param format Printf-style format string for the time value.
	 * @return True if the game calendar is valid and the slider was drawn.
	 */
	bool DrawGameHourSlider(const char* label = "Game Time", const char* format = "%.2f");

	/**
	 * @brief Game-hour slider with paused overlay format and error-colored text when time is paused.
	 * @param id Hidden ImGui id (e.g. "##FeatureGameTime").
	 * @return True if the game calendar is valid and the slider was drawn.
	 */
	bool DrawPausedAwareGameHourSlider(const char* id);

	/**
	 * @brief Shared pause/resume icon toggle (clock-pause glyph, red chrome when paused).
	 * @param id Hidden ImGui id.
	 * @param size Button size; (0,0) uses the current frame height.
	 * @return True if the button was clicked (pause state is toggled on click).
	 */
	bool DrawTimePauseToggle(const char* id, const ImVec2& size = ImVec2(0, 0));

	/** @brief Logarithmic time-scale slider synced to the calendar, disabled while time is paused. */
	void DrawTimeScaleSlider(const char* id, RE::Calendar* calendar);

	/** @brief Draw the full time controls panel (pause, game time, timescale). */
	void DrawTimeControls();

	/** @brief Which time periods the game-time slider segments. */
	enum class TimePeriodSet : std::uint8_t
	{
		None,
		Weather,  ///< Four sky colour times.
		Scene     ///< Six scene periods.
	};

	/** @brief Sets the slider's periods when the current window is focused; the last focused editor surface wins. */
	void ClaimTimePeriodSet(TimePeriodSet periodSet);

	/** @brief Returns true if ESC should close the editor (no popup open and none just consumed ESC this frame). */
	bool ShouldHandleEscapeKey();

	/** @brief Set by popup close-on-ESC to suppress the same key-up from also closing the editor. */
	bool suppressNextEditorEscape = false;

	/** @brief Closes the current popup on ESC without letting the same press close the editor. @return True if it closed. */
	static bool ClosePopupOnEscape();

	/** @brief Returns true if the editor can be opened (game is loaded and not in main menu). */
	static bool CanBeOpen();

	/** @brief True while a loading screen or a game menu is up; the editor breaks if it opens over one. */
	static bool IsOpeningBlocked();

	/** @brief Shows the "cannot open here" warning card, once per press rather than stacking repeats. */
	void WarnOpeningBlocked();

	/** @brief True while a notification card is still on screen. */
	bool HasNotifications() const { return !notifications.empty(); }

	/** @brief Shows the Base Settings window on a feature and brings it to the front. */
	void OpenBaseSettings(const std::string& featureShortName);

	/** @brief Shows a category in the objects window, by its stable English ID, and brings that window to the front. */
	void SelectCategory(std::string category)
	{
		m_selectedCategory = std::move(category);
		m_focusBrowser = true;
	}

	/** @brief Hide the game HUD and menus (equivalent to the 'tm' console command). */
	void HideGameMenus();

	/** @brief Show the game HUD and menus, reversing a previous HideGameMenus call. */
	void ShowGameMenus();

	/** @brief Call every frame from the overlay renderer to track open/close transitions. */
	void UpdateOpenState();

	/// Undo and redo for everything edited in the editor.
	UndoHistory undoHistory;

	/** @brief An undo target for a widget, found again by form key so its entries outlive the widget object. */
	UndoHistory::Target MakeUndoTarget(const Widget& widget);

	/** @brief The widget editing this form, the cell lighting widget included, or null. */
	Widget* FindWidget(const FormEditSources::FormKey& key);

	/** @brief Remembers the focused widget by form key, not object lifetime. */
	void SetFocusedWidget(const Widget& widget) { lastFocusedWidgetKey = widget.GetFormKey(); }

	/** @brief Steps the newest change back and says what it undid. */
	void PerformUndo();

	/** @brief Reapplies the newest undone change and says what it redid. */
	void PerformRedo();

	/**
	 * @brief Discards a widget's unsaved changes by reloading its saved file (vanilla values when it
	 *        has none). Undoable.
	 */
	void DiscardWidgetChanges(Widget& widget);

	/** @brief Discards every widget's unsaved changes. Undoable, one step per widget. */
	void DiscardAllWidgetChanges();

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
	void ShowNotification(const std::string& message, const ImVec4& color = Util::Colors::GetError(), float duration = kNotificationDuration);

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
		/// When false (default), Debug TreeNode / CollapsingHeader sections are hidden in the Features editor.
		bool showFeatureDebug = false;
		float editorUIScale = 1.0f;
		std::vector<std::string> favoriteWidgets;
		std::map<std::string, std::vector<std::string>> recentWidgets;
		int maxRecentWidgets = 10;
		bool showViewport = true;
		/// Base Settings window; the key predates the rename from Features.
		bool showFeaturesWindow = false;
		std::string selectedCategory = "Weather";
		/// Browser form pages show the inspector beside (or under) the list.
		bool browserShowInspector = true;
		/// Browser category sidebar collapsed to icons.
		bool browserSidebarCompact = false;
		/// A tool's unwritten changes are discarded without asking when switching or closing.
		bool skipToolDiscardPrompt = false;

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

	/** @brief Destructor. Releases owned textures and widget resources. */
	~EditorWindow();

private:
	friend class Widget;

	/// Seconds between full unsaved-change passes over every widget.
	static constexpr double kUnsavedScanInterval = 0.5;
	/// ImGui time of the last full pass; reset after a save so the indicator clears at once.
	double lastUnsavedScan = -kUnsavedScanInterval;
	bool anyWidgetUnsaved = false;

	void SaveSettings();
	void LoadSettings();
	void ShowSettingsWindow();
	void Load();
	json j;
	std::string settingsFilename = "EditorSettings";
	bool showSettingsWindow = false;
	std::string settingsSelectedCategory = "Flags";

	// Widget focus tracking for Ctrl+W
	std::optional<FormEditSources::FormKey> lastFocusedWidgetKey;

	/** @brief Locks the current weather once any in-progress transition finishes, unless the user already locked one. */
	void LockWeatherForOverlay();

	/// True while the lock belongs to the overlay, so closing it only releases what it took.
	bool weatherLockedByOverlay = false;
	/** @brief The main menu was open when the editor opened, so closing the editor reopens it. */
	bool returnToMenu = false;

	/// Asks what to do with a tool's unwritten changes before an action that would lose them.
	Util::ConfirmationPopup toolPrompt;
	ExternalTool* promptedTool = nullptr;
	/// What the prompted action does once the changes are written or discarded.
	std::function<void()> toolPromptProceed;
	/// The next close goes ahead without asking: the prompt was just answered.
	bool skipToolPrompt = false;
	/** @brief Acts on the answer to toolPrompt. */
	void DrawToolPrompt();

	/// True from overlay open until its weather lock engages or the overlay closes.
	bool overlayWeatherLockPending = false;
	/// Sky region at lock time, restored on unlock since ForceWeather clears it.
	RE::TESRegion* regionBeforeLock = nullptr;

	// Time control state
	bool timePaused = false;
	float savedTimeScale = kVanillaTimeScale;
	float timeScaleSlider = kVanillaTimeScale;
	bool timeRestoredForMenu = false;
	bool wasPausedBeforeMenu = false;
	// Each refresh recomputes the whole terrain shadow map, so scrubbing is throttled well below frame rate.
	static constexpr double kGameHourScrubRefreshIntervalSeconds = 0.1;
	double lastGameHourScrubRefreshTime = 0.0;
	bool gameHourScrubRefreshIssued = false;

	Widget* pendingDeleteWidget = nullptr;
	bool pendingDeletePopupRequested = false;

	void OnWidgetJsonAttachmentChanged(Widget* widget);
	std::unordered_map<Widget*, bool> jsonAttachmentCache;
	void RefreshJsonAttachmentCache(const std::vector<Widget*>& widgets);
	bool HasCachedJsonAttachment(Widget* widget) const;
	void InvalidateJsonAttachmentCache(Widget* widget = nullptr);

	// Objects window (CS Editor Browser) state
	std::string m_selectedCategory = "Weather";
	TimePeriodSet timePeriodSet = TimePeriodSet::Weather;
	std::string m_previousSelectedCategory = "Weather";
	/// Set by SelectCategory; the objects window takes focus on its next Begin.
	bool m_focusBrowser = false;
	/// Filters, sort, cached rows and selection of the form-list pages.
	Browser::FormListState m_formList;
	/// Compact state the sidebar column was last sized for; unset until the first frame sizes it.
	std::optional<bool> m_sidebarWasCompact;
	/// Width the user had dragged the expanded sidebar to, restored when it expands again.
	float m_sidebarExpandedWidth = 0.0f;
	void DrawBrowserSidebar(bool compact);
	void DrawBrowserPage();
	static std::string ResolveEditorId(RE::TESForm* form, const WidgetVec& widgets);
};
