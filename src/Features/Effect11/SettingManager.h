#pragma once

#include <filesystem>
#include <shared_mutex>

/**
 * Map a time-of-day period name to its corresponding index (Dawn=0 .. InteriorNight=7).
 * @param name Case-sensitive name of the time-of-day period (e.g. "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight").
 * @returns Index for the named period (0 through 7). Returns 0 if the name is not recognized.
 */
enum class SettingType
{
	Bool,
	Float,
	TimeOfDay,
	ColorTimeOfDay
};

// Shared time-of-day index lookup used by both TimeOfDayValue and ColorTimeOfDayValue
inline int TimeOfDayIndexFromName(const std::string& name)
{
	static const std::pair<const char*, int> lookup[] = {
		{ "Dawn", 0 }, { "Sunrise", 1 }, { "Day", 2 }, { "Sunset", 3 },
		{ "Dusk", 4 }, { "Night", 5 }, { "InteriorDay", 6 }, { "InteriorNight", 7 }
	};
	for (const auto& [n, idx] : lookup) {
		if (name == n)
			return idx;
	}
	return 0;  // Default to Dawn
}

/**
	 * Represents a set of values for eight semantic times of day.
	 *
	 * Stores eight floats (initialized to 1.0f) corresponding to the periods defined by the Index enum:
	 * Dawn, Sunrise, Day, Sunset, Dusk, Night, InteriorDay, and InteriorNight. Provides read/write
	 * indexed access via operator[] using the Index enumeration.
	 */
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

	/**
 * Get the time-of-day value for the specified period name.
 * @param name Period name used to index the internal array (e.g., "Dawn", "Morning", "Noon", "Afternoon", "Dusk", "Evening", "Night", "InteriorNight").
 * @returns Reference to the stored float value for the named period.
 */
float& GetByName(const std::string& name) { return values[TimeOfDayIndexFromName(name)]; }
};

/**
	 * @brief Holds per-period RGB color values for time-of-day driven settings.
	 *
	 * Stores eight RGB entries corresponding to named time-of-day periods (Dawn, Sunrise, Day,
	 * Sunset, Dusk, Night, InteriorDay, InteriorNight). Each entry defaults to {1.0f, 1.0f, 1.0f}.
	 *
	 * The nested Index enum names the semantic slots and the Total value indicates the fixed slot count.
	 *
	 * @note Use the Index enum to index into the values array to avoid hard-coded indices.
	 *
	 * @param[in] idx Index enum value selecting which time-of-day color to access.
	 * @return (non-const) Reference to the RGB color for the selected time-of-day period.
	 * @return (const) Const reference to the RGB color for the selected time-of-day period.
	 */
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

	float3& GetByName(const std::string& name) { return values[TimeOfDayIndexFromName(name)]; }
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

/**
 * Return the global SettingManager singleton instance.
 * @returns Reference to the shared SettingManager.
 */

/**
 * Register a boolean setting.
 * @param key Unique setting key within the category.
 * @param category Category name to group the setting.
 * @param defaultValue Default boolean value for the setting.
 * @param hasWeatherSupport If true, the setting participates in per-weather data and blending.
 */

/**
 * Register a float setting.
 * @param key Unique setting key within the category.
 * @param category Category name to group the setting.
 * @param defaultValue Default numeric value for the setting.
 * @param minValue Minimum allowed value for the setting.
 * @param maxValue Maximum allowed value for the setting.
 * @param step Increment step used for UI or clamping.
 * @param hasWeatherSupport If true, the setting participates in per-weather data and blending.
 */

/**
 * Register a time-of-day setting (per-period numeric values).
 * @param key Unique setting key within the category.
 * @param category Category name to group the setting.
 * @param defaultValue Default numeric value used for all time-of-day periods before per-period overrides.
 * @param minValue Minimum allowed value for the setting.
 * @param maxValue Maximum allowed value for the setting.
 * @param step Increment step used for UI or clamping.
 * @param hasWeatherSupport If true, the setting participates in per-weather data and blending.
 */

/**
 * Register a color time-of-day setting (per-period RGB values).
 * @param key Unique setting key within the category.
 * @param category Category name to group the setting.
 * @param defaultValue Default RGB value used for all time-of-day periods before per-period overrides.
 * @param hasWeatherSupport If true, the setting participates in per-weather data and blending.
 */

/**
 * Retrieve a setting value by key and category.
 * @param key Setting key.
 * @param category Category name.
 * @param rawValue If true, return the stored raw value without time-of-day or weather interpolation.
 * @returns The setting value cast to the requested type `T`.
 */

/**
 * Retrieve a setting value by numeric ID.
 * @param id Setting numeric identifier.
 * @param rawValue If true, return the stored raw value without time-of-day or weather interpolation.
 * @returns The setting value cast to the requested type `T`.
 */

/**
 * Set a setting value by key and category.
 * @param key Setting key.
 * @param category Category name.
 * @param value New value to assign to the setting.
 */

/**
 * Set a setting value by numeric ID.
 * @param id Setting numeric identifier.
 * @param value New value to assign to the setting.
 */

/**
 * Lookup the numeric ID for a setting given its key and category.
 * @param key Setting key.
 * @param category Category name.
 * @returns Numeric ID of the setting or 0 if not found.
 */

/**
 * Compute an interpolated scalar value for a time-of-day setting, applying time-of-day, interior blending, and weather blending.
 * @param key Setting key.
 * @param category Category name.
 * @returns Interpolated float result for the current time/weather/interior state.
 */

/**
 * Compute an interpolated RGB value for a color time-of-day setting, applying time-of-day, interior blending, and weather blending.
 * @param key Setting key.
 * @param category Category name.
 * @returns Interpolated float3 RGB result for the current time/weather/interior state.
 */

/**
 * Check whether a setting exists.
 * @param key Setting key.
 * @param category Category name.
 * @returns `true` if the setting is registered, `false` otherwise.
 */

/**
 * Get metadata for a setting by key and category.
 * @param key Setting key.
 * @param category Category name.
 * @returns Pointer to the Setting metadata, or `nullptr` if not found.
 */

/**
 * Get metadata for a setting by numeric ID.
 * @param id Setting numeric identifier.
 * @returns Pointer to the Setting metadata, or `nullptr` if the id is invalid.
 */

/**
 * List all setting keys registered in a category in registration order.
 * @param category Category name.
 * @returns Vector of setting keys (may be empty if the category does not exist).
 */

/**
 * Return all registered category names in registration order.
 * @returns Vector of category names.
 */

/**
 * Determine whether any setting in the category supports per-weather data.
 * @param category Category name.
 * @returns `true` if the category contains at least one weather-aware setting, `false` otherwise.
 */

/**
 * Set the current and previous weather IDs and the blend factor used when interpolating weather-based values.
 * @param currentWeatherID Numeric ID of the current weather.
 * @param lastWeatherID Numeric ID of the previous weather.
 * @param blendFactor Blend factor in [0,1] where 0 = fully lastWeatherID, 1 = fully currentWeatherID.
 */

/**
 * Load per-weather settings for the given weather IDs from the specified file path.
 * @param weatherIDs List of weather numeric IDs to load.
 * @param filePath Path to the weather settings file/source.
 */

/**
 * Save per-weather settings for the specified weather key to a file.
 * @param weatherKey Identifier used to name or select the weather data to save.
 * @param filePath Destination file path.
 */

/**
 * Save all currently loaded per-weather settings to their configured destinations.
 */

/**
 * Reload all per-weather settings from their configured sources, ignoring cached file timestamps as needed.
 */

/**
 * Load general settings (non-weather) from the specified file.
 * @param filePath Path to the settings file.
 */

/**
 * Save general settings (non-weather) to the specified file.
 * @param filePath Destination file path.
 */

/**
 * Perform a full settings load sequence (including main and weather data) to initialize runtime state.
 */

/**
 * Perform a full settings save sequence (including main and weather data) to persist runtime state.
 */

/**
 * Load category-level flags that indicate whether each category should ignore the external weather system.
 * @param filePath Path to the weather-ignore configuration file.
 */

/**
 * Query whether the specified category ignores the external weather system for non-interior settings.
 * @param category Category name.
 * @returns `true` if the category ignores the weather system, `false` otherwise.
 */

/**
 * Query whether the specified category ignores the external weather system for interior settings.
 * @param category Category name.
 * @returns `true` if the category ignores the weather system for interior variants, `false` otherwise.
 */

/**
 * Set whether the specified category should ignore the external weather system for non-interior settings.
 * @param category Category name.
 * @param ignore If `true`, the category will ignore weather-driven overrides.
 */

/**
 * Set whether the specified category should ignore the external weather system for interior settings.
 * @param category Category name.
 * @param ignore If `true`, the category will ignore weather-driven overrides for interior variants.
 */

/**
 * Provide the time-of-day input vectors and interior blending factor used by time-of-day interpolation routines.
 * @param timeOfDay1 Four-component control vector for the first time-of-day sample.
 * @param timeOfDay2 Four-component control vector for the second time-of-day sample.
 * @param interiorFactor Blend factor in [0,1] that controls interior vs. exterior contribution.
 */

/**
 * Internal helper to register a fully-populated Setting into internal containers.
 * @param setting Setting object to register; ownership remains with the caller/context.
 */

/**
 * Retrieve a typed value from the internal setting storage by ID.
 * @param id Setting numeric identifier.
 * @param rawValue If true, return the stored raw value without time-of-day or weather interpolation.
 * @returns The setting value cast to the requested type `T`.
 */

/**
 * Set a typed value in the internal setting storage by ID.
 * @param id Setting numeric identifier.
 * @param value New value to assign to the setting.
 */

/**
 * Internal lookup for a setting ID given key and category without taking external locks.
 * @param key Setting key.
 * @param category Category name.
 * @returns Numeric ID of the setting or 0 if not found.
 */

/**
 * Interpolate between two SettingValue instances according to the parameter t.
 * @param a Value at t=0.
 * @param b Value at t=1.
 * @param t Interpolation factor in [0,1].
 * @returns Resulting interpolated SettingValue.
 */

/**
 * Compute a single scalar result by applying time-of-day and configured blending to a TimeOfDayValue.
 * @param value TimeOfDayValue containing per-period floats.
 * @returns Interpolated scalar result for the current time/interior/weather state.
 */

/**
 * Compute an interpolated RGB result by applying time-of-day and configured blending to a ColorTimeOfDayValue.
 * @param value ColorTimeOfDayValue containing per-period float3 RGBs.
 * @returns Interpolated float3 RGB result for the current time/interior/weather state.
 */

/**
 * Load a single Setting from an INI-like file source into the provided Setting structure.
 * @param filePath Path to the INI-like file.
 * @param section INI section/name under which the setting is stored.
 * @param key INI key name for the specific setting.
 * @param setting Setting object to populate from file values.
 */

/**
 * Save a single Setting into an INI-like file destination under the given section/key.
 * @param filePath Path to the target INI-like file.
 * @param section INI section/name under which to store the setting.
 * @param key INI key name for the specific setting.
 * @param setting Setting object whose values will be written.
 */
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
	void SetTimeOfDayData(const float timeOfDay1[4], const float timeOfDay2[4], float interiorFactor);

private:
	struct CategorySettings
	{
		std::unordered_map<std::string, uint32_t> settings;  // key -> ID
		std::vector<std::string> settingOrder;
		bool ignoreWeatherSystem = false;
		bool ignoreWeatherSystemInterior = true;
		bool lastSavedIgnoreWeatherSystem = false;
		bool lastSavedIgnoreWeatherSystemInterior = true;
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
	float interiorFactor = 0.0f;

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