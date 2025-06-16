#include "UI.h"

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
			return false;
		}

		// Initialize output to nullptr
		*out_srv = nullptr;

		// Load from disk into a raw RGBA buffer
		int image_width = 0;
		int image_height = 0;
		int channels_in_file;
		unsigned char* image_data = stbi_load(filename, &image_width, &image_height, &channels_in_file, 4);
		if (image_data == NULL)
			return false;

		// Create texture
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = image_width;
		desc.Height = image_height;
		desc.MipLevels = 1;  // Start with single mip level for initial data
		desc.ArraySize = 1;
		// Preserve icon colour fidelity
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		desc.SampleDesc.Count = 1;
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
			stbi_image_free(image_data);
			return false;
		}

		// Create texture view
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 0;  // Use all available mip levels
		srvDesc.Texture2D.MostDetailedMip = 0;

		hr = device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
		if (FAILED(hr) || !*out_srv) {
			// Clean up on failure
			pTexture->Release();
			stbi_image_free(image_data);
			*out_srv = nullptr;
			return false;
		}

		// Generate mipmaps for smooth scaling at different DPI levels
		if (globals::d3d::context) {
			globals::d3d::context->GenerateMips(*out_srv);
		}

		// Success - clean up intermediate resources
		pTexture->Release();
		stbi_image_free(image_data);

		out_size = ImVec2((float)image_width, (float)image_height);
		return true;
	}

	bool InitializeMenuIcons(Menu* menu)
	{
		if (!menu) {
			return false;
		}

		// Get the D3D device from globals
		ID3D11Device* device = globals::d3d::device;
		if (!device) {
			return false;
		}

		// Define path to icons
		std::string basePath = "Data\\Interface\\CommunityShaders\\Icons\\";

		// Initialize all texture pointers to nullptr for safe cleanup
		menu->uiIcons.saveSettings.texture = nullptr;
		menu->uiIcons.loadSettings.texture = nullptr;
		menu->uiIcons.clearCache.texture = nullptr;
		menu->uiIcons.clearDiskCache.texture = nullptr;
		menu->uiIcons.logo.texture = nullptr;

		// Load icons one by one, cleaning up on any failure
		if (!LoadTextureFromFile(device, (basePath + "Microsoft Icons\\save-settings.png").c_str(), &menu->uiIcons.saveSettings.texture, menu->uiIcons.saveSettings.size)) {
			goto cleanup_and_fail;
		}

		if (!LoadTextureFromFile(device, (basePath + "Microsoft Icons\\load-settings.png").c_str(), &menu->uiIcons.loadSettings.texture, menu->uiIcons.loadSettings.size)) {
			goto cleanup_and_fail;
		}

		if (!LoadTextureFromFile(device, (basePath + "Microsoft Icons\\clear-cache.png").c_str(), &menu->uiIcons.clearCache.texture, menu->uiIcons.clearCache.size)) {
			goto cleanup_and_fail;
		}

		if (!LoadTextureFromFile(device, (basePath + "Microsoft Icons\\clear-disk.png").c_str(), &menu->uiIcons.clearDiskCache.texture, menu->uiIcons.clearDiskCache.size)) {
			goto cleanup_and_fail;
		}

		if (!LoadTextureFromFile(device, (basePath + "Community Shaders Logo\\cs-logo.png").c_str(), &menu->uiIcons.logo.texture, menu->uiIcons.logo.size)) {
			goto cleanup_and_fail;
		}

		// All icons loaded successfully
		return true;

cleanup_and_fail:
		// Release any successfully loaded SRVs to prevent GPU memory leaks
		if (menu->uiIcons.saveSettings.texture) {
			menu->uiIcons.saveSettings.texture->Release();
			menu->uiIcons.saveSettings.texture = nullptr;
		}
		if (menu->uiIcons.loadSettings.texture) {
			menu->uiIcons.loadSettings.texture->Release();
			menu->uiIcons.loadSettings.texture = nullptr;
		}
		if (menu->uiIcons.clearCache.texture) {
			menu->uiIcons.clearCache.texture->Release();
			menu->uiIcons.clearCache.texture = nullptr;
		}
		if (menu->uiIcons.clearDiskCache.texture) {
			menu->uiIcons.clearDiskCache.texture->Release();
			menu->uiIcons.clearDiskCache.texture = nullptr;
		}
		if (menu->uiIcons.logo.texture) {
			menu->uiIcons.logo.texture->Release();
			menu->uiIcons.logo.texture = nullptr;
		}

		return false;
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

		// Note: For this simplified version, we don't use TreeNode
		// The sections are always expanded in FeatureIssues UI
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
}  // namespace Util
