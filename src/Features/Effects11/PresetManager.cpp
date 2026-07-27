#include "PresetManager.h"

PresetManager& PresetManager::GetSingleton()
{
	static PresetManager instance;
	return instance;
}

bool PresetManager::UseDataFolder() const
{
	return std::filesystem::exists("Data\\enbseries.ini") && std::filesystem::exists("Data\\enbseries\\enbeffect.fx");
}

std::filesystem::path PresetManager::GetENBSeriesPath() const
{
	if (UseDataFolder())
		return std::filesystem::absolute("Data\\enbseries");
	return std::filesystem::absolute("enbseries");
}

std::filesystem::path PresetManager::GetENBSeriesIniPath() const
{
	if (UseDataFolder())
		return std::filesystem::absolute("Data\\enbseries.ini");
	return std::filesystem::absolute("enbseries.ini");
}
