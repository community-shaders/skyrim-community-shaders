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
#include "ShaderCache.h"
#include "State.h"
#include "Streamline.h"
#include "TruePBR.h"
#include "Upscaling.h"

#include "Features/LightLimitFix/ParticleLights.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::PaletteColors,
	Background,
	Text,
	Border)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::StatusPaletteColors,
	Disable,
	RestartNeeded,
	Error,
	CurrentHotkey)

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
	Palette,
	Style,
	FullPalette)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::Settings,
	ToggleKey,
	SkipCompilationKey,
	EffectToggleKey,
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
bool ShowPerfOverlay = false;

Menu::~Menu()
{
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

	ImFontConfig font_config;
	font_config.GlyphExtraSpacing.x = -0.5;

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
		if (!ImGui::IsWindowDocked()) {
			ImGui::SetWindowFontScale(1.5f);
			ImGui::TextUnformatted(title.c_str());
			ImGui::SetWindowFontScale(1.f);

			ImGui::Spacing();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
			ImGui::Spacing();
		}

		auto shaderCache = globals::shaderCache;

		if (ImGui::BeginTable("##LeButtons", 4, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableNextColumn();
			if (ImGui::Button("Save Settings", { -1, 0 })) {
				globals::state->Save();
			}

			ImGui::TableNextColumn();
			if (ImGui::Button("Load Settings", { -1, 0 })) {
				globals::state->Load();
				globals::features::llf::particleLights->GetConfigs();
			}

			ImGui::TableNextColumn();
			if (ImGui::Button("Clear Shader Cache", { -1, 0 })) {
				shaderCache->Clear();
				// any features should be added to shadercache's clear.
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"The Shader Cache is the collection of compiled shaders which replace the vanilla shaders at runtime. "
					"Clearing the shader cache will mean that shaders are recompiled only when the game re-encounters them. "
					"This is only needed for hot-loading shaders for development purposes. ");
			}

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

		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
		ImGui::Spacing();

		float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3 + 3.0f;  // text + separator

		ImGui::BeginChild("Menus Table", ImVec2(0, -footer_height));
		if (ImGui::BeginTable("Menus Table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
			ImGui::TableSetupColumn("##ListOfMenus", 0, 2);
			ImGui::TableSetupColumn("##MenuConfig", 0, 8);

			static size_t selectedMenu = 0;

			// some type erasure bs for virtual-free polymorphism
			struct BuiltInMenu
			{
				std::string name;
				std::function<void()> func;
			};
			using MenuFuncInfo = std::variant<BuiltInMenu, std::string, Feature*>;
			struct ListMenuVisitor
			{
				size_t listId;

				void operator()(const BuiltInMenu& menu)
				{
					if (ImGui::Selectable(fmt::format(" {} ", menu.name).c_str(), selectedMenu == listId, ImGuiSelectableFlags_SpanAllColumns))
						selectedMenu = listId;
				}
				void operator()(const std::string& label)
				{
					ImGui::SeparatorText(label.c_str());
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
					if (ImGui::Selectable(fmt::format(" {} ", feat->GetName()).c_str(), selectedMenu == listId, ImGuiSelectableFlags_SpanAllColumns)) {
						selectedMenu = listId;
					}

					// Restore original text color
					ImGui::PopStyleColor();

					// Show tooltip based on the state
					if (isDisabled) {
						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text("Disabled at boot. Reenable, save settings, and restart.");
						}
					} else if (!isLoaded) {
						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text(hasFailedMessage ? feat->failedLoadedMessage.c_str() : "Feature pending restart.");
						}
					}

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
				void operator()(Feature* feat)
				{
					const auto featureName = feat->GetShortName();
					bool isDisabled = globals::state->IsFeatureDisabled(featureName);
					bool isLoaded = feat->loaded;
					bool hasFailedMessage = !feat->failedLoadedMessage.empty();
					auto& themeSettings = globals::menu->settings.Theme;

					if (ImGui::BeginTable("##FeatureButtons", 2, ImGuiTableFlags_SizingStretchSame)) {
						ImGui::TableNextColumn();

						ImVec4 textColor;

						// Determine the text color based on the state
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

						ImGui::PopStyleColor();

						ImGui::TableNextColumn();

						if (!isDisabled && isLoaded) {
							if (ImGui::Button("Restore Defaults", { -1, 0 })) {
								feat->RestoreDefaultSettings();
							}
							if (auto _tt = Util::HoverTooltipWrapper()) {
								ImGui::Text(
									"Restores the feature's settings back to their default values. "
									"You will still need to Save Settings to make these changes permanent.");
							}
						}

						ImGui::EndTable();
					}

					if (hasFailedMessage) {
						ImGui::TextColored(themeSettings.StatusPalette.Error, feat->failedLoadedMessage.c_str());
					}

					if (!isDisabled && isLoaded) {
						if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, true)) {
							feat->DrawSettings();
						}
						ImGui::EndChild();
					}
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
			};

			menuList.push_back("Core Features"s);
			std::ranges::copy(
				sortedFeatureList | std::ranges::views::filter([](Feature* feat) {
					return feat->IsCore() && feat->loaded;
				}),
				std::back_inserter(menuList));

			menuList.push_back("Features"s);
			std::ranges::copy(
				sortedFeatureList | std::ranges::views::filter([](Feature* feat) {
					return !feat->IsCore() && feat->loaded;
				}),
				std::back_inserter(menuList));

			auto unloadedFeatures = sortedFeatureList | std::ranges::views::filter([](Feature* feat) {
				return !feat->loaded;
			});
			if (std::ranges::distance(unloadedFeatures) != 0) {
				menuList.push_back("Unloaded Features"s);
				std::ranges::copy(unloadedFeatures, std::back_inserter(menuList));
			}

			ImGui::TableNextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
			if (ImGui::BeginListBox("##MenusList", { -FLT_MIN, -FLT_MIN })) {
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
				for (size_t i = 0; i < menuList.size(); i++) {
					std::visit(ListMenuVisitor{ i }, menuList[i]);
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
			ImGui::TableNextColumn();
			ImGui::Checkbox("Perf Overlay", &ShowPerfOverlay);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Shows an overlay with performance statistics.");
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
			if (ImGui::BeginTabItem("Sizes")) {
				if (ImGui::SliderFloat("Global Scale", &themeSettings.GlobalScale, -1.f, 1.f, "%.2f")) {
					float trueScale = exp2(themeSettings.GlobalScale);

					auto& io = ImGui::GetIO();
					io.FontGlobalScale = trueScale;
				}

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
				ImGui::ColorEdit4("Restart Needed Text", (float*)&themeSettings.StatusPalette.RestartNeeded);
				ImGui::ColorEdit4("Current Hotkey Text", (float*)&themeSettings.StatusPalette.CurrentHotkey);

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
	ProcessInputEventQueue();  //Synchronize Inputs to frame

	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetFailedTasks();
	auto hide = shaderCache->IsHideErrors();

	if (!(shaderCache->IsCompiling() || IsEnabled || inTestMode || (failed && !hide) || ShowPerfOverlay)) {
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
			ImGui::End();
		}
	}

	if (IsEnabled) {
		ImGui::GetIO().MouseDrawCursor = true;
		DrawSettings();
	} else {
		ImGui::GetIO().MouseDrawCursor = false;
	}

	if (ShowPerfOverlay)
		DrawPerfOverlay();

	if (inTestMode) {  // In test mode
		float seconds = (float)duration_cast<std::chrono::milliseconds>(high_resolution_clock::now() - lastTestSwitch).count() / 1000;
		auto remaining = (float)testInterval - seconds;
		if (remaining < 0) {
			usingTestConfig = !usingTestConfig;
			logger::info("Swapping mode to {}", usingTestConfig ? "test" : "user");
			globals::state->Load(usingTestConfig ? State::ConfigMode::TEST : State::ConfigMode::USER);
			lastTestSwitch = high_resolution_clock::now();
		}
		ImGui::SetNextWindowBgAlpha(1);
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
	ImGui::SetNextWindowPos(ImVec2(Util::GetNativeViewportSizeScaled(1.f).x, Util::GetNativeViewportSizeScaled(0.f).y + 20.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
	ImGui::SetNextWindowSize(Util::GetNativeViewportSizeScaled(0.2f), ImGuiCond_Appearing);
	ImGui::Begin("PerformanceOverlay", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
	{
		ImGui::Text("Draw Calls:");
		//ImGui::Text(std::format("None: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::None])).c_str());
		ImGui::Text(std::format("Grass: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Grass])).c_str());
		ImGui::Text(std::format("Sky: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Sky])).c_str());
		ImGui::Text(std::format("Water: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Water])).c_str());
		ImGui::Text(std::format("BloodSplatter: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::BloodSplatter])).c_str());
		ImGui::Text(std::format("ImageSpace: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::ImageSpace])).c_str());
		ImGui::Text(std::format("Lighting: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Lighting])).c_str());
		ImGui::Text(std::format("Effect: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Effect])).c_str());
		ImGui::Text(std::format("Utility: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Utility])).c_str());
		ImGui::Text(std::format("DistantTree: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::DistantTree])).c_str());
		ImGui::Text(std::format("Particle: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Particle])).c_str());
		ImGui::Text(std::format("Total: {}", int(globals::state->smoothDrawCalls[RE::BSShader::Type::Total])).c_str());

		if (dxgiAdapter3) {
			DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo;
			dxgiAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfo);

			float currentGpuUsage = videoMemoryInfo.CurrentUsage / (1024.f * 1024.f * 1024.f);
			float totalGpuMemory = videoMemoryInfo.Budget / (1024.f * 1024.f * 1024.f);
			float percent = currentGpuUsage / totalGpuMemory;

			auto progressOverlay = std::format("GPU: {:.02f}GB/{:.02f}GB ({:2.1f}%) ", currentGpuUsage, totalGpuMemory, 100 * percent);
			ImGui::ProgressBar(percent, ImVec2(200.f, ImGui::GetTextLineHeight()), progressOverlay.c_str());
		}
		ImGui::End();
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
				if (settingToggleKey) {
					settings.ToggleKey = key;
					settingToggleKey = false;
				} else if (settingSkipCompilationKey) {
					settings.SkipCompilationKey = key;
					settingSkipCompilationKey = false;
				} else if (settingsEffectsToggle) {
					settings.EffectToggleKey = key;
					settingsEffectsToggle = false;
				} else if (key == settings.ToggleKey) {
					IsEnabled = !IsEnabled;
				} else if (key == settings.SkipCompilationKey) {
					auto shaderCache = globals::shaderCache;
					shaderCache->backgroundCompilation = true;
				} else if (key == settings.EffectToggleKey) {
					auto shaderCache = globals::shaderCache;
					shaderCache->SetEnabled(!shaderCache->IsEnabled());
				} else if (key == priorShaderKey && globals::state->IsDeveloperMode()) {
					auto shaderCache = globals::shaderCache;
					shaderCache->IterateShaderBlock();
				} else if (key == nextShaderKey && globals::state->IsDeveloperMode()) {
					auto shaderCache = globals::shaderCache;
					shaderCache->IterateShaderBlock(false);
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
