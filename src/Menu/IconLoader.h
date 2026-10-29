#pragma once

#include <cmath>
#include <d3d11.h>
#include <imgui.h>

struct ID3D11Device;
class Menu;

namespace Util
{
	/** @brief Loads menu icon textures from disk into GPU shader resource views. */
	namespace IconLoader
	{
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
			UIIcon deleteSettings;
			UIIcon clearCache;
			UIIcon logo;
			UIIcon logoMonochrome;  ///< Always the monochrome logo, tinted per theme on the Presets page
			UIIcon search;
			UIIcon featureSettingRevert;
			UIIcon applyToGame;
			UIIcon pauseTime;
			UIIcon undo;
			UIIcon freeCamera;
			UIIcon playMode;
			UIIcon discord;

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

			void ReleaseAll();
		};

		/**
		 * @brief The view of an icon to draw with, chosen for the size it is drawn at.
		 *
		 * ImGui samples a texture's top level only, so an icon stored at 128 px and drawn at 40 px aliases. Icons loaded
		 * through LoadTextureFromFile carry a pre-filtered view per mip level; this returns the level just above the draw
		 * size, which leaves at most a small bilinear step. Falls back to @p texture for icons without levels.
		 * @param texture The icon's full view (UIIcon::texture).
		 * @param drawSize The size, in screen pixels, the icon is about to be drawn at.
		 */
		[[nodiscard]] ID3D11ShaderResourceView* ViewForSize(ID3D11ShaderResourceView* texture, const ImVec2& drawSize);

		/** @brief Rounds a screen position or size to whole pixels, so an icon's edges land on pixel boundaries. */
		[[nodiscard]] inline ImVec2 SnapToPixel(const ImVec2& value)
		{
			return ImVec2(std::round(value.x), std::round(value.y));
		}

		[[nodiscard]] UIIcons& GetIcons();
		[[nodiscard]] bool& PendingReload();

		/**
		 * @brief Loads all menu icon textures including theme-specific overrides.
		 *
		 * Releases any previously loaded icon textures, then loads the full set of
		 * action icons, category icons, and the logo from the base icon directory.
		 * Falls back from monochrome to colored variants when a monochrome icon is
		 * missing. After loading base icons, applies any theme-specific overrides
		 * from the active theme directory.
		 *
		 * @param menu Pointer to the Menu instance (theme settings / paths).
		 * @return true if at least one icon was loaded successfully, false otherwise.
		 */
		bool InitializeMenuIcons(Menu* menu);
	}
}
