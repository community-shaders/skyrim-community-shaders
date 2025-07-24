#include "UI.h"
#include "Menu.h"

#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "../Feature.h"
#include "../Globals.h"
#include "../Menu.h"

#define STB_IMAGE_IMPLEMENTATION
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stb_image.h>
#include <string>
#include <vector>

namespace Util
{
	HoverTooltipWrapper::HoverTooltipWrapper()
	{
		hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
		if (hovered) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		}
	}

	HoverTooltipWrapper::~HoverTooltipWrapper()
	{
		if (hovered) {
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	DisableGuard::DisableGuard(bool disable) :
		disable(disable)
	{
		if (disable)
			ImGui::BeginDisabled();
	}
	DisableGuard::~DisableGuard()
	{
		if (disable)
			ImGui::EndDisabled();
	}

	bool PercentageSlider(const char* label, float* data, float lb, float ub, const char* format)
	{
		float percentageData = (*data) * 1e2f;
		bool retval = ImGui::SliderFloat(label, &percentageData, lb, ub, format);
		(*data) = percentageData * 1e-2f;
		return retval;
	}

	ImVec2 GetNativeViewportSizeScaled(float scale)
	{
		const auto Size = ImGui::GetMainViewport()->Size;
		return { Size.x * scale, Size.y * scale };
	}
	// Icon loading functions (moved from UIIconLoader)
	bool LoadTextureFromFile(ID3D11Device* device,
		const char* filename,
		ID3D11ShaderResourceView** out_srv,
		ImVec2& out_size)
	{
		// Validate input parameters
		if (!device || !out_srv) {
			logger::warn("LoadTextureFromFile: Invalid parameters - device: {}, out_srv: {}",
				device ? "valid" : "null", out_srv ? "valid" : "null");
			return false;
		}

		// Initialize output to nullptr
		*out_srv = nullptr;

		logger::debug("LoadTextureFromFile: Attempting to load {}", filename);

		// Load from disk into a raw RGBA buffer
		int image_width = 0;
		int image_height = 0;
		int channels_in_file;
		unsigned char* image_data = stbi_load(filename, &image_width, &image_height, &channels_in_file, 4);
		if (image_data == NULL) {
			logger::warn("LoadTextureFromFile: Failed to load image data from {}", filename);
			return false;
		}
		// Creates Textures for Icons with Mipmapping to support high DPI displays.
		logger::debug("LoadTextureFromFile: Loaded image {}x{} with {} channels from {}",
			image_width, image_height, channels_in_file, filename);
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = image_width;
		desc.Height = image_height;
		desc.MipLevels = 1;  // Start with just one mip level
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		desc.CPUAccessFlags = 0;

		ID3D11Texture2D* pTexture = nullptr;
		D3D11_SUBRESOURCE_DATA subResource;
		subResource.pSysMem = image_data;
		subResource.SysMemPitch = desc.Width * 4;
		subResource.SysMemSlicePitch = 0;

		HRESULT hr = device->CreateTexture2D(&desc, &subResource, &pTexture);
		if (FAILED(hr) || !pTexture) {
			logger::warn("LoadTextureFromFile: Failed to create D3D11 texture, HRESULT: 0x{:08X}", static_cast<uint32_t>(hr));
			stbi_image_free(image_data);
			return false;
		}
		// Create simple shader resource view
		hr = device->CreateShaderResourceView(pTexture, nullptr, out_srv);
		if (FAILED(hr) || !*out_srv) {
			logger::warn("LoadTextureFromFile: Failed to create shader resource view, HRESULT: 0x{:08X}", static_cast<uint32_t>(hr));
			pTexture->Release();
			stbi_image_free(image_data);
			*out_srv = nullptr;
			return false;
		}

		// Generate mipmaps for better icon quality at different scales
		ID3D11DeviceContext* context = nullptr;
		device->GetImmediateContext(&context);
		if (context) {
			context->GenerateMips(*out_srv);
			context->Release();
		}
		// Success - clean up intermediate resources
		pTexture->Release();
		stbi_image_free(image_data);

		out_size = ImVec2((float)image_width, (float)image_height);
		logger::debug("LoadTextureFromFile: Successfully loaded {} ({}x{})", filename, image_width, image_height);
		return true;
	}
	bool InitializeMenuIcons(Menu* menu)
	{
		if (!menu) {
			logger::warn("InitializeMenuIcons: Menu pointer is null");
			return false;
		}

		// Get the D3D device from globals
		ID3D11Device* device = globals::d3d::device;
		if (!device) {
			logger::warn("InitializeMenuIcons: D3D device is null");
			return false;
		}
		// Define path to icons
		std::string basePath = "Data\\Interface\\CommunityShaders\\Icons\\";
		logger::info("InitializeMenuIcons: Loading icons from base path: {}", basePath);

		// Initialize all texture pointers to nullptr for safe cleanup
		std::array<ID3D11ShaderResourceView**, 13> texturePointers = {
			&menu->uiIcons.saveSettings.texture,
			&menu->uiIcons.loadSettings.texture,
			&menu->uiIcons.clearCache.texture,
			&menu->uiIcons.clearDiskCache.texture,
			&menu->uiIcons.logo.texture,
			&menu->uiIcons.characters.texture,
			&menu->uiIcons.grass.texture,
			&menu->uiIcons.lighting.texture,
			&menu->uiIcons.sky.texture,
			&menu->uiIcons.landscape.texture,
			&menu->uiIcons.water.texture,
			&menu->uiIcons.debug.texture,
			&menu->uiIcons.materials.texture
		};

		// Safely release existing textures
		for (auto* texturePtr : texturePointers) {
			if (*texturePtr) {
				(*texturePtr)->Release();
				*texturePtr = nullptr;
			}
		}

		// Instead of failing completely if one icon fails, try to load each one individually
		bool anyIconLoaded = false;
		int iconsLoaded = 0;

		// Load save settings icon
		if (LoadTextureFromFile(device, (basePath + "Microsoft Icons\\save-settings.png").c_str(), &menu->uiIcons.saveSettings.texture, menu->uiIcons.saveSettings.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded save-settings icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load save-settings icon from: {}", basePath + "Microsoft Icons\\save-settings.png");
		}

		// Load load settings icon
		if (LoadTextureFromFile(device, (basePath + "Microsoft Icons\\load-settings.png").c_str(), &menu->uiIcons.loadSettings.texture, menu->uiIcons.loadSettings.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded load-settings icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load load-settings icon from: {}", basePath + "Microsoft Icons\\load-settings.png");
		}

		// Load clear cache icon
		if (LoadTextureFromFile(device, (basePath + "Microsoft Icons\\clear-cache.png").c_str(), &menu->uiIcons.clearCache.texture, menu->uiIcons.clearCache.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded clear-cache icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load clear-cache icon from: {}", basePath + "Microsoft Icons\\clear-cache.png");
		}

		// Load logo icon
		if (LoadTextureFromFile(device, (basePath + "Community Shaders Logo\\cs-logo.png").c_str(), &menu->uiIcons.logo.texture, menu->uiIcons.logo.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded logo icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load logo icon from: {}", basePath + "Community Shaders Logo\\cs-logo.png");
		}

		// Load category icons
		if (LoadTextureFromFile(device, (basePath + "Categories\\characters.png").c_str(), &menu->uiIcons.characters.texture, menu->uiIcons.characters.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded characters icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load characters icon from: {}", basePath + "Categories\\characters.png");
		}

		if (LoadTextureFromFile(device, (basePath + "Categories\\grass.png").c_str(), &menu->uiIcons.grass.texture, menu->uiIcons.grass.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded grass icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load grass icon from: {}", basePath + "Categories\\grass.png");
		}

		if (LoadTextureFromFile(device, (basePath + "Categories\\lighting.png").c_str(), &menu->uiIcons.lighting.texture, menu->uiIcons.lighting.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded lighting icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load lighting icon from: {}", basePath + "Categories\\lighting.png");
		}

		if (LoadTextureFromFile(device, (basePath + "Categories\\sky.png").c_str(), &menu->uiIcons.sky.texture, menu->uiIcons.sky.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded sky icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load sky icon from: {}", basePath + "Categories\\sky.png");
		}

		if (LoadTextureFromFile(device, (basePath + "Categories\\landscape.png").c_str(), &menu->uiIcons.landscape.texture, menu->uiIcons.landscape.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded landscape icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load landscape icon from: {}", basePath + "Categories\\landscape.png");
		}

		if (LoadTextureFromFile(device, (basePath + "Categories\\water.png").c_str(), &menu->uiIcons.water.texture, menu->uiIcons.water.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded water icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load water icon from: {}", basePath + "Categories\\water.png");
		}

		if (LoadTextureFromFile(device, (basePath + "Categories\\debug.png").c_str(), &menu->uiIcons.debug.texture, menu->uiIcons.debug.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded debug icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load debug icon from: {}", basePath + "Categories\\debug.png");
		}

		// Materials placeholder - will fail to load for now
		if (LoadTextureFromFile(device, (basePath + "Categories\\materials.png").c_str(), &menu->uiIcons.materials.texture, menu->uiIcons.materials.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded materials icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load materials icon from: {}", basePath + "Categories\\materials.png");
		}

		logger::info("InitializeMenuIcons: Loaded {}/13 icons successfully", iconsLoaded);

		return anyIconLoaded;
	}

	// Text rendering helpers
	ImVec2 DrawSharpText(const char* text, bool alignToPixelGrid, float scale)
	{
		ImVec2 startPos = ImGui::GetCursorPos();

		if (alignToPixelGrid) {
			// Get current position
			ImVec2 pos = ImGui::GetCursorPos();

			// Align to pixel grid for sharper rendering
			pos.x = std::round(pos.x);
			pos.y = std::round(pos.y);

			// Set aligned position
			ImGui::SetCursorPos(pos);
		}
		// Apply scale if needed
		if (scale != 1.0f) {
			ImGui::SetWindowFontScale(scale);
		}

		// Use Text instead of TextUnformatted for better rendering
		ImGui::Text("%s", text);
		// Restore original scale if needed
		if (scale != 1.0f)
			ImGui::SetWindowFontScale(1.0f);

		// Calculate and return the rendered size
		ImVec2 endPos = ImGui::GetCursorPos();
		return ImVec2(endPos.x - startPos.x, endPos.y - startPos.y);
	}

	ImVec2 DrawAlignedTextWithLogo(ID3D11ShaderResourceView* logoTexture, const ImVec2& logoSize, const char* text, float textScale)
	{
		// Save current cursor position
		ImVec2 startPos = ImGui::GetCursorPos();

		// Calculate scaled text height
		float fontHeight = ImGui::GetFontSize() * textScale;
		float logoHeight = logoSize.y;

		// Calculate vertical offset to center align logo with text
		float verticalOffset = (fontHeight - logoHeight) * 0.5f;

		// Position cursor for logo with vertical alignment
		ImGui::SetCursorPos(ImVec2(startPos.x, startPos.y + verticalOffset));

		// Render logo
		ImGui::Image(logoTexture, logoSize);
		ImGui::SameLine();

		// Reset cursor for text with proper vertical alignment
		ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX(), startPos.y));
		// Use windowed font scale for sharper text
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGui::SetWindowFontScale(textScale);

		// Render text aligned to pixel grid for sharpness
		ImGui::Text("%s", text);
		// Restore style
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleVar();

		// Calculate and return the total rendered size
		ImVec2 endPos = ImGui::GetCursorPos();
		return ImVec2(endPos.x - startPos.x, endPos.y - startPos.y);
	}
	// StyledButtonWrapper implementation
	StyledButtonWrapper::StyledButtonWrapper(const ImVec4& normalColor, const ImVec4& hoveredColor, const ImVec4& activeColor) :
		m_pushedStyles(0)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, normalColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
		m_pushedStyles = 3;
	}

	StyledButtonWrapper::~StyledButtonWrapper()
	{
		if (m_pushedStyles > 0) {
			ImGui::PopStyleColor(m_pushedStyles);
		}
	}

	// SectionWrapper implementation
	SectionWrapper::SectionWrapper(const char* title, const char* description, const ImVec4& titleColor, bool isVisible) :
		m_shouldDraw(isVisible),
		m_treeNodeOpened(false)
	{
		if (!m_shouldDraw) {
			return;
		}

		ImGui::TextColored(titleColor, "%s", title);
		ImGui::Spacing();

		if (description && strlen(description) > 0) {
			ImGui::TextWrapped("%s", description);
			ImGui::Spacing();
		}
	}

	SectionWrapper::~SectionWrapper()
	{
		if (m_shouldDraw) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
	}

	SectionWrapper::operator bool() const
	{
		return m_shouldDraw;
	}

	bool DrawCategoryHeader(const char* categoryName, bool& isExpanded, int categoryCount)
	{
		// Get the appropriate icon for this category
		ID3D11ShaderResourceView* categoryIcon = nullptr;
		auto& menu = Menu::GetSingleton()->uiIcons;

		if (strcmp(categoryName, "Characters") == 0) {
			categoryIcon = menu.characters.texture;
		} else if (strcmp(categoryName, "Grass") == 0) {
			categoryIcon = menu.grass.texture;
		} else if (strcmp(categoryName, "Lighting") == 0) {
			categoryIcon = menu.lighting.texture;
		} else if (strcmp(categoryName, "Sky") == 0) {
			categoryIcon = menu.sky.texture;
		} else if (strcmp(categoryName, "Landscape & Textures") == 0) {
			categoryIcon = menu.landscape.texture;
		} else if (strcmp(categoryName, "Water") == 0) {
			categoryIcon = menu.water.texture;
		} else if (strcmp(categoryName, "Debug") == 0) {
			categoryIcon = menu.debug.texture;
		} else if (strcmp(categoryName, "Materials") == 0) {
			categoryIcon = menu.materials.texture;
		}

		// Add categoryCount to categoryName
		std::string displayName = std::format("{} ({})", categoryName, categoryCount);

		// Draw category header with custom styling
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float availableWidth = ImGui::GetContentRegionAvail().x;

		// Calculate icon size based on current font size to match text scaling
		// This ensures icons scale consistently with text when the font scale changes
		const float currentFontSize = ImGui::GetFontSize();
		const float iconSize = currentFontSize * 1.2f;     // 20% larger than font height
		const float iconSpacing = currentFontSize * 0.3f;  // 30% of font height for spacing
		ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());

		// Calculate total content width (icon + spacing + text)
		float contentWidth = textSize.x;
		if (categoryIcon) {
			contentWidth += iconSize + iconSpacing;
		}

		// Calculate line positions
		float lineY = pos.y + textSize.y * 0.5f;
		float lineLength = (availableWidth - contentWidth - 20.0f) * 0.5f;  // 20px for padding

		// Create selectable area for the entire header
		ImGui::PushID(displayName.c_str());
		bool hovered = false;
		bool clicked = false;

		// Invisible button for hover detection and clicking
		ImGui::SetCursorScreenPos(pos);
		if (ImGui::InvisibleButton("##CategoryHeader", ImVec2(availableWidth, textSize.y + 4.0f))) {
			clicked = true;
		}
		hovered = ImGui::IsItemHovered();

		// Draw the lines and text using Menu theme colors
		auto& theme = globals::menu->GetTheme().FeatureHeading;

		// Get the color based on hover state
		ImVec4 color = hovered ? theme.ColorHovered : theme.ColorDefault;
		// If minimized, apply the minimized factor
		if (!isExpanded) {
			color.w *= theme.MinimizedFactor;
		}
		ImU32 headerColor = ImGui::GetColorU32(color);

		// Left line
		if (lineLength > 0) {
			drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), headerColor, 1.0f);
		}

		// Right line
		float rightLineStart = pos.x + lineLength + 10.0f + contentWidth + 10.0f;
		if (rightLineStart < pos.x + availableWidth) {
			drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), headerColor, 1.0f);
		}

		// Draw icon and text
		float currentX = pos.x + lineLength + 10.0f;

		// Draw icon if available
		if (categoryIcon) {
			ImVec2 iconPos = ImVec2(currentX, pos.y + (textSize.y - iconSize) * 0.5f + 2.0f);
			ImVec2 iconMax = ImVec2(iconPos.x + iconSize, iconPos.y + iconSize);

			// Apply the same color tint as the text
			ImU32 iconTint = headerColor;
			drawList->AddImage(categoryIcon, iconPos, iconMax, ImVec2(0, 0), ImVec2(1, 1), iconTint);

			currentX += iconSize + iconSpacing;
		}

		// Center text
		ImVec2 textPos = ImVec2(currentX, pos.y + 2.0f);
		drawList->AddText(textPos, headerColor, displayName.c_str());

		// Handle click to toggle expansion
		if (clicked) {
			isExpanded = !isExpanded;
		}

		ImGui::PopID();

		// Move cursor to next line
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
		return clicked;
	}

	bool DrawSectionHeader(const char* sectionName, bool useWhiteText, bool isCollapsible, bool* isExpanded)
	{
		bool stateChanged = false;

		// Use Menu theme colors for consistent styling
		auto& theme = globals::menu->GetTheme().FeatureHeading;
		ImVec4 color = useWhiteText ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : theme.ColorDefault;

		ImU32 headerColor = ImGui::GetColorU32(color);

		if (isCollapsible && isExpanded) {
			// Use collapsible header similar to DrawCategoryHeader
			ImGui::PushID(sectionName);

			ImGui::PushStyleColor(ImGuiCol_Text, headerColor);

			if (ImGui::CollapsingHeader(sectionName, ImGuiTreeNodeFlags_DefaultOpen)) {
				if (!*isExpanded) {
					stateChanged = true;
				}
				*isExpanded = true;
			} else {
				if (*isExpanded) {
					stateChanged = true;
				}
				*isExpanded = false;
			}

			ImGui::PopStyleColor();
			ImGui::PopID();
		} else {
			// Non-collapsible header - use custom styled header similar to CategoryHeader
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 pos = ImGui::GetCursorScreenPos();
			float availableWidth = ImGui::GetContentRegionAvail().x;
			ImVec2 textSize = ImGui::CalcTextSize(sectionName);

			// Calculate line positions
			float lineY = pos.y + textSize.y * 0.5f;
			float lineLength = (availableWidth - textSize.x - 20.0f) * 0.5f;  // 20px for padding

			// Left line
			if (lineLength > 0) {
				drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), headerColor, 1.0f);
			}

			// Right line
			float rightLineStart = pos.x + lineLength + 10.0f + textSize.x + 10.0f;
			if (rightLineStart < pos.x + availableWidth) {
				drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), headerColor, 1.0f);
			}

			// Center text
			ImVec2 textPos = ImVec2(pos.x + lineLength + 10.0f, pos.y + 2.0f);
			drawList->AddText(textPos, headerColor, sectionName);

			// Move cursor to next line
			ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
		}

		return stateChanged;
	}

	// ColorCodedValueConfig static helper implementations
	ColorCodedValueConfig ColorCodedValueConfig::HighIsBad(float low, float med, float high)
	{
		ColorCodedValueConfig config;
		const auto& theme = globals::menu->GetTheme().StatusPalette;
		config.thresholds = {
			{ low, theme.Disable },    // Very low - gray
			{ med, theme.InfoColor },  // Low - blue
			{ high, theme.Warning },   // Medium - orange
			{ FLT_MAX, theme.Error }   // High - red (bad)
		};
		return config;
	}

	ColorCodedValueConfig ColorCodedValueConfig::HighIsGood(float low, float med, float high)
	{
		ColorCodedValueConfig config;
		const auto& theme = globals::menu->GetTheme().StatusPalette;
		config.thresholds = {
			{ low, theme.Disable },          // Very low - gray
			{ med, theme.InfoColor },        // Low - blue
			{ high, theme.Warning },         // Medium - orange
			{ FLT_MAX, theme.SuccessColor }  // High - green (good)
		};
		return config;
	}

	void DrawColorCodedValue(
		const std::string& label,
		float valueToCheck,
		const std::string& valueStr,
		const ColorCodedValueConfig& config,
		bool useBullet)
	{
		// Display label
		if (useBullet) {
			ImGui::BulletText("%s", label.c_str());
		} else {
			ImGui::Text("%s", label.c_str());
		}
		if (config.sameLine) {
			ImGui::SameLine();
		}

		// Determine color based on thresholds
		ImVec4 valueColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // Default white
		for (const auto& tc : config.thresholds) {
			if (valueToCheck < tc.threshold) {
				valueColor = tc.color;
				break;
			}
		}

		// Display colored value (arbitrary string)
		ImGui::TextColored(valueColor, "%s", valueStr.c_str());

		// Add tooltip if provided
		if (config.tooltipText) {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", config.tooltipText);
			}
		}
	}

	void DrawMultiLineTooltip(const std::vector<std::string>& lines, const std::vector<ImVec4>& colors)
	{
		for (size_t i = 0; i < lines.size(); ++i) {
			const char* lineCStr = lines[i].c_str();
			if (!colors.empty() && i < colors.size()) {
				// Use provided color for this line
				ImGui::TextColored(colors[i], "%s", lineCStr);
			} else {
				// Use default color
				ImGui::Text("%s", lineCStr);
			}
		}
	}

	void DrawColoredMultiLineTooltip(const ColoredTextLines& lines)
	{
		for (const auto& line : lines) {
			ImGui::TextColored(line.color, "%s", line.text.c_str());
		}
	}

	void SortTableRowsByColumn(std::vector<std::vector<std::string>>& rows, size_t column, bool ascending)
	{
		std::sort(rows.begin(), rows.end(), [column, ascending](const auto& a, const auto& b) {
			if (column >= a.size() || column >= b.size())
				return false;
			return ascending ? (a[column] < b[column]) : (a[column] > b[column]);
		});
	}

	bool VersionStringLess(const std::string& a, const std::string& b, bool ascending)
	{
		auto split = [](const std::string& s) {
			std::vector<int> parts;
			size_t start = 0, end = 0;
			while ((end = s.find('.', start)) != std::string::npos) {
				try {
					parts.push_back(std::stoi(s.substr(start, end - start)));
				} catch (...) {
					parts.push_back(0);
				}
				start = end + 1;
			}
			if (start < s.size()) {
				try {
					parts.push_back(std::stoi(s.substr(start)));
				} catch (...) {
					parts.push_back(0);
				}
			}
			return parts;
		};
		auto va = split(a), vb = split(b);
		for (size_t i = 0; i < std::max(va.size(), vb.size()); ++i) {
			int ai = i < va.size() ? va[i] : 0;
			int bi = i < vb.size() ? vb[i] : 0;
			if (ai != bi)
				return ascending ? (ai < bi) : (ai > bi);
		}
		return false;
	}

	bool VersionSortComparator(const std::string& a, const std::string& b, bool asc)
	{
		return VersionStringLess(a, b, asc);
	}

	bool StringSortComparator(const std::string& a, const std::string& b, bool ascending)
	{
		return ascending ? (a < b) : (b < a);
	}

	ImVec4 GetThresholdColor(float value, float good, float warn, ImVec4 goodColor, ImVec4 warnColor, ImVec4 badColor)
	{
		if (value < good)
			return goodColor;
		else if (value < warn)
			return warnColor;
		else
			return badColor;
	}

	bool FeatureMatchesSearch(Feature* feat, const std::string& searchQuery)
	{
		if (searchQuery.empty())
			return true;

		// Get both short name and display name
		std::string shortName = feat->GetShortName();
		std::string displayName = feat->GetName();
		std::string query = searchQuery;

		// Convert all to lowercase for case-insensitive search
		std::transform(shortName.begin(), shortName.end(), shortName.begin(), ::tolower);
		std::transform(displayName.begin(), displayName.end(), displayName.begin(), ::tolower);
		std::transform(query.begin(), query.end(), query.begin(), ::tolower);

		// Search in both short name and display name
		return shortName.find(query) != std::string::npos ||
		       displayName.find(query) != std::string::npos;
	}

	void DrawFeatureSearchBar(std::string& searchString, float availableWidth)
	{
		ImGui::PushID("FeatureSearchBar");

		float iconSize = 20.0f;
		float iconSpace = iconSize + 14.0f;

		// Get the current cursor position and available width
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		if (availableWidth <= 0.0f) {
			availableWidth = ImGui::GetContentRegionAvail().x;
		}
		float frameHeight = ImGui::GetFrameHeight();

		// Custom style - always transparent background to avoid click blocking
		ImVec4 bgColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		ImVec4 bgColorActive = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);
		ImVec4 textColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);

		ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, bgColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, bgColorActive);
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(iconSpace, 6.0f));

		// Draw the input field
		ImGui::SetNextItemWidth(availableWidth);
		char buffer[256];
		strncpy_s(buffer, searchString.c_str(), sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';

		if (ImGui::InputTextWithHint("##feature_search", "Search Features...", buffer, sizeof(buffer))) {
			searchString = buffer;
		}

		// Draw a simple search icon (magnifying glass shape)
		ImVec2 iconPos = ImVec2(cursorPos.x + 8.0f, cursorPos.y + (frameHeight - iconSize) * 0.5f);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		ImVec2 center = ImVec2(iconPos.x + iconSize * 0.46f, iconPos.y + iconSize * 0.5f);
		float radius = iconSize * 0.3f;
		ImU32 placeholderColor = IM_COL32(140, 140, 140, 180);

		// Draw circle
		drawList->AddCircle(center, radius, placeholderColor, 12, 2.2f);

		// Draw handle
		ImVec2 handleStart = ImVec2(center.x + radius * 0.81f, center.y + radius * 0.81f);
		ImVec2 handleEnd = ImVec2(handleStart.x + iconSize * 0.29f, handleStart.y + iconSize * 0.29f);
		drawList->AddLine(handleStart, handleEnd, placeholderColor, 2.1f);

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
		ImGui::PopID();
	}

	void ShowSortedStringTableStrings(
		const char* table_id,
		const std::vector<std::string>& headers,
		const std::vector<std::vector<std::string>>& rows,
		size_t sortColumn,
		bool ascending,
		const std::vector<TableSortFunc>& customSorts,
		TableCellRenderFunc cellRender)
	{
		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable;
		if (ImGui::BeginTable(table_id, static_cast<int>(headers.size()), flags)) {
			for (const auto& header : headers)
				ImGui::TableSetupColumn(header.c_str());
			ImGui::TableHeadersRow();

			// Determine sorting
			int sortCol = static_cast<int>(sortColumn);
			bool sortAsc = ascending;
			if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
				if (sortSpecs->SpecsCount > 0) {
					sortCol = sortSpecs->Specs->ColumnIndex;
					sortAsc = sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending;
				}
			}

			// Make a copy if sorting is needed
			std::vector<std::vector<std::string>> sortedRows = rows;
			if (sortCol >= 0 && static_cast<size_t>(sortCol) < headers.size()) {
				// Fallback to default string sort if no custom sort is provided
				auto cmp = (sortCol < static_cast<int>(customSorts.size()) && customSorts[sortCol]) ? customSorts[sortCol] : StringSortComparator;
				std::sort(sortedRows.begin(), sortedRows.end(), [sortCol, sortAsc, &cmp](const std::vector<std::string>& a, const std::vector<std::string>& b) {
					const std::string& aVal = (sortCol < a.size()) ? a[sortCol] : std::string();
					const std::string& bVal = (sortCol < b.size()) ? b[sortCol] : std::string();
					return cmp(aVal, bVal, sortAsc);
				});
			}

			// Render rows
			for (size_t rowIdx = 0; rowIdx < sortedRows.size(); ++rowIdx) {
				const auto& row = sortedRows[rowIdx];
				ImGui::TableNextRow();
				for (size_t col = 0; col < headers.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					if (cellRender) {
						const std::string& value = (col < row.size()) ? row[col] : std::string();
						cellRender(static_cast<int>(rowIdx), static_cast<int>(col), value);
					} else {
						if (col < row.size())
							ImGui::TextUnformatted(row[col].c_str());
					}
				}
			}
			ImGui::EndTable();
		}
	}

	// Theme-aware color accessor functions
	namespace Colors
	{
		ImVec4 GetTimerGood()
		{
			return globals::menu->GetTheme().StatusPalette.SuccessColor;
		}

		ImVec4 GetTimerWarning()
		{
			return globals::menu->GetTheme().StatusPalette.Warning;
		}

		ImVec4 GetTimerCritical()
		{
			return globals::menu->GetTheme().StatusPalette.Error;
		}

		ImVec4 GetDefault()
		{
			return globals::menu->GetTheme().Palette.Text;
		}

		ImVec4 GetSuccess()
		{
			return globals::menu->GetTheme().StatusPalette.SuccessColor;
		}

		ImVec4 GetWarning()
		{
			return globals::menu->GetTheme().StatusPalette.Warning;
		}

		ImVec4 GetError()
		{
			return globals::menu->GetTheme().StatusPalette.Error;
		}

		ImVec4 GetInfo()
		{
			return globals::menu->GetTheme().StatusPalette.InfoColor;
		}

		ImVec4 GetDisabled()
		{
			return globals::menu->GetTheme().StatusPalette.Disable;
		}
	}
}  // namespace Util