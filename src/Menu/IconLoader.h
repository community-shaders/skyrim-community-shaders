#pragma once

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
