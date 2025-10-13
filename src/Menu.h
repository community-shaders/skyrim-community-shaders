
#pragma once
#include "Feature.h"
#include "Menu/ThemeManager.h"
#include "Utils/Serialize.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <dxgi1_4.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

using json = nlohmann::json;

struct ImFont;

class Menu
{
public:
	/**
	 * @brief Semantic font roles for hierarchical UI typography
	 *
	 * FONT ROLE SYSTEM:
	 * =================
	 * Replaces legacy single-font approach with semantic typography system.
	 * Each role can use different font family, style, and size scaling.
	 *
	 * Roles:
	 * - Body (0):       Default UI text, setting labels, general content
	 * - Heading (1):    Feature section headers
	 * - Subheading (2): Subsection headers within features
	 * - Subtitle (3):   Secondary descriptive text, tooltips
	 *
	 * Theme JSON Configuration:
	 * "FontRoles": [
	 *   { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
	 *   { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
	 *   { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
	 *   { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 }
	 * ]
	 *
	 * SizeScale multiplies the base FontSize for each role.
	 * Example: FontSize=27, Heading SizeScale=1.05 → 28.35px rendered size
	 *
	 * Migration from Legacy:
	 * Old "FontName" field auto-populates Body role on theme load.
	 * Themes without FontRoles get defaults (Jost family).
	 */
	enum class FontRole : std::uint8_t
	{
		Body = 0,    // Default UI text
		Heading,     // Feature headers
		Subheading,  // Subsection headers
		Subtitle,    // Secondary text
		Count        // Total number of roles
	};

	struct FontRoleDescriptor
	{
		std::string_view key;
		std::string_view displayName;
		float defaultScale;
	};

	static inline constexpr std::array<FontRoleDescriptor, static_cast<size_t>(FontRole::Count)> FontRoleDescriptors = {
		FontRoleDescriptor{ "Body", "Body Text", 1.0f },
		FontRoleDescriptor{ "Heading", "Headings", 1.0f },
		FontRoleDescriptor{ "Subheading", "Subheadings", 1.0f },
		FontRoleDescriptor{ "Subtitle", "Subtitles", 1.0f }
	};

	static constexpr std::string_view GetFontRoleKey(FontRole role)
	{
		return FontRoleDescriptors[static_cast<size_t>(role)].key;
	}

	static constexpr std::string_view GetFontRoleDisplayName(FontRole role)
	{
		return FontRoleDescriptors[static_cast<size_t>(role)].displayName;
	}

	static constexpr float GetFontRoleDefaultScale(FontRole role)
	{
		return FontRoleDescriptors[static_cast<size_t>(role)].defaultScale;
	}

	static std::optional<FontRole> ResolveFontRole(std::string_view key);

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

	void Load(json& o_json);
	void Save(json& o_json);

	void LoadTheme(json& o_json);
	void SaveTheme(json& o_json);

	// Multi-theme support
	std::vector<std::string> DiscoverThemes();
	bool LoadThemePreset(const std::string& themeName);
	void CreateDefaultThemes();

	void Init();
	void DrawSettings();

	// Search bar state
	std::string featureSearch;  // For left pane feature search
	void DrawOverlay();
	void DrawWeatherDetailsWindow();

	void ProcessInputEvents(RE::InputEvent* const* a_events);
	bool ShouldSwallowInput();
	std::string BuildFontSignature(float baseFontSize) const;

public:
	// Input handling flags (made public for InputEventHandler access)
	bool settingToggleKey = false;
	bool settingSkipCompilationKey = false;
	bool settingsEffectsToggle = false;
	bool settingOverlayToggleKey = false;
	uint32_t priorShaderKey = VK_PRIOR;  // used for blocking shaders in debugging
	uint32_t nextShaderKey = VK_NEXT;    // used for blocking shaders in debugging

	// Font caching (made public for ThemeManager and OverlayRenderer access)
	// Marked mutable because they're cache fields that may be updated from const methods
	float cachedFontSize = ThemeManager::Constants::DEFAULT_FONT_SIZE;  // Tracks whether font has been modified and may require reloading
	mutable std::string cachedFontName = "Jost/Jost-Regular.ttf";       // Tracks whether font file has changed and may require reloading
	std::array<std::string, static_cast<size_t>(FontRole::Count)> cachedFontFilesByRole = []() {
		std::array<std::string, static_cast<size_t>(FontRole::Count)> files{};
		auto setFile = [&files](FontRole role, std::string value) {
			files[static_cast<size_t>(role)] = std::move(value);
		};
		setFile(FontRole::Body, "Jost/Jost-Regular.ttf");
		setFile(FontRole::Heading, "Jost/Jost-Regular.ttf");
		setFile(FontRole::Subheading, "Jost/Jost-Regular.ttf");
		setFile(FontRole::Subtitle, "Jost/Jost-Regular.ttf");
		return files;
	}();
	mutable std::array<float, static_cast<size_t>(FontRole::Count)> cachedFontPixelSizesByRole = {};
	std::string cachedFontSignature;
	mutable std::array<ImFont*, static_cast<size_t>(FontRole::Count)> loadedFontRoles = {};

	// Deferred font reload system (public for SettingsTabRenderer access)
	bool pendingFontReload = false;

	// Used for resetting input keys to solve alt-tab stuck issue
	std::atomic<bool> focusChanged = false;
	void OnFocusChanged();

	struct Constants
	{
		static constexpr std::uint16_t KEY_PRESSED_MASK = 0x8000;
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
		UIIcon logo;                  // New logo icon
		UIIcon search;                // Search icon for search bars
		UIIcon featureSettingRevert;  // Feature revert settings icon

		// Social media/external link icons
		UIIcon discord;

		// Category icons
		UIIcon characters;
		UIIcon display;
		UIIcon grass;
		UIIcon lighting;
		UIIcon sky;
		UIIcon landscape;
		UIIcon water;
		UIIcon debug;
		UIIcon materials;
		UIIcon postProcessing;
	} uiIcons;

	struct ThemeSettings
	{
		struct FontRoleSettings
		{
			std::string Family;
			std::string Style;
			std::string File;
			float SizeScale = 1.0f;
		};

		float FontSize = ThemeManager::Constants::DEFAULT_FONT_SIZE;
		std::string FontName = "Jost/Jost-Regular.ttf";         // Default font file name (legacy)
		float GlobalScale = REL::Module::IsVR() ? -0.5f : 0.f;  // exponential
		std::array<FontRoleSettings, static_cast<size_t>(FontRole::Count)> FontRoles = []() {
			std::array<FontRoleSettings, static_cast<size_t>(FontRole::Count)> roles{};
			auto setRole = [&roles](FontRole role, std::string family, std::string style, std::string file, float sizeScale) {
				auto index = static_cast<size_t>(role);
				roles[index].Family = std::move(family);
				roles[index].Style = std::move(style);
				roles[index].File = std::move(file);
				roles[index].SizeScale = sizeScale;
			};

			setRole(FontRole::Body, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
			setRole(FontRole::Heading, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
			setRole(FontRole::Subheading, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
			setRole(FontRole::Subtitle, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);

			return roles;
		}();

		bool UseSimplePalette = false;   // DEPRECATED: No longer affects behavior. UI now shows both Simple and Advanced controls.
		bool ShowActionIcons = true;     // whether to show action buttons as icons
		float TooltipHoverDelay = 0.5f;  // tooltip hover delay in seconds

		// Scrollbar opacity settings
		struct ScrollbarOpacitySettings
		{
			float Background = 0.0f;     // Background of the scrollbar area
			float Thumb = 0.5f;          // The draggable thumb/grip
			float ThumbHovered = 0.75f;  // Thumb when hovered
			float ThumbActive = 0.9f;    // Thumb when being dragged
		} ScrollbarOpacity;
		struct PaletteColors
		{
			ImVec4 Background{ 0.10f, 0.10f, 0.10f, 0.80f };
			ImVec4 Text{ 1.0f, 1.0f, 1.0f, 1.0f };
			// Separated border controls for better theming granularity
			ImVec4 WindowBorder{ 0.5f, 0.5f, 0.5f, 0.8f };  // Outer window borders
			ImVec4 FrameBorder{ 0.4f, 0.4f, 0.4f, 0.7f };   // Button, slider, input field borders
			ImVec4 Separator{ 0.5f, 0.5f, 0.5f, 0.6f };     // Internal separators and dividers
			ImVec4 ResizeGrip{ 0.6f, 0.6f, 0.6f, 0.8f };    // Window resize grips
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
			float MinimizedFactor = 0.7f;  // 70% of original alpha for when the header is minimized
		} FeatureHeading;

		ImGuiStyle Style = []() {
			ImGuiStyle style = {};
			style.WindowBorderSize = 2.0f;
			style.ChildBorderSize = 0.0f;
			style.FrameBorderSize = 1.0f;
			style.WindowPadding = { 8.0f, 8.0f };
			style.WindowRounding = 12.0f;
			style.IndentSpacing = 8.0f;
			style.FramePadding = { 8.0f, 4.0f };
			style.CellPadding = { 8.0f, 2.0f };
			style.ItemSpacing = { 4.0f, 8.0f };
			style.FrameRounding = 4.0f;
			style.TabRounding = 4.0f;
			style.ScrollbarRounding = 9.0f;
			style.ScrollbarSize = 12.0f;
			style.GrabRounding = 3.0f;
			style.GrabMinSize = 12.0f;
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

	static const ThemeSettings::FontRoleSettings& GetDefaultFontRole(FontRole role);

	struct Settings
	{
		uint32_t ToggleKey = VK_END;
		uint32_t SkipCompilationKey = VK_ESCAPE;
		uint32_t EffectToggleKey = VK_MULTIPLY;  // toggle all effects
		uint32_t OverlayToggleKey = VK_F10;      // Global overlay toggle key for all overlays
		bool FirstTimeSetupCompleted = false;    // Track if first-time setup has been completed
		ThemeSettings Theme;
		std::string SelectedThemePreset = "";  // Currently selected theme preset (empty = custom/user theme)
	};
	const ThemeSettings& GetTheme() const { return settings.Theme; }                // Provide read-only access to the Theme.
	Settings& GetSettings() { return settings; }                                    // Provide access to settings for other components
	winrt::com_ptr<IDXGIAdapter3> GetDXGIAdapter3() const { return dxgiAdapter3; }  // Provide access to dxgiAdapter3
	ThemeSettings::FontRoleSettings& GetFontRoleSettings(FontRole role) { return settings.Theme.FontRoles[static_cast<size_t>(role)]; }
	const ThemeSettings::FontRoleSettings& GetFontRoleSettings(FontRole role) const { return settings.Theme.FontRoles[static_cast<size_t>(role)]; }
	ImFont* GetFont(FontRole role) const { return loadedFontRoles[static_cast<size_t>(role)]; }

	void SelectFeatureMenu(const std::string& featureName);
	static std::unordered_map<std::string, int> categoryCounts;  // Number of features in each feature category

	bool overlayVisible = false;

public:
	// Move KeyEvent struct here
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
			keyCode(0),  // For thumbstick events, keyCode/value are replaced by x/y floats
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()),
			value(0),
			heldDownSecs(0),
			thumbstickX(a_event->xValue),
			thumbstickY(a_event->yValue)
		{}
		// For thumbstick events, keyCode/value are replaced by x/y floats
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
	// VR overlay input and cursor helpers
	void ProcessVROverlayInput();

private:
	Settings settings;

	std::string cachedIniPath;  // io.IniFilename must point to a string that lives for the duration of the runtime

	// Menu navigation
	std::string pendingFeatureSelection;  // Feature to select on next frame

	// Input event handling
	std::vector<KeyEvent> _keyEventQueue;
	mutable std::shared_mutex _inputEventMutex;

	Menu() = default;

	void DrawGeneralSettings();
	void DrawAdvancedSettings();
	void DrawDisableAtBootSettings();
	void DrawFooter();
	void BuildCategoryCounts();

	void addToEventQueue(KeyEvent e);
	void ProcessInputEventQueue();
	winrt::com_ptr<IDXGIAdapter3> dxgiAdapter3;
};