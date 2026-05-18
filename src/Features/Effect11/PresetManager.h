#pragma once

#include <d3d11.h>
#include <filesystem>
#include <string>
#include <vector>
#include <winrt/base.h>

class PresetManager
{
public:
	static PresetManager& GetSingleton();

	struct PresetInfo
	{
		std::string folderName;
		std::string displayName;
		std::string description;
		std::string thumbnailPath;
		std::vector<std::string> requiredPlugins;
		std::filesystem::path basePath;
		bool isRoot = false;

		winrt::com_ptr<ID3D11ShaderResourceView> thumbnailSRV;
		float thumbnailWidth = 0;
		float thumbnailHeight = 0;
	};

	void Initialize();

	std::filesystem::path GetENBSeriesPath() const;
	std::filesystem::path GetENBSeriesIniPath() const;

	const std::vector<PresetInfo>& GetPresets() const { return presets; }
	int GetActivePresetIndex() const { return activePresetIndex; }
	const PresetInfo* GetActivePreset() const;

	void SetActivePreset(int index);
	bool AreRequirementsMet(const PresetInfo& preset) const;

private:
	void DiscoverPresets();
	void LoadPresetMetadata(PresetInfo& preset);
	void LoadThumbnail(PresetInfo& preset);
	void LoadPersistedChoice();
	void SavePersistedChoice();

	std::vector<PresetInfo> presets;
	int activePresetIndex = -1;
};
