#pragma once

class WeatherPickerWindow
{
public:
	static WeatherPickerWindow* GetSingleton()
	{
		static WeatherPickerWindow singleton;
		return &singleton;
	}

	void Draw();
	void DrawContents();
	void Save();
	void Load();

	bool open = true;
};
