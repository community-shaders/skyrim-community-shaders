#include "Menu.h"

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "DX12SwapChain.h"
#include "Deferred.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "ShaderCache.h"
#include "State.h"
#include "Streamline.h"
#include "TruePBR.h"
#include "Upscaling.h"
#include "Util.h"
#include "Utils/UI.h"

#include "Features/LightLimitFix/ParticleLights.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::PaletteColors,
	Background,
	Text,
	Border)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::StatusPaletteColors,
	Disable,
	Error,
	Warning,
	RestartNeeded,
	CurrentHotkey,
	SuccessColor,
	InfoColor)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::FeatureHeadingColors,
	LineColorDefault,
	LineColorHovered,
	TextColorDefault,
	TextColorHovered,
	TextColorWhite)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::Settings::PerfOverlaySettings,
	Enabled,
	ShowDrawCalls,
	ShowVRAM,
	ShowFPS,
	ShowPreFGFrameTime,
	ShowPreFGFrameTimeGraph,
	ShowPreFGFPS,
	ShowPostFGFPS,
	ShowPostFGFrameTime,
	ShowPostFGFrameTimeGraph,
	UpdateInterval,
	FrameHistorySize,
	Size,
	BackgroundOpacity,
	ShowBorder,
	Position,
	PositionSet,
	OverlayToggleKey)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ImGuiStyle,
	WindowPadding,
	WindowRounding,
	WindowBorderSize,
	WindowMinSize,
	ChildRounding,
	ChildBorderSize,
	PopupRounding,
	PopupBorderSize,
	FramePadding,
	FrameRounding,
	FrameBorderSize,
	ItemSpacing,
	ItemInnerSpacing,
	CellPadding,
	IndentSpacing,
	ColumnsMinSpacing,
	ScrollbarSize,
	ScrollbarRounding,
	GrabMinSize,
	GrabRounding,
	LogSliderDeadzone,
	TabRounding,
	TabBorderSize,
	TabMinWidthForCloseButton,
	TabBarBorderSize,
	TableAngledHeadersAngle,
	ColorButtonPosition,
	ButtonTextAlign,
	SelectableTextAlign,
	SeparatorTextBorderSize,
	SeparatorTextAlign,
	SeparatorTextPadding,
	DockingSeparatorSize,
	MouseCursorScale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings,
	GlobalScale,
	UseSimplePalette,
	ShowActionIcons,
	Palette,
	StatusPalette,
	FeatureHeading,
	Style,
	FullPalette)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::Settings,
	ToggleKey,
	SkipCompilationKey,
	EffectToggleKey,
	Theme,
	PerfOverlay)

void Menu::SetupImGuiStyle() const
{
	auto& style = ImGui::GetStyle();
	auto& colors = style.Colors;

	// Theme based on https://github.com/powerof3/DialogueHistory
	auto& themeSettings = settings.Theme;

	// rescale here
	auto styleCopy = themeSettings.Style;
	styleCopy.ScaleAllSizes(exp2(settings.Theme.GlobalScale));
	styleCopy.MouseCursorScale = 1.f;
	style = styleCopy;

	if (themeSettings.UseSimplePalette) {
		float hovoredAlpha{ 0.1f };

		ImVec4 resizeGripHovered = themeSettings.Palette.Border;
		resizeGripHovered.w = hovoredAlpha;

		ImVec4 textDisabled = themeSettings.Palette.Text;
		textDisabled.w = 0.3f;

		ImVec4 header{ 1.0f, 1.0f, 1.0f, 0.15f };
		ImVec4 headerHovered = header;
		headerHovered.w = hovoredAlpha;

		ImVec4 tabHovered{ 0.2f, 0.2f, 0.2f, 1.0f };

		ImVec4 sliderGrab{ 1.0f, 1.0f, 1.0f, 0.245f };
		ImVec4 sliderGrabActive{ 1.0f, 1.0f, 1.0f, 0.531f };

		ImVec4 scrollbarGrab{ 0.31f, 0.31f, 0.31f, 1.0f };
		ImVec4 scrollbarGrabHovered{ 0.41f, 0.41f, 0.41f, 1.0f };
		ImVec4 scrollbarGrabActive{ 0.51f, 0.51f, 0.51f, 1.0f };

		colors[ImGuiCol_WindowBg] = themeSettings.Palette.Background;
		colors[ImGuiCol_ChildBg] = ImVec4();
		colors[ImGuiCol_ScrollbarBg] = ImVec4();
		colors[ImGuiCol_TableHeaderBg] = ImVec4();
		colors[ImGuiCol_TableRowBg] = ImVec4();
		colors[ImGuiCol_TableRowBgAlt] = ImVec4();

		colors[ImGuiCol_Border] = themeSettings.Palette.Border;
		colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
		colors[ImGuiCol_ResizeGrip] = colors[ImGuiCol_Border];
		colors[ImGuiCol_ResizeGripHovered] = resizeGripHovered;
		colors[ImGuiCol_ResizeGripActive] = colors[ImGuiCol_ResizeGripHovered];

		colors[ImGuiCol_Text] = themeSettings.Palette.Text;
		colors[ImGuiCol_TextDisabled] = textDisabled;

		colors[ImGuiCol_FrameBg] = themeSettings.Palette.Background;
		colors[ImGuiCol_FrameBgHovered] = headerHovered;
		colors[ImGuiCol_FrameBgActive] = colors[ImGuiCol_FrameBg];

		colors[ImGuiCol_DockingEmptyBg] = themeSettings.Palette.Border;
		colors[ImGuiCol_DockingPreview] = themeSettings.Palette.Border;

		colors[ImGuiCol_PlotHistogram] = themeSettings.Palette.Border;

		colors[ImGuiCol_SliderGrab] = sliderGrab;
		colors[ImGuiCol_SliderGrabActive] = sliderGrabActive;

		colors[ImGuiCol_Header] = header;
		colors[ImGuiCol_HeaderActive] = colors[ImGuiCol_Header];
		colors[ImGuiCol_HeaderHovered] = headerHovered;

		colors[ImGuiCol_Button] = ImVec4();
		colors[ImGuiCol_ButtonHovered] = headerHovered;
		colors[ImGuiCol_ButtonActive] = ImVec4();

		colors[ImGuiCol_ScrollbarGrab] = scrollbarGrab;
		colors[ImGuiCol_ScrollbarGrabHovered] = scrollbarGrabHovered;
		colors[ImGuiCol_ScrollbarGrabActive] = scrollbarGrabActive;

		colors[ImGuiCol_TitleBg] = themeSettings.Palette.Background;
		colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_TitleBg];
		colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];

		colors[ImGuiCol_Tab] = ImVec4();
		colors[ImGuiCol_TabHovered] = tabHovered;
		colors[ImGuiCol_TabActive] = colors[ImGuiCol_TabHovered];
		colors[ImGuiCol_TabUnfocused] = colors[ImGuiCol_Tab];
		colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_TabHovered];

		colors[ImGuiCol_CheckMark] = themeSettings.Palette.Text;

		colors[ImGuiCol_NavHighlight] = ImVec4();
	} else {
		std::copy(themeSettings.FullPalette.begin(), themeSettings.FullPalette.end(), std::span(colors).begin());
	}
}

bool IsEnabled = false;
std::unordered_map<std::string, int> Menu::categoryCounts;

Menu::~Menu()
{  // Release icon textures if loaded
	uiIcons.saveSettings.Release();
	uiIcons.loadSettings.Release();
	uiIcons.clearCache.Release();
	uiIcons.clearDiskCache.Release();
	uiIcons.logo.Release();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	dxgiAdapter3 = nullptr;
}

void Menu::Load(json& o_json)
{
	settings = o_json;
}

void Menu::Save(json& o_json)
{
	o_json = settings;
}

#define IM_VK_KEYPAD_ENTER (VK_RETURN + 256)

void Menu::Init()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	auto& imgui_io = ImGui::GetIO();
	imgui_io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
	imgui_io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset;

	// Enhanced font configuration for sharper text rendering
	ImFontConfig font_config;
	font_config.GlyphExtraSpacing.x = 0.0f;  // Neutral spacing for cleaner look
	font_config.OversampleH = 3;             // Increased horizontal oversampling for sharper text
	font_config.OversampleV = 2;             // Increased vertical oversampling
	font_config.PixelSnapH = true;           // Align to pixel grid for sharper rendering
	font_config.RasterizerMultiply = 1.1f;   // Slightly darker font rendering
	font_config.FontBuilderFlags = 0;        // No additional flags needed

	// Add high-quality font with improved settings
	imgui_io.Fonts->AddFontFromFileTTF("Data\\Interface\\CommunityShaders\\Fonts\\Jost-Regular.ttf", 36, &font_config);

	DXGI_SWAP_CHAIN_DESC desc;
	globals::d3d::swapChain->GetDesc(&desc);

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(desc.OutputWindow);
	ImGui_ImplDX11_Init(globals::d3d::device, globals::d3d::context);

	auto& io = ImGui::GetIO();
	io.FontGlobalScale = exp2(settings.Theme.GlobalScale);

	{
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (!FAILED(globals::d3d::device->QueryInterface(dxgiDevice.put()))) {
			winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
			if (!FAILED(dxgiDevice->GetAdapter(dxgiAdapter.put()))) {
				dxgiAdapter->QueryInterface(dxgiAdapter3.put());
			}
		}
	}
	// Load UI icons
	if (!Util::InitializeMenuIcons(this)) {
		logger::warn("Failed to load UI icons. Will fallback to text buttons");
	}

	BuildCategoryCounts();

	initialized = true;
}

void Menu::DrawSettings()
{
	ImGui::DockSpaceOverViewport(NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::SetNextWindowPos(Util::GetNativeViewportSizeScaled(0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Util::GetNativeViewportSizeScaled(0.8f), ImGuiCond_FirstUseEver);

	auto title = std::format("Community Shaders {}", Util::GetFormattedVersion(Plugin::VERSION));
	ImGui::Begin(title.c_str(), &IsEnabled, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
	{
		auto shaderCache = globals::shaderCache;
		const float iconSize = 48.0f;
		const ImVec2 buttonSize(iconSize, iconSize);  // No padding for header icons
		// Check if we can show icons - require setting enabled and at least some icons loaded
		bool canShowIcons = settings.Theme.ShowActionIcons &&
		                    (uiIcons.saveSettings.texture ||
								uiIcons.loadSettings.texture ||
								uiIcons.clearCache.texture ||
								uiIcons.clearDiskCache.texture);

		// Always show logo if available, regardless of action icons setting
		bool showLogo = uiIcons.logo.texture != nullptr;

		// Begin a layout - with or without action buttons depending on settings
		if ((showLogo || canShowIcons) && ImGui::BeginTable("##HeaderLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableNextColumn();  // Title on the left with logo
			if (!ImGui::IsWindowDocked()) {
				const float textScaleFactor = 1.7f;
				const float logoHeightScale = 1.3f;
				const float titleHeight = ImGui::GetFontSize() * logoHeightScale;

				// Always display logo if texture is available
				if (showLogo) {
					float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
					ImVec2 logoSize(titleHeight * logoAspectRatio, titleHeight);

					// Add a bit of padding before the logo and text
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);

					// Use our helper to render aligned logo and text with perfect vertical alignment
					Util::DrawAlignedTextWithLogo(
						uiIcons.logo.texture,
						logoSize,
						title.c_str(),
						textScaleFactor);
				} else {
					// No logo, just render the text with proper alignment
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);
					Util::DrawSharpText(title.c_str(), true, textScaleFactor);
					ImGui::PopStyleVar();
				}
			}  // Buttons on the right
			ImGui::TableNextColumn();
			// Only show action buttons if canShowIcons is true
			if (canShowIcons) {
				// Create a horizontal layout for the buttons and remove button borders
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));             // Tighter spacing for the icons
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);                       // Remove button borders
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));                     // Transparent button background
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.7f, 0.7f, 0.2f));  // Subtle hover effect

				// Save Settings Button
				if (uiIcons.saveSettings.texture) {
					if (ImGui::ImageButton("##SaveSettingsBtn", uiIcons.saveSettings.texture, buttonSize)) {
						globals::state->Save();
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Save Settings");
					}
					ImGui::SameLine();
				}

				// Load Settings Button
				if (uiIcons.loadSettings.texture) {
					if (ImGui::ImageButton("##LoadSettingsBtn", uiIcons.loadSettings.texture, buttonSize)) {
						globals::state->Load();
						globals::features::llf::particleLights->GetConfigs();
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Load Settings");
					}
					ImGui::SameLine();
				}

				// Clear Shader Cache Button
				if (uiIcons.clearCache.texture) {
					if (ImGui::ImageButton("##ClearShaderCacheBtn", uiIcons.clearCache.texture, buttonSize)) {
						shaderCache->Clear();
						// any features should be added to shadercache's clear.
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(
							"Clear Shader Cache\n\n"
							"The Shader Cache is the collection of compiled shaders which replace the vanilla shaders at runtime. "
							"Clearing the shader cache will mean that shaders are recompiled only when the game re-encounters them. "
							"This is only needed for hot-loading shaders for development purposes. ");
					}
					ImGui::SameLine();
				}

				// Clear Disk Cache Button
				if (uiIcons.clearDiskCache.texture) {
					if (ImGui::ImageButton("##ClearDiskCacheBtn", uiIcons.clearDiskCache.texture, buttonSize)) {
						shaderCache->DeleteDiskCache();
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(
							"Clear Disk Cache\n\n"
							"The Disk Cache is a collection of compiled shaders on disk, which are automatically created when shaders are added to the Shader Cache. "
							"If you do not have a Disk Cache, or it is outdated or invalid, you will see \"Compiling Shaders\" in the upper-left corner. "
							"After this has completed you will no longer see this message apart from when loading from the Disk Cache. "
							"Only delete the Disk Cache manually if you are encountering issues. ");
					}
				}
				// Restore default style only if we pushed styles
				ImGui::PopStyleVar(2);    // Pop both style variables: ItemSpacing and FrameBorderSize
				ImGui::PopStyleColor(2);  // Pop both style colors: Button and ButtonHovered
			}  // End of canShowIcons action buttons section

			ImGui::EndTable();
		} else if (!(showLogo || canShowIcons)) {
			// No icons available - show just the title without the table layout
			if (!ImGui::IsWindowDocked()) {
				ImGui::SetWindowFontScale(1.5f);
				ImGui::TextUnformatted(title.c_str());
				ImGui::SetWindowFontScale(1.0f);
			}
		}
		// First separator - always shown
		if (!ImGui::IsWindowDocked()) {
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
			ImGui::Spacing();
		}

		// If icons are disabled or missing textures, show action buttons as text between separators
		if (!canShowIcons) {
			if (ImGui::BeginTable("##ActionButtons", 4, ImGuiTableFlags_SizingStretchSame)) {
				// Save Settings Button
				ImGui::TableNextColumn();
				if (ImGui::Button("Save Settings", { -1, 0 })) {
					globals::state->Save();
				}

				// Load Settings Button
				ImGui::TableNextColumn();
				if (ImGui::Button("Load Settings", { -1, 0 })) {
					globals::state->Load();
					globals::features::llf::particleLights->GetConfigs();
				}

				// Clear Shader Cache Button
				ImGui::TableNextColumn();
				if (ImGui::Button("Clear Shader Cache", { -1, 0 })) {
					shaderCache->Clear();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"The Shader Cache is the collection of compiled shaders which replace the vanilla shaders at runtime. "
						"Clearing the shader cache will mean that shaders are recompiled only when the game re-encounters them. "
						"This is only needed for hot-loading shaders for development purposes. ");
				}

				// Clear Disk Cache Button
				ImGui::TableNextColumn();
				if (ImGui::Button("Clear Disk Cache", { -1, 0 })) {
					shaderCache->DeleteDiskCache();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"The Disk Cache is a collection of compiled shaders on disk, which are automatically created when shaders are added to the Shader Cache. "
						"If you do not have a Disk Cache, or it is outdated or invalid, you will see \"Compiling Shaders\" in the upper-left corner. "
						"After this has completed you will no longer see this message apart from when loading from the Disk Cache. "
						"Only delete the Disk Cache manually if you are encountering issues. ");
				}

				// Error message toggle if needed
				if (shaderCache->GetFailedTasks()) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (ImGui::Button("Toggle Error Message", { -1, 0 })) {
						shaderCache->ToggleErrorMessages();
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(
							"Hide or show the shader failure message. "
							"Your installation is broken and will likely see errors in game. "
							"Please double check you have updated all features and that your load order is correct. "
							"See CommunityShaders.log for details and check the Nexus Mods page or Discord server. ");
					}
				}

				ImGui::EndTable();
			}

			// Second separator - only shown if icons are disabled/missing or if there are failed tasks
			if (!ImGui::IsWindowDocked()) {
				ImGui::Spacing();
				ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
				ImGui::Spacing();
			}
		} else if (shaderCache->GetFailedTasks()) {
			// If icons are enabled but there are failed tasks, show error toggle button
			// and add the second separator
			if (ImGui::Button("Toggle Error Message", { -1, 0 })) {
				shaderCache->ToggleErrorMessages();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Hide or show the shader failure message. "
					"Your installation is broken and will likely see errors in game. "
					"Please double check you have updated all features and that your load order is correct. "
					"See CommunityShaders.log for details and check the Nexus Mods page or Discord server. ");
			}

			// Add second separator when showing error button
			if (!ImGui::IsWindowDocked()) {
				ImGui::Spacing();
				ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
				ImGui::Spacing();
			}
		} else {  // No additional separator needed - already handled in the conditional block above
		}

		// Main content starts here - no additional separator needed as it's already handled in the conditions above

		float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3 + 3.0f;  // text + separator

		ImGui::BeginChild("Menus Table", ImVec2(0, -footer_height));
		if (ImGui::BeginTable("Menus Table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
			ImGui::TableSetupColumn("##ListOfMenus", 0, 2);
			ImGui::TableSetupColumn("##MenuConfig", 0, 8);

			static size_t selectedMenu = 0;  // some type erasure bs for virtual-free polymorphism
			struct BuiltInMenu
			{
				std::string name;
				std::function<void()> func;
			};
			struct CategoryHeader
			{
				std::string name;
			};
			using MenuFuncInfo = std::variant<BuiltInMenu, std::string, CategoryHeader, Feature*>;

			// Static storage for category expansion states
			static std::map<std::string, bool> categoryExpansionStates;

			struct ListMenuVisitor
			{
				size_t listId;
				size_t& selectedMenuRef;

				void operator()(const BuiltInMenu& menu)
				{
					// Use error color for Feature Issues menu item
					bool isFeatureIssues = (menu.name == "Feature Issues");
					if (isFeatureIssues) {
						auto& themeSettings = globals::menu->settings.Theme;
						ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Error);
					}

					if (ImGui::Selectable(fmt::format(" {} ", menu.name).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns))
						selectedMenuRef = listId;

					if (isFeatureIssues) {
						ImGui::PopStyleColor();
					}
				}
				void operator()(const std::string& label)
				{
					// Style "Unloaded Features" to match category headers
					if (label == "Unloaded Features") {
						Util::DrawSectionHeader(label.c_str(), true);
					} else {
						// Use default separator text for other labels
						ImGui::SeparatorText(label.c_str());
					}
				}
				void operator()(const CategoryHeader& header)
				{
					// Get expansion state from static map
					bool isExpanded = categoryExpansionStates[header.name];

					// Draw category header with custom styling using util:UI function
					int count = categoryCounts[std::string(header.name)];
					Util::DrawCategoryHeader(header.name.c_str(), isExpanded, count);

					// Update expansion state
					categoryExpansionStates[header.name] = isExpanded;
				}
				void operator()(Feature* feat)
				{
					const auto featureName = feat->GetShortName();
					bool isDisabled = globals::state->IsFeatureDisabled(featureName);
					bool isLoaded = feat->loaded;
					bool hasFailedMessage = !feat->failedLoadedMessage.empty();
					auto& themeSettings = globals::menu->settings.Theme;

					ImVec4 textColor;

					// Determine the text color based on the state
					if (isDisabled) {
						textColor = themeSettings.StatusPalette.Disable;
					} else if (isLoaded) {
						textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
					} else if (hasFailedMessage) {
						textColor = feat->version.empty() ? themeSettings.StatusPalette.Disable : themeSettings.StatusPalette.Error;
					} else {
						textColor = themeSettings.StatusPalette.RestartNeeded;
					}

					// Set text color
					ImGui::PushStyleColor(ImGuiCol_Text, textColor);

					// Create selectable item
					if (ImGui::Selectable(fmt::format(" {} ", feat->GetName()).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns)) {
						selectedMenuRef = listId;
					}

					// Restore original text color
					ImGui::PopStyleColor();

					// Display version if loaded
					if (isLoaded) {
						ImGui::SameLine();
						ImGui::TextDisabled(fmt::format("({})", feat->version).c_str());
					}
				}
			};
			struct DrawMenuVisitor
			{
				void operator()(const BuiltInMenu& menu)
				{
					if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, true)) {
						menu.func();
					}
					ImGui::EndChild();
				}
				void operator()(const std::string&)
				{
					// std::unreachable() from c++23
					// you are not supposed to have selected a label!
				}
				void operator()(const CategoryHeader&)
				{
					// Category headers are not selectable in the right panel
					ImGui::TextDisabled("Please select a feature from the left.");
				}
				void operator()(Feature* feat)
				{
					const auto featureName = feat->GetShortName();
					bool isDisabled = globals::state->IsFeatureDisabled(featureName);
					bool isLoaded = feat->loaded;
					bool hasFailedMessage = !feat->failedLoadedMessage.empty();
					auto& themeSettings = globals::menu->settings.Theme;

					if (ImGui::BeginTabBar("##FeatureTabs")) {
						if (ImGui::BeginTabItem("Settings")) {
							if (ImGui::BeginChild("##FeatureSettingsFrame", { 0, 0 }, true)) {
								ImGui::SeparatorText("Feature Management");
								
								// Disable/Enable at boot
								ImVec4 textColor;
								if (isDisabled) {
									textColor = themeSettings.StatusPalette.Disable;
								} else if (hasFailedMessage) {
									textColor = themeSettings.StatusPalette.Error;
								} else {
									textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
								}
								ImGui::PushStyleColor(ImGuiCol_Text, textColor);
								if (ImGui::Button(isDisabled ? "Enable at Boot" : "Disable at Boot", { -1, 0 })) {
									bool newState = feat->ToggleAtBootSetting();
									logger::info("{}: {} at boot.", featureName, newState ? "Enabled" : "Disabled");
								}
								ImGui::PopStyleColor();
								if (auto _tt = Util::HoverTooltipWrapper()) {
									ImGui::Text(
										"Current State: %s\n"
										"%s the feature settings at boot. "
										"Restart will be required to reenable. "
										"This is the same as deleting the ini file. "
										"This should remove any performance impact for the feature.",
										isDisabled ? "Disabled" : "Enabled",
										isDisabled ? "Enable" : "Disable");
								}

								// Restore Defaults buttons shows when feature is not disabled and is loaded
								if (!isDisabled && isLoaded) {
									ImGui::Spacing();
									if (ImGui::Button("Restore Defaults", { -1, 0 })) {
										feat->RestoreDefaultSettings();
									}
									if (auto _tt = Util::HoverTooltipWrapper()) {
										ImGui::Text(
											"Restores the feature's settings back to their default values. "
											"You will still need to Save Settings to make these changes permanent.");
									}
								}

								ImGui::Spacing();
								ImGui::Spacing();

								// Feature-specific settings section
								ImGui::SeparatorText("Feature Settings");
								if (isDisabled) {
									// Show disabled message
									ImGui::TextColored(themeSettings.StatusPalette.Disable, "Feature settings are hidden because this feature is disabled at boot.");
									ImGui::Spacing();
									ImGui::Text("Enable the feature above to access its configuration options.");
								} else {
									if (isLoaded) {
										feat->DrawSettings();
									} else {
										feat->DrawUnloadedUI();
										// Add download link if available
										if (!feat->GetFeatureModLink().empty()) {
											ImGui::Spacing();
											const auto downloadText = fmt::format("Click here to download this feature ({})", feat->GetFeatureModLink());
											if (ImGui::Selectable(downloadText.c_str())) {
												ShellExecuteA(NULL, "open", feat->GetFeatureModLink().c_str(), NULL, NULL, SW_SHOWNORMAL);
											}
											if (auto _tt = Util::HoverTooltipWrapper()) {
												ImGui::Text("Download the feature from the mod page.");
											}
										}
									}
								}

								// Error Messages
								if (hasFailedMessage && feat->DrawFailLoadMessage()) {
									ImGui::Spacing();
									ImGui::SeparatorText("Error");
									ImGui::TextColored(themeSettings.StatusPalette.Error, feat->failedLoadedMessage.c_str());
								}
							}
							ImGui::EndChild();
							ImGui::EndTabItem();
						}

						// About Tab - Information about the feature and how it works
						if (ImGui::BeginTabItem("About")) {
							if (ImGui::BeginChild("##FeatureAboutFrame", { 0, 0 }, true)) {
								// Status Section
								ImGui::SeparatorText("Status");
								
								ImVec4 statusColor;
								const char* statusText;
								if (isDisabled) {
									statusColor = themeSettings.StatusPalette.Disable;
									statusText = "Disabled at boot.";
								} else if (hasFailedMessage) {
									statusColor = themeSettings.StatusPalette.Error;
									statusText = "Failed to load.";
								} else if (!isLoaded) {
									statusColor = themeSettings.StatusPalette.RestartNeeded;
									statusText = "Pending restart.";
								} else {
									statusColor = themeSettings.StatusPalette.SuccessColor;
									statusText = "Active.";
								}

								ImGui::TextColored(statusColor, "Current State: %s", statusText);

								// Feature Info - Description and key features
								if (isLoaded) {
									auto [description, keyFeatures] = feat->GetFeatureSummary();
									if (!description.empty()) {
										ImGui::Spacing();
										ImGui::SeparatorText("Description");
										ImGui::TextWrapped("%s", description.c_str());

										if (!keyFeatures.empty()) {
											ImGui::Spacing();
											ImGui::SeparatorText("Key Features");
											for (const auto& feature : keyFeatures) {
												ImGui::BulletText("%s", feature.c_str());
											}
										}
									}
								} else {
									// For unloaded features, show basic info if available
									ImGui::Spacing();
									ImGui::SeparatorText("Information");
									ImGui::Text("This feature is not currently loaded.");
									if (hasFailedMessage) {
										ImGui::Spacing();
										ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", feat->failedLoadedMessage.c_str());
									}
								}
							}
							ImGui::EndChild();
							ImGui::EndTabItem();
						}
					}
					ImGui::EndTabBar();
				}
			};

			auto& featureList = Feature::GetFeatureList();
			auto sortedFeatureList{ featureList };  // need a copy so the load order is not lost
			std::ranges::sort(sortedFeatureList, [](Feature* a, Feature* b) {
				return a->GetName() < b->GetName();
			});

			auto menuList = std::vector<MenuFuncInfo>{
				BuiltInMenu{ "General", [&]() { DrawGeneralSettings(); } },
				BuiltInMenu{ "Advanced", [&]() { DrawAdvancedSettings(); } },
				BuiltInMenu{ "Display", [&]() { DrawDisplaySettings(); } }
			};  // NOTE: The menu list is rebuilt every frame, so category expansion states
			// persist correctly. This is acceptable since the list is small and built
			// infrequently, but could be optimized if performance becomes an issue.

			// Group features by category
			std::map<std::string, std::vector<Feature*>> categorizedFeatures;
			for (Feature* feat : sortedFeatureList) {
				if (feat->IsInMenu() && feat->loaded) {
					std::string category(feat->GetCategory());
					categorizedFeatures[category].push_back(feat);
				}
			}

			// Sort features within each category
			for (auto& [category, features] : categorizedFeatures) {
				std::ranges::sort(features, [](Feature* a, Feature* b) {
					return a->GetName() < b->GetName();
				});
			}

			// Define category order
			std::vector<std::string> categoryOrder = { "Characters", "Grass", "Lighting", "Sky", "Landscape & Textures", "Water", "Other" };
			// Add categorized features to menu with collapsible headers
			for (const std::string& category : categoryOrder) {
				if (categorizedFeatures.find(category) != categorizedFeatures.end() && !categorizedFeatures[category].empty()) {
					// Initialize expansion state if not exists
					if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
						categoryExpansionStates[category] = true;  // Default to expanded
					}

					// Add category header
					menuList.push_back(CategoryHeader{ category });

					// Add features only if category is expanded
					if (categoryExpansionStates[category]) {
						std::ranges::copy(categorizedFeatures[category], std::back_inserter(menuList));
					}
				}
			}

			// Add any categories not in the predefined order
			for (const auto& [category, features] : categorizedFeatures) {
				if (std::find(categoryOrder.begin(), categoryOrder.end(), category) == categoryOrder.end() && !features.empty()) {
					// Initialize expansion state if not exists
					if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
						categoryExpansionStates[category] = true;  // Default to expanded
					}

					// Add category header
					menuList.push_back(CategoryHeader{ category });

					// Add features only if category is expanded
					if (categoryExpansionStates[category]) {
						std::ranges::copy(features, std::back_inserter(menuList));
					}
				}
			}

			auto unloadedFeatures = sortedFeatureList | std::ranges::views::filter([](Feature* feat) {
				return !feat->loaded && feat->IsInMenu();
			});
			if (std::ranges::distance(unloadedFeatures) != 0) {
				menuList.push_back("Unloaded Features"s);
				std::ranges::copy(unloadedFeatures, std::back_inserter(menuList));
			}
			// Add top section for feature issues (rejected features, obsolete info, etc.)
			if (FeatureIssues::HasFeatureIssues()) {
				menuList.insert(menuList.begin(), BuiltInMenu{ "Feature Issues", []() {
																  FeatureIssues::DrawFeatureIssuesUI();
															  } });
			}

			// Handle pending feature selection
			if (!pendingFeatureSelection.empty()) {
				for (size_t i = 0; i < menuList.size(); ++i) {
					if (std::holds_alternative<Feature*>(menuList[i])) {
						Feature* feature = std::get<Feature*>(menuList[i]);
						if (feature->GetShortName() == pendingFeatureSelection) {
							selectedMenu = i;
							logger::info("Navigated to {} feature menu", pendingFeatureSelection);
							break;
						}
					}
				}
				pendingFeatureSelection.clear();  // Clear after processing
			}

			ImGui::TableNextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
			if (ImGui::BeginListBox("##MenusList", { -FLT_MIN, -FLT_MIN })) {
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
				for (size_t i = 0; i < menuList.size(); i++) {
					std::visit(ListMenuVisitor{ i, selectedMenu }, menuList[i]);
				}
				ImGui::EndListBox();
			}

			ImGui::TableNextColumn();

			if (selectedMenu < menuList.size()) {
				std::visit(DrawMenuVisitor{}, menuList[selectedMenu]);
			} else {
				ImGui::TextDisabled("Please select an item on the left.");
			}

			ImGui::EndTable();
		}
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
		ImGui::Spacing();

		DrawFooter();
	}
	ImGui::End();
}

void Menu::DrawGeneralSettings()
{
	auto shaderCache = globals::shaderCache;
	auto& themeSettings = settings.Theme;

	if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		bool useCustomShaders = shaderCache->IsEnabled();
		if (ImGui::BeginTable("##GeneralToggles", 3, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableNextColumn();
			if (ImGui::Checkbox("Enable Shaders", &useCustomShaders)) {
				shaderCache->SetEnabled(useCustomShaders);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Disabling this effectively disables all features.");
			}

			bool useDiskCache = shaderCache->IsDiskCache();

			ImGui::TableNextColumn();

			if (ImGui::Checkbox("Enable Disk Cache", &useDiskCache)) {
				shaderCache->SetDiskCache(useDiskCache);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Disabling this stops shaders from being loaded from disk, as well as stops shaders from being saved to it.");
			}

			bool useAsync = shaderCache->IsAsync();

			ImGui::TableNextColumn();

			if (ImGui::Checkbox("Enable Async", &useAsync)) {
				shaderCache->SetAsync(useAsync);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Skips a shader being replaced if it hasn't been compiled yet. Also makes compilation blazingly fast!");
			}

			ImGui::EndTable();
		}
	}

	if (ImGui::CollapsingHeader("Keybindings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		if (settingToggleKey) {
			ImGui::Text("Press any key to set as toggle key...");
		} else {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Toggle Key:");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", KeyIdToString(settings.ToggleKey));

			ImGui::AlignTextToFramePadding();
			ImGui::SameLine();
			if (ImGui::Button("Change##toggle")) {
				settingToggleKey = true;
			}
		}
		if (settingsEffectsToggle) {
			ImGui::Text("Press any key to set as a toggle key for all effects...");
		} else {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Effect Toggle Key:");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", KeyIdToString(settings.EffectToggleKey));

			ImGui::AlignTextToFramePadding();
			ImGui::SameLine();
			if (ImGui::Button("Change##EffectToggle")) {
				settingsEffectsToggle = true;
			}
		}
		if (settingSkipCompilationKey) {
			ImGui::Text("Press any key to set as Skip Compilation Key...");
		} else {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Skip Compilation Key:");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", KeyIdToString(settings.SkipCompilationKey));

			ImGui::AlignTextToFramePadding();
			ImGui::SameLine();
			if (ImGui::Button("Change##skip")) {
				settingSkipCompilationKey = true;
			}
		}
	}

	if (ImGui::CollapsingHeader("Theme", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		auto& style = themeSettings.Style;
		auto& colors = themeSettings.FullPalette;

		if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
			if (ImGui::BeginTabItem("UI Options")) {
				if (ImGui::SliderFloat("Global Scale", &themeSettings.GlobalScale, -1.f, 1.f, "%.2f")) {
					float trueScale = exp2(themeSettings.GlobalScale);

					auto& io = ImGui::GetIO();
					io.FontGlobalScale = trueScale;
				}

				ImGui::SeparatorText("UI Elements");
				ImGui::Checkbox("Use Icon Buttons in Header", &themeSettings.ShowActionIcons);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"When enabled: Shows action buttons (Save, Load, Clear Cache, Clear Disk Cache) as icons in the header\n"
						"When disabled: Shows as text buttons below the header");
				}

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Sizes")) {
				ImGui::SeparatorText("Main");
				ImGui::SliderFloat2("Window Padding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
				ImGui::SliderFloat2("Frame Padding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
				ImGui::SliderFloat2("Item Spacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
				ImGui::SliderFloat2("Item Inner Spacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
				ImGui::SliderFloat("Indent Spacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
				ImGui::SliderFloat("Scrollbar Size", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
				ImGui::SliderFloat("Grab Min Size", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");

				ImGui::SeparatorText("Borders");
				ImGui::SliderFloat("Window Border Size", &style.WindowBorderSize, 0.0f, 5.0f, "%.0f");
				ImGui::SliderFloat("Child Border Size", &style.ChildBorderSize, 0.0f, 5.0f, "%.0f");
				ImGui::SliderFloat("Popup Border Size", &style.PopupBorderSize, 0.0f, 5.0f, "%.0f");
				ImGui::SliderFloat("Frame Border Size", &style.FrameBorderSize, 0.0f, 5.0f, "%.0f");
				ImGui::SliderFloat("Tab Border Size", &style.TabBorderSize, 0.0f, 5.0f, "%.0f");
				ImGui::SliderFloat("Tab Bar Border Size", &style.TabBarBorderSize, 0.0f, 5.0f, "%.0f");

				ImGui::SeparatorText("Rounding");
				ImGui::SliderFloat("Window Rounding", &style.WindowRounding, 0.0f, 12.0f, "%.0f");
				ImGui::SliderFloat("Child Rounding", &style.ChildRounding, 0.0f, 12.0f, "%.0f");
				ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f");
				ImGui::SliderFloat("Popup Rounding", &style.PopupRounding, 0.0f, 12.0f, "%.0f");
				ImGui::SliderFloat("Scrollbar Rounding", &style.ScrollbarRounding, 0.0f, 12.0f, "%.0f");
				ImGui::SliderFloat("Grab Rounding", &style.GrabRounding, 0.0f, 12.0f, "%.0f");
				ImGui::SliderFloat("Tab Rounding", &style.TabRounding, 0.0f, 12.0f, "%.0f");

				ImGui::SeparatorText("Tables");
				ImGui::SliderFloat2("Cell Padding", (float*)&style.CellPadding, 0.0f, 20.0f, "%.0f");
				ImGui::SliderAngle("Table Angled Headers Angle", &style.TableAngledHeadersAngle, -50.0f, +50.0f);

				ImGui::SeparatorText("Widgets");
				ImGui::Combo("ColorButtonPosition", (int*)&style.ColorButtonPosition, "Left\0Right\0");
				ImGui::SliderFloat2("Button Text Align", (float*)&style.ButtonTextAlign, 0.0f, 1.0f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Alignment applies when a button is larger than its text content.");
				ImGui::SliderFloat2("Selectable Text Align", (float*)&style.SelectableTextAlign, 0.0f, 1.0f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Alignment applies when a selectable is larger than its text content.");
				ImGui::SliderFloat("Separator Text Border Size", &style.SeparatorTextBorderSize, 0.0f, 10.0f, "%.0f");
				ImGui::SliderFloat2("Separator Text Align", (float*)&style.SeparatorTextAlign, 0.0f, 1.0f, "%.2f");
				ImGui::SliderFloat2("Separator Text Padding", (float*)&style.SeparatorTextPadding, 0.0f, 40.0f, "%.0f");
				ImGui::SliderFloat("Log Slider Deadzone", &style.LogSliderDeadzone, 0.0f, 12.0f, "%.0f");

				ImGui::SeparatorText("Docking");
				ImGui::SliderFloat("Docking Splitter Size", &style.DockingSeparatorSize, 0.0f, 12.0f, "%.0f");

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Colors")) {
				ImGui::SeparatorText("Status");

				ImGui::ColorEdit4("Disabled Text", (float*)&themeSettings.StatusPalette.Disable);
				ImGui::ColorEdit4("Error Text", (float*)&themeSettings.StatusPalette.Error);
				ImGui::ColorEdit4("Warning Text", (float*)&themeSettings.StatusPalette.Warning);
				ImGui::ColorEdit4("Restart Needed Text", (float*)&themeSettings.StatusPalette.RestartNeeded);
				ImGui::ColorEdit4("Current Hotkey Text", (float*)&themeSettings.StatusPalette.CurrentHotkey);
				ImGui::ColorEdit4("Success Text", (float*)&themeSettings.StatusPalette.SuccessColor);
				ImGui::ColorEdit4("Info Text", (float*)&themeSettings.StatusPalette.InfoColor);

				ImGui::SeparatorText("Feature Headings");

				ImGui::ColorEdit4("Line Color Default", (float*)&themeSettings.FeatureHeading.LineColorDefault);
				ImGui::ColorEdit4("Line Color Hovered", (float*)&themeSettings.FeatureHeading.LineColorHovered);
				ImGui::ColorEdit4("Text Color Default", (float*)&themeSettings.FeatureHeading.TextColorDefault);
				ImGui::ColorEdit4("Text Color Hovered", (float*)&themeSettings.FeatureHeading.TextColorHovered);
				ImGui::ColorEdit4("Text Color White", (float*)&themeSettings.FeatureHeading.TextColorWhite);

				ImGui::SeparatorText("Palette");

				if (ImGui::RadioButton("Simple Palette", themeSettings.UseSimplePalette))
					themeSettings.UseSimplePalette = true;
				ImGui::SameLine();
				if (ImGui::RadioButton("Full Palette", !themeSettings.UseSimplePalette))
					themeSettings.UseSimplePalette = false;

				if (themeSettings.UseSimplePalette) {
					ImGui::ColorEdit4("Background", (float*)&themeSettings.Palette.Background);
					ImGui::ColorEdit4("Text", (float*)&themeSettings.Palette.Text);
					ImGui::ColorEdit4("Border", (float*)&themeSettings.Palette.Border);
				} else {
					static ImGuiTextFilter filter;
					filter.Draw("Filter colors", ImGui::GetFontSize() * 16);

					for (int i = 0; i < ImGuiCol_COUNT; i++) {
						const char* name = ImGui::GetStyleColorName(i);
						if (!filter.PassFilter(name))
							continue;
						ImGui::ColorEdit4(name, (float*)&colors[i], ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					}
				}

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
}

void Menu::DrawAdvancedSettings()
{
	auto shaderCache = globals::shaderCache;
	if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		bool useDump = shaderCache->IsDump();
		if (ImGui::Checkbox("Dump Shaders", &useDump)) {
			shaderCache->SetDump(useDump);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Dump shaders at startup. This should be used only when reversing shaders. Normal users don't need this.");
		}
		spdlog::level::level_enum logLevel = globals::state->GetLogLevel();
		const char* items[] = {
			"trace",
			"debug",
			"info",
			"warn",
			"err",
			"critical",
			"off"
		};
		static int item_current = static_cast<int>(logLevel);
		if (ImGui::Combo("Log Level", &item_current, items, IM_ARRAYSIZE(items))) {
			ImGui::SameLine();
			globals::state->SetLogLevel(static_cast<spdlog::level::level_enum>(item_current));
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Log level. Trace is most verbose. Default is info.");
		}

		auto& shaderDefines = globals::state->shaderDefinesString;
		if (ImGui::InputText("Shader Defines", &shaderDefines)) {
			globals::state->SetDefines(shaderDefines);
		}
		if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemActive() &&
													   (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) ||
														   ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadEnter))))) {
			globals::state->SetDefines(shaderDefines);
			shaderCache->Clear();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Defines for Shader Compiler. Semicolon \";\" separated. Clear with space. Rebuild shaders after making change. Compute Shaders require a restart to recompile.");
		}
		ImGui::Spacing();
		ImGui::SliderInt("Compiler Threads", &shaderCache->compilationThreadCount, 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Number of threads to use to compile shaders. "
				"The more threads the faster compilation will finish but may make the system unresponsive. ");
		}
		ImGui::SliderInt("Background Compiler Threads", &shaderCache->backgroundCompilationThreadCount, 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Number of threads to use to compile shaders while playing game. "
				"This is activated if the startup compilation is skipped. "
				"The more threads the faster compilation will finish but may make the system unresponsive. ");
		}

		if (ImGui::SliderInt("Test Interval", reinterpret_cast<int*>(&testInterval), 0, 10)) {
			if (testInterval == 0) {
				inTestMode = false;
				logger::info("Disabling test mode.");
				globals::state->Load(State::ConfigMode::TEST);  // restore last settings before entering test mode
			} else if (!inTestMode) {
				logger::info("Saving current settings for test mode and starting test with interval {}.", testInterval);
				globals::state->Save(State::ConfigMode::TEST);
				inTestMode = true;
			} else {
				logger::info("Setting new interval {}.", testInterval);
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Sets number of seconds before toggling between default USER and TEST config. "
				"0 disables. Non-zero will enable testing mode. "
				"Enabling will save current settings as TEST config. "
				"This has no impact if no settings are changed. ");
		}
		bool useFileWatcher = shaderCache->UseFileWatcher();
		ImGui::TableNextColumn();
		if (ImGui::Checkbox("Enable File Watcher", &useFileWatcher)) {
			shaderCache->SetFileWatcher(useFileWatcher);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Automatically recompile shaders on file change. "
				"Intended for developing.");
		}

		if (ImGui::Button("Dump Ini Settings", { -1, 0 })) {
			Util::DumpSettingsOptions();
		}
		if (!shaderCache->blockedKey.empty()) {
			auto blockingButtonString = std::format("Stop Blocking {} Shaders", shaderCache->blockedIDs.size());
			if (ImGui::Button(blockingButtonString.c_str(), { -1, 0 })) {
				shaderCache->DisableShaderBlocking();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Stop blocking Community Shaders shader. "
					"Blocking is helpful when debugging shader errors in game to determine which shader has issues. "
					"Blocking is enabled if in developer mode and pressing PAGEUP and PAGEDOWN. "
					"Specific shader will be printed to logfile. ");
			}
		}
		if (ImGui::TreeNodeEx("Addresses")) {
			auto Renderer = globals::game::renderer;
			auto BSShaderAccumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
			auto RendererShadowState = globals::game::shadowState;
			ADDRESS_NODE(Renderer)
			ADDRESS_NODE(BSShaderAccumulator)
			ADDRESS_NODE(RendererShadowState)
			ImGui::TreePop();
		}
		if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text(std::format("Shader Compiler : {}", shaderCache->GetShaderStatsString()).c_str());
			ImGui::TreePop();
		}
		ImGui::Checkbox("Frame Annotations", &globals::state->frameAnnotations);
	}

	if (ImGui::CollapsingHeader("Replace Original Shaders", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		auto state = globals::state;
		if (ImGui::BeginTable("##ReplaceToggles", 3, ImGuiTableFlags_SizingStretchSame)) {
			for (int classIndex = 0; classIndex < RE::BSShader::Type::Total - 1; ++classIndex) {
				ImGui::TableNextColumn();

				auto type = (RE::BSShader::Type)(classIndex + 1);
				if (!(SIE::ShaderCache::IsSupportedShader(type) || state->IsDeveloperMode())) {
					ImGui::BeginDisabled();
					ImGui::Checkbox(std::format("{}", magic_enum::enum_name(type)).c_str(), &state->enabledClasses[classIndex]);
					ImGui::EndDisabled();
				} else
					ImGui::Checkbox(std::format("{}", magic_enum::enum_name(type)).c_str(), &state->enabledClasses[classIndex]);
			}
			if (state->IsDeveloperMode()) {
				ImGui::Checkbox("Vertex", &state->enableVShaders);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Replace Vertex Shaders. "
						"When false, will disable the custom Vertex Shaders for the types above. "
						"For developers to test whether CS shaders match vanilla behavior. ");
				}

				ImGui::Checkbox("Pixel", &state->enablePShaders);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Replace Pixel Shaders. "
						"When false, will disable the custom Pixel Shaders for the types above. "
						"For developers to test whether CS shaders match vanilla behavior. ");
				}

				ImGui::Checkbox("Compute", &state->enableCShaders);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Replace Compute Shaders. "
						"When false, will disable the custom Compute Shaders for the types above. "
						"For developers to test whether CS shaders match vanilla behavior. ");
				}
			}
			ImGui::EndTable();
		}
	}

	globals::truePBR->DrawSettings();
	Menu::DrawDisableAtBootSettings();
	// Developer Mode Testing Section
	if (globals::state->IsDeveloperMode()) {
		FeatureIssues::Test::DrawDeveloperModeTestingUI();
	}
}

void Menu::DrawDisableAtBootSettings()
{
	auto state = globals::state;
	auto& disabledFeatures = state->GetDisabledFeatures();

	if (ImGui::CollapsingHeader("Disable at Boot", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Text(
			"Select features to disable at boot. "
			"This is the same as deleting a feature.ini file. "
			"Restart will be required to reenable.");

		if (ImGui::CollapsingHeader("Special Features")) {
			// Prepare a sorted list of special feature names
			std::vector<std::string> specialFeatureNames;
			for (const auto& [featureName, _] : state->specialFeatures) {
				specialFeatureNames.push_back(featureName);
			}
			std::sort(specialFeatureNames.begin(), specialFeatureNames.end());

			// Display sorted special features
			for (const auto& featureName : specialFeatureNames) {
				// Check if the feature is currently disabled
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];

				// Create a checkbox for each feature
				if (ImGui::Checkbox(featureName.c_str(), &isDisabled)) {
					// Update the disabledFeatures map based on user interaction
					disabledFeatures[featureName] = isDisabled;
				}
			}
		}

		if (ImGui::CollapsingHeader("Features")) {
			// Prepare a sorted list of feature pointers
			auto featureList = Feature::GetFeatureList();
			std::sort(featureList.begin(), featureList.end(), [](Feature* a, Feature* b) {
				return a->GetShortName() < b->GetShortName();
			});

			// Display sorted features
			for (auto* feature : featureList) {
				const std::string featureName = feature->GetShortName();
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];

				if (ImGui::Checkbox(featureName.c_str(), &isDisabled)) {
					// Update the disabledFeatures map based on user interaction
					disabledFeatures[featureName] = isDisabled;
				}
			}
		}
	}
}

void Menu::DrawDisplaySettings()
{
	if (!globals::state->upscalerLoaded) {
		auto& themeSettings = settings.Theme;

		const std::vector<std::pair<std::string, std::function<void()>>> features = {
			{ "Upscaling", []() { globals::upscaling->DrawSettings(); } },
			{ "Performance Overlay", [this]() { DrawPerformanceOverlaySettings(); } }
		};

		for (const auto& [featureName, drawFunc] : features) {
			bool isDisabled = globals::state->IsFeatureDisabled(featureName);

			if (featureName == "Frame Generation" && REL::Module::IsVR()) {
				isDisabled = true;
			}

			if (!isDisabled) {
				if (ImGui::CollapsingHeader(featureName.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
					drawFunc();
				}
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Disable);
				ImGui::CollapsingHeader(featureName.c_str(), ImGuiTreeNodeFlags_NoTreePushOnOpen);
				ImGui::PopStyleColor();
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"%s has been disabled at boot. "
						"Reenable in the Advanced -> Disable at Boot Menu.",
						featureName.c_str());
				}
			}
		}
	} else {
		ImGui::Text("Display options disabled due to Skyrim Upscaler");
	}
}

void Menu::DrawFooter()
{
	ImGui::BulletText(std::format("Game Version: {} {}", magic_enum::enum_name(REL::Module::GetRuntime()), Util::GetFormattedVersion(REL::Module::get().version()).c_str()).c_str());
	ImGui::SameLine();
	ImGui::BulletText(std::format("D3D12 Interop: {}", globals::upscaling->d3d12Interop ? "Active" : "Inactive").c_str());
	ImGui::SameLine();
	ImGui::Text(std::format("GPU: {}", globals::state->adapterDescription.c_str()).c_str());
}

void Menu::DrawOverlay()
{
	ProcessInputEventQueue();  //Synchronize Inputs to frame

	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetFailedTasks();
	auto hide = shaderCache->IsHideErrors();
	if (!(shaderCache->IsCompiling() || IsEnabled || inTestMode || (failed && !hide) || settings.PerfOverlay.Enabled)) {
		auto& io = ImGui::GetIO();
		io.ClearInputKeys();
		io.ClearEventsQueue();
		return;
	}

	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGuiStyle oldStyle = ImGui::GetStyle();
	SetupImGuiStyle();

	uint64_t totalShaders = 0;
	uint64_t compiledShaders = 0;

	compiledShaders = shaderCache->GetCompletedTasks();
	totalShaders = shaderCache->GetTotalTasks();

	auto state = globals::state;
	auto& themeSettings = settings.Theme;

	auto progressTitle = fmt::format("{}Compiling Shaders: {}",
		shaderCache->backgroundCompilation ? "Background " : "",
		shaderCache->GetShaderStatsString(!state->IsDeveloperMode()).c_str());
	auto percent = (float)compiledShaders / (float)totalShaders;
	auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", compiledShaders, totalShaders, 100 * percent);
	if (shaderCache->IsCompiling()) {
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(progressTitle.c_str());
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());
		if (!shaderCache->backgroundCompilation && shaderCache->menuLoaded) {
			auto skipShadersText = fmt::format(
				"Press {} to proceed without completing shader compilation. ",
				KeyIdToString(settings.SkipCompilationKey));
			ImGui::TextUnformatted(skipShadersText.c_str());
			ImGui::TextUnformatted("WARNING: Uncompiled shaders will have visual errors or cause stuttering when loading.");
		}

		ImGui::End();
	} else if (failed) {
		if (!hide) {
			ImGui::SetNextWindowPos(ImVec2(10, 10));
			if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
				ImGui::End();
				return;
			}

			ImGui::TextColored(themeSettings.StatusPalette.Error, "ERROR: %d shaders failed to compile. Check installation and CommunityShaders.log", failed, totalShaders);

			// Check for features that may cause shader compilation issues
			if (FeatureIssues::HasPotentialShaderModifyingFeatures()) {
				ImGui::TextColored(themeSettings.StatusPalette.Error, "Features that may have modified shaders detected. Check Feature Issues in the Menu.");
			}

			ImGui::End();
		}
	}

	if (IsEnabled) {
		ImGui::GetIO().MouseDrawCursor = true;
		DrawSettings();
	} else {
		ImGui::GetIO().MouseDrawCursor = false;
	}
	if (settings.PerfOverlay.Enabled)
		DrawPerfOverlay();

	if (inTestMode) {  // In test mode
		float seconds = (float)duration_cast<std::chrono::milliseconds>(high_resolution_clock::now() - lastTestSwitch).count() / 1000.0f;
		auto remaining = (float)testInterval - seconds;
		if (remaining < 0.0f) {
			usingTestConfig = !usingTestConfig;
			logger::info("Swapping mode to {}", usingTestConfig ? "test" : "user");
			globals::state->Load(usingTestConfig ? State::ConfigMode::TEST : State::ConfigMode::USER);
			lastTestSwitch = high_resolution_clock::now();
		}
		ImGui::SetNextWindowBgAlpha(1.0f);
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		if (!ImGui::Begin("Testing", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::Text(fmt::format("{} Mode : {:.1f} seconds left", usingTestConfig ? "Test" : "User", remaining).c_str());
		ImGui::End();
	}

	ImGuiStyle& style = ImGui::GetStyle();
	style = oldStyle;

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Menu::DrawPerfOverlay()
{
	if (!settings.PerfOverlay.Enabled) {
		return;
	}

	// Set window flags - no decoration and only movable when ShowBorder is true
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;

	// Only allow mouse interaction when the main menu is open
	if (!IsEnabled) {
		windowFlags |= ImGuiWindowFlags_NoInputs;
	}

	if (!settings.PerfOverlay.ShowBorder) {
		windowFlags |= ImGuiWindowFlags_NoBackground;
	} else {
		windowFlags &= ~ImGuiWindowFlags_NoDecoration;
		windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
	}

	// Set background opacity
	ImGui::PushStyleColor(ImGuiCol_WindowBg,
		ImVec4(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x,
			ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).y,
			ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).z,
			settings.PerfOverlay.BackgroundOpacity));

	// Set text size based on user preference
	perfOverlayState.textScale = perfOverlayState.SetTextScale(settings.PerfOverlay);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, settings.PerfOverlay.ShowBorder ? 1.0f : 0.0f);

	// Set initial position if not already set
	if (!settings.PerfOverlay.PositionSet) {
		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
		settings.PerfOverlay.Position = ImVec2(10.0f, 10.0f);
		settings.PerfOverlay.PositionSet = true;
	} else {
		ImGui::SetNextWindowPos(settings.PerfOverlay.Position, ImGuiCond_FirstUseEver);
	}

	// Set window size based on whether graphs are shown, was rapidly changing size based on text
	perfOverlayState.hasGraphs =
		(settings.PerfOverlay.ShowPreFGFrameTimeGraph &&
			(!perfOverlayState.isFrameGenerationActive ||
				(perfOverlayState.isFrameGenerationActive && settings.PerfOverlay.ShowPreFGFPS))) ||
		(settings.PerfOverlay.ShowPostFGFrameTimeGraph &&
			perfOverlayState.isFrameGenerationActive &&
			settings.PerfOverlay.ShowPostFGFPS);
	if (!perfOverlayState.hasGraphs) {
		float fixedWidth = 325.0f * perfOverlayState.textScale;
		ImGui::SetNextWindowSize(ImVec2(fixedWidth, 0), ImGuiCond_Always);
	}

	// Create the window
	ImGui::Begin("Performance Overlay", NULL, windowFlags);

	// Remember window position for next frame
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetWindowPos(settings.PerfOverlay.Position);
	}

	// Track if window has been moved
	ImVec2 currentPos = ImGui::GetWindowPos();
	if (currentPos.x != settings.PerfOverlay.Position.x || currentPos.y != settings.PerfOverlay.Position.y) {
		settings.PerfOverlay.Position = currentPos;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));  // Tighter spacing
	ImGui::SetWindowFontScale(perfOverlayState.textScale);

	// Initialize Performance Counter if necessary
	if (!perfOverlayState.initialized) {
		REX::W32::QueryPerformanceFrequency(&perfOverlayState.frequency);
		REX::W32::QueryPerformanceCounter(&perfOverlayState.lastFrameCounter);
		perfOverlayState.initialized = true;
	} else {
		REX::W32::QueryPerformanceCounter(&perfOverlayState.currentFrameCounter);
		int64_t elapsedCounter = perfOverlayState.currentFrameCounter - perfOverlayState.lastFrameCounter;
		perfOverlayState.lastFrameCounter = perfOverlayState.currentFrameCounter;

		// Calculate frametime and fps
		perfOverlayState.frameTimeMs = Util::performanceOverlay.CalcFrameTime(elapsedCounter, perfOverlayState.frequency);
		perfOverlayState.fps = Util::performanceOverlay.CalcFPS(perfOverlayState.frameTimeMs);

		// Calculate smooth values for display using the user-defined update interval
		auto now = std::chrono::steady_clock::now();
		float deltaTime = std::chrono::duration<float>(now - perfOverlayState.lastUpdateTime).count();
		perfOverlayState.lastUpdateTime = now;

		// Update graph values
		perfOverlayState.UpdateGraphValues(settings.PerfOverlay);

		// Update smooth values with user-specified interval
		perfOverlayState.updateTimer += deltaTime;
		if (perfOverlayState.updateTimer >= settings.PerfOverlay.UpdateInterval) {
			perfOverlayState.smoothFps = perfOverlayState.fps;
			perfOverlayState.smoothFrameTimeMs = perfOverlayState.frameTimeMs;
			perfOverlayState.updateTimer = 0.0f;
		}

		// Check if Frame Generation is active
		perfOverlayState.isFrameGenerationActive = globals::upscaling && globals::upscaling->IsFrameGenerationActive();

		if (perfOverlayState.isFrameGenerationActive) {
			perfOverlayState.UpdateFGFrameTime(settings.PerfOverlay);
		}

		// Show FPS counter if enabled
		if (settings.PerfOverlay.ShowFPS) {
			perfOverlayState.DrawFPS(settings.PerfOverlay);
		}
	}

	// Show Draw Calls if enabled
	if (settings.PerfOverlay.ShowDrawCalls) {
		perfOverlayState.DrawDrawCalls();
	}

	// VRAM & GPU Usage
	if (settings.PerfOverlay.ShowVRAM && dxgiAdapter3) {
		perfOverlayState.DrawVRAM(dxgiAdapter3);
	}

	ImGui::PopStyleVar();             // ItemSpacing
	ImGui::SetWindowFontScale(1.0f);  // Reset font scale

	ImGui::End();
	ImGui::PopStyleVar();    // WindowBorderSize
	ImGui::PopStyleColor();  // WindowBg
}

float Menu::PerfOverlayState::SetTextScale(Settings::PerfOverlaySettings& settings)
{
	switch (settings.Size) {
	case Settings::PerfOverlaySettings::TextSize::Small:
		return 0.8f;
	case Settings::PerfOverlaySettings::TextSize::Medium:
		return 1.0f;
	case Settings::PerfOverlaySettings::TextSize::Large:
		return 1.2f;
	}
	return 1.0f;
}

/**
 * @brief Updates all runtime state related to the performance overlay graph.
 *
 * This function synchronizes the frame time history buffer, tracks min/max frame times,
 * and computes the normalized Y-axis range for the frame time graph using statistical analysis.
 *
 * Steps performed:
 *   1. Resizes the frameTimeHistory buffer if the user has changed the setting.
 *   2. Inserts the latest frame time into the circular history buffer.
 *   3. Updates instantaneous min/max frame time values, with full rescans if necessary.
 *   4. Calculates the average (mean) and standard deviation of frame times in the buffer.
 *   5. Sets the graph Y-axis range to be centered on the average, with a spread of ±2 standard deviations,
 *      clamped to user-friendly minimum and maximum values.
 *   6. Smooths the min/max Y-axis values for visual stability using exponential smoothing.
 *
 *
 * @param settings Reference to the current performance overlay settings (controls buffer size, etc.).
 */
void Menu::PerfOverlayState::UpdateGraphValues(Settings::PerfOverlaySettings& settings)
{
	// Sync frame history buffer size with user settings
	UpdateFrameTimeHistorySizes(settings);

	// Insert latest frame time into circular buffer
	float oldFrameTime = frameTimeHistory[frameTimeHistoryIndex];
	frameTimeHistory[frameTimeHistoryIndex] = frameTimeMs;
	frameTimeHistoryIndex = (frameTimeHistoryIndex + 1) % settings.FrameHistorySize;

	// Maintain instantaneous min/max tracking
	if (frameTimeMs > maxFrameTime) {
		maxFrameTime = frameTimeMs;
	} else if (frameTimeMs < minFrameTime) {
		minFrameTime = frameTimeMs;
	} else if (oldFrameTime == minFrameTime) {
		UpdateMinFrameTime();
	} else if (oldFrameTime == maxFrameTime) {
		UpdateMaxFrameTime();
	}

	float avgFrameTime, stdDev, graphMin, graphMax;
	// Calculate mean and standard deviation for normalized graph range
	if (frameTimeHistory.empty()) {
		// Default to 60 FPS
		avgFrameTime = 16.67f;
		stdDev = 0.0f;
		graphMin = 0.0f;
		graphMax = 33.0f;
	} else {
		// Calculate average frame time
		avgFrameTime = std::accumulate(frameTimeHistory.begin(), frameTimeHistory.end(), 0.0f) / frameTimeHistory.size();

		// Calculate standard deviation
		float variance = 0.0f;
		for (float ft : frameTimeHistory) {
			float diff = ft - avgFrameTime;
			variance += diff * diff;
		}
		variance /= frameTimeHistory.size();
		stdDev = std::sqrt(variance);

		// Calculate graph range
		float spread = std::clamp(stdDev * 2.0f, 2.0f, 20.0f);
		graphMin = std::max(0.0f, avgFrameTime - spread);
		graphMax = avgFrameTime + spread;
	}

	// Exponential smoothing for stable graph scaling
	smoothedMinFrameTime += kSmoothingFactor * (graphMin - smoothedMinFrameTime);
	smoothedMaxFrameTime += kSmoothingFactor * (graphMax - smoothedMaxFrameTime);
}

/**
 * @brief Updates the minimum frame time value by scanning the frame time history buffer.
 *
 * Finds the smallest frame time currently in the frameTimeHistory buffer and updates minFrameTime accordingly.
 * Assumes frameTimeHistory is non-empty.
 */
void Menu::PerfOverlayState::UpdateMinFrameTime()
{
	minFrameTime = *std::min_element(frameTimeHistory.begin(), frameTimeHistory.end());
}

/**
 * @brief Updates the maximum frame time value by scanning the frame time history buffer.
 *
 * Finds the largest frame time currently in the frameTimeHistory buffer and updates maxFrameTime accordingly.
 * Assumes frameTimeHistory is non-empty.
 */
void Menu::PerfOverlayState::UpdateMaxFrameTime()
{
	maxFrameTime = *std::max_element(frameTimeHistory.begin(), frameTimeHistory.end());
}

/**
 * @brief Updates post-frame generation (FG) frame time and FPS history values.
 *
 * Retrieves the latest frame time from the Frame Generation system if available, updates smoothed values,
 * and maintains a circular buffer of post-FG frame times. Falls back to an approximation if FG timing is unavailable.
 *
 * @param settings Reference to the current performance overlay settings (controls buffer size, etc.).
 */
void Menu::PerfOverlayState::UpdateFGFrameTime(Settings::PerfOverlaySettings& settings)
{
	// Get frametime directly from the Frame Generation system
	float fgDeltaTime = globals::upscaling->GetFrameGenerationFrameTime();
	if (fgDeltaTime > 0.0f) {
		postFGFrameTimeMs = fgDeltaTime * 1000.0f;
		postFGFps = 1000.0f / postFGFrameTimeMs;

		// Update post-FG smooth values when timer elapses
		if (updateTimer <= 0.0f) {
			postFGSmoothFps = postFGFps;
			postFGSmoothFrameTimeMs = postFGFrameTimeMs;
		}

		// Update post-FG frametime history
		postFGFrameTimeHistory[postFGFrameTimeHistoryIndex] = postFGFrameTimeMs;
		postFGFrameTimeHistoryIndex = (postFGFrameTimeHistoryIndex + 1) % settings.FrameHistorySize;
	} else {
		// Fallback if FG time is not available
		postFGFrameTimeMs = frameTimeMs / 2.0f;  // Approximate
		postFGFps = fps * 2.0f;                  // Approximate

		// Update smooth values when timer elapses
		if (updateTimer <= 0.0f) {
			postFGSmoothFps = postFGFps;
			postFGSmoothFrameTimeMs = postFGFrameTimeMs;
		}

		// Update post-FG frametime history with approximation
		postFGFrameTimeHistory[postFGFrameTimeHistoryIndex] = postFGFrameTimeMs;
		postFGFrameTimeHistoryIndex = (postFGFrameTimeHistoryIndex + 1) % settings.FrameHistorySize;
	}
}

/**
 * @brief Renders the FPS and frametime statistics using ImGui, including graphs and reference lines.
 *
 * Displays pre- and post-frame generation FPS and frametime values, as well as line graphs if enabled in settings.
 * Handles both standard and frame generation rendering modes, and draws reference lines for common FPS targets.
 *
 * @param settings Reference to the current performance overlay settings (controls what is displayed).
 */
void Menu::PerfOverlayState::DrawFPS(Settings::PerfOverlaySettings& settings)
{
	if (isFrameGenerationActive) {
		if (settings.ShowPostFGFPS) {
			ImGui::Text("FPS: %.1f", postFGSmoothFps);
		}

		if (settings.ShowPreFGFPS) {
			ImGui::Text("Pre-FG FPS: %.1f", smoothFps);
		}
	} else {
		ImGui::Text("FPS: %.1f", smoothFps);
	}

	if (isFrameGenerationActive) {
		if (settings.ShowPostFGFPS && settings.ShowPostFGFrameTime) {
			ImGui::Text("Frametime: %.2f ms", postFGSmoothFrameTimeMs);
		}
		if (settings.ShowPreFGFPS && settings.ShowPreFGFrameTime) {
			ImGui::Text("Pre-FG Frametime: %.2f ms", smoothFrameTimeMs);
		}
	} else {
		if (settings.ShowPreFGFrameTime) {
			ImGui::Text("Frametime: %.2f ms", smoothFrameTimeMs);
		}
	}

	// Show Pre-FG frametime graph if enabled
	if (settings.ShowPreFGFrameTimeGraph &&
		(!isFrameGenerationActive || (isFrameGenerationActive && settings.ShowPreFGFPS))) {
		// Prepare overlay text
		char overlay_text[128];
		snprintf(overlay_text, IM_ARRAYSIZE(overlay_text),
			"%s%.2f ms (%.1f FPS)",
			isFrameGenerationActive ? "Pre-FG: " : "",
			smoothFrameTimeMs, smoothFps);

		// Set graph colors
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));  // Green line

		// Draw the graph
		ImGui::PlotLines("##frametime",
			frameTimeHistory.data(),
			settings.FrameHistorySize,
			frameTimeHistoryIndex,
			overlay_text,
			smoothedMinFrameTime, smoothedMaxFrameTime,
			ImVec2(ImGui::GetWindowWidth() * 0.9f, 50.0f * textScale));

		ImGui::PopStyleColor();

		// Draw frametime target reference lines
		if (ImGui::BeginTable("FrametimeTargets", 3, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableNextColumn();
			ImGui::Text("30 FPS: 33.3 ms");

			ImGui::TableNextColumn();
			ImGui::Text("60 FPS: 16.7 ms");

			ImGui::TableNextColumn();
			ImGui::Text("120 FPS: 8.3 ms");

			ImGui::EndTable();
		}
	}

	// Show Post-FG frametime graph if enabled
	if (settings.ShowPostFGFrameTimeGraph && isFrameGenerationActive && settings.ShowPostFGFPS) {
		DrawPostFGFrameTimeGraph(settings);
	}
}

/**
 * @brief Renders the post-frame generation frametime graph using ImGui.
 *
 * Plots the post-FG frametime history and displays reference lines for common FPS targets.
 * Only called if frame generation is active and the relevant settings are enabled.
 *
 * @param settings Reference to the current performance overlay settings (controls graph appearance and size).
 */
void Menu::PerfOverlayState::DrawPostFGFrameTimeGraph(Settings::PerfOverlaySettings& settings)
{
	// Prepare overlay text
	char overlay_text[128];
	snprintf(overlay_text, IM_ARRAYSIZE(overlay_text),
		"Post-FG: %.2f ms (%.1f FPS)",
		postFGSmoothFrameTimeMs, postFGSmoothFps);

	// Set graph colors - blue for post-FG
	ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 0.5f, 1.0f, 1.0f));  // Blue line

	// Draw the graph
	ImGui::PlotLines("##postfgframetime",
		postFGFrameTimeHistory.data(),
		settings.FrameHistorySize,
		postFGFrameTimeHistoryIndex,
		overlay_text,
		smoothedMinFrameTime, smoothedMaxFrameTime,
		ImVec2(ImGui::GetWindowWidth() * 0.9f, 50.0f * textScale));

	ImGui::PopStyleColor();

	// Draw frametime target reference lines
	if (ImGui::BeginTable("PostFGFrametimeTargets", 3, ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextColumn();
		ImGui::Text("30 FPS: 33.3 ms");

		ImGui::TableNextColumn();
		ImGui::Text("60 FPS: 16.7 ms");

		ImGui::TableNextColumn();
		ImGui::Text("120 FPS: 8.3 ms");

		ImGui::EndTable();
	}
}

/**
 * @brief Renders the current draw call counts for various shader types using ImGui.
 *
 * Displays a breakdown of draw calls by type (e.g., Grass, Sky, Water, etc.) and the total count.
 * Values are sourced from the global state.
 */
void Menu::PerfOverlayState::DrawDrawCalls()
{
	ImGui::Text("Draw Calls:");
	ImGui::Indent();
	ImGui::Text("Grass: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Grass]));
	ImGui::Text("Sky: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Sky]));
	ImGui::Text("Water: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Water]));
	ImGui::Text("Lighting: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Lighting]));
	ImGui::Text("Effect: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Effect]));
	ImGui::Text("Utility: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Utility]));
	ImGui::Text("Distant Tree: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::DistantTree]));
	ImGui::Text("Particle: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Particle]));
	ImGui::Text("Total: %d", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Total]));

	ImGui::Unindent();
}

/**
 * @brief Renders the current GPU VRAM usage using ImGui.
 *
 * Queries the DXGI adapter for video memory info and displays current usage, total budget, and a progress bar.
 * Falls back to a message if VRAM info is unavailable.
 *
 * @param dxgiAdapter3 A COM pointer to the IDXGIAdapter3 interface for querying video memory info.
 */
void Menu::PerfOverlayState::DrawVRAM(winrt::com_ptr<IDXGIAdapter3> dxgiAdapter3)
{
	DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo{};
	HRESULT hr = dxgiAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfo);

	// Only proceed if the call succeeded and Budget is not zero
	if (SUCCEEDED(hr) && videoMemoryInfo.Budget > 0) {
		float currentGpuUsage = videoMemoryInfo.CurrentUsage / (1024.f * 1024.f * 1024.f);
		float totalGpuMemory = videoMemoryInfo.Budget / (1024.f * 1024.f * 1024.f);
		float percent = currentGpuUsage / totalGpuMemory;

		// Center the VRAM text
		ImGui::Text("VRAM Usage:");

		// Use a centered text format for the numeric values
		std::string vramText = std::format("{:.2f}GB/{:.2f}GB ({:.1f}%)", currentGpuUsage, totalGpuMemory, 100 * percent);
		float textWidth = ImGui::CalcTextSize(vramText.c_str()).x;
		float windowWidth = ImGui::GetWindowWidth();

		// Center the text if it fits within the window
		if (textWidth < windowWidth) {
			ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
			ImGui::Text("%s", vramText.c_str());
		} else {
			ImGui::Text("%s", vramText.c_str());
		}

		// Only move the progress bar, not the text
		ImGui::ProgressBar(percent, ImVec2(ImGui::GetWindowWidth() * 0.9f, 0.0f), "");
	} else {
		// Display a fallback message if we couldn't get the VRAM info
		ImGui::Text("VRAM Usage: Not available");
	}
}

/**
 * @brief Ensures frame time history buffers are sized according to the current settings.
 *
 * Resizes the frameTimeHistory and postFGFrameTimeHistory buffers to match the user-configured history size,
 * clamping the size within allowed bounds. Resets indices if they are out of bounds after resizing.
 *
 * @param settings Reference to the current performance overlay settings (controls buffer size and limits).
 */
void Menu::PerfOverlayState::UpdateFrameTimeHistorySizes(Settings::PerfOverlaySettings& settings)
{
	settings.FrameHistorySize = std::clamp(
		settings.FrameHistorySize,
		settings.kMinFrameHistorySize,
		settings.kMaxFrameHistorySize);

	if (frameTimeHistory.size() != static_cast<size_t>(settings.FrameHistorySize)) {
		frameTimeHistory.resize(settings.FrameHistorySize, 0.0f);
		if (frameTimeHistoryIndex >= settings.FrameHistorySize) {  // Reset index if it's out of new bounds
			frameTimeHistoryIndex = 0;
		}
	}
	if (postFGFrameTimeHistory.size() != static_cast<size_t>(settings.FrameHistorySize)) {
		postFGFrameTimeHistory.resize(settings.FrameHistorySize, 0.0f);
		if (postFGFrameTimeHistoryIndex >= settings.FrameHistorySize) {
			postFGFrameTimeHistoryIndex = 0;
		}
	}
}

void Menu::DrawPerformanceOverlaySettings()
{
	auto& themeSettings = settings.Theme;

	ImGui::Checkbox("Enable Performance Overlay", &settings.PerfOverlay.Enabled);

	if (settings.PerfOverlay.Enabled) {
		ImGui::Indent();

		// Display options
		if (ImGui::CollapsingHeader("Display Options", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();

			// FPS options
			ImGui::Checkbox("Show FPS Counter", &settings.PerfOverlay.ShowFPS);

			bool isFrameGenerationActive = globals::upscaling && globals::upscaling->IsFrameGenerationActive();
			if (settings.PerfOverlay.ShowFPS) {
				ImGui::Indent();

				if (isFrameGenerationActive) {
					// Pre-Frame Generation FPS
					if (ImGui::TreeNodeEx("Pre-Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
						ImGui::Checkbox("Show Pre-FG FPS", &settings.PerfOverlay.ShowPreFGFPS);
						if (settings.PerfOverlay.ShowPreFGFPS) {
							ImGui::Indent();
							ImGui::Checkbox("Show Pre-FG Frametime", &settings.PerfOverlay.ShowPreFGFrameTime);
							ImGui::Checkbox("Show Pre-FG Frametime Graph", &settings.PerfOverlay.ShowPreFGFrameTimeGraph);
							ImGui::Unindent();
						}
						ImGui::TreePop();
					}

					// Post-Frame Generation FPS
					if (ImGui::TreeNodeEx("Post-Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
						ImGui::Checkbox("Show Post-FG FPS", &settings.PerfOverlay.ShowPostFGFPS);
						if (settings.PerfOverlay.ShowPostFGFPS) {
							ImGui::Indent();
							ImGui::Checkbox("Show Post-FG Frametime", &settings.PerfOverlay.ShowPostFGFrameTime);
							ImGui::Checkbox("Show Post-FG Frametime Graph", &settings.PerfOverlay.ShowPostFGFrameTimeGraph);
							ImGui::Unindent();
						}
						ImGui::TreePop();
					}
				} else {
					// Regular FPS options when frame generation is not active
					ImGui::Checkbox("Show Frametime", &settings.PerfOverlay.ShowPreFGFrameTime);
					ImGui::Checkbox("Show Frametime Graph", &settings.PerfOverlay.ShowPreFGFrameTimeGraph);
				}

				ImGui::Unindent();
			}

			ImGui::Checkbox("Show Draw Calls", &settings.PerfOverlay.ShowDrawCalls);
			ImGui::Checkbox("Show VRAM Usage", &settings.PerfOverlay.ShowVRAM);

			ImGui::Unindent();
		}
		// Hotkey settings
		if (ImGui::CollapsingHeader("Hotkeys", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();

			// Add hotkey configuration for toggling overlay
			if (settingOverlayToggleKey) {
				ImGui::Text("Press any key to set as Performance Overlay toggle key...");
			} else {
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Toggle Key:");
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", KeyIdToString(settings.PerfOverlay.OverlayToggleKey));
				ImGui::AlignTextToFramePadding();
				ImGui::SameLine();
				if (ImGui::Button("Change##overlayToggle")) {
					settingOverlayToggleKey = true;
				}
			}

			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Set a key to show/hide the performance overlay");
			}
			ImGui::Unindent();
		}

		// Appearance settings
		if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();

			// Text size options
			const char* sizes[] = { "Small", "Medium", "Large" };
			int currentSize = static_cast<int>(settings.PerfOverlay.Size);
			if (ImGui::Combo("Text Size", &currentSize, sizes, IM_ARRAYSIZE(sizes))) {
				settings.PerfOverlay.Size = static_cast<Settings::PerfOverlaySettings::TextSize>(currentSize);
			}

			// Background opacity slider
			ImGui::SliderFloat("Background Opacity", &settings.PerfOverlay.BackgroundOpacity, 0.0f, 1.0f, "%.2f");

			// Border toggle
			ImGui::Checkbox("Show Border", &settings.PerfOverlay.ShowBorder);

			// FPS update interval slider - Make this slider affect all FPS and frametime displays
			ImGui::SliderFloat("Update Interval", &settings.PerfOverlay.UpdateInterval, 0.001f, 2.0f, "%.2f seconds");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("How frequently all performance metrics should update (FPS and frametime)");
			}

			// Frame history size slider
			ImGui::SliderInt("Frame History Size", &settings.PerfOverlay.FrameHistorySize, Settings::PerfOverlaySettings::kMinFrameHistorySize, Settings::PerfOverlaySettings::kMaxFrameHistorySize);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Number of frames to keep in history for graphing.\n"
					"E.g. 60 frames = 1 second @ 60fps.");
			}

			// Position options - moved inside appearance section
			ImGui::Separator();
			ImGui::Text("Position:");

			// Reset position button
			if (ImGui::Button("Reset Position")) {
				settings.PerfOverlay.PositionSet = false;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Reset the position of the performance overlay to default");
			}

			ImGui::Unindent();
		}

		ImGui::Unindent();
	}
}

const ImGuiKey Menu::VirtualKeyToImGuiKey(WPARAM vkKey)
{
	switch (vkKey) {
	case VK_TAB:
		return ImGuiKey_Tab;
	case VK_LEFT:
		return ImGuiKey_LeftArrow;
	case VK_RIGHT:
		return ImGuiKey_RightArrow;
	case VK_UP:
		return ImGuiKey_UpArrow;
	case VK_DOWN:
		return ImGuiKey_DownArrow;
	case VK_PRIOR:
		return ImGuiKey_PageUp;
	case VK_NEXT:
		return ImGuiKey_PageDown;
	case VK_HOME:
		return ImGuiKey_Home;
	case VK_END:
		return ImGuiKey_End;
	case VK_INSERT:
		return ImGuiKey_Insert;
	case VK_DELETE:
		return ImGuiKey_Delete;
	case VK_BACK:
		return ImGuiKey_Backspace;
	case VK_SPACE:
		return ImGuiKey_Space;
	case VK_RETURN:
		return ImGuiKey_Enter;
	case VK_ESCAPE:
		return ImGuiKey_Escape;
	case VK_OEM_7:
		return ImGuiKey_Apostrophe;
	case VK_OEM_COMMA:
		return ImGuiKey_Comma;
	case VK_OEM_MINUS:
		return ImGuiKey_Minus;
	case VK_OEM_PERIOD:
		return ImGuiKey_Period;
	case VK_OEM_2:
		return ImGuiKey_Slash;
	case VK_OEM_1:
		return ImGuiKey_Semicolon;
	case VK_OEM_PLUS:
		return ImGuiKey_Equal;
	case VK_OEM_4:
		return ImGuiKey_LeftBracket;
	case VK_OEM_5:
		return ImGuiKey_Backslash;
	case VK_OEM_6:
		return ImGuiKey_RightBracket;
	case VK_OEM_3:
		return ImGuiKey_GraveAccent;
	case VK_CAPITAL:
		return ImGuiKey_CapsLock;
	case VK_SCROLL:
		return ImGuiKey_ScrollLock;
	case VK_NUMLOCK:
		return ImGuiKey_NumLock;
	case VK_SNAPSHOT:
		return ImGuiKey_PrintScreen;
	case VK_PAUSE:
		return ImGuiKey_Pause;
	case VK_NUMPAD0:
		return ImGuiKey_Keypad0;
	case VK_NUMPAD1:
		return ImGuiKey_Keypad1;
	case VK_NUMPAD2:
		return ImGuiKey_Keypad2;
	case VK_NUMPAD3:
		return ImGuiKey_Keypad3;
	case VK_NUMPAD4:
		return ImGuiKey_Keypad4;
	case VK_NUMPAD5:
		return ImGuiKey_Keypad5;
	case VK_NUMPAD6:
		return ImGuiKey_Keypad6;
	case VK_NUMPAD7:
		return ImGuiKey_Keypad7;
	case VK_NUMPAD8:
		return ImGuiKey_Keypad8;
	case VK_NUMPAD9:
		return ImGuiKey_Keypad9;
	case VK_DECIMAL:
		return ImGuiKey_KeypadDecimal;
	case VK_DIVIDE:
		return ImGuiKey_KeypadDivide;
	case VK_MULTIPLY:
		return ImGuiKey_KeypadMultiply;
	case VK_SUBTRACT:
		return ImGuiKey_KeypadSubtract;
	case VK_ADD:
		return ImGuiKey_KeypadAdd;
	case IM_VK_KEYPAD_ENTER:
		return ImGuiKey_KeypadEnter;
	case VK_LSHIFT:
		return ImGuiKey_LeftShift;
	case VK_LCONTROL:
		return ImGuiKey_LeftCtrl;
	case VK_LMENU:
		return ImGuiKey_LeftAlt;
	case VK_LWIN:
		return ImGuiKey_LeftSuper;
	case VK_RSHIFT:
		return ImGuiKey_RightShift;
	case VK_RCONTROL:
		return ImGuiKey_RightCtrl;
	case VK_RMENU:
		return ImGuiKey_RightAlt;
	case VK_RWIN:
		return ImGuiKey_RightSuper;
	case VK_APPS:
		return ImGuiKey_Menu;
	case '0':
		return ImGuiKey_0;
	case '1':
		return ImGuiKey_1;
	case '2':
		return ImGuiKey_2;
	case '3':
		return ImGuiKey_3;
	case '4':
		return ImGuiKey_4;
	case '5':
		return ImGuiKey_5;
	case '6':
		return ImGuiKey_6;
	case '7':
		return ImGuiKey_7;
	case '8':
		return ImGuiKey_8;
	case '9':
		return ImGuiKey_9;
	case 'A':
		return ImGuiKey_A;
	case 'B':
		return ImGuiKey_B;
	case 'C':
		return ImGuiKey_C;
	case 'D':
		return ImGuiKey_D;
	case 'E':
		return ImGuiKey_E;
	case 'F':
		return ImGuiKey_F;
	case 'G':
		return ImGuiKey_G;
	case 'H':
		return ImGuiKey_H;
	case 'I':
		return ImGuiKey_I;
	case 'J':
		return ImGuiKey_J;
	case 'K':
		return ImGuiKey_K;
	case 'L':
		return ImGuiKey_L;
	case 'M':
		return ImGuiKey_M;
	case 'N':
		return ImGuiKey_N;
	case 'O':
		return ImGuiKey_O;
	case 'P':
		return ImGuiKey_P;
	case 'Q':
		return ImGuiKey_Q;
	case 'R':
		return ImGuiKey_R;
	case 'S':
		return ImGuiKey_S;
	case 'T':
		return ImGuiKey_T;
	case 'U':
		return ImGuiKey_U;
	case 'V':
		return ImGuiKey_V;
	case 'W':
		return ImGuiKey_W;
	case 'X':
		return ImGuiKey_X;
	case 'Y':
		return ImGuiKey_Y;
	case 'Z':
		return ImGuiKey_Z;
	case VK_F1:
		return ImGuiKey_F1;
	case VK_F2:
		return ImGuiKey_F2;
	case VK_F3:
		return ImGuiKey_F3;
	case VK_F4:
		return ImGuiKey_F4;
	case VK_F5:
		return ImGuiKey_F5;
	case VK_F6:
		return ImGuiKey_F6;
	case VK_F7:
		return ImGuiKey_F7;
	case VK_F8:
		return ImGuiKey_F8;
	case VK_F9:
		return ImGuiKey_F9;
	case VK_F10:
		return ImGuiKey_F10;
	case VK_F11:
		return ImGuiKey_F11;
	case VK_F12:
		return ImGuiKey_F12;
	default:
		return ImGuiKey_None;
	};
}

inline const uint32_t Menu::DIKToVK(uint32_t DIK)
{
	switch (DIK) {
	case DIK_LEFTARROW:
		return VK_LEFT;
	case DIK_RIGHTARROW:
		return VK_RIGHT;
	case DIK_UPARROW:
		return VK_UP;
	case DIK_DOWNARROW:
		return VK_DOWN;
	case DIK_DELETE:
		return VK_DELETE;
	case DIK_END:
		return VK_END;
	case DIK_HOME:
		return VK_HOME;  // pos1
	case DIK_PRIOR:
		return VK_PRIOR;  // page up
	case DIK_NEXT:
		return VK_NEXT;  // page down
	case DIK_INSERT:
		return VK_INSERT;
	case DIK_NUMPAD0:
		return VK_NUMPAD0;
	case DIK_NUMPAD1:
		return VK_NUMPAD1;
	case DIK_NUMPAD2:
		return VK_NUMPAD2;
	case DIK_NUMPAD3:
		return VK_NUMPAD3;
	case DIK_NUMPAD4:
		return VK_NUMPAD4;
	case DIK_NUMPAD5:
		return VK_NUMPAD5;
	case DIK_NUMPAD6:
		return VK_NUMPAD6;
	case DIK_NUMPAD7:
		return VK_NUMPAD7;
	case DIK_NUMPAD8:
		return VK_NUMPAD8;
	case DIK_NUMPAD9:
		return VK_NUMPAD9;
	case DIK_DECIMAL:
		return VK_DECIMAL;
	case DIK_NUMPADENTER:
		return IM_VK_KEYPAD_ENTER;
	case DIK_RMENU:
		return VK_RMENU;  // right alt
	case DIK_RCONTROL:
		return VK_RCONTROL;  // right control
	case DIK_LWIN:
		return VK_LWIN;  // left win
	case DIK_RWIN:
		return VK_RWIN;  // right win
	case DIK_APPS:
		return VK_APPS;
	default:
		return DIK;
	}
}

void Menu::ProcessInputEventQueue()
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	ImGuiIO& io = ImGui::GetIO();

	for (auto& event : _keyEventQueue) {
		if (event.eventType == RE::INPUT_EVENT_TYPE::kChar) {
			io.AddInputCharacter(event.keyCode);
			continue;
		}

		if (event.device == RE::INPUT_DEVICE::kMouse) {
			logger::trace("Detect mouse scan code {} value {} pressed: {}", event.keyCode, event.value, event.IsPressed());
			if (event.keyCode > 7) {  // middle scroll
				io.AddMouseWheelEvent(0, event.value * (event.keyCode == 8 ? 1 : -1));
			} else {
				if (event.keyCode > 5)
					event.keyCode = 5;
				io.AddMouseButtonEvent(event.keyCode, event.IsPressed());
			}
		}

		if (event.device == RE::INPUT_DEVICE::kKeyboard) {
			uint32_t key = DIKToVK(event.keyCode);
			logger::trace("Detected key code {} ({})", event.keyCode, key);
			if (key == event.keyCode)
				key = MapVirtualKeyEx(event.keyCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
			if (!event.IsPressed()) {
				struct HotkeyAction
				{
					uint32_t* settingKey;
					bool* settingFlag;
					std::function<void(uint32_t)> action;
				};
				auto shaderCache = globals::shaderCache;
				auto devMode = globals::state->IsDeveloperMode();
				HotkeyAction hotkeyActions[] = {
					{ &settings.ToggleKey, &settingToggleKey, [this](uint32_t key) { settings.ToggleKey = key; settingToggleKey = false; } },
					{ &settings.SkipCompilationKey, &settingSkipCompilationKey, [this](uint32_t key) { settings.SkipCompilationKey = key; settingSkipCompilationKey = false; } },
					{ &settings.EffectToggleKey, &settingsEffectsToggle, [this](uint32_t key) { settings.EffectToggleKey = key; settingsEffectsToggle = false; } },
					{ &settings.PerfOverlay.OverlayToggleKey, &settingOverlayToggleKey, [this](uint32_t key) { settings.PerfOverlay.OverlayToggleKey = key; settingOverlayToggleKey = false; } },
				};
				bool handled = false;
				for (auto& h : hotkeyActions) {
					if (*(h.settingFlag)) {
						h.action(key);
						handled = true;
						break;
					}
				}
				if (!handled) {
					struct KeyAction
					{
						uint32_t settingKey;
						std::function<void()> action;
					};
					KeyAction keyActions[] = {
						{ settings.ToggleKey, [this]() { IsEnabled = !IsEnabled; } },
						{ settings.SkipCompilationKey, [shaderCache]() { shaderCache->backgroundCompilation = true; } },
						{ settings.EffectToggleKey, [shaderCache]() { shaderCache->SetEnabled(!shaderCache->IsEnabled()); } },
						{ priorShaderKey, [shaderCache, devMode]() { if (devMode) shaderCache->IterateShaderBlock(); } },
						{ nextShaderKey, [shaderCache, devMode]() { if (devMode) shaderCache->IterateShaderBlock(false); } },
						{ settings.PerfOverlay.OverlayToggleKey, [this]() { settings.PerfOverlay.Enabled = !settings.PerfOverlay.Enabled; } },
					};
					for (auto& ka : keyActions) {
						if (key == ka.settingKey) {
							ka.action();
							break;
						}
					}
				}
				if (key == VK_ESCAPE && IsEnabled) {
					IsEnabled = false;
				}
			}

			io.AddKeyEvent(VirtualKeyToImGuiKey(key), event.IsPressed());

			if (key == VK_LCONTROL || key == VK_RCONTROL)
				io.AddKeyEvent(ImGuiMod_Ctrl, event.IsPressed());
			else if (key == VK_LSHIFT || key == VK_RSHIFT)
				io.AddKeyEvent(ImGuiMod_Shift, event.IsPressed());
			else if (key == VK_LMENU || key == VK_RMENU)
				io.AddKeyEvent(ImGuiMod_Alt, event.IsPressed());
		}
	}

	_keyEventQueue.clear();
}

void Menu::addToEventQueue(KeyEvent e)
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	_keyEventQueue.emplace_back(e);
}

void Menu::OnFocusLost()
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	_keyEventQueue.clear();
}

void Menu::ProcessInputEvents(RE::InputEvent* const* a_events)
{
	for (auto it = *a_events; it; it = it->next) {
		if (it->GetEventType() != RE::INPUT_EVENT_TYPE::kButton && it->GetEventType() != RE::INPUT_EVENT_TYPE::kChar)  // we do not care about non button or char events
			continue;

		auto event = it->GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? KeyEvent(static_cast<RE::ButtonEvent*>(it)) : KeyEvent(static_cast<CharEvent*>(it));

		addToEventQueue(event);
	}
}

bool Menu::ShouldSwallowInput()
{
	return IsEnabled;
}

const char* Menu::KeyIdToString(uint32_t key)
{
	if (key >= 256)
		return "";

	static const char* keyboard_keys_international[256] = {
		"", "Left Mouse", "Right Mouse", "Cancel", "Middle Mouse", "X1 Mouse", "X2 Mouse", "", "Backspace", "Tab", "", "", "Clear", "Enter", "", "",
		"Shift", "Control", "Alt", "Pause", "Caps Lock", "", "", "", "", "", "", "Escape", "", "", "", "",
		"Space", "Page Up", "Page Down", "End", "Home", "Left Arrow", "Up Arrow", "Right Arrow", "Down Arrow", "Select", "", "", "Print Screen", "Insert", "Delete", "Help",
		"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
		"", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
		"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Windows", "Right Windows", "Apps", "", "Sleep",
		"Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad *", "Numpad +", "", "Numpad -", "Numpad Decimal", "Numpad /",
		"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
		"F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "", "", "", "", "", "", "", "",
		"Num Lock", "Scroll Lock", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
		"Left Shift", "Right Shift", "Left Control", "Right Control", "Left Menu", "Right Menu", "Browser Back", "Browser Forward", "Browser Refresh", "Browser Stop", "Browser Search", "Browser Favorites", "Browser Home", "Volume Mute", "Volume Down", "Volume Up",
		"Next Track", "Previous Track", "Media Stop", "Media Play/Pause", "Mail", "Media Select", "Launch App 1", "Launch App 2", "", "", "OEM ;", "OEM +", "OEM ,", "OEM -", "OEM .", "OEM /",
		"OEM ~", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
		"", "", "", "", "", "", "", "", "", "", "", "OEM [", "OEM \\", "OEM ]", "OEM '", "OEM 8",
		"", "", "OEM <", "", "", "", "", "", "", "", "", "", "", "", "", "",
		"", "", "", "", "", "", "Attn", "CrSel", "ExSel", "Erase EOF", "Play", "Zoom", "", "PA1", "OEM Clear", ""
	};

	return keyboard_keys_international[key];
}

void Menu::SelectFeatureMenu(const std::string& featureName)
{
	pendingFeatureSelection = featureName;
	logger::info("Queued navigation to {} feature menu", featureName);
}

void Menu::BuildCategoryCounts() {
	const std::vector<Feature*>& features = Feature::GetFeatureList();
	// Get the category of each feature, and if it is not in the featureGroups 
	// vector, add it and increment the count
	for (auto& feature : features) {
		std::string_view category = feature->GetCategory();
		categoryCounts[std::string(category)]++;
	}
}