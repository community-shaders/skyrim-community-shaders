#include "PCH.h"

#include "IconLoader.h"

#include "Globals.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stb_image.h>
#include <thread>
#include <vector>

namespace Util
{
	namespace
	{
		/// {6c1c5f0e-6f6e-4c8e-9a1d-3f0b7b2d5a11}: private data on an icon's full view, holding its per-level views.
		constexpr GUID kIconMipViewsGuid = { 0x6c1c5f0e, 0x6f6e, 0x4c8e, { 0x9a, 0x1d, 0x3f, 0x0b, 0x7b, 0x2d, 0x5a, 0x11 } };

		/** @brief One single-level view per mip of an icon, owned by the icon's full view through D3D private data. */
		class IconMipViews final : public IUnknown
		{
		public:
			IconMipViews(std::vector<ID3D11ShaderResourceView*> levelViews, int texWidth, int texHeight) :
				views(std::move(levelViews)), width(texWidth), height(texHeight) {}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
			{
				if (riid == __uuidof(IUnknown)) {
					*out = static_cast<IUnknown*>(this);
					AddRef();
					return S_OK;
				}
				*out = nullptr;
				return E_NOINTERFACE;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG left = --refs;
				if (left == 0) {
					for (auto* view : views)
						if (view)
							view->Release();
					delete this;
				}
				return left;
			}

			std::vector<ID3D11ShaderResourceView*> views;
			int width;
			int height;

		private:
			std::atomic<ULONG> refs{ 1 };
		};

		/** @brief sRGB byte to linear light, so downsampling averages light rather than encoded values. */
		float SrgbToLinear(uint8_t value)
		{
			static const auto table = [] {
				std::array<float, 256> t{};
				for (size_t i = 0; i < t.size(); i++) {
					const float c = static_cast<float>(i) / 255.0f;
					t[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
				}
				return t;
			}();
			return table[value];
		}

		uint8_t LinearToSrgb(float value)
		{
			value = std::clamp(value, 0.0f, 1.0f);
			const float c = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint8_t>(std::lround(c * 255.0f));
		}

		/**
		 * @brief Halves an RGBA8 image with a 2x2 box filter. Colour is averaged premultiplied by alpha and in linear
		 * light, so transparent pixels leave no dark fringe and thin strokes keep their brightness.
		 */
		std::vector<uint8_t> HalveRGBA(const std::vector<uint8_t>& src, int w, int h, int& outW, int& outH)
		{
			outW = std::max(1, w / 2);
			outH = std::max(1, h / 2);
			std::vector<uint8_t> dst(static_cast<size_t>(outW) * outH * 4);
			for (int y = 0; y < outH; y++) {
				for (int x = 0; x < outW; x++) {
					float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumA = 0.0f;
					for (int dy = 0; dy < 2; dy++) {
						for (int dx = 0; dx < 2; dx++) {
							const int sx = std::min(x * 2 + dx, w - 1);
							const int sy = std::min(y * 2 + dy, h - 1);
							const uint8_t* px = &src[(static_cast<size_t>(sy) * w + sx) * 4];
							const float alpha = static_cast<float>(px[3]) / 255.0f;
							sumR += SrgbToLinear(px[0]) * alpha;
							sumG += SrgbToLinear(px[1]) * alpha;
							sumB += SrgbToLinear(px[2]) * alpha;
							sumA += alpha;
						}
					}
					uint8_t* out = &dst[(static_cast<size_t>(y) * outW + x) * 4];
					if (sumA > 0.0f) {
						out[0] = LinearToSrgb(sumR / sumA);
						out[1] = LinearToSrgb(sumG / sumA);
						out[2] = LinearToSrgb(sumB / sumA);
					} else {
						out[0] = out[1] = out[2] = 0;
					}
					out[3] = static_cast<uint8_t>(std::lround(sumA * 0.25f * 255.0f));
				}
			}
			return dst;
		}
	}

	bool LoadTextureFromFile(ID3D11Device* device, const char* filename, ID3D11ShaderResourceView** out_srv, ImVec2& out_size)
	{
		int image_width = 0;
		int image_height = 0;
		unsigned char* image_data = stbi_load(filename, &image_width, &image_height, nullptr, 4);
		if (image_data == nullptr) {
			return false;
		}

		// The whole mip chain is built here, alpha-correct, because ImGui's sampler never leaves level 0 on its own
		std::vector<std::vector<uint8_t>> levels;
		std::vector<std::pair<int, int>> dims;
		levels.emplace_back(image_data, image_data + static_cast<size_t>(image_width) * image_height * 4);
		dims.emplace_back(image_width, image_height);
		stbi_image_free(image_data);
		while (dims.back().first > 1 || dims.back().second > 1) {
			int nextW = 0;
			int nextH = 0;
			auto next = HalveRGBA(levels.back(), dims.back().first, dims.back().second, nextW, nextH);
			levels.push_back(std::move(next));
			dims.emplace_back(nextW, nextH);
		}

		std::vector<D3D11_SUBRESOURCE_DATA> initial(levels.size());
		for (size_t i = 0; i < levels.size(); i++) {
			initial[i].pSysMem = levels[i].data();
			initial[i].SysMemPitch = static_cast<UINT>(dims[i].first) * 4;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = image_width;
		desc.Height = image_height;
		desc.MipLevels = static_cast<UINT>(levels.size());
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		ID3D11Texture2D* pTexture = nullptr;
		device->CreateTexture2D(&desc, initial.data(), &pTexture);
		if (!pTexture) {
			return false;
		}
		Util::SetResourceName(pTexture, "IconLoader::%s", filename);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);
		srvDesc.Texture2D.MostDetailedMip = 0;

		HRESULT hr = device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
		if (FAILED(hr)) {
			pTexture->Release();
			return false;
		}
		Util::SetResourceName(*out_srv, "IconLoader::%s SRV", filename);

		// One view per level, so a draw can start from the level nearest its size (see IconLoader::ViewForSize)
		std::vector<ID3D11ShaderResourceView*> levelViews(levels.size(), nullptr);
		for (size_t i = 0; i < levels.size(); i++) {
			D3D11_SHADER_RESOURCE_VIEW_DESC levelDesc = srvDesc;
			levelDesc.Texture2D.MipLevels = 1;
			levelDesc.Texture2D.MostDetailedMip = static_cast<UINT>(i);
			if (FAILED(device->CreateShaderResourceView(pTexture, &levelDesc, &levelViews[i])))
				levelViews[i] = nullptr;
		}
		auto* holder = new IconMipViews(std::move(levelViews), image_width, image_height);
		(*out_srv)->SetPrivateDataInterface(kIconMipViewsGuid, holder);
		holder->Release();  // The view keeps its own reference

		pTexture->Release();

		out_size = ImVec2(static_cast<float>(image_width), static_cast<float>(image_height));
		return true;
	}
}

namespace Util::IconLoader
{
	ID3D11ShaderResourceView* ViewForSize(ID3D11ShaderResourceView* texture, const ImVec2& drawSize)
	{
		if (!texture || drawSize.x <= 0.0f || drawSize.y <= 0.0f)
			return texture;
		IUnknown* raw = nullptr;
		UINT dataSize = sizeof(raw);
		if (FAILED(texture->GetPrivateData(Util::kIconMipViewsGuid, &dataSize, &raw)) || !raw)
			return texture;
		auto* holder = static_cast<Util::IconMipViews*>(raw);
		// How many times smaller the icon is drawn than stored; each level halves the source
		const float ratio = std::min(static_cast<float>(holder->width) / drawSize.x, static_cast<float>(holder->height) / drawSize.y);
		ID3D11ShaderResourceView* view = texture;
		if (ratio > 1.0f) {
			const int level = std::clamp(static_cast<int>(std::floor(std::log2(ratio) + ThemeManager::Constants::ICON_MIP_SELECT_BIAS)), 0,
				static_cast<int>(holder->views.size()) - 1);
			if (holder->views[level])
				view = holder->views[level];
		}
		raw->Release();  // GetPrivateData added a reference
		return view;
	}

	UIIcons& GetIcons()
	{
		static UIIcons icons;
		return icons;
	}

	bool& PendingReload()
	{
		static bool pending = false;
		return pending;
	}

	void UIIcons::ReleaseAll()
	{
		saveSettings.Release();
		loadSettings.Release();
		deleteSettings.Release();
		clearCache.Release();
		logo.Release();
		logoMonochrome.Release();
		search.Release();
		featureSettingRevert.Release();
		applyToGame.Release();
		pauseTime.Release();
		undo.Release();
		freeCamera.Release();
		playMode.Release();
		discord.Release();
		characters.Release();
		display.Release();
		grass.Release();
		lighting.Release();
		sky.Release();
		landscape.Release();
		water.Release();
		debug.Release();
		materials.Release();
		postProcessing.Release();
	}

	namespace
	{
		struct IconDefinition
		{
			std::string filename;
			ID3D11ShaderResourceView** texture;
			ImVec2* size;
		};

		std::vector<IconDefinition> GetIconDefinitions(Menu* menu)
		{
			auto& icons = GetIcons();
			const bool useMonochrome = menu->GetSettings().Theme.UseMonochromeIcons;
			const bool useMonochromeLogo = menu->GetSettings().Theme.UseMonochromeLogo;
			const std::string iconFolder = useMonochrome ? "Action Icons\\Monochrome\\" : "Action Icons\\";
			const char* logoPath = useMonochromeLogo ? "Community Shaders Logo\\Monochrome\\cs-logo.png" : "Community Shaders Logo\\cs-logo.png";

			return {
				{ iconFolder + "save-settings.png", &icons.saveSettings.texture, &icons.saveSettings.size },
				{ iconFolder + "load-settings.png", &icons.loadSettings.texture, &icons.loadSettings.size },
				{ iconFolder + "clear-cache.png", &icons.clearCache.texture, &icons.clearCache.size },
				{ iconFolder + "delete.png", &icons.deleteSettings.texture, &icons.deleteSettings.size },
				{ logoPath, &icons.logo.texture, &icons.logo.size },
				{ "Community Shaders Logo\\Monochrome\\cs-logo.png", &icons.logoMonochrome.texture, &icons.logoMonochrome.size },
				{ iconFolder + "restore-settings.png", &icons.featureSettingRevert.texture, &icons.featureSettingRevert.size },
				{ iconFolder + "discord.png", &icons.discord.texture, &icons.discord.size },
				{ iconFolder + "apply-to-game.png", &icons.applyToGame.texture, &icons.applyToGame.size },
				{ iconFolder + "pause.png", &icons.pauseTime.texture, &icons.pauseTime.size },
				{ iconFolder + "undo.png", &icons.undo.texture, &icons.undo.size },
				{ iconFolder + "free-camera.png", &icons.freeCamera.texture, &icons.freeCamera.size },
				{ iconFolder + "play-mode.png", &icons.playMode.texture, &icons.playMode.size },

				{ "Categories\\characters.png", &icons.characters.texture, &icons.characters.size },
				{ "Categories\\display.png", &icons.display.texture, &icons.display.size },
				{ "Categories\\grass.png", &icons.grass.texture, &icons.grass.size },
				{ "Categories\\lighting.png", &icons.lighting.texture, &icons.lighting.size },
				{ "Categories\\sky.png", &icons.sky.texture, &icons.sky.size },
				{ "Categories\\landscape.png", &icons.landscape.texture, &icons.landscape.size },
				{ "Categories\\water.png", &icons.water.texture, &icons.water.size },
				{ "Categories\\debug.png", &icons.debug.texture, &icons.debug.size },
				{ "Categories\\materials.png", &icons.materials.texture, &icons.materials.size },
				{ "Categories\\post-processing.png", &icons.postProcessing.texture, &icons.postProcessing.size }
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

					if (Util::LoadTextureFromFile(device, iconPath.string().c_str(), iconDef.texture, *iconDef.size)) {
						logger::debug("LoadThemeSpecificIcons: Loaded custom icon: {}", iconPath.filename().string());
						iconsOverridden++;
					}
				}
			}

			if (iconsOverridden > 0) {
				logger::info("LoadThemeSpecificIcons: Loaded {} custom icon(s) from theme '{}'", iconsOverridden, selectedTheme);
			}
		}
	}  // namespace

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

		GetIcons().ReleaseAll();

		bool anyIconLoaded = false;
		int iconsLoaded = 0;

		for (const auto& iconDef : iconDefs) {
			std::string fullPath = basePath + iconDef.filename;
			if (Util::LoadTextureFromFile(device, fullPath.c_str(), iconDef.texture, *iconDef.size)) {
				iconsLoaded++;
				anyIconLoaded = true;
			} else {
				// If monochrome icon failed to load, try fallback to colored version
				std::string fallbackPath = fullPath;
				const size_t pos = fallbackPath.find("\\Monochrome");
				if (pos != std::string::npos) {
					fallbackPath.erase(pos, 11);  // Remove "\Monochrome"
					if (Util::LoadTextureFromFile(device, fallbackPath.c_str(), iconDef.texture, *iconDef.size)) {
						iconsLoaded++;
						anyIconLoaded = true;
					} else {
						logger::warn("InitializeMenuIcons: Failed to load icon from: {} (and fallback: {})", fullPath, fallbackPath);
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
