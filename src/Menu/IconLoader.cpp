#include "PCH.h"

#include "IconLoader.h"

#include "Globals.h"
#include "Menu.h"
#include "Utils/FileSystem.h"

#include <d3d11.h>
#include <imgui.h>
#include <stb_image.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace Util::IconLoader
{
	struct IconDefinition
	{
		std::string filename;
		ID3D11ShaderResourceView** texture;
		ImVec2* size;
	};

	bool LoadTextureFromFile(ID3D11Device* device, const char* filename, ID3D11ShaderResourceView** out_srv, ImVec2& out_size)
	{
		int image_width = 0;
		int image_height = 0;
		unsigned char* image_data = stbi_load(filename, &image_width, &image_height, nullptr, 4);
		if (image_data == nullptr) {
			return false;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = image_width;
		desc.Height = image_height;
		desc.MipLevels = 0;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		ID3D11Texture2D* pTexture = nullptr;
		device->CreateTexture2D(&desc, nullptr, &pTexture);
		if (!pTexture) {
			stbi_image_free(image_data);
			return false;
		}

		ID3D11DeviceContext* context = nullptr;
		device->GetImmediateContext(&context);
		if (context) {
			context->UpdateSubresource(pTexture, 0, nullptr, image_data, desc.Width * 4, 0);
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);
		srvDesc.Texture2D.MostDetailedMip = 0;

		HRESULT hr = device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
		if (FAILED(hr)) {
			pTexture->Release();
			stbi_image_free(image_data);
			if (context)
				context->Release();
			return false;
		}

		if (context) {
			context->GenerateMips(*out_srv);
			context->Release();
		}

		pTexture->Release();
		stbi_image_free(image_data);

		out_size = ImVec2((float)image_width, (float)image_height);
		return true;
	}

	std::vector<IconDefinition> GetIconDefinitions(Menu* menu)
	{
		const bool useMonochrome = menu->GetSettings().Theme.UseMonochromeIcons;
		const bool useMonochromeLogo = menu->GetSettings().Theme.UseMonochromeLogo;
		const char* iconFolder = useMonochrome ? "Action Icons\\Monochrome" : "Action Icons";
		const char* logoPath = useMonochromeLogo ? "Community Shaders Logo\\Monochrome\\cs-logo.png" : "Community Shaders Logo\\cs-logo.png";

		return {
			{ std::string(iconFolder) + "\\save-settings.png", &menu->uiIcons.saveSettings.texture, &menu->uiIcons.saveSettings.size },
			{ std::string(iconFolder) + "\\load-settings.png", &menu->uiIcons.loadSettings.texture, &menu->uiIcons.loadSettings.size },
			{ std::string(iconFolder) + "\\clear-cache.png", &menu->uiIcons.clearCache.texture, &menu->uiIcons.clearCache.size },
			{ logoPath, &menu->uiIcons.logo.texture, &menu->uiIcons.logo.size },
			{ std::string(iconFolder) + "\\restore-settings.png", &menu->uiIcons.featureSettingRevert.texture, &menu->uiIcons.featureSettingRevert.size },
			{ std::string(iconFolder) + "\\discord.png", &menu->uiIcons.discord.texture, &menu->uiIcons.discord.size },
			{ "Categories\\characters.png", &menu->uiIcons.characters.texture, &menu->uiIcons.characters.size },
			{ "Categories\\display.png", &menu->uiIcons.display.texture, &menu->uiIcons.display.size },
			{ "Categories\\grass.png", &menu->uiIcons.grass.texture, &menu->uiIcons.grass.size },
			{ "Categories\\lighting.png", &menu->uiIcons.lighting.texture, &menu->uiIcons.lighting.size },
			{ "Categories\\sky.png", &menu->uiIcons.sky.texture, &menu->uiIcons.sky.size },
			{ "Categories\\landscape.png", &menu->uiIcons.landscape.texture, &menu->uiIcons.landscape.size },
			{ "Categories\\water.png", &menu->uiIcons.water.texture, &menu->uiIcons.water.size },
			{ "Categories\\debug.png", &menu->uiIcons.debug.texture, &menu->uiIcons.debug.size },
			{ "Categories\\materials.png", &menu->uiIcons.materials.texture, &menu->uiIcons.materials.size },
			{ "Categories\\post-processing.png", &menu->uiIcons.postProcessing.texture, &menu->uiIcons.postProcessing.size }
		};
	}

	void LoadThemeSpecificIcons(Menu* menu, ID3D11Device* device, const std::vector<IconDefinition>& iconDefs)
	{
		const auto& selectedTheme = menu->GetSettings().SelectedThemePreset;
		if (selectedTheme.empty()) {
			return;
		}

		std::filesystem::path themeIconsPath = Util::PathHelpers::GetThemesPath() / selectedTheme;
		if (!std::filesystem::exists(themeIconsPath) || !std::filesystem::is_directory(themeIconsPath)) {
			logger::debug("LoadThemeSpecificIcons: Theme folder does not exist: {}", themeIconsPath.string());
			return;
		}

		logger::info("LoadThemeSpecificIcons: Checking for custom icons in theme '{}' at path: {}", selectedTheme, themeIconsPath.string());

		ID3D11DeviceContext* context = globals::d3d::context;
		if (context)
			context->Flush();

		int iconsOverridden = 0;

		for (const auto& iconDef : iconDefs) {
			std::filesystem::path iconPath = themeIconsPath / std::filesystem::path(iconDef.filename).filename();

			logger::trace("LoadThemeSpecificIcons: Checking for icon: {}", iconPath.string());

			if (std::filesystem::exists(iconPath)) {
				if (*iconDef.texture) {
					(*iconDef.texture)->Release();
					*iconDef.texture = nullptr;
				}

				if (LoadTextureFromFile(device, iconPath.string().c_str(), iconDef.texture, *iconDef.size)) {
					logger::debug("LoadThemeSpecificIcons: Loaded custom icon: {}", iconPath.filename().string());
					iconsOverridden++;
				}
			}
		}

		if (iconsOverridden > 0) {
			logger::info("LoadThemeSpecificIcons: Loaded {} custom icon(s) from theme '{}'", iconsOverridden, selectedTheme);
		}
	}

	bool InitializeMenuIcons(Menu* menu)
	{
		if (!menu) {
			logger::warn("InitializeMenuIcons: Menu pointer is null");
			return false;
		}

		ID3D11Device* device = globals::d3d::device;
		ID3D11DeviceContext* context = globals::d3d::context;
		if (!device || !context) {
			logger::warn("InitializeMenuIcons: D3D device or context is null");
			return false;
		}

		// Flush and wait for GPU idle before releasing textures
		context->Flush();
		winrt::com_ptr<ID3D11Query> eventQuery;
		D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
		if (SUCCEEDED(device->CreateQuery(&queryDesc, eventQuery.put()))) {
			context->End(eventQuery.get());
			BOOL queryData = FALSE;
			for (int i = 0; i < 1000 && context->GetData(eventQuery.get(), &queryData, sizeof(BOOL), 0) != S_OK; i++) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		std::string basePath = Util::PathHelpers::GetIconsPath().string() + "\\";
		logger::info("InitializeMenuIcons: Loading icons from base path: {}", basePath);

		auto iconDefs = GetIconDefinitions(menu);

		for (auto* texturePtr : { &menu->uiIcons.saveSettings.texture, &menu->uiIcons.loadSettings.texture,
				 &menu->uiIcons.clearCache.texture, &menu->uiIcons.logo.texture,
				 &menu->uiIcons.featureSettingRevert.texture, &menu->uiIcons.discord.texture,
				 &menu->uiIcons.characters.texture, &menu->uiIcons.display.texture,
				 &menu->uiIcons.grass.texture, &menu->uiIcons.lighting.texture,
				 &menu->uiIcons.sky.texture, &menu->uiIcons.landscape.texture,
				 &menu->uiIcons.water.texture, &menu->uiIcons.debug.texture,
				 &menu->uiIcons.materials.texture, &menu->uiIcons.postProcessing.texture }) {
			if (*texturePtr) {
				(*texturePtr)->Release();
				*texturePtr = nullptr;
			}
		}

		bool anyIconLoaded = false;
		int iconsLoaded = 0;

		for (const auto& iconDef : iconDefs) {
			std::string fullPath = basePath + iconDef.filename;
			if (LoadTextureFromFile(device, fullPath.c_str(), iconDef.texture, *iconDef.size)) {
				iconsLoaded++;
				anyIconLoaded = true;
			} else {
				// If monochrome icon failed to load, try fallback to colored version
				if (basePath.find("Monochrome") != std::string::npos) {
					std::string fallbackPath = basePath;
					size_t pos = fallbackPath.find("\\Monochrome");
					if (pos != std::string::npos) {
						fallbackPath.erase(pos, 11);  // Remove "\Monochrome"
					}
					fallbackPath += iconDef.filename;
					// Try to extract just the filename from iconDef.filename if it has path
					size_t lastSlash = iconDef.filename.find_last_of("\\/");
					if (lastSlash != std::string::npos) {
						std::string justFilename = iconDef.filename.substr(lastSlash + 1);
						fallbackPath = fallbackPath.substr(0, fallbackPath.find_last_of("\\/") + 1) + justFilename;
					}
					if (LoadTextureFromFile(device, fallbackPath.c_str(), iconDef.texture, *iconDef.size)) {
						iconsLoaded++;
						anyIconLoaded = true;
					} else {
						logger::warn("InitializeMenuIcons: Failed to load icon from: {} (and fallback)", fullPath);
					}
				} else {
					logger::warn("InitializeMenuIcons: Failed to load icon from: {}", fullPath);
				}
			}
		}

		logger::info("InitializeMenuIcons: Loaded {}/{} icons successfully", iconsLoaded, iconDefs.size());

		LoadThemeSpecificIcons(menu, device, iconDefs);

		return anyIconLoaded;
	}
}
