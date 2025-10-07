#pragma once

enum class SettingType
{
	Bool,
	Float,
	TimeOfDay,
	ColorTimeOfDay
};

struct TimeOfDayValue
{
	float values[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

	enum Index
	{
		Dawn = 0,
		Sunrise = 1,
		Day = 2,
		Sunset = 3,
		Dusk = 4,
		Night = 5,
		InteriorDay = 6,
		InteriorNight = 7,
		Total = 8
	};

	float& operator[](Index idx) { return values[idx]; }
	const float& operator[](Index idx) const { return values[idx]; }

	float& GetByName(const std::string& name)
	{
		if (name == "Dawn")
			return values[Dawn];
		if (name == "Sunrise")
			return values[Sunrise];
		if (name == "Day")
			return values[Day];
		if (name == "Sunset")
			return values[Sunset];
		if (name == "Dusk")
			return values[Dusk];
		if (name == "Night")
			return values[Night];
		if (name == "InteriorDay")
			return values[InteriorDay];
		if (name == "InteriorNight")
			return values[InteriorNight];
		return values[Dawn];
	}
};

struct ColorTimeOfDayValue
{
	float3 values[8] = {
		{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }
	};

	enum Index
	{
		Dawn = 0,
		Sunrise = 1,
		Day = 2,
		Sunset = 3,
		Dusk = 4,
		Night = 5,
		InteriorDay = 6,
		InteriorNight = 7,
		Total = 8
	};

	float3& operator[](Index idx) { return values[idx]; }
	const float3& operator[](Index idx) const { return values[idx]; }

	float3& GetByName(const std::string& name)
	{
		if (name == "Dawn")
			return values[Dawn];
		if (name == "Sunrise")
			return values[Sunrise];
		if (name == "Day")
			return values[Day];
		if (name == "Sunset")
			return values[Sunset];
		if (name == "Dusk")
			return values[Dusk];
		if (name == "Night")
			return values[Night];
		if (name == "InteriorDay")
			return values[InteriorDay];
		if (name == "InteriorNight")
			return values[InteriorNight];
		return values[Dawn];
	}
};

using SettingValue = std::variant<bool, float, TimeOfDayValue, ColorTimeOfDayValue>;

struct Setting
{
	std::string key;
	std::string category;
	SettingType type;
	bool hasWeatherSupport;
	SettingValue defaultValue;
	SettingValue currentValue;
	float minValue = 0.0f;
	float maxValue = 10.0f;
};

class SettingManager
{
public:
	static SettingManager& GetSingleton();

	// Setting registration
	void RegisterBoolSetting(const std::string& key, const std::string& category,
		bool defaultValue, bool hasWeatherSupport = false);
	void RegisterFloatSetting(const std::string& key, const std::string& category,
		float defaultValue, float minValue = 0.0f, float maxValue = 10.0f, bool hasWeatherSupport = false);
	void RegisterTimeOfDaySetting(const std::string& key, const std::string& category,
		float defaultValue, bool hasWeatherSupport = false);
	void RegisterColorTimeOfDaySetting(const std::string& key, const std::string& category,
		float3 defaultValue, bool hasWeatherSupport = false);

	template <typename T>
	T GetValue(const std::string& key, const std::string& category, bool rawValue = false);

	template <typename T>
	void SetValue(const std::string& key, const std::string& category, const T& value);

	float GetInterpolatedTimeOfDayValue(const std::string& key, const std::string& category);
	float3 GetInterpolatedColorTimeOfDayValue(const std::string& key, const std::string& category);

	bool HasSetting(const std::string& key, const std::string& category) const;
	const Setting* GetSettingInfo(const std::string& key, const std::string& category) const;
	std::vector<std::string> GetSettingsByCategory(const std::string& category) const;
	std::vector<std::string> GetAllCategories() const;
	bool CategoryHasWeatherSupport(const std::string& category) const;

	// Weather integration
	void SetWeatherBlendFactors(uint32_t currentWeatherID, uint32_t lastWeatherID, float blendFactor);
	void LoadWeatherSettings(const std::string& weatherKey, const std::string& filePath);
	void SaveWeatherSettings(const std::string& weatherKey, const std::string& filePath);
	void SaveAllWeatherSettings();
	void ReloadAllWeatherSettings();

	// File I/O
	void LoadFromFile(const std::string& filePath);
	void SaveToFile(const std::string& filePath);

	// Effect save/load coordination
	void Load();
	void Save();

	// Weather ignore settings management
	void LoadWeatherIgnoreSettings(const std::string& filePath);
	bool GetIgnoreWeatherSystem(const std::string& category) const;
	bool GetIgnoreWeatherSystemInterior(const std::string& category) const;
	void SetIgnoreWeatherSystem(const std::string& category, bool ignore);
	void SetIgnoreWeatherSystemInterior(const std::string& category, bool ignore);

	// Time of day interpolation data
	void SetTimeOfDayData(const float timeOfDay1[4], const float timeOfDay2[4], float interiorFactor);

private:
	struct CategorySettings
	{
		std::unordered_map<std::string, Setting> settings;
		bool ignoreWeatherSystem = false;
		bool ignoreWeatherSystemInterior = true;
	};

	std::unordered_map<std::string, CategorySettings> categories;
	std::unordered_map<uint32_t, std::unordered_map<std::string, SettingValue>> weatherData;

	uint32_t currentWeatherID = 0;
	uint32_t lastWeatherID = 0;
	float weatherBlendFactor = 0.0f;

	float timeOfDay1[4] = { 0, 0, 0, 0 };
	float timeOfDay2[4] = { 0, 0, 0, 0 };
	float interiorFactor = 0.0f;

	SettingValue InterpolateValues(const SettingValue& a, const SettingValue& b, float t);
	float ComputeTimeOfDayInterpolation(const TimeOfDayValue& value);
	float3 ComputeColorTimeOfDayInterpolation(const ColorTimeOfDayValue& value);
	void LoadSettingFromFile(const std::string& filePath, const std::string& section, const std::string& key, Setting& setting);
	void SaveSettingToFile(const std::string& filePath, const std::string& section, const std::string& key, const Setting& setting);
};