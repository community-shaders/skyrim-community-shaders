#include "UI.h"
#include "Menu.h"

#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "../Globals.h"
#include "../Menu.h"

#define STB_IMAGE_IMPLEMENTATION
#include <cmath>
#include <stb_image.h>

namespace Util
{
	PerformanceOverlay performanceOverlay;

	HoverTooltipWrapper::HoverTooltipWrapper()
	{
		hovered = ImGui::IsItemHovered();
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
		std::array<ID3D11ShaderResourceView**, 5> texturePointers = {
			&menu->uiIcons.saveSettings.texture,
			&menu->uiIcons.loadSettings.texture,
			&menu->uiIcons.clearCache.texture,
			&menu->uiIcons.clearDiskCache.texture,
			&menu->uiIcons.logo.texture
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

		// Load clear disk cache icon
		if (LoadTextureFromFile(device, (basePath + "Microsoft Icons\\clear-disk.png").c_str(), &menu->uiIcons.clearDiskCache.texture, menu->uiIcons.clearDiskCache.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded clear-disk icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load clear-disk icon from: {}", basePath + "Microsoft Icons\\clear-disk.png");
		}

		// Load logo icon
		if (LoadTextureFromFile(device, (basePath + "Community Shaders Logo\\cs-logo.png").c_str(), &menu->uiIcons.logo.texture, menu->uiIcons.logo.size)) {
			logger::info("InitializeMenuIcons: Successfully loaded logo icon");
			iconsLoaded++;
			anyIconLoaded = true;
		} else {
			logger::warn("InitializeMenuIcons: Failed to load logo icon from: {}", basePath + "Community Shaders Logo\\cs-logo.png");
		}

		logger::info("InitializeMenuIcons: Loaded {}/5 icons successfully", iconsLoaded);
		return anyIconLoaded;
	}

	// Text rendering helpers (moved from UITextHelper)
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
		// Add categoryCount to categoryName
		std::string displayName = std::format("{} ({})", categoryName, categoryCount);

		// Draw category header with custom styling
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float availableWidth = ImGui::GetContentRegionAvail().x;
		ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());

		// Calculate line positions
		float lineY = pos.y + textSize.y * 0.5f;
		float lineLength = (availableWidth - textSize.x - 20.0f) * 0.5f;  // 20px for padding

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
		auto& theme = Menu::GetSingleton()->GetTheme().FeatureHeading;
		ImU32 lineColor = hovered ? theme.LineColorHovered : theme.LineColorDefault;
		ImU32 textColor = hovered ? theme.TextColorHovered : theme.TextColorDefault;

		// Left line
		if (lineLength > 0) {
			drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), lineColor, 1.0f);
		}

		// Right line
		float rightLineStart = pos.x + lineLength + 10.0f + textSize.x + 10.0f;
		if (rightLineStart < pos.x + availableWidth) {
			drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), lineColor, 1.0f);
		}

		// Center text
		ImVec2 textPos = ImVec2(pos.x + lineLength + 10.0f, pos.y + 2.0f);
		drawList->AddText(textPos, textColor, displayName.c_str());

		// Handle click to toggle expansion
		if (clicked) {
			isExpanded = !isExpanded;
		}

		ImGui::PopID();

		// Move cursor to next line
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
		return clicked;
	}

	void DrawSectionHeader(const char* sectionName, bool useWhiteText)
	{
		// Draw custom styled header similar to CategoryHeader but non-collapsible
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float availableWidth = ImGui::GetContentRegionAvail().x;
		ImVec2 textSize = ImGui::CalcTextSize(sectionName);

		// Calculate line positions
		float lineY = pos.y + textSize.y * 0.5f;
		float lineLength = (availableWidth - textSize.x - 20.0f) * 0.5f;  // 20px for padding
		// Use Menu theme colors for consistent styling
		auto& theme = Menu::GetSingleton()->GetTheme().FeatureHeading;
		ImU32 lineColor = theme.LineColorDefault;
		ImU32 textColor = useWhiteText ? theme.TextColorWhite : theme.TextColorDefault;

		// Left line
		if (lineLength > 0) {
			drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), lineColor, 1.0f);
		}

		// Right line
		float rightLineStart = pos.x + lineLength + 10.0f + textSize.x + 10.0f;
		if (rightLineStart < pos.x + availableWidth) {
			drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), lineColor, 1.0f);
		}

		// Center text
		ImVec2 textPos = ImVec2(pos.x + lineLength + 10.0f, pos.y + 2.0f);
		drawList->AddText(textPos, textColor, sectionName);

		// Move cursor to next line
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
	}
}  // namespace Util
