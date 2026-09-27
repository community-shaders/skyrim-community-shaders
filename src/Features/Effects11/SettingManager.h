#pragma once

#include <optional>
#include <shared_mutex>
#include <string_view>

/** @brief Lets the string-keyed setting maps be probed with a string_view, so the per-frame
 *  lookups made with string literals never allocate a temporary std::string. */
struct SettingStringHash
{
	using is_transparent = void;
	size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
};

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

};

static_assert(TimeOfDayValue::Total <= 8 && ColorTimeOfDayValue::Total <= 8, "periods must fit the uint8_t mask");
/** @brief Defined-period mask covering every time-of-day period; scalar settings always use it. */
inline constexpr uint8_t AllPeriodsMask = static_cast<uint8_t>((1u << TimeOfDayValue::Total) - 1);

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
	std::string dependsOnKey;
	std::string dependsOnCategory;
	std::vector<std::string> legacyKeys;
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
	T GetValue(std::string_view key, std::string_view category, bool rawValue = false);

	template <typename T>
	T GetValue(uint32_t id, bool rawValue = false);

	template <typename T>
	void SetValue(uint32_t id, const T& value);

	uint32_t GetSettingID(std::string_view key, std::string_view category) const;

	float GetInterpolatedTimeOfDayValue(std::string_view key, std::string_view category);
	float3 GetInterpolatedColorTimeOfDayValue(std::string_view key, std::string_view category);

	const Setting* GetSettingInfo(std::string_view key, std::string_view category) const;
	std::vector<std::string> GetSettingsByCategory(const std::string& category) const;
	bool CategoryHasWeatherSupport(const std::string& category) const;
	void SetCategoryExteriorOnly(const std::string& category, bool exteriorOnly);
	bool IsCategoryExteriorOnly(const std::string& category) const;

	/** @brief Every category in registration order. */
	std::vector<std::string> GetCategories() const;

	void SetCategoryDependency(const std::string& category, const std::string& dependsOnKey, const std::string& dependsOnCategory);
	/** @brief The {key, category} of the bool setting that switches a category on, or empty strings when it has none. */
	std::pair<std::string, std::string> GetCategoryDependency(const std::string& category) const;
	void SetSettingDependency(const std::string& key, const std::string& category, const std::string& dependsOnKey, const std::string& dependsOnCategory);
	void SetSettingLegacyKey(const std::string& key, const std::string& category, const std::string& legacyKey);
	bool IsCategoryEnabled(const std::string& category);
	bool IsSettingEnabled(const std::string& key, const std::string& category);

	// Weather integration
	/** @brief True when EnableMultipleWeathers is on, so weather-aware settings read and write the weather files. */
	bool IsWeatherSystemEnabled() const;
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
	bool GetIgnoreWeatherSystem(const std::string& category) const;
	bool GetIgnoreWeatherSystemInterior(const std::string& category) const;
	void SetIgnoreWeatherSystem(const std::string& category, bool ignore);
	void SetIgnoreWeatherSystemInterior(const std::string& category, bool ignore);

	// Time of day interpolation data
	void SetTimeOfDayData(const float timeOfDay1[4], const float timeOfDay2[4]);

private:
	struct CategorySettings
	{
		std::unordered_map<std::string, uint32_t, SettingStringHash, std::equal_to<>> settings;  // key -> ID
		std::vector<std::string> settingOrder;
		bool ignoreWeatherSystem = false;
		bool ignoreWeatherSystemInterior = true;
		bool lastSavedIgnoreWeatherSystem = false;
		bool lastSavedIgnoreWeatherSystemInterior = true;
		bool exteriorOnly = false;
		std::string dependsOnKey;
		std::string dependsOnCategory;
	};

	std::vector<Setting> allSettings;
	std::unordered_map<std::string, CategorySettings, SettingStringHash, std::equal_to<>> categories;
	std::vector<std::string> categoryOrder;
	std::unordered_map<uint32_t, std::vector<SettingValue>> weatherData;
	std::unordered_map<uint32_t, std::vector<SettingValue>> lastSavedWeatherData;
	// Per setting, the periods each weather actually defines (in its file or by a UI edit). The others read the live
	// enbseries.ini value rather than the copy snapshotted when the weather file was loaded.
	std::unordered_map<uint32_t, std::vector<uint8_t>> weatherDefined;
	std::unordered_map<uint32_t, std::vector<uint8_t>> lastSavedWeatherDefined;

	uint32_t currentWeatherID = 0;
	uint32_t lastWeatherID = 0;
	float weatherBlendFactor = 0.0f;
	uint32_t multipleWeathersSettingID = 0xFFFFFFFF;

	float timeOfDay1[4] = { 0, 0, 0, 0 };
	float timeOfDay2[4] = { 0, 0, 0, 0 };

	mutable std::shared_mutex mutex;

	void RegisterSettingInternal(Setting& setting);
	void LoadWeatherIgnoreSettings(const std::string& filePath);
	bool IsWeatherSystemEnabledInternal() const;
	/** @brief Bitmask of the periods the weather defines for the setting, in its file or through a UI edit. */
	uint8_t GetWeatherDefinedMask(uint32_t weatherID, uint32_t settingID) const;
	/** @brief The setting as the weather sees it: its defined periods over the live enbseries.ini value. */
	SettingValue ResolveWeatherValue(uint32_t weatherID, uint32_t settingID) const;

	template <typename T>
	T GetValueInternal(uint32_t id, bool rawValue = false) const;
	template <typename T>
	void SetValueInternal(uint32_t id, const T& value);
	uint32_t GetSettingIDInternal(std::string_view key, std::string_view category) const;

	SettingValue InterpolateValues(const SettingValue& a, const SettingValue& b, float t) const;
	float ComputeTimeOfDayInterpolation(const TimeOfDayValue& value) const;
	float3 ComputeColorTimeOfDayInterpolation(const ColorTimeOfDayValue& value) const;
	/** @return Bitmask of the periods the file set under any of its keys; scalars set every bit. */
	uint8_t LoadSettingFromFile(const std::string& filePath, const std::string& section, const std::string& key, Setting& setting);
	/** @brief Writes the setting; time-of-day types write only the periods in periodMask. */
	void SaveSettingToFile(const std::string& filePath, const std::string& section, const std::string& key, const Setting& setting, uint8_t periodMask = AllPeriodsMask);
};