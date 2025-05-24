#pragma once

#include <dxgi1_4.h>

using namespace std::chrono;
#define BUFFER_VIEWER_NODE(a_value, a_scale)                                                                 \
	if (ImGui::TreeNode(#a_value)) {                                                                         \
		ImGui::Image(a_value->srv.get(), { a_value->desc.Width * a_scale, a_value->desc.Height * a_scale }); \
		ImGui::TreePop();                                                                                    \
	}

#define BUFFER_VIEWER_NODE_BULLET(a_value, a_scale) \
	ImGui::BulletText(#a_value);                    \
	ImGui::Image(a_value->srv.get(), { a_value->desc.Width * a_scale, a_value->desc.Height * a_scale });

#define ADDRESS_NODE(a_value)                                                                        \
	if (ImGui::Button(#a_value)) {                                                                   \
		ImGui::SetClipboardText(std::format("{0:x}", reinterpret_cast<uintptr_t>(a_value)).c_str()); \
	}                                                                                                \
	if (ImGui::IsItemHovered())                                                                      \
		ImGui::SetTooltip(std::format("Copy {} Address to Clipboard", #a_value).c_str());

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

	void ProcessInputEvents(RE::InputEvent* const* a_events);
	bool ShouldSwallowInput();
	void OnFocusLost();

	struct ThemeSettings
	{
		float GlobalScale = REL::Module::IsVR() ? -0.5f : 0.f;  // exponential

		bool UseSimplePalette = true;  // simple palette or full customization
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
			ImVec4 RestartNeeded{ 0.5f, 1.f, 0.5f, 1.f };
			ImVec4 CurrentHotkey{ 1.f, 1.f, 0.f, 1.f };
		} StatusPalette;

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
	};

private:
	Settings settings;

	uint32_t priorShaderKey = VK_PRIOR;  // used for blocking shaders in debugging
	uint32_t nextShaderKey = VK_NEXT;    // used for blocking shaders in debugging

	bool settingToggleKey = false;
	bool settingSkipCompilationKey = false;
	bool settingsEffectsToggle = false;
	uint32_t testInterval = 0;     // Seconds to wait before toggling user/test settings
	bool inTestMode = false;       // Whether we're in test mode
	bool usingTestConfig = false;  // Whether we're using the test config

	std::chrono::steady_clock::time_point lastTestSwitch = high_resolution_clock::now();  // Time of last test switch

	Menu() = default;
	void SetupImGuiStyle() const;
	const char* KeyIdToString(uint32_t key);
	const ImGuiKey VirtualKeyToImGuiKey(WPARAM vkKey);

	void DrawGeneralSettings();
	void DrawAdvancedSettings();
	void DrawDisplaySettings();
	void DrawDisableAtBootSettings();
	void DrawFooter();

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