#pragma once

class WeatherPickerWindow
{
public:
	static WeatherPickerWindow* GetSingleton()
	{
		static WeatherPickerWindow singleton;
		return &singleton;
	}

	/** @brief Draw the floating weather picker (anchored above the palette until moved). */
	void Draw();
	/** @brief Picker body: weather combo, lock and transition controls. */
	void DrawContents();
	/** @brief Persist picker state to the editor settings. */
	void Save();
	/** @brief Load picker state from the editor settings. */
	void Load();

	bool open = true;
};
