#pragma once

#include <filesystem>
#include <optional>
#include <shared_mutex>

enum class SettingType
{
	Bool,
	Float,
	TimeOfDay,
	ColorTimeOfDay
};

// Shared time-of-day index lookup used by both TimeOfDayValue and ColorTimeOfDayValue
inline std::optional<int> TimeOfDayIndexFromName(const std::string& name)
{
	static const std::pair<const char*, int> lookup[] = {
		{ "Dawn", 0 }, { "Sunrise", 1 }, { "Day", 2 }, { "Sunset", 3 },
		{ "Dusk", 4 }, { "Night", 5 }, { "InteriorDay", 6 }, { "InteriorNight", 7 }
	};
	for (const auto& [n, idx] : lookup) {
		if (name == n)
			return idx;
	}
	logger::warn("[SettingManager] Unknown time-of-day name '{}', no default used", name);
	return std::nullopt;
}

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

	bool operator==(const TimeOfDayValue& other) const
	{
		return std::equal(std::begin(values), std::end(values), std::begin(other.values));
	}

	float& GetByName(const std::string& name)
	{
		auto idx = TimeOfDayIndexFromName(name);
		return values[idx.value_or(0)];
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

	bool operator==(const ColorTimeOfDayValue& other) const
	{
		for (int i = 0; i < 8; ++i) {
			if (values[i].x != other.values[i].x || values[i].y != other.values[i].y || values[i].z != other.values[i].z) {
				return false;
			}
		}
		return true;
	}

	float3& GetByName(const std::string& name)
	{
		auto idx = TimeOfDayIndexFromName(name);
		return values[idx.value_or(0)];
	}
};

using SettingValue = std::variant<bool, float, TimeOfDayValue, ColorTimeOfDayValue>;

struct Setting
{
	uint32_t id = 0;
	std::string key;
	std::string category;
	SettingType type;
	bool hasWeatherSupport;
	SettingValue defaultValue;
	SettingValue currentValue;
	SettingValue lastSavedValue;
	float minValue = 0.0f;
	float maxValue = 10.0f;
	float step = 0.01f;
};

class SettingManager
{
	friend class WeatherManager;

public:
	static SettingManager& GetSingleton();

	// Setting registration
	void RegisterBoolSetting(const std::string& key, const std::string& category,
		bool defaultValue, bool hasWeatherSupport = false);
	void RegisterFloatSetting(const std::string& key, const std::string& category,
		float defaultValue, float minValue = 0.0f, float maxValue = 10.0f, float step = 0.01f, bool hasWeatherSupport = false);
	void RegisterTimeOfDaySetting(const std::string& key, const std::string& category,
		float defaultValue, float minValue = 0.0f, float maxValue = 10.0f, float step = 0.01f, bool hasWeatherSupport = false);
	void RegisterColorTimeOfDaySetting(const std::string& key, const std::string& category,
		float3 defaultValue, bool hasWeatherSupport = false);

	template <typename T>
	T GetValue(const std::string& key, const std::string& category, bool rawValue = false);

	template <typename T>
	T GetValue(uint32_t id, bool rawValue = false);

	template <typename T>
	void SetValue(const std::string& key, const std::string& category, const T& value);

	template <typename T>
	void SetValue(uint32_t id, const T& value);

	uint32_t GetSettingID(const std::string& key, const std::string& category) const;

	float GetInterpolatedTimeOfDayValue(const std::string& key, const std::string& category);
	float3 GetInterpolatedColorTimeOfDayValue(const std::string& key, const std::string& category);

	bool HasSetting(const std::string& key, const std::string& category) const;
	const Setting* GetSettingInfo(const std::string& key, const std::string& category) const;
	const Setting* GetSettingInfo(uint32_t id) const;
	std::vector<std::string> GetSettingsByCategory(const std::string& category) const;
	std::vector<std::string> GetAllCategories() const;
	bool CategoryHasWeatherSupport(const std::string& category) const;
	void SetCategoryExteriorOnly(const std::string& category, bool exteriorOnly);
	bool IsCategoryExteriorOnly(const std::string& category) const;

	// Weather integration
	void SetWeatherBlendFactors(uint32_t currentWeatherID, uint32_t lastWeatherID, float blendFactor);
	void LoadWeatherSettings(const std::vector<uint32_t>& weatherIDs, const std::string& filePath);
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
	void SetTimeOfDayData(const float timeOfDay1[4], const float timeOfDay2[4]);

private:
	struct CategorySettings
	{
		std::unordered_map<std::string, uint32_t> settings;  // key -> ID
		std::vector<std::string> settingOrder;
		bool ignoreWeatherSystem = false;
		bool ignoreWeatherSystemInterior = true;
		bool lastSavedIgnoreWeatherSystem = false;
		bool lastSavedIgnoreWeatherSystemInterior = true;
		bool exteriorOnly = false;
	};

	std::vector<Setting> allSettings;
	std::unordered_map<std::string, CategorySettings> categories;
	std::vector<std::string> categoryOrder;
	std::unordered_map<uint32_t, std::vector<SettingValue>> weatherData;
	std::unordered_map<uint32_t, std::vector<SettingValue>> lastSavedWeatherData;

	uint32_t currentWeatherID = 0;
	uint32_t lastWeatherID = 0;
	float weatherBlendFactor = 0.0f;

	float timeOfDay1[4] = { 0, 0, 0, 0 };
	float timeOfDay2[4] = { 0, 0, 0, 0 };

	// INI file modification time tracking to skip redundant reloads
	std::filesystem::file_time_type lastMainIniWriteTime{};
	std::unordered_map<std::string, std::filesystem::file_time_type> weatherFileWriteTimes;

	mutable std::shared_mutex mutex;

	void RegisterSettingInternal(Setting& setting);

	template <typename T>
	T GetValueInternal(uint32_t id, bool rawValue = false) const;
	template <typename T>
	void SetValueInternal(uint32_t id, const T& value);
	uint32_t GetSettingIDInternal(const std::string& key, const std::string& category) const;

	SettingValue InterpolateValues(const SettingValue& a, const SettingValue& b, float t) const;
	float ComputeTimeOfDayInterpolation(const TimeOfDayValue& value) const;
	float3 ComputeColorTimeOfDayInterpolation(const ColorTimeOfDayValue& value) const;
	void LoadSettingFromFile(const std::string& filePath, const std::string& section, const std::string& key, Setting& setting);
	void SaveSettingToFile(const std::string& filePath, const std::string& section, const std::string& key, const Setting& setting);
};