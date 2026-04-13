
#pragma once
#include "Feature.h"
#include "ImGui/imgui_impl_win32.h"
#include "Utils/Input.h"
#include <atomic>
#include <cstdint>
#include <dxgi1_4.h>
#include <nlohmann/json.hpp>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>
#include <winrt/base.h>

using json = nlohmann::json;

class Menu
{
public:
	~Menu();
	Menu(const Menu&) = delete;
	Menu& operator=(const Menu&) = delete;

	static Menu* GetSingleton()
	{
		static Menu menu;
		return &menu;
	}

	bool initialized = false;
	bool IsEnabled = false;

	// Minimal theme settings for color palettes used across features
	struct ThemeSettings
	{
		struct PaletteColors
		{
			ImVec4 Background{ 0.10f, 0.10f, 0.10f, 0.80f };
			ImVec4 Text{ 1.0f, 1.0f, 1.0f, 1.0f };
			ImVec4 WindowBorder{ 0.5f, 0.5f, 0.5f, 0.8f };
			ImVec4 FrameBorder{ 0.4f, 0.4f, 0.4f, 0.7f };
			ImVec4 Separator{ 0.5f, 0.5f, 0.5f, 0.6f };
			ImVec4 ResizeGrip{ 0.6f, 0.6f, 0.6f, 0.8f };
		} Palette;
		struct StatusPaletteColors
		{
			ImVec4 Disable{ 0.5f, 0.5f, 0.5f, 1.0f };
			ImVec4 Error{ 1.0f, 0.4f, 0.4f, 1.0f };
			ImVec4 Warning{ 1.0f, 0.6f, 0.2f, 1.0f };
			ImVec4 RestartNeeded{ 0.4f, 1.0f, 0.4f, 1.0f };
			ImVec4 CurrentHotkey{ 1.0f, 1.0f, 0.0f, 1.0f };
			ImVec4 SuccessColor{ 0.0f, 1.0f, 0.0f, 1.0f };
			ImVec4 InfoColor{ 0.2f, 0.6f, 1.0f, 1.0f };
		} StatusPalette;
		struct FeatureHeadingColors
		{
			ImVec4 ColorDefault{ 0.8f, 0.8f, 0.8f, 1.0f };
			ImVec4 ColorHovered{ 0.6f, 0.6f, 0.6f, 1.0f };
			float MinimizedFactor = 0.7f;
			float FeatureTitleScale = 1.5f;
		} FeatureHeading;
	};

	const ThemeSettings& GetTheme() const { return themeSettings; }
	void SelectFeatureMenu(const std::string& featureName) { pendingFeatureSelection = featureName; }
	winrt::com_ptr<IDXGIAdapter3> GetDXGIAdapter3() const { return dxgiAdapter3; }

	void Load(json& o_json);
	void Save(json& o_json);

	void Init();
	void DrawSettings();
	void DrawOverlay();

	void ProcessInputEvents(RE::InputEvent* const* a_events);
	bool ShouldSwallowInput();
	bool IsPreviewFlying();

	bool overlayVisible = false;

	// Input handling flags
	bool settingToggleKey = false;
	bool settingSkipCompilationKey = false;
	bool settingsEffectsToggle = false;
	bool settingOverlayToggleKey = false;
	bool settingShaderBlockPrevKey = false;
	bool settingShaderBlockNextKey = false;
	bool settingWeatherEditorToggleKey = false;

	// Used for resetting input keys to solve alt-tab stuck issue
	std::atomic<bool> focusChanged = false;
	void OnFocusChanged();

	struct Constants
	{
		static constexpr std::uint16_t KEY_PRESSED_MASK = 0x8000;
	};

	struct Settings
	{
		std::vector<InputCombo> ToggleKey = { InputCombo::Keyboard(VK_END) };
		std::vector<InputCombo> SkipCompilationKey = { InputCombo::Keyboard(VK_ESCAPE) };
		std::vector<InputCombo> EffectToggleKey = { InputCombo::Keyboard(VK_MULTIPLY) };
		std::vector<InputCombo> OverlayToggleKey = { InputCombo::Keyboard(VK_F10) };
		std::vector<InputCombo> ShaderBlockPrevKey = { InputCombo::Keyboard(VK_PRIOR) };
		std::vector<InputCombo> ShaderBlockNextKey = { InputCombo::Keyboard(VK_NEXT) };
		std::vector<InputCombo> WeatherEditorToggleKey = { InputCombo::Keyboard(VK_F11) };
		bool EnableShaderBlocking = false;
		float UIScale = 1.0f;
	};
	Settings& GetSettings() { return settings; }

	// CharEvent for text input (ported from OAR)
	class CharEvent : public RE::InputEvent
	{
	public:
		uint32_t keyCode;  // ascii code
	};

	// KeyEvent struct (ported from OAR, extended for VR thumbstick)
	struct KeyEvent
	{
		explicit KeyEvent(const RE::ButtonEvent* a_event) :
			keyCode(a_event->GetIDCode()),
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()),
			value(a_event->Value()),
			heldDownSecs(a_event->HeldDuration()),
			thumbstickX(0.0f),
			thumbstickY(0.0f) {}

		explicit KeyEvent(const CharEvent* a_event) :
			keyCode(a_event->keyCode),
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()),
			value(0),
			heldDownSecs(0),
			thumbstickX(0.0f),
			thumbstickY(0.0f) {}

		explicit KeyEvent(const RE::ThumbstickEvent* a_event) :
			keyCode(0),
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()),
			value(0),
			heldDownSecs(0),
			thumbstickX(a_event->xValue),
			thumbstickY(a_event->yValue) {}

		uint32_t keyCode;
		RE::INPUT_DEVICE device;
		RE::INPUT_EVENT_TYPE eventType;
		float value = 0;
		float heldDownSecs = 0;
		float thumbstickX = 0.0f;
		float thumbstickY = 0.0f;
		[[nodiscard]] constexpr bool IsPressed() const noexcept { return value > 0.0F; }
		[[nodiscard]] constexpr bool IsRepeating() const noexcept { return heldDownSecs > 0.0F; }
		[[nodiscard]] constexpr bool IsDown() const noexcept { return IsPressed() && (heldDownSecs == 0.0F); }
		[[nodiscard]] constexpr bool IsHeld() const noexcept { return IsPressed() && IsRepeating(); }
		[[nodiscard]] constexpr bool IsUp() const noexcept { return (value == 0.0F) && IsRepeating(); }
	};

	// VR overlay input helpers
	void ProcessVROverlayInput();

	// Input consumption (ported from OAR)
	bool ShouldConsumeInput() const;
	void AddInputConsumer();
	void RemoveInputConsumer();

private:
	Settings settings;
	ThemeSettings themeSettings;
	std::string pendingFeatureSelection;
	winrt::com_ptr<IDXGIAdapter3> dxgiAdapter3;

	Menu() = default;

	// OAR-style styling (ported from OAR UIManager)
	static ImGuiStyle GetDefaultStyle();
	void UpdateStyle();
	float _prevScale = 1.f;

	// Input state (ported from OAR)
	uint8_t _menusConsumingInput = 0;
	bool _bShiftHeld = false;
	bool _bCtrlHeld = false;
	bool _bAltHeld = false;
	bool _bFocusLost = false;

	OAR_ImGuiUserData _userData;

	// Input event handling
	std::vector<KeyEvent> _keyEventQueue;
	mutable std::shared_mutex _inputEventMutex;

	void addToEventQueue(KeyEvent e);
	void ProcessInputEventQueue();

	// Settings layout helpers
	void DrawGeneralSettings();
	void DrawAdvancedSettings();
	void DrawDisableAtBootSettings();
	void DrawFooter();

	// Navigation state
	size_t selectedMenu = 0;
	std::string featureSearch;
	std::map<std::string, bool> categoryExpansionStates;

	// Rendering helpers
	void RenderShaderCompilationStatus();
	void RenderShaderBlockingStatus();
	void RenderFeatureOverlays();
	void HandleABTesting();

	// OAR-style ScaleAllSizes (ported from OAR UICommon)
	static void ScaleAllSizes(ImGuiStyle& style, float scaleFactor);
};
