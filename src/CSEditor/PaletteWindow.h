#pragma once

#include "WeatherUtils.h"

namespace CSEditorWindows
{
	/// Chrome shared by the floating, auto-sized editor panels (palette and weather picker): no title bar, not resizable or scrollable.
	inline constexpr ImGuiWindowFlags kFloatingPanelFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse;
}

class PaletteWindow
{
public:
	/** @brief Returns the global PaletteWindow singleton instance. */
	static PaletteWindow* GetSingleton()
	{
		static PaletteWindow singleton;
		return &singleton;
	}

	bool open = true;

	struct ColorEntry
	{
		float3 color;
		int useCount = 0;
		float lastUsedTime = 0.0f;
	};

	/** @brief Draw the palette window (favourites + recently used colours). */
	void Draw();

	/**
	 * @brief Record usage of a colour for recency tracking.
	 * @param color The RGB colour that was used.
	 */
	void TrackColorUsage(const float3& color);

	/** @brief Persist palette entries (colours, favorites) to the editor settings. */
	void Save();

	/** @brief Load palette entries from the editor settings. */
	void Load();

private:
	std::vector<ColorEntry> colorEntries;

	// Favorites - fixed slots that users can drag colors into
	static constexpr int maxFavoriteSlots = 10;
	std::array<std::optional<float3>, maxFavoriteSlots> favoriteColors;

	void DrawContents();
	std::vector<ColorEntry*> GetRecentColors(int count);
};
