#pragma once

#include "WeatherUtils.h"

class PaletteWindow
{
public:
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
		bool isFavorite = false;
	};

	struct ValueEntry
	{
		std::string name;
		float value;
		int useCount = 0;
		float lastUsedTime = 0.0f;
		bool isFavorite = false;
	};

	void Draw();
	void TrackColorUsage(const float3& color);
	void TrackValueUsage(const std::string& name, float value);
	void Save();
	void Load();

private:
	std::vector<ColorEntry> colorEntries;
	std::vector<ValueEntry> valueEntries;
	
	// Favorites - fixed slots that users can drag colors into
	static constexpr int maxFavoriteSlots = 10;
	std::array<std::optional<float3>, maxFavoriteSlots> favoriteColors;
	
	// Clipboard
	float3 copiedColor = { 0.0f, 0.0f, 0.0f };
	float copiedValue = 0.0f;
	std::string copiedValueName;
	bool hasColorInClipboard = false;
	bool hasValueInClipboard = false;

	void DrawColorsTab();
	void DrawValuesTab();
	std::vector<ColorEntry*> GetRecentColors(int count);
	std::vector<ColorEntry*> GetMostUsedColors(int count);
	std::vector<ValueEntry*> GetRecentValues(int count);
	std::vector<ValueEntry*> GetMostUsedValues(int count);
};
