#pragma once

#include <filesystem>
#include <string>

class PresetManager
{
public:
	static PresetManager& GetSingleton();

	std::filesystem::path GetENBSeriesPath() const;
	std::filesystem::path GetENBSeriesIniPath() const;

private:
	bool UseDataFolder() const;
};
