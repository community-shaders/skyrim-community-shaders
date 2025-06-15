#include "UI.h"

#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "../Globals.h"
#include "../Menu.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <cmath>

namespace Util
{
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
	bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, ImVec2& out_size)
	{
		// Validate output parameter
		if (!out_srv) {
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

		// Validate that we have a valid D3D device
		if (!globals::d3d::device) {
			stbi_image_free(image_data);
			return false;
		}

		// Create texture
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = image_width;
		desc.Height = image_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;

		ID3D11Texture2D* pTexture = nullptr;
		D3D11_SUBRESOURCE_DATA subResource;
		subResource.pSysMem = image_data;
		subResource.SysMemPitch = desc.Width * 4;
		subResource.SysMemSlicePitch = 0;

		HRESULT hr = globals::d3d::device->CreateTexture2D(&desc, &subResource, &pTexture);
		if (FAILED(hr) || !pTexture) {
			stbi_image_free(image_data);
			return false;
		}

		// Create texture view
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;

		hr = globals::d3d::device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
		if (FAILED(hr) || !*out_srv) {
			// Clean up on failure
			pTexture->Release();
			stbi_image_free(image_data);
			*out_srv = nullptr;
			return false;
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

		bool success = true;
		// Define path to icons
		std::string basePath = "Data\\Interface\\CommunityShaders\\Icons\\";

		// Load all required icons
		success &= LoadTextureFromFile((basePath + "Microsoft Icons\\save-settings.png").c_str(), &menu->uiIcons.saveSettings.texture, menu->uiIcons.saveSettings.size);
		success &= LoadTextureFromFile((basePath + "Microsoft Icons\\load-settings.png").c_str(), &menu->uiIcons.loadSettings.texture, menu->uiIcons.loadSettings.size);
		success &= LoadTextureFromFile((basePath + "Microsoft Icons\\clear-cache.png").c_str(), &menu->uiIcons.clearCache.texture, menu->uiIcons.clearCache.size);
		success &= LoadTextureFromFile((basePath + "Microsoft Icons\\clear-disk.png").c_str(), &menu->uiIcons.clearDiskCache.texture, menu->uiIcons.clearDiskCache.size);
		success &= LoadTextureFromFile((basePath + "Community Shaders Logo\\cs-logo.png").c_str(), &menu->uiIcons.logo.texture, menu->uiIcons.logo.size);

		return success;
	}

	// Text rendering helpers (moved from UITextHelper)
	void RenderSharpText(const char* text, bool alignToPixelGrid, float scale)
	{
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
		if (scale != 1.0f)
			ImGui::SetWindowFontScale(scale);

		// Use Text instead of TextUnformatted for better rendering
		ImGui::Text("%s", text);

		// Restore scale if needed
		if (scale != 1.0f)
			ImGui::SetWindowFontScale(1.0f);
	}

	void RenderAlignedTextWithLogo(ID3D11ShaderResourceView* logoTexture, const ImVec2& logoSize, const char* text, float textScale)
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
	}
}  // namespace Util
