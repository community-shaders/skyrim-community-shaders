#include "Menu.h"

#include "IconsFontAwesome5.h"

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
#include "FeatureVersions.h"
#include "ShaderCache.h"
#include "State.h"
#include "Streamline.h"
#include "TruePBR.h"
#include "Upscaling.h"
#include "Util.h"
#include "Utils/UI.h"

#include "Features/LightLimitFix/ParticleLights.h"
#include "Features/PerformanceOverlay.h"
#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/WeatherPicker.h"

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
	ColorDefault,
	ColorHovered,
	MinimizedFactor)

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
	FontSize,
	GlobalScale,
	UseSimplePalette,
	ShowActionIcons,
	TooltipHoverDelay,
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
	OverlayToggleKey,
	Theme)

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
	style.HoverDelayNormal = themeSettings.TooltipHoverDelay;

	if (themeSettings.UseSimplePalette) {
		float hoveredAlpha{ 0.1f };

		ImVec4 resizeGripHovered = themeSettings.Palette.Border;
		resizeGripHovered.w = hoveredAlpha;

		ImVec4 textDisabled = themeSettings.Palette.Text;
		textDisabled.w = 0.3f;

		ImVec4 header{ 1.0f, 1.0f, 1.0f, 0.15f };
		ImVec4 headerHovered = header;
		headerHovered.w = hoveredAlpha;

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
	font_config.OversampleH = Constants::FCONF_OVERSAMPLE_H;
	font_config.OversampleV = Constants::FCONF_OVERSAMPLE_V;
	font_config.PixelSnapH = Constants::FCONF_PIXELSNAP_H;
	font_config.RasterizerMultiply = Constants::FCONF_RASTERIZER_MULTIPLY;

	DXGI_SWAP_CHAIN_DESC desc{};
	globals::d3d::swapChain->GetDesc(&desc);

	float fontSize = settings.Theme.FontSize;

	if (std::round(fontSize) != std::round(Constants::DEFAULT_FONT_SIZE)) {
		if (globals::state->screenSize.y > 0) {
			fontSize = globals::state->screenSize.y * Constants::DEFAULT_FONT_RATIO;
		} else {
			logger::warn("Menu::Init() - Failed to get game resolution from globals::state->screenSize.");
		}
	}

	fontSize = std::clamp(fontSize, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);

	if (!imgui_io.Fonts->AddFontFromFileTTF("Data\\Interface\\CommunityShaders\\Fonts\\Jost-Regular.ttf",
			std::round(fontSize), &font_config)) {
		logger::warn("Menu::Init() - Failed to load custom font. Using default font.");
		imgui_io.Fonts->AddFontDefault();
	}

	imgui_io.FontGlobalScale = exp2(settings.Theme.GlobalScale);

		// add font awesome 5
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.FontDataOwnedByAtlas = true;

	imgui_io.Fonts->AddFontFromFileTTF("Data\\Interface\\CommunityShaders\\Fonts\\fa-solid-900.ttf", 24, &icons_config, icons_ranges);

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(desc.OutputWindow);
	ImGui_ImplDX11_Init(globals::d3d::device, globals::d3d::context);

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
		logger::warn("Menu::Init() - Failed to load UI icons. Will fallback to text buttons");
	}

	BuildCategoryCounts();

	initialized = true;
}

void Menu::DrawSettings()
{
	if (focusChanged) {
		OnFocusChanged();
		focusChanged = false;
	}
	ImGui::DockSpaceOverViewport(NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::SetNextWindowPos(Util::GetNativeViewportSizeScaled(0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Util::GetNativeViewportSizeScaled(0.8f), ImGuiCond_FirstUseEver);
	auto title = std::format("Community Shaders {}", Util::GetFormattedVersion(Plugin::VERSION));

	// Determine window flags based on docking state
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
	// Check if this will be docked (we need to peek at the docking state)
	static bool wasDocked = false;
	bool willBeDocked = wasDocked;  // Use previous frame's state as approximation

	// Only hide title bar when not docked
	if (!willBeDocked) {
		windowFlags |= ImGuiWindowFlags_NoTitleBar;
	}

	ImGui::Begin(title.c_str(), &IsEnabled, windowFlags);
	{
		auto shaderCache = globals::shaderCache;
		// Update docking state tracking
		bool isDocked = ImGui::IsWindowDocked();
		wasDocked = isDocked;

		const float uiScale = exp2(settings.Theme.GlobalScale);  // Get current UI scale
																 // Check if we can show icons - require setting enabled and at least some icons loaded (for undocked)
		// For docked mode, always show icons if textures are available
		bool canShowIcons = settings.Theme.ShowActionIcons &&
		                    (uiIcons.saveSettings.texture ||
								uiIcons.loadSettings.texture ||
								uiIcons.clearCache.texture ||
								uiIcons.clearDiskCache.texture);

		// Always show logo if available, regardless of action icons setting
		bool showLogo = uiIcons.logo.texture != nullptr;
		// Define action icon metadata and callbacks
		struct ActionIcon
		{
			ID3D11ShaderResourceView* texture;
			const char* tooltip;
			std::function<void()> callback;
		};
		std::vector<ActionIcon> actionIcons;
		// Populate icons if user setting allows icons and textures are available
		if (canShowIcons) {
			// Build list of available action icons (in display order)
			if (uiIcons.saveSettings.texture) {
				actionIcons.push_back({ uiIcons.saveSettings.texture,
					"Save Settings",
					[]() { globals::state->Save(); } });
			}
			if (uiIcons.loadSettings.texture) {
				actionIcons.push_back({ uiIcons.loadSettings.texture,
					"Load Settings",
					[]() {
						globals::state->Load();
						globals::features::llf::particleLights->GetConfigs();
					} });
			}
			if (uiIcons.clearCache.texture) {
				actionIcons.push_back({ uiIcons.clearCache.texture,
					"Clear Shader Cache\n\n"
					"The Shader Cache is the collection of compiled shaders which replace\n"
					"the vanilla shaders at runtime. Clearing the shader cache will mean\n"
					"that shaders are recompiled only when the game re-encounters them.\n"
					"This is only needed for hot-loading shaders for development purposes.",
					[shaderCache]() { shaderCache->Clear(); } });
			}
			if (uiIcons.clearDiskCache.texture) {
				actionIcons.push_back({ uiIcons.clearDiskCache.texture,
					"Clear Disk Cache\n\n"
					"The Disk Cache is a collection of compiled shaders on disk, which\n"
					"are automatically created when shaders are added to the Shader Cache.\n"
					"If you do not have a Disk Cache, or it is outdated or invalid, you will\n"
					"see \"Compiling Shaders\" in the upper-left corner. After this has\n"
					"completed you will no longer see this message apart from when loading\n"
					"from the Disk Cache. Only delete the Disk Cache manually if you are\n"
					"encountering issues.",
					[shaderCache]() { shaderCache->DeleteDiskCache(); } });
			}
		}

		// Unified function to render action icons for both docked and undocked states
		auto renderActionIcons = [&](bool isDocked) {
			if (actionIcons.empty())
				return;

			if (isDocked) {
				// Docked: Draw larger icons in the title bar using foreground draw list
				const float iconSize = 40.0f * uiScale;  // Increased by 10% for better visual balance
				const float iconSpacing = 8.0f * uiScale;
				const float rightMargin = 45.0f * uiScale;  // Space for close button

				// Get window position and calculate title bar area
				ImVec2 windowPos = ImGui::GetWindowPos();
				ImVec2 windowSize = ImGui::GetWindowSize();
				float titleBarHeight = ImGui::GetFrameHeight();

				// Use foreground draw list to draw over the title bar
				ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

				// Calculate icon positions (right to left from close button)
				float iconX = windowPos.x + windowSize.x - rightMargin;
				float iconY = windowPos.y + (titleBarHeight - iconSize) * 0.5f;
				// Draw icons from right to left
				for (auto it = actionIcons.rbegin(); it != actionIcons.rend(); ++it) {
					iconX -= iconSize + iconSpacing;

					// Slightly reduce the icon rendering area to minimize any transparent padding
					const float paddingReduction = 2.0f * uiScale;
					ImVec2 iconMin(iconX + paddingReduction, iconY + paddingReduction);
					ImVec2 iconMax(iconX + iconSize - paddingReduction, iconY + iconSize - paddingReduction);

					// Use the full area for mouse interaction (including padding)
					ImVec2 interactionMin(iconX, iconY);
					ImVec2 interactionMax(iconX + iconSize, iconY + iconSize);

					// Check mouse interaction against full area
					ImVec2 mousePos = ImGui::GetMousePos();
					bool isHovered = mousePos.x >= interactionMin.x && mousePos.x <= interactionMax.x &&
					                 mousePos.y >= interactionMin.y && mousePos.y <= interactionMax.y;

					// Draw icon with hover effect, using reduced area to minimize padding
					ImU32 tintColor = isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 220);
					fgDrawList->AddImage(it->texture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tintColor);
					// Handle interaction
					if (isHovered) {
						// Draw subtle background for hovered icon using interaction area
						fgDrawList->AddRectFilled(interactionMin, interactionMax, IM_COL32(255, 255, 255, 40));

						if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
							it->callback();
						}

						// Set tooltip manually since we're drawing outside normal ImGui flow
						ImGui::SetTooltip("%s", it->tooltip);
					}
				}
			} else {                               // Undocked: Draw icons as ImageButtons in a table column
				const float baseIconSize = 48.0f;  // Reduced by 25% from 64.0f for better proportions
				const float iconSize = baseIconSize * uiScale;
				const float paddingReduction = 4.0f * uiScale;  // Reduce padding to minimize dead space
				const ImVec2 buttonSize(iconSize, iconSize);
				const ImVec2 imageSize(iconSize - paddingReduction, iconSize - paddingReduction);

				// Setup button styling for transparent background with hover effects
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));              // Slightly increased spacing
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);                        // Remove button borders
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));                      // Transparent button background
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));  // Slightly more visible hover effect

				// Draw action icons as ImageButtons
				for (size_t i = 0; i < actionIcons.size(); ++i) {
					const auto& icon = actionIcons[i];
					std::string buttonId = std::format("##ActionBtn{}", i);

					// Use ImageButton with reduced image size to minimize padding
					if (ImGui::ImageButton(buttonId.c_str(), icon.texture, imageSize)) {
						icon.callback();
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("%s", icon.tooltip);
					}

					// Add SameLine except for the last button
					if (i < actionIcons.size() - 1) {
						ImGui::SameLine();
					}
				}

				// Restore default style
				ImGui::PopStyleVar(2);    // Pop both style variables: ItemSpacing and FrameBorderSize
				ImGui::PopStyleColor(2);  // Pop both style colors: Button and ButtonHovered
			}
		};
		// Handle docked vs undocked layout differently
		if (isDocked) {
			// When docked, draw logo as a background watermark if available
			if (showLogo && uiIcons.logo.texture) {
				// Get current window's drawable area (excluding title bar)
				ImVec2 windowPos = ImGui::GetWindowPos();
				ImVec2 windowSize = ImGui::GetWindowSize();
				float titleBarHeight = ImGui::GetFrameHeight();

				// Calculate content area (below title bar)
				ImVec2 contentPos(windowPos.x, windowPos.y + titleBarHeight);
				ImVec2 contentSize(windowSize.x, windowSize.y - titleBarHeight);
				// Calculate watermark logo size - base it on height for consistent sizing
				const float watermarkHeightPercent = 0.50f;  // 25% of content height
				float watermarkHeight = contentSize.y * watermarkHeightPercent;
				float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
				float watermarkWidth = watermarkHeight * logoAspectRatio;

				// Position watermark in the center of the content area
				float logoX = contentPos.x + (contentSize.x - watermarkWidth) * 0.5f;   // Horizontally centered
				float logoY = contentPos.y + (contentSize.y - watermarkHeight) * 0.5f;  // Vertically centered

				// Draw watermark logo with transparency and blending
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				ImVec2 logoMin(logoX, logoY);
				ImVec2 logoMax(logoX + watermarkWidth, logoY + watermarkHeight);

				// Use very low alpha for subtle watermark effect
				ImU32 watermarkColor = IM_COL32(255, 255, 255, 45);
				drawList->AddImage(uiIcons.logo.texture, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), watermarkColor);
			}

			// Draw action icons in the title bar area
			renderActionIcons(true);
		} else {
			// When not docked, show the custom header

			// Begin a layout - with or without action buttons depending on settings
			if ((showLogo || canShowIcons) && ImGui::BeginTable("##HeaderLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableNextColumn();  // Title on the left with logo

				// Determine scaling based on GlobalScale setting
				const float baseTextScale = 1.7f;
				const float baseIconSize = 48.0f;  // Reduced by 25% from 64.0f to match action icons

				// Apply UI scale to the base scaling factors
				const float textScaleFactor = baseTextScale * uiScale;
				const float logoSize = baseIconSize * uiScale;  // Match action icon size

				// Always display logo if texture is available
				if (showLogo) {
					float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
					ImVec2 logoSizeVec(logoSize * logoAspectRatio, logoSize);

					// Add a bit of padding before the logo and text
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);

					// Use our helper to render aligned logo and text with perfect vertical alignment
					Util::DrawAlignedTextWithLogo(
						uiIcons.logo.texture,
						logoSizeVec,
						title.c_str(),
						textScaleFactor);
				} else {
					// No logo, just render the text with proper alignment
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);
					Util::DrawSharpText(title.c_str(), true, textScaleFactor);
					ImGui::PopStyleVar();
				}

				// Buttons on the right
				ImGui::TableNextColumn();
				renderActionIcons(false);

				ImGui::EndTable();
			} else if (!(showLogo || canShowIcons)) {
				// No icons available - show just the title without the table layout
				const float baseTextScale = 1.5f;
				const float textScaleFactor = baseTextScale * uiScale;  // Apply UI scale

				ImGui::SetWindowFontScale(textScaleFactor);
				ImGui::TextUnformatted(title.c_str());
				ImGui::SetWindowFontScale(1.0f);
			}
		}
		// Add separators - no separator needed for docked mode since icons are in title bar
		if (!isDocked) {
			// First separator - always shown when not docked
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
			ImGui::Spacing();
		}
		// If icons are disabled or missing, show action buttons as text between separators (only when not docked)
		if (!canShowIcons && !isDocked) {
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

			// Second separator - only shown if icons are disabled/missing or if there are failed tasks (and not docked)
			if (!isDocked) {
				ImGui::Spacing();
				ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
				ImGui::Spacing();
			}
		} else if (shaderCache->GetFailedTasks() && !isDocked) {
			// If icons are enabled but there are failed tasks, show error toggle button
			// and add the second separator (only when not docked)
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
			ImGui::Spacing();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
			ImGui::Spacing();
		} else {  // No additional separator needed - already handled in the conditional block above
		}

		// Main content starts here - no additional separator needed as it's already handled in the conditions above

		float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3 + 3.0f;  // text + separator

		ImGui::BeginChild("Menus Table", ImVec2(0, -footer_height));

		// Move all declarations before the table setup
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
					// No failed message but not loaded - check if INI file exists
					if (!std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feat->GetShortName()))) {
						// INI file missing - treat as missing feature (grey)
						textColor = themeSettings.StatusPalette.Disable;
					} else {
						// INI file exists but feature not loaded - truly pending restart (green)
						textColor = themeSettings.StatusPalette.RestartNeeded;
					}
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
					std::string formattedVersion = feat->version;
					std::replace(formattedVersion.begin(), formattedVersion.end(), '-', '.');
					ImGui::TextDisabled(fmt::format("({})", formattedVersion).c_str());
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
				// Calculate button widths based on text content
				const char* bootButtonText = isDisabled ? "Enable at Boot" : "Disable at Boot";
				const char* defaultsButtonText = "Restore Defaults";

				float buttonPadding = 16.0f;
				float buttonSpacing = 8.0f;
				float bootButtonWidth = ImGui::CalcTextSize(bootButtonText).x + buttonPadding;
				float defaultsButtonWidth = ImGui::CalcTextSize(defaultsButtonText).x + buttonPadding;

				float totalButtonWidth = bootButtonWidth;
				if (!isDisabled && isLoaded) {
					totalButtonWidth += defaultsButtonWidth + buttonSpacing;
				}

				if (ImGui::BeginTabBar("##FeatureTabs", ImGuiTabBarFlags_Reorderable)) {
					// Draw standard tabs
					if (ImGui::BeginTabItem("Settings")) {
						if (ImGui::BeginChild("##FeatureSettingsFrame", { 0, 0 }, true)) {
							// Feature-specific settings section
							ImGui::SeparatorText("Feature Settings");
							if (isDisabled) {
								// Show disabled message
								ImGui::TextColored(themeSettings.StatusPalette.Disable, "Feature settings are hidden because this feature is disabled at boot.");
								ImGui::Spacing();
								ImGui::Text("Enable the feature above to access its configuration options.");
							} else {
								if (isLoaded) {
									// Check if the feature has any settings by monitoring cursor position (if the feature draws settings, the imgui cursor position will change)
									ImVec2 cursorPosBefore = ImGui::GetCursorPos();

									feat->DrawSettings();

									ImVec2 cursorPosAfter = ImGui::GetCursorPos();

									// If cursor position hasn't changed significantly, no visible settings were drawn
									const float epsilon = 0.1f;  // Simple check to ensure we don't trigger on minor cursor movements / weird imgui math
									bool cursorMoved = (std::abs(cursorPosAfter.x - cursorPosBefore.x) > epsilon ||
														std::abs(cursorPosAfter.y - cursorPosBefore.y) > epsilon);
									if (!cursorMoved) {
										ImGui::TextColored(themeSettings.StatusPalette.Disable, "There are no settings available for this feature.");
									}
								} else {
									// Check if feature is obsolete first - always show error for obsolete features
									if (FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
										// Obsolete feature - show detailed unloaded UI with error info
										feat->DrawUnloadedUI();
									} else if (std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feat->GetShortName()))) {
										// INI file exists - show simple pending restart message
										ImGui::Text("This feature will be available after restart.");
									} else {
										// INI file missing - show detailed unloaded UI with installation info
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
							}

							// Error Messages (Not for obsolete features as this is already covered by DrawUnloadedUI)
							if (hasFailedMessage && feat->DrawFailLoadMessage() && !FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
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
								// Check if INI file exists to determine actual status
								if (!std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feat->GetShortName()))) {
									// INI file missing - feature not installed
									statusColor = themeSettings.StatusPalette.Error;
									statusText = "Not installed.";
								} else {
									// INI file exists but feature not loaded - truly pending restart
									statusColor = themeSettings.StatusPalette.RestartNeeded;
									statusText = "Pending restart.";
								}
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
								if (hasFailedMessage) {
									ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", feat->failedLoadedMessage.c_str());
								} else {
									// For features that are pending restart or not installed,
									// the detailed information is shown in the Settings tab.
									// Here we just show a simple message directing users there.
									if (!std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feat->GetShortName()))) {
										ImGui::Text("Feature installation details are available in the Settings tab.");
									} else {
										// INI file exists but feature not loaded - truly pending restart
										ImGui::Text("This feature is pending restart.");
									}
								}
							}
						}
						ImGui::EndChild();
						ImGui::EndTabItem();
					}

					// Position buttons on the right side of the tab bar
					ImGui::SameLine();
					float availableSpace = ImGui::GetContentRegionAvail().x;
					float rightOffset = availableSpace - totalButtonWidth;
					if (rightOffset > 0) {
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rightOffset);
					}

					// Disable/Enable at boot button
					ImVec4 textColor;
					if (isDisabled) {
						textColor = themeSettings.StatusPalette.Disable;
					} else if (hasFailedMessage) {
						textColor = themeSettings.StatusPalette.Error;
					} else {
						textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
					}

					ImGui::PushStyleColor(ImGuiCol_Text, textColor);
					if (ImGui::Button(bootButtonText, { bootButtonWidth, 0 })) {
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

					// Restore Defaults button (when feature is not disabled and is loaded)
					if (!isDisabled && isLoaded) {
						ImGui::SameLine();
						if (ImGui::Button(defaultsButtonText, { defaultsButtonWidth, 0 })) {
							feat->RestoreDefaultSettings();
						}

						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text(
								"Restores the feature's settings back to their default values. "
								"You will still need to Save Settings to make these changes permanent.");
						}
					}
				}
				ImGui::EndTabBar();
			}
		};

		// Build the menu list
		auto& featureList = Feature::GetFeatureList();
		auto sortedFeatureList{ featureList };  // need a copy so the load order is not lost
		std::ranges::sort(sortedFeatureList, [](Feature* a, Feature* b) {
			return a->GetName() < b->GetName();
		});

		// Filter features by search string
		if (!featureSearch.empty()) {
			auto it = std::remove_if(sortedFeatureList.begin(), sortedFeatureList.end(),
				[this](Feature* feat) { return !Util::FeatureMatchesSearch(feat, featureSearch); });
			sortedFeatureList.erase(it, sortedFeatureList.end());
		}

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
			return !feat->loaded && feat->IsInMenu() && (!FeatureIssues::IsObsoleteFeature(feat->GetShortName()) || globals::state->IsDeveloperMode());
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

		// Now create the table with the declared variables available
		if (ImGui::BeginTable("Menus Table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
			ImGui::TableSetupColumn("##ListOfMenus", 0, 2);
			ImGui::TableSetupColumn("##MenuConfig", 0, 8);

			ImGui::TableNextColumn();
			// Draw the feature list
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
			if (ImGui::BeginListBox("##MenusList", { -FLT_MIN, -FLT_MIN })) {
				// Find where built-in menus end (General, Advanced, Display)
				size_t builtInMenuCount = 0;
				for (size_t i = 0; i < menuList.size(); i++) {
					if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
						BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
						if (menu.name == "General" || menu.name == "Advanced" || menu.name == "Display") {
							builtInMenuCount++;
						}
					}
				}

				// First render the built-in menus (General, Advanced, Display)
				size_t renderedBuiltIns = 0;
				for (size_t i = 0; i < menuList.size() && renderedBuiltIns < 3; i++) {
					if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
						BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
						if (menu.name == "General" || menu.name == "Advanced" || menu.name == "Display") {
							std::visit(ListMenuVisitor{ i, selectedMenu }, menuList[i]);
							renderedBuiltIns++;
						}
					}
				}

				// Add Features header and search bar after built-in settings
				Util::DrawSectionHeader("Features", true);
				Util::DrawFeatureSearchBar(featureSearch);

				// Then render the rest (features and categories, but skip already rendered built-ins)
				for (size_t i = 0; i < menuList.size(); i++) {
					if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
						BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
						if (menu.name == "General" || menu.name == "Advanced" || menu.name == "Display") {
							continue;  // Skip, already rendered
						}
					}
					std::visit(ListMenuVisitor{ i, selectedMenu }, menuList[i]);
				}

				ImGui::EndListBox();
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();

			ImGui::TableNextColumn();
			ImGui::Dummy(ImVec2(0, 8));  // spacing

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

	if (ImGui::BeginTabBar("##GeneralTabBar", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Shaders")) {
			bool useCustomShaders = shaderCache->IsEnabled();
			if (ImGui::Checkbox("Use Custom Shaders", &useCustomShaders)) {
				shaderCache->SetEnabled(useCustomShaders);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Disabling this effectively disables all features.");
			}

			bool useDiskCache = shaderCache->IsDiskCache();
			if (ImGui::Checkbox("Enable Disk Cache", &useDiskCache)) {
				shaderCache->SetDiskCache(useDiskCache);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Disabling this stops shaders from being loaded from disk, as well as stops shaders from being saved to it.");
			}

			bool useAsync = shaderCache->IsAsync();
			if (ImGui::Checkbox("Enable Async", &useAsync)) {
				shaderCache->SetAsync(useAsync);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Skips a shader being replaced if it hasn't been compiled yet. Also makes compilation blazingly fast!");
			}

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Keybindings")) {
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
			if (settingOverlayToggleKey) {
				ImGui::Text("Press any key to set as a toggle key for displaying the overlay...");
			} else {
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Overlay Toggle Key:");
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", KeyIdToString(settings.OverlayToggleKey));

				ImGui::AlignTextToFramePadding();
				ImGui::SameLine();
				if (ImGui::Button("Change##OverlayToggle")) {
					settingOverlayToggleKey = true;
				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Interface")) {
			auto& style = themeSettings.Style;
			auto& colors = themeSettings.FullPalette;

			if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
				if (ImGui::BeginTabItem("UI Options")) {
					ImGui::SeparatorText("UI Elements");
					ImGui::Checkbox("Use Icon Buttons in Header", &themeSettings.ShowActionIcons);
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(
							"When enabled: Shows action buttons (Save, Load, Clear Cache, Clear Disk Cache) as icons in the header\n"
							"When disabled: Shows as text buttons below the header");
					}

					ImGui::SliderFloat("Tooltip Hover Delay", &themeSettings.TooltipHoverDelay, 0.0f, 2.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted("Time in seconds to wait before a tooltip appears when hovering over an item.");
					}

					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Sizes")) {
					ImGui::SeparatorText("Main");
					if (ImGui::SliderFloat("Global Scale", &themeSettings.GlobalScale, -1.f, 1.f, "%.2f")) {
						float trueScale = exp2(themeSettings.GlobalScale);

						auto& io = ImGui::GetIO();
						io.FontGlobalScale = trueScale;
					}
					ImGui::SliderFloat("Font Size", &themeSettings.FontSize, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE, "%.0f");
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

					ImGui::ColorEdit4("Regular", (float*)&themeSettings.FeatureHeading.ColorDefault);
					ImGui::ColorEdit4("Hovered", (float*)&themeSettings.FeatureHeading.ColorHovered);
					ImGui::SliderFloat("Minimized Alpha Factor", &themeSettings.FeatureHeading.MinimizedFactor, 0.0f, 1.0f, "%.2f");

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
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
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

		// A/B Testing settings
		auto* abTestingManager = ABTestingManager::GetSingleton();
		abTestingManager->DrawSettingsUI();
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
			globals::state->ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
				ImGui::TableNextColumn();

				if (!(SIE::ShaderCache::IsSupportedShader(type) || state->IsDeveloperMode())) {
					ImGui::BeginDisabled();
					ImGui::Checkbox(std::format("{}", magic_enum::enum_name(type)).c_str(), &state->enabledClasses[classIndex]);
					ImGui::EndDisabled();
				} else
					ImGui::Checkbox(std::format("{}", magic_enum::enum_name(type)).c_str(), &state->enabledClasses[classIndex]);
			});
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
			{ "Upscaling", []() { globals::upscaling->DrawSettings(); } }
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
	ProcessInputEventQueue();  // Synchronize Inputs to frame

	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetFailedTasks();
	auto hide = shaderCache->IsHideErrors();
	auto* abTestingManager = ABTestingManager::GetSingleton();
	if (!(shaderCache->IsCompiling() || IsEnabled || abTestingManager->IsEnabled() || (failed && !hide) || PerformanceOverlay::GetSingleton()->settings.ShowInOverlay)) {
		auto& io = ImGui::GetIO();
		io.ClearInputKeys();
		io.ClearEventsQueue();
		return;
	}

	// Reload font if user changed something
	if (std::abs(cachedFontSize - settings.Theme.FontSize) > 0.01f) {
		ReloadFont();
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
	// load overlays
	for (Feature* feat : Feature::GetFeatureList()) {
		if (feat && feat->loaded) {
			if (auto* overlay = dynamic_cast<OverlayFeature*>(feat)) {
				overlay->DrawOverlay();
			}
		}
	}

	// A/B Testing management
	abTestingManager->Update();

	// Always update test data during TEST phase, regardless of overlay visibility
	if (abTestingManager->IsEnabled()) {
		PerformanceOverlay::GetSingleton()->UpdateAllShaderTestData();

		// Add A/B test aggregator data collection here
		if (auto* overlay = PerformanceOverlay::GetSingleton()) {
			auto [mainRows, summaryRows] = overlay->BuildDrawCallRows();
			std::vector<DrawCallRow> allRows = mainRows;
			allRows.insert(allRows.end(), summaryRows.begin(), summaryRows.end());

			// Update the A/B test aggregator with current frame data
			abTestingManager->GetAggregator().OnFrame(allRows);
		}
	}

	// Draw A/B testing overlay
	abTestingManager->DrawOverlayUI();

	ImGuiStyle& style = ImGui::GetStyle();
	style = oldStyle;

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

/**
 * @brief Renders the current draw call counts for various shader types using ImGui.
 *
 * Displays a breakdown of draw calls by type (e.g., Grass, Sky, Water, etc.) and the total count.
 * Values are sourced from the global state.
 */

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
					{ &settings.OverlayToggleKey, &settingOverlayToggleKey, [this](uint32_t key) { settings.OverlayToggleKey = key; settingOverlayToggleKey = false; } },
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
						{ settings.OverlayToggleKey, []() {
							 Menu::GetSingleton()->overlayVisible = !Menu::GetSingleton()->overlayVisible;
						 } },
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

	// Fallback: release stuck Shift and Tab if OS reports them not pressed
	if ((io.KeysDown[ImGuiKey_LeftShift] && !(GetAsyncKeyState(VK_LSHIFT) & Constants::KEY_PRESSED_MASK)) ||
		(io.KeysDown[ImGuiKey_RightShift] && !(GetAsyncKeyState(VK_RSHIFT) & Constants::KEY_PRESSED_MASK))) {
		io.AddKeyEvent(ImGuiKey_LeftShift, false);
		io.AddKeyEvent(ImGuiKey_RightShift, false);
	}
	if (io.KeysDown[ImGuiKey_Tab] && !(GetAsyncKeyState(VK_TAB) & Constants::KEY_PRESSED_MASK)) {
		io.AddKeyEvent(ImGuiKey_Tab, false);
	}
}

void Menu::addToEventQueue(KeyEvent e)
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	_keyEventQueue.emplace_back(e);
}

void Menu::OnFocusChanged()
{
	// Solves the alt+tab stuck issue, but disables tab after tabbing back in.
	if (const auto& inputMgr = RE::BSInputDeviceManager::GetSingleton()) {
		if (const auto& device = inputMgr->GetKeyboard()) {
			device->Reset();
		}
	}
	// Allows tab to work again after alt+tabbing back in.
	ImGui::GetIO().ClearInputKeys();
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

void Menu::DrawWeatherDetailsWindow()
{
	if (!WeatherPicker::GetSingleton()->WeatherDetailsWindow.Enabled) {
		return;
	}

	// Use Weather core feature for all window management and rendering
	auto weather = globals::features::weatherPicker;
	if (weather) {
		bool* p_open = &WeatherPicker::GetSingleton()->WeatherDetailsWindow.Enabled;
		weather->RenderWeatherDetailsWindow(p_open);
	}
}

void Menu::BuildCategoryCounts()
{
	const std::vector<Feature*>& features = Feature::GetFeatureList();
	// Get the category of each feature, and increment the count for that category
	for (auto& feature : features) {
		if (feature->IsInMenu() && feature->loaded) {
			std::string_view category = feature->GetCategory();
			categoryCounts[std::string(category)]++;
		}
	}
}

void Menu::ReloadFont()
{
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();

	ImFontConfig font_config;

	font_config.OversampleH = Constants::FCONF_OVERSAMPLE_H;                // Increased horizontal oversampling for sharper text
	font_config.OversampleV = Constants::FCONF_OVERSAMPLE_V;                // Increased vertical oversampling
	font_config.PixelSnapH = Constants::FCONF_PIXELSNAP_H;                  // Align to pixel grid for sharper rendering
	font_config.RasterizerMultiply = Constants::FCONF_RASTERIZER_MULTIPLY;  // Slightly darker font rendering

	float fontSize = settings.Theme.FontSize;
	fontSize = std::clamp(fontSize, Constants::MIN_FONT_SIZE,
		Constants::MAX_FONT_SIZE);

	if (!io.Fonts->AddFontFromFileTTF("Data\\Interface\\CommunityShaders\\Fonts\\Jost-Regular.ttf",
			std::round(fontSize), &font_config)) {
		logger::warn("Menu::ReloadFont() - Failed to load custom font. Using default font.");
		io.Fonts->AddFontDefault();
	}

	io.Fonts->Build();

	ImGui_ImplDX11_InvalidateDeviceObjects();

	io.FontGlobalScale = exp2(settings.Theme.GlobalScale);

	cachedFontSize = settings.Theme.FontSize;
}