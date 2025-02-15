#pragma once

struct Feature
{
	bool loaded = false;
	std::string version;
	std::string failedLoadedMessage;

	virtual std::string GetName() = 0;
	virtual std::string GetShortName() = 0;
	virtual std::string_view GetShaderDefineName() { return ""; }
	virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() { return {}; }

	virtual bool HasShaderDefine(RE::BSShader::Type) { return false; }
	/**
	 * Whether the feature supports VR.
	 * 
	 * \return true if VR supported; else false
	 */
	virtual bool SupportsVR() { return false; }

	/**
	 * Whether the feature is a CORE feature
	 * This will place it under "Core Features" in UI
	 * Also need to create a file named "CORE" in the root of the feature folder
	 * if it should be merged into main cs zip file
	 */
	virtual bool IsCore() const { return false; }

	virtual void SetupResources() {}
	virtual void Reset() {}

	virtual void DrawSettings() {}

	virtual void ReflectionsPrepass(){};
	virtual void Prepass() {}
	virtual void EarlyPrepass() {}

	virtual void DataLoaded() {}
	virtual void PostPostLoad() {}

	void Load(json& o_json);
	void Save(json& o_json);

	virtual void SaveSettings(json&) {}
	virtual void LoadSettings(json&) {}

	virtual void RestoreDefaultSettings() {}
	virtual bool ToggleAtBootSetting();

	virtual bool ValidateCache(CSimpleIniA& a_ini);
	virtual void WriteDiskCacheInfo(CSimpleIniA& a_ini);
	virtual void ClearShaderCache() {}

	static const std::vector<Feature*>& GetFeatureList();
};