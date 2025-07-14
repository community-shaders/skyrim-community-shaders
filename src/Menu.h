#pragma once

#include "Feature.h"
#include "Utils/Serialize.h"
#include <dxgi1_4.h>
#include <winrt/base.h>

class Menu
{
public:
	~Menu();

	static Menu* GetSingleton()
	{
		static Menu menu;
		return &menu;
	}

	bool initialized = false;

	void Load(json& o_json);
	void Save(json& o_json);

	void Init();
	void DrawSettings();
	void DrawOverlay();
	void DrawPerfOverlay();
	void DrawWeatherDetailsWindow();

	void ProcessInputEvents(RE::InputEvent* const* a_events);
	bool ShouldSwallowInput();

	// Used for resetting input keys to solve alt-tab stuck issue
	std::atomic<bool> focusChanged = false;
	void OnFocusChanged();

	struct Constants
	{
		static constexpr std::uint16_t KEY_PRESSED_MASK = 0x8000;
		static constexpr float DEFAULT_SCREEN_HEIGHT = 1080.0f; // Default screen resolution to use for subsequent calculations
		static constexpr float DEFAULT_FONT_RATIO = 0.025f; // Default 2.5% of screen height
		static constexpr float MIN_FONT_SIZE = 16.0f; // ~1.5% @ 1080px height
		static constexpr float MAX_FONT_SIZE = 108.0f; // 5.0% @ 2160px height
		static constexpr float DEFAULT_FONT_SIZE = 27.0f;
		static constexpr int FCONF_OVERSAMPLE_H = 3; // ImGui default = 2
		static constexpr int FCONF_OVERSAMPLE_V = 2; // ImGui default = 1
		static constexpr bool FCONF_PIXELSNAP_H = true; // ImGui default = false
		static constexpr float FCONF_RASTERIZER_MULTIPLY = 1.1f; // ImGui default = 1.0f. "Linearly brighten (>1.0f) or darken (<1.0f) font output."
	};

	// UI icon textures
	struct UIIcon
	{
		ID3D11ShaderResourceView* texture = nullptr;
		ImVec2 size = ImVec2(32.0f, 32.0f);

		void Release()
		{
			if (texture) {
				texture->Release();
				texture = nullptr;
			}
		}
	};
	struct UIIcons
	{
		UIIcon saveSettings;
		UIIcon loadSettings;
		UIIcon clearCache;
		UIIcon clearDiskCache;
		UIIcon logo;  // New logo icon
	} uiIcons;

	struct ThemeSettings
	{
		float FontSize = Constants::DEFAULT_FONT_SIZE;
		float GlobalScale = REL::Module::IsVR() ? -0.5f : 0.f;  // exponential

		bool UseSimplePalette = true;    // simple palette or full customization
		bool ShowActionIcons = true;     // whether to show action buttons as icons
		float TooltipHoverDelay = 0.5f;  // tooltip hover delay in seconds
		struct PaletteColors
		{
			ImVec4 Background{ 0.f, 0.f, 0.f, 0.5882353186607361f };
			ImVec4 Text{ 1.f, 1.f, 1.f, 1.f };
			ImVec4 Border{ 0.5882353186607361f, 0.5882353186607361f, 0.5882353186607361f, 0.5882353186607361f };
		} Palette;
		struct StatusPaletteColors
		{
			ImVec4 Disable{ 0.5f, 0.5f, 0.5f, 1.f };
			ImVec4 Error{ 1.f, 0.5f, 0.5f, 1.f };
			ImVec4 Warning{ 1.0f, 0.6f, 0.2f, 1.0f };
			ImVec4 RestartNeeded{ 0.5f, 1.f, 0.5f, 1.f };
			ImVec4 CurrentHotkey{ 1.f, 1.f, 0.f, 1.f };
			ImVec4 SuccessColor{ 0.0f, 1.0f, 0.0f, 1.0f };
			ImVec4 InfoColor{ 0.0f, 0.5f, 1.0f, 1.0f };
		} StatusPalette;
		struct FeatureHeadingColors
		{
			ImVec4 ColorDefault{ 0.47f, 0.47f, 0.47f, 1.00f };  // ~120, 120, 120
			ImVec4 ColorHovered{ 0.39f, 0.39f, 0.39f, 1.00f };  // ~100, 100, 100
			float MinimizedFactor = 0.7f;                       // 70% of original alpha for when the header is minimized
		} FeatureHeading;

		ImGuiStyle Style = []() {
			ImGuiStyle style = {};
			style.WindowBorderSize = 3.f;
			style.ChildBorderSize = 0.f;
			style.FrameBorderSize = 1.5f;
			style.WindowPadding = { 16.f, 16.f };
			style.WindowRounding = 0.f;
			style.IndentSpacing = 8.f;
			style.FramePadding = { 4.0f, 4.0f };
			style.CellPadding = { 16.f, 2.f };
			style.ItemSpacing = { 8.f, 12.f };
			return std::move(style);
		}();
		// Theme by @Maksasj, edited by FiveLimbedCat
		// url: https://github.com/ocornut/imgui/issues/707#issuecomment-1494706165
		std::array<ImVec4, ImGuiCol_COUNT> FullPalette = {
			ImVec4(0.9f, 0.9f, 0.9f, 0.9f),
			ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
			ImVec4(0.1f, 0.1f, 0.15f, 1.0f),
			ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
			ImVec4(0.05f, 0.05f, 0.1f, 0.85f),
			ImVec4(0.7f, 0.7f, 0.7f, 0.65f),
			ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
			ImVec4(0.0f, 0.0f, 0.0f, 1.0f),
			ImVec4(0.9f, 0.8f, 0.8f, 0.4f),
			ImVec4(0.9f, 0.65f, 0.65f, 0.45f),
			ImVec4(0.0f, 0.0f, 0.0f, 0.83f),
			ImVec4(0.0f, 0.0f, 0.0f, 0.87f),
			ImVec4(0.4f, 0.4f, 0.8f, 0.2f),
			ImVec4(0.01f, 0.01f, 0.02f, 0.8f),
			ImVec4(0.2f, 0.25f, 0.3f, 0.6f),
			ImVec4(0.55f, 0.53f, 0.55f, 0.51f),
			ImVec4(0.56f, 0.56f, 0.56f, 1.0f),
			ImVec4(0.56f, 0.56f, 0.56f, 0.91f),
			ImVec4(0.9f, 0.9f, 0.9f, 0.83f),
			ImVec4(0.7f, 0.7f, 0.7f, 0.62f),
			ImVec4(0.3f, 0.3f, 0.3f, 0.84f),
			ImVec4(0.48f, 0.72f, 0.89f, 0.49f),
			ImVec4(0.5f, 0.69f, 0.99f, 0.68f),
			ImVec4(0.8f, 0.5f, 0.5f, 1.0f),
			ImVec4(0.3f, 0.69f, 1.0f, 0.53f),
			ImVec4(0.44f, 0.61f, 0.86f, 1.0f),
			ImVec4(0.38f, 0.62f, 0.83f, 1.0f),
			ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
			ImVec4(0.7f, 0.6f, 0.6f, 1.0f),
			ImVec4(0.9f, 0.7f, 0.7f, 1.0f),
			ImVec4(1.0f, 1.0f, 1.0f, 0.85f),
			ImVec4(1.0f, 1.0f, 1.0f, 0.6f),
			ImVec4(1.0f, 1.0f, 1.0f, 0.9f),
			ImVec4(0.4f, 0.52f, 0.67f, 0.84f),  // Tab
			ImVec4(0.0f, 0.46f, 1.0f, 0.8f),    // TabHovered
			ImVec4(0.2f, 0.41f, 0.68f, 1.0f),   // TabActive
			ImVec4(0.07f, 0.1f, 0.15f, 0.97f),  // TabUnfocused
			ImVec4(0.13f, 0.26f, 0.42f, 1.0f),  // TabUnfocusedActive
			ImVec4(0.7f, 0.6f, 0.6f, 0.5f),     // DockingPreview
			ImVec4(0.0f, 0.0f, 0.0f, 0.0f),     // DockingEmptyBg
			ImVec4(1.0f, 1.0f, 1.0f, 1.0f),     // PlotLines
			ImVec4(0.0f, 0.87f, 1.0f, 1.0f),
			ImVec4(0.22f, 0.26f, 0.7f, 1.0f),
			ImVec4(0.8f, 0.26f, 0.26f, 1.0f),
			ImVec4(0.48f, 0.72f, 0.89f, 0.49f),
			ImVec4(0.3f, 0.3f, 0.35f, 1.0f),
			ImVec4(0.23f, 0.23f, 0.25f, 1.0f),
			ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
			ImVec4(1.0f, 1.0f, 1.0f, 0.06f),
			ImVec4(0.0f, 0.0f, 1.0f, 0.35f),  // TextSelectedBg
			ImVec4(0.8f, 0.5f, 0.5f, 1.0f),
			ImVec4(0.44f, 0.61f, 0.86f, 1.0f),
			ImVec4(0.3f, 0.3f, 0.3f, 0.56f),
			ImVec4(0.2f, 0.2f, 0.2f, 0.35f),
			ImVec4(0.2f, 0.2f, 0.2f, 0.35f),
		};
	};

	struct Settings
	{
		uint32_t ToggleKey = VK_END;
		uint32_t SkipCompilationKey = VK_ESCAPE;
		uint32_t EffectToggleKey = VK_MULTIPLY;  // toggle all effects
		ThemeSettings Theme;

		struct PerfOverlaySettings
		{
			bool Enabled = false;
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
			uint32_t OverlayToggleKey = VK_F10;
		} PerfOverlay;

		struct WeatherDetailsWindowSettings
		{
			bool Enabled = false;
			ImVec2 Position = ImVec2(50.f, 50.f);
			bool PositionSet = false;
		} WeatherDetailsWindow;
	};
	const ThemeSettings& GetTheme() const { return settings.Theme; }  // Provide read-only access to the Theme.
	Settings& GetSettings() { return settings; }                      // Provide access to settings for other components

	void SelectFeatureMenu(const std::string& featureName);
	static std::unordered_map<std::string, int> categoryCounts;  // Number of features in each feature category

	// Static utility functions
	static const char* KeyIdToString(uint32_t key);

private:
	Settings settings;

	bool pendingFontChange = false; // Tracks whether font has been modified and may require reloading
	void ReloadFont(); // Credit to user patchuli: https://github.com/Patchu1i/ModExplorerMenu/tree/master

	// Menu navigation
	std::string pendingFeatureSelection;  // Feature to select on next frame

	uint32_t priorShaderKey = VK_PRIOR;  // used for blocking shaders in debugging
	uint32_t nextShaderKey = VK_NEXT;    // used for blocking shaders in debugging

	bool settingToggleKey = false;
	bool settingSkipCompilationKey = false;
	bool settingsEffectsToggle = false;
	bool settingOverlayToggleKey = false;
	uint32_t testInterval = 0;     // Seconds to wait before toggling user/test settings
	bool inTestMode = false;       // Whether we're in test mode
	bool usingTestConfig = false;  // Whether we're using the test config

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
		float SetTextScale(Settings::PerfOverlaySettings& settings);
		void UpdateGraphValues(Settings::PerfOverlaySettings& settings);
		void UpdateFrameTimeHistorySizes(Settings::PerfOverlaySettings& settings);
		void UpdateMinFrameTime();
		void UpdateMaxFrameTime();
		void UpdateFGFrameTime(Settings::PerfOverlaySettings& settings);
		void DrawPostFGFrameTimeGraph(Settings::PerfOverlaySettings& settings);
		void DrawDrawCalls();
		void DrawFPS(Settings::PerfOverlaySettings& settings);
		void DrawVRAM(winrt::com_ptr<IDXGIAdapter3> dxgiAdapter3);
	} perfOverlayState;

	std::chrono::steady_clock::time_point lastTestSwitch = std::chrono::steady_clock::now();  // Time of last test switch

	Menu() = default;
	void SetupImGuiStyle() const;
	const ImGuiKey VirtualKeyToImGuiKey(WPARAM vkKey);

	void DrawGeneralSettings();
	void DrawAdvancedSettings();
	void DrawDisplaySettings();
	void DrawDisableAtBootSettings();
	void DrawFooter();
	void DrawPerformanceOverlaySettings();
	void BuildCategoryCounts();

	class CharEvent : public RE::InputEvent
	{
	public:
		uint32_t keyCode;  // 18 (ascii code)
	};

	struct KeyEvent
	{
		explicit KeyEvent(const RE::ButtonEvent* a_event) :
			keyCode(a_event->GetIDCode()),
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()),
			value(a_event->Value()),
			heldDownSecs(a_event->HeldDuration()) {}

		explicit KeyEvent(const CharEvent* a_event) :
			keyCode(a_event->keyCode),
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()) {}

		[[nodiscard]] constexpr bool IsPressed() const noexcept { return value > 0.0F; }
		[[nodiscard]] constexpr bool IsRepeating() const noexcept { return heldDownSecs > 0.0F; }
		[[nodiscard]] constexpr bool IsDown() const noexcept { return IsPressed() && (heldDownSecs == 0.0F); }
		[[nodiscard]] constexpr bool IsHeld() const noexcept { return IsPressed() && IsRepeating(); }
		[[nodiscard]] constexpr bool IsUp() const noexcept { return (value == 0.0F) && IsRepeating(); }

		uint32_t keyCode;
		RE::INPUT_DEVICE device;
		RE::INPUT_EVENT_TYPE eventType;
		float value = 0;
		float heldDownSecs = 0;
	};
	const uint32_t DIKToVK(uint32_t DIK);
	mutable std::shared_mutex _inputEventMutex;
	std::vector<KeyEvent> _keyEventQueue{};
	void addToEventQueue(KeyEvent e);
	void ProcessInputEventQueue();
	winrt::com_ptr<IDXGIAdapter3> dxgiAdapter3;
};