#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Forward declaration
class WeatherManager;

enum class SettingType
{
	Bool,
	Float,
	TimeOfDay
};

// Time of Day setting with interpolation
struct TimeOfDayValue
{
	float Dawn = 1.0f;
	float Sunrise = 1.0f;
	float Day = 1.0f;
	float Sunset = 1.0f;
	float Dusk = 1.0f;
	float Night = 1.0f;
	float InteriorDay = 1.0f;
	float InteriorNight = 1.0f;

	float& operator[](const std::string& timeOfDay)
	{
		if (timeOfDay == "Dawn")
			return Dawn;
		if (timeOfDay == "Sunrise")
			return Sunrise;
		if (timeOfDay == "Day")
			return Day;
		if (timeOfDay == "Sunset")
			return Sunset;
		if (timeOfDay == "Dusk")
			return Dusk;
		if (timeOfDay == "Night")
			return Night;
		if (timeOfDay == "InteriorDay")
			return InteriorDay;
		if (timeOfDay == "InteriorNight")
			return InteriorNight;
		return Dawn;  // fallback
	}

	const float& operator[](const std::string& timeOfDay) const
	{
		if (timeOfDay == "Dawn")
			return Dawn;
		if (timeOfDay == "Sunrise")
			return Sunrise;
		if (timeOfDay == "Day")
			return Day;
		if (timeOfDay == "Sunset")
			return Sunset;
		if (timeOfDay == "Dusk")
			return Dusk;
		if (timeOfDay == "Night")
			return Night;
		if (timeOfDay == "InteriorDay")
			return InteriorDay;
		if (timeOfDay == "InteriorNight")
			return InteriorNight;
		return Dawn;  // fallback
	}
};

// Setting value variant
using SettingValue = std::variant<bool, float, TimeOfDayValue>;

// Setting metadata
struct SettingInfo
{
	std::string key;
	std::string category;
	SettingType type;
	bool hasWeatherSupport;
	SettingValue defaultValue;
	SettingValue currentValue;

	// UI metadata
	float minValue = 0.0f;   // For float settings
	float maxValue = 10.0f;  // For float settings
};

class SettingsRegistry
{
public:
	static SettingsRegistry& GetSingleton();

	// Setting registration
	void RegisterBoolSetting(const std::string& key, const std::string& category,
		bool defaultValue, bool hasWeatherSupport = false);
	void RegisterFloatSetting(const std::string& key, const std::string& category,
		float defaultValue, float minValue = 0.0f, float maxValue = 10.0f, bool hasWeatherSupport = false);
	void RegisterTimeOfDaySetting(const std::string& key, const std::string& category,
		const TimeOfDayValue& defaultValue, bool hasWeatherSupport = false);

	// Setting access
	template <typename T>
	T GetValue(const std::string& key, const std::string& category);

	template <typename T>
	void SetValue(const std::string& key, const std::string& category, const T& value);

	// Get interpolated time-of-day value (automatically handles weather blending if enabled)
	float GetInterpolatedTimeOfDayValue(const std::string& key);

	float GetInterpolatedTimeOfDayValue(const std::string& key, const std::string& category);

	// Setting queries
	bool HasSetting(const std::string& key, const std::string& category) const;
	const SettingInfo* GetSettingInfo(const std::string& key, const std::string& category) const;
	std::vector<std::string> GetSettingsByCategory(const std::string& category) const;
	std::vector<std::string> GetAllCategories() const;
	bool CategoryHasWeatherSupport(const std::string& category) const;
	std::vector<std::string> GetCategoriesWithWeatherSupport() const;

	// Weather integration
	void SetWeatherBlendFactors(uint32_t currentWeatherID, uint32_t lastWeatherID, float blendFactor);
	void LoadWeatherSettings(const std::string& weatherKey, const std::string& filePath);

	// File I/O
	void LoadFromFile(const std::string& filePath);
	void SaveToFile(const std::string& filePath);
	
	// Weather ignore settings management
	void SaveWeatherIgnoreSettings(const std::string& filePath);
	void LoadWeatherIgnoreSettings(const std::string& filePath);
	bool GetIgnoreWeatherSystem(const std::string& category) const;
	bool GetIgnoreWeatherSystemInterior(const std::string& category) const;
	void SetIgnoreWeatherSystem(const std::string& category, bool ignore);
	void SetIgnoreWeatherSystemInterior(const std::string& category, bool ignore);

	// Time of day interpolation data
	void SetTimeOfDayData(const float timeOfDay1[4], const float timeOfDay2[4], float interiorFactor);

private:
	SettingsRegistry() = default;
	~SettingsRegistry() = default;
	SettingsRegistry(const SettingsRegistry&) = delete;
	SettingsRegistry& operator=(const SettingsRegistry&) = delete;

	std::unordered_map<std::string, std::unique_ptr<SettingInfo>> settings;
	std::unordered_map<std::string, std::unordered_map<std::string, SettingValue>> weatherSettings;  // weatherKey -> settingKey -> value

	// Current weather state
	uint32_t currentWeatherID = 0;
	uint32_t lastWeatherID = 0;
	float weatherBlendFactor = 0.0f;
	
	// Weather ignore settings per category
	std::unordered_map<std::string, bool> ignoreWeatherSystem;        // category -> bool
	std::unordered_map<std::string, bool> ignoreWeatherSystemInterior; // category -> bool

	// Time of day interpolation state
	float timeOfDay1[4] = { 0, 0, 0, 0 };
	float timeOfDay2[4] = { 0, 0, 0, 0 };
	float interiorFactor = 0.0f;

	// Helper methods
	std::string MakeCompositeKey(const std::string& key, const std::string& category) const;
	SettingValue InterpolateWeatherValues(const SettingValue& currentValue, const SettingValue& lastValue, float t);
	TimeOfDayValue InterpolateTimeOfDayValues(const TimeOfDayValue& a, const TimeOfDayValue& b, float t);
	float ComputeTimeOfDayInterpolation(const TimeOfDayValue& value);
	void LoadSettingFromFile(const std::string& filePath, const std::string& section, const std::string& key, SettingInfo& setting);
	void SaveSettingToFile(const std::string& filePath, const std::string& section, const std::string& key, const SettingInfo& setting);
};