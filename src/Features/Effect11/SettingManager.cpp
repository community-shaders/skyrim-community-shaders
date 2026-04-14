#include "SettingManager.h"

#include "WeatherManager.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <tuple>

static const char* const timeOfDayNames[] = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

/**
 * @brief Parses a boolean value from a string accepting "true"/"1" and "false"/"0" (whitespace-insensitive, case-insensitive).
 *
 * Trims leading/trailing whitespace and compares the lowercased string to the accepted literal forms.
 *
 * @param a_value Input string to parse.
 * @param[out] a_out Parsed boolean when parsing succeeds.
 * @return true if the input matches an accepted boolean representation, false otherwise.
 */
static bool TryParseBool(const std::string& a_value, bool& a_out)
{
	std::string s = a_value;
	s.erase(0, s.find_first_not_of(" \t\r\n"));
	s.erase(s.find_last_not_of(" \t\r\n") + 1);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	if (s == "true" || s == "1") {
		a_out = true;
		return true;
	}
	if (s == "false" || s == "0") {
		a_out = false;
		return true;
	}
	return false;
}

/**
 * @brief Parses a floating-point value from a string when the string contains a valid float optionally followed only by whitespace.
 *
 * Attempts to convert the entire numeric prefix of `a_value` to a `float` and succeeds only if any remaining characters are whitespace. On success, writes the parsed value to `a_out`.
 *
 * @param a_value Input string to parse.
 * @param[out] a_out Receives the parsed float when the function returns `true`.
 * @return `true` if `a_value` contains a valid float with only trailing whitespace, `false` otherwise.
 */
static bool TryParseFloat(const std::string& a_value, float& a_out)
{
	if (a_value.empty())
		return false;
	try {
		size_t pos;
		a_out = std::stof(a_value, &pos);
		for (size_t i = pos; i < a_value.size(); ++i) {
			if (!std::isspace(static_cast<unsigned char>(a_value[i]))) {
				return false;
			}
		}
		return true;
	} catch (...) {
		return false;
	}
}

/**
 * @brief Parses a weather key of the form "weather_<id>" and extracts the numeric ID.
 *
 * @param a_key Input key string expected to start with "weather_" followed by a decimal number.
 * @param a_out Output parameter set to the parsed unsigned integer ID when parsing succeeds.
 * @return true if `a_key` begins with "weather_" and the entire suffix is a valid unsigned integer; `false` otherwise.
 */
static bool TryParseWeatherID(const std::string& a_key, uint32_t& a_out)
{
	const std::string prefix = "weather_";
	if (a_key.size() <= prefix.size() || a_key.compare(0, prefix.size(), prefix) != 0) {
		return false;
	}

	try {
		std::string idStr = a_key.substr(prefix.size());
		size_t pos;
		a_out = std::stoul(idStr, &pos);
		return pos == idStr.size();
	} catch (...) {
		return false;
	}
}

/**
 * @brief Accesses the global SettingManager singleton.
 *
 * @return SettingManager& Reference to the single SettingManager instance.
 */
SettingManager& SettingManager::GetSingleton()
{
	static SettingManager instance;
	return instance;
}

/**
 * @brief Registers or updates a setting and assigns a stable numeric ID.
 *
 * Ensures the setting's category exists (adding it to categoryOrder if new),
 * then either appends a new Setting to allSettings (assigning setting.id and
 * initializing lastSavedValue) or replaces the existing Setting at the same
 * numeric ID while preserving that ID and the previously stored lastSavedValue.
 *
 * @param setting Reference to the Setting to register or update; on return
 *                `setting.id` and `setting.lastSavedValue` are set appropriately
 *                and the global registration structures (`categories`,
 *                `categoryOrder`, `allSettings`) are modified.
 *
 * @note Caller must hold the manager's unique_lock when calling this method.
 */
void SettingManager::RegisterSettingInternal(Setting& setting)
{
	// Already holding unique_lock from public Register... methods
	if (categories.find(setting.category) == categories.end()) {
		categoryOrder.push_back(setting.category);
	}

	auto& cat = categories[setting.category];
	auto it = cat.settings.find(setting.key);

	if (it == cat.settings.end()) {
		setting.id = static_cast<uint32_t>(allSettings.size());
		setting.lastSavedValue = setting.currentValue;
		cat.settings[setting.key] = setting.id;
		cat.settingOrder.push_back(setting.key);
		allSettings.push_back(setting);
	} else {
		// Update existing setting info but keep the same ID
		uint32_t existingID = it->second;
		setting.id = existingID;
		setting.lastSavedValue = allSettings[existingID].lastSavedValue;
		allSettings[existingID] = setting;
	}
}

/**
 * @brief Register a boolean configuration setting and initialize its default and current values.
 *
 * Creates a new setting entry identified by `category` and `key`, sets its type to boolean,
 * initializes both the stored default and current values to `defaultValue`, and records
 * whether the setting supports per-weather overrides.
 *
 * @param key The setting's key name within the section/category.
 * @param category The section or category under which the setting is registered.
 * @param defaultValue The value used to initialize both the setting's default and current value.
 * @param hasWeatherSupport If `true`, the setting will support per-weather overrides and blending.
 */
void SettingManager::RegisterBoolSetting(const std::string& key, const std::string& category,
	bool defaultValue, bool hasWeatherSupport)
{
	std::unique_lock lock(mutex);
	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::Bool;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = defaultValue;
	setting.currentValue = defaultValue;

	RegisterSettingInternal(setting);
}

/**
 * @brief Registers a floating-point setting with bounds, step, and optional weather support.
 *
 * Creates or updates a setting identified by `category` and `key`, initializing its default
 * and current values to `defaultValue` and storing `minValue`, `maxValue`, `step`, and whether
 * the setting participates in weather-based overrides.
 *
 * @param key Setting key name within the section.
 * @param category INI section or logical category for the setting.
 * @param defaultValue Default value assigned to the setting when created.
 * @param minValue Minimum allowed value for the setting; loaded values are clamped to this.
 * @param maxValue Maximum allowed value for the setting; loaded values are clamped to this.
 * @param step Increment step used for UI adjustments or snapping.
 * @param hasWeatherSupport If `true`, the setting supports per-weather overrides and blending.
 */
void SettingManager::RegisterFloatSetting(const std::string& key, const std::string& category,
	float defaultValue, float minValue, float maxValue, float step, bool hasWeatherSupport)
{
	std::unique_lock lock(mutex);
	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::Float;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = defaultValue;
	setting.currentValue = defaultValue;
	setting.minValue = minValue;
	setting.maxValue = maxValue;
	setting.step = step;

	RegisterSettingInternal(setting);
}

/**
 * @brief Registers a time-of-day setting whose eight time slots are initialized to the same value.
 *
 * Creates a Setting of type TimeOfDay with every element of the TimeOfDayValue initialized to
 * defaultValue, stores the provided bounds and step, and registers it so it receives a stable
 * numeric ID and is tracked for persistence and (optionally) weather-specific overrides.
 *
 * @param key INI key or identifier for the setting.
 * @param category Category or section name under which the setting is grouped.
 * @param defaultValue Value used to initialize every time-of-day slot.
 * @param minValue Minimum allowed value for each time-of-day element.
 * @param maxValue Maximum allowed value for each time-of-day element.
 * @param step Adjustment increment for the setting's value.
 * @param hasWeatherSupport Enable per-weather overrides and blending for this setting when true.
 */
void SettingManager::RegisterTimeOfDaySetting(const std::string& key, const std::string& category,
	float defaultValue, float minValue, float maxValue, float step, bool hasWeatherSupport)
{
	std::unique_lock lock(mutex);
	TimeOfDayValue timeOfDayDefault;
	for (int i = 0; i < TimeOfDayValue::Total; ++i) {
		timeOfDayDefault.values[i] = defaultValue;
	}

	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::TimeOfDay;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = timeOfDayDefault;
	setting.currentValue = timeOfDayDefault;
	setting.minValue = minValue;
	setting.maxValue = maxValue;
	setting.step = step;

	RegisterSettingInternal(setting);
}

/**
 * @brief Registers a color time-of-day setting and initializes its default and current values.
 *
 * Creates a setting of type ColorTimeOfDay identified by the given key and category, sets
 * every time-slot element to the provided default color, and configures whether the setting
 * participates in weather-based overrides.
 *
 * @param key Setting key name.
 * @param category Setting category/INI section.
 * @param defaultValue Color value used to initialize every time-of-day slot.
 * @param hasWeatherSupport If `true`, the setting can be overridden per-weather; otherwise weather is ignored.
 */
void SettingManager::RegisterColorTimeOfDaySetting(const std::string& key, const std::string& category,
	float3 defaultValue, bool hasWeatherSupport)
{
	std::unique_lock lock(mutex);
	ColorTimeOfDayValue colorTimeOfDayDefault;
	for (int i = 0; i < ColorTimeOfDayValue::Total; ++i) {
		colorTimeOfDayDefault.values[i] = defaultValue;
	}

	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::ColorTimeOfDay;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = colorTimeOfDayDefault;
	setting.currentValue = colorTimeOfDayDefault;

	RegisterSettingInternal(setting);
}

template <typename T>
/**
 * @brief Retrieve the stored value for a named setting, optionally bypassing weather blending.
 *
 * @tparam T The expected value type; must match the registered setting's stored type.
 * @param rawValue If true, return the selected weather value without interpolating between weather entries; if false, return the blended/interpolated value when weather support applies.
 * @return T Default-constructed `T` if the setting identified by `key` and `category` is not found; otherwise the setting's current value (possibly weather-blended).
 */
T SettingManager::GetValue(const std::string& key, const std::string& category, bool rawValue)
{
	std::shared_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id == 0xFFFFFFFF) {
		return T{};
	}
	return GetValueInternal<T>(id, rawValue);
}

template <typename T>
/**
 * @brief Retrieve a setting value by numeric ID, optionally selecting the raw (uninterpolated) weather value.
 *
 * @tparam T The expected value type (e.g., `bool`, `float`, `TimeOfDayValue`, `ColorTimeOfDayValue`).
 * @param id Numeric identifier of the setting.
 * @param rawValue If true, return the nearer stored weather entry's value instead of interpolating between weather entries.
 * @return T The setting's value; returns a default-constructed `T` if the `id` is not found.
 */
T SettingManager::GetValue(uint32_t id, bool rawValue)
{
	std::shared_lock lock(mutex);
	return GetValueInternal<T>(id, rawValue);
}

template <typename T>
/**
 * @brief Retrieve the resolved value for a setting ID, applying weather overrides and blending when applicable.
 *
 * When the setting supports weather and weather data exists, this returns the weather-specific value:
 * - If the category's ignore-weather flag applies (depends on interiorFactor), returns the stored current value.
 * - If rawValue is true, returns either the current or last weather value based on weatherBlendFactor.
 * - Otherwise returns the interpolated value between last and current weather entries using weatherBlendFactor.
 *
 * @tparam T The expected setting value type (bool, float, TimeOfDayValue, or ColorTimeOfDayValue).
 * @param id Numeric setting ID to retrieve.
 * @param rawValue If true, select the nearer weather value based on weatherBlendFactor instead of interpolating.
 * @return T The resolved setting value; returns a default-constructed `T` if `id` is out of range.
 */
T SettingManager::GetValueInternal(uint32_t id, bool rawValue) const
{
	if (id >= allSettings.size()) {
		return T{};
	}

	const auto& setting = allSettings[id];
	const auto& categorySettings = categories.at(setting.category);

	if (setting.hasWeatherSupport) {
		bool shouldIgnoreWeather = (interiorFactor > 0.5f) ?
		                               categorySettings.ignoreWeatherSystemInterior :
		                               categorySettings.ignoreWeatherSystem;

		if (shouldIgnoreWeather) {
			return std::get<T>(setting.currentValue);
		}

		auto currentIt = weatherData.find(currentWeatherID);
		auto lastIt = weatherData.find(lastWeatherID);

		if (currentIt != weatherData.end() || lastIt != weatherData.end()) {
			SettingValue currentValue = setting.currentValue;
			SettingValue lastValue = setting.currentValue;

			if (currentIt != weatherData.end() && id < currentIt->second.size()) {
				currentValue = currentIt->second[id];
			}

			if (lastIt != weatherData.end() && id < lastIt->second.size()) {
				lastValue = lastIt->second[id];
			}

			if (rawValue) {
				return std::get<T>(weatherBlendFactor > 0.5f ? currentValue : lastValue);
			}

			SettingValue blendedValue = InterpolateValues(lastValue, currentValue, weatherBlendFactor);
			return std::get<T>(blendedValue);
		}
	}

	return std::get<T>(setting.currentValue);
}

template <typename T>
/**
 * @brief Set a registered setting identified by key and category to the provided value.
 *
 * If the setting exists, updates its stored value and propagates the change to weather-specific
 * entries when applicable. If the setting is not found, the call is a no-op.
 *
 * @tparam T Type of the setting value.
 * @param key Setting key within the category.
 * @param category Setting category.
 * @param value New value to store for the setting.
 */
void SettingManager::SetValue(const std::string& key, const std::string& category, const T& value)
{
	std::unique_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id != 0xFFFFFFFF) {
		SetValueInternal<T>(id, value);
	}
}

template <typename T>
/**
 * @brief Set a setting's value by its numeric identifier.
 *
 * Updates the stored value for the setting with the given id. If the setting has weather
 * support, the provided value will be propagated into the appropriate weather data arrays
 * according to the current weather blend state; otherwise the setting's current value is
 * updated directly.
 *
 * @tparam T Type of the setting value (e.g., `bool`, `float`, `TimeOfDayValue`, `ColorTimeOfDayValue`).
 * @param id Numeric identifier of the setting (as returned by GetSettingID / registration).
 * @param value New value to store for the setting.
 */
void SettingManager::SetValue(uint32_t id, const T& value)
{
	std::unique_lock lock(mutex);
	SetValueInternal<T>(id, value);
}

template <typename T>
/**
 * @brief Sets a setting's current value and, when applicable, propagates the change into weather-specific data.
 *
 * If `id` is out of range this is a no-op. For settings with weather support, the function either updates the
 * base current value (when the category's ignore-weather flag applies) or writes `value` into the weather array
 * for the chosen target weather ID. The target weather ID is selected from current/last based on `weatherBlendFactor`.
 * When writing weather data, all weather IDs that share the same weather file are also updated. Weather arrays are
 * resized and initialized from the stored current values when necessary before writing.
 *
 * @param id Numeric setting identifier previously assigned by the manager.
 * @param value New value to assign to the setting (or to write into weather data).
 */
void SettingManager::SetValueInternal(uint32_t id, const T& value)
{
	if (id >= allSettings.size()) {
		return;
	}

	auto& setting = allSettings[id];
	const auto& categorySettings = categories.at(setting.category);

	if (setting.hasWeatherSupport) {
		bool shouldIgnoreWeather = (interiorFactor > 0.5f) ?
		                               categorySettings.ignoreWeatherSystemInterior :
		                               categorySettings.ignoreWeatherSystem;

		if (shouldIgnoreWeather) {
			setting.currentValue = value;
			return;
		}

		uint32_t targetWeatherID = (weatherBlendFactor > 0.5f) ? currentWeatherID : lastWeatherID;

		// Update all weather IDs sharing the same file
		auto& weatherManager = WeatherManager::GetSingleton();
		auto* entry = weatherManager.FindWeatherEntry(targetWeatherID);

		if (entry) {
			for (uint32_t linkedID : entry->weatherIDs) {
				auto& data = weatherData[linkedID];
				if (data.size() < allSettings.size()) {
					data.resize(allSettings.size());
					// Initialize with current values from allSettings
					for (size_t i = 0; i < allSettings.size(); ++i) {
						data[i] = allSettings[i].currentValue;
					}
				}
				data[id] = value;
			}
		} else {
			// Fallback: update target ID only
			auto& data = weatherData[targetWeatherID];
			if (data.size() < allSettings.size()) {
				data.resize(allSettings.size());
				for (size_t i = 0; i < allSettings.size(); ++i) {
					data[i] = allSettings[i].currentValue;
				}
			}
			data[id] = value;
		}
		return;
	}

	setting.currentValue = value;
}

/**
 * @brief Retrieves the numeric ID for a setting identified by key and category.
 *
 * @return uint32_t The setting's numeric ID, or `0xFFFFFFFF` if the setting does not exist.
 */
uint32_t SettingManager::GetSettingID(const std::string& key, const std::string& category) const
{
	std::shared_lock lock(mutex);
	return GetSettingIDInternal(key, category);
}

/**
 * Retrieve the numeric ID for a setting given its key and category.
 *
 * @param key Setting key name.
 * @param category Category name containing the setting.
 * @return uint32_t The setting's numeric ID, or 0xFFFFFFFF if the setting or category does not exist.
 */
uint32_t SettingManager::GetSettingIDInternal(const std::string& key, const std::string& category) const
{
	auto catIt = categories.find(category);
	if (catIt != categories.end()) {
		auto setIt = catIt->second.settings.find(key);
		if (setIt != catIt->second.settings.end()) {
			return setIt->second;
		}
	}
	return 0xFFFFFFFF;
}

/**
 * @brief Computes the interpolated time-of-day value for a registered setting.
 *
 * Retrieves the time-of-day setting identified by key and category and computes its
 * interpolated scalar using the manager's current time-of-day and interior/exterior factors.
 *
 * @return float Interpolated time-of-day value for the setting, or 0.0f if the setting is not found.
 */
float SettingManager::GetInterpolatedTimeOfDayValue(const std::string& key, const std::string& category)
{
	std::shared_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id == 0xFFFFFFFF)
		return 0.0f;
	TimeOfDayValue timeOfDayValue = GetValueInternal<TimeOfDayValue>(id);
	return ComputeTimeOfDayInterpolation(timeOfDayValue);
}

/**
 * @brief Computes the color for a time-of-day setting by applying the manager's interpolation state.
 *
 * @param key The setting key name.
 * @param category The setting category / INI section name.
 * @return float3 The interpolated RGB color for the current time-of-day and interior/exterior factors; returns {0,0,0} if the setting is not found.
 */
float3 SettingManager::GetInterpolatedColorTimeOfDayValue(const std::string& key, const std::string& category)
{
	std::shared_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id == 0xFFFFFFFF)
		return {};
	ColorTimeOfDayValue colorTimeOfDayValue = GetValueInternal<ColorTimeOfDayValue>(id);
	return ComputeColorTimeOfDayInterpolation(colorTimeOfDayValue);
}

/**
 * @brief Check whether a setting exists in the specified category.
 *
 * @param key Setting key within the category.
 * @param category Category name to search.
 * @return `true` if a setting with `key` exists in `category`, `false` otherwise.
 */
bool SettingManager::HasSetting(const std::string& key, const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() && categoryIt->second.settings.find(key) != categoryIt->second.settings.end();
}

/**
 * @brief Retrieve metadata for a registered setting by key and category.
 *
 * @param key The setting's key name within its category.
 * @param category The category name that groups the setting.
 * @return const Setting* Pointer to the Setting when found, `nullptr` otherwise.
 */
const Setting* SettingManager::GetSettingInfo(const std::string& key, const std::string& category) const
{
	std::shared_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id < allSettings.size())
		return &allSettings[id];
	return nullptr;
}

/**
 * @brief Retrieve metadata for a setting by its numeric ID.
 *
 * Looks up the setting registered at the given stable numeric ID and returns a pointer to its stored
 * Setting record, or `nullptr` if the ID is not valid.
 *
 * @param id Numeric setting ID assigned during registration.
 * @return const Setting* Pointer to the Setting for `id`, or `nullptr` if `id` is out of range.
 */
const Setting* SettingManager::GetSettingInfo(uint32_t id) const
{
	std::shared_lock lock(mutex);
	if (id < allSettings.size()) {
		return &allSettings[id];
	}
	return nullptr;
}

/**
 * @brief Retrieves the ordered list of setting keys for the given category.
 *
 * @param category Name of the category to query.
 * @return std::vector<std::string> Vector of setting keys in the category's insertion order, or an empty vector if the category does not exist.
 */
std::vector<std::string> SettingManager::GetSettingsByCategory(const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	if (categoryIt != categories.end()) {
		return categoryIt->second.settingOrder;
	}
	return {};
}

/**
 * @brief Returns the list of registered setting categories in registration order.
 *
 * This call is safe to use concurrently; it returns a copy of the internal category order.
 *
 * @return std::vector<std::string> Category names in the order they were registered.
 */
std::vector<std::string> SettingManager::GetAllCategories() const
{
	std::shared_lock lock(mutex);
	return categoryOrder;
}

/**
 * @brief Checks whether any setting in the given category supports per-weather overrides.
 *
 * @param category Name of the category to check.
 * @return true if the category contains at least one setting with weather support, false otherwise.
 */
bool SettingManager::CategoryHasWeatherSupport(const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	if (categoryIt == categories.end())
		return false;

	for (const auto& [key, id] : categoryIt->second.settings) {
		if (allSettings[id].hasWeatherSupport) {
			return true;
		}
	}
	return false;
}

/**
 * @brief Update the active weather identifiers and the blend factor used for weather-based value blending.
 *
 * Sets the current and last weather IDs and the blend factor that determines how values are selected or interpolated
 * between those weather states when retrieving or writing weather-supported settings.
 *
 * @param newCurrentWeatherID Weather ID considered the "current" weather state.
 * @param newLastWeatherID    Weather ID considered the "last" (previous) weather state.
 * @param blendFactor         Blend weight in [0,1] where values closer to 1 favor `newCurrentWeatherID` and closer to 0 favor `newLastWeatherID`.
 */
void SettingManager::SetWeatherBlendFactors(uint32_t newCurrentWeatherID, uint32_t newLastWeatherID, float blendFactor)
{
	std::unique_lock lock(mutex);
	currentWeatherID = newCurrentWeatherID;
	lastWeatherID = newLastWeatherID;
	weatherBlendFactor = blendFactor;
}

/**
 * @brief Loads weather-specific setting values from an INI file and stores them for the given weather IDs.
 *
 * Reads the file at filePath (if it exists) and, for each registered setting that supports weather,
 * loads its value from the file. File I/O is performed without holding the manager mutex. If the
 * file's last-write time matches a previously recorded time for the same path, the function returns
 * early to avoid redundant work. After loading, the function records the file's write time and stores
 * the loaded values into internal weather data maps for each weather ID in weatherIDs.
 *
 * @param weatherIDs Vector of weather IDs to populate with the loaded values; each ID will be mapped
 *                   to the same loaded values array.
 * @param filePath   Path to the INI file to read; if the file does not exist, the function logs a
 *                   warning and returns without modifying internal state.
 *
 * Side effects:
 * - Updates weatherFileWriteTimes[filePath] to the file's last-write time.
 * - Sets weatherData[weatherID] and lastSavedWeatherData[weatherID] to the loaded values for each
 *   provided weatherID.
 */
void SettingManager::LoadWeatherSettings(const std::vector<uint32_t>& weatherIDs, const std::string& filePath)
{
	if (!std::filesystem::exists(filePath)) {
		logger::warn("[SettingManager] Weather file not found: {}", filePath);
		return;
	}

	if (weatherIDs.empty()) {
		return;
	}

	auto writeTime = std::filesystem::last_write_time(filePath);

	// Snapshot allSettings and check for changes under a short shared lock
	std::vector<Setting> settingsCopy;
	{
		std::shared_lock lock(mutex);
		auto it = weatherFileWriteTimes.find(filePath);
		if (it != weatherFileWriteTimes.end() && it->second == writeTime) {
			return;  // File unchanged since last load
		}
		settingsCopy = allSettings;
	}

	// Do all file I/O without holding any lock
	std::vector<SettingValue> loadedValues(settingsCopy.size());
	for (size_t i = 0; i < settingsCopy.size(); ++i) {
		loadedValues[i] = settingsCopy[i].currentValue;
	}
	for (auto& setting : settingsCopy) {
		if (setting.hasWeatherSupport) {
			LoadSettingFromFile(filePath, setting.category, setting.key, setting);
			loadedValues[setting.id] = setting.currentValue;
		}
	}

	// Store loaded values for all provided weather IDs under write lock
	{
		std::unique_lock lock(mutex);
		weatherFileWriteTimes[filePath] = writeTime;
		for (uint32_t weatherID : weatherIDs) {
			weatherData[weatherID] = loadedValues;
			lastSavedWeatherData[weatherID] = loadedValues;
		}
	}
}

/**
 * @brief Saves weather-specific settings for a given weather key into an INI file.
 *
 * Parses `weatherKey` as `"weather_<id>"`, compares the current per-setting weather values
 * to the last-saved snapshot for that weather ID, and writes only settings whose weather
 * value changed to the specified INI `filePath`. Updates the internal last-saved snapshot
 * for the weather ID and flushes the Windows INI cache after writing.
 *
 * @param weatherKey Weather key string in the form `"weather_<id>"`. If parsing fails the function returns without writing.
 * @param filePath Path to the INI file to write changed weather settings to.
 */
void SettingManager::SaveWeatherSettings(const std::string& weatherKey, const std::string& filePath)
{
	uint32_t weatherID;
	if (!TryParseWeatherID(weatherKey, weatherID)) {
		logger::error("[SettingManager] Invalid weather key: {}", weatherKey);
		return;
	}

	std::vector<std::tuple<std::string, std::string, Setting>> settingsToWrite;

	{
		std::unique_lock lock(mutex);
		auto weatherIt = weatherData.find(weatherID);
		if (weatherIt == weatherData.end()) {
			return;
		}

		auto lastIt = lastSavedWeatherData.find(weatherID);
		const auto& weatherValues = weatherIt->second;

		for (const auto& setting : allSettings) {
			if (setting.hasWeatherSupport && setting.id < weatherValues.size()) {
				bool changed = true;
				if (lastIt != lastSavedWeatherData.end() && setting.id < lastIt->second.size()) {
					if (weatherValues[setting.id] == lastIt->second[setting.id]) {
						changed = false;
					}
				}

				if (changed) {
					Setting tempSetting = setting;
					tempSetting.currentValue = weatherValues[setting.id];
					settingsToWrite.emplace_back(setting.category, setting.key, tempSetting);
				}
			}
		}

		// Update last saved state
		lastSavedWeatherData[weatherID] = weatherValues;
	}

	if (settingsToWrite.empty()) {
		return;
	}

	// Perform IO outside of lock to prevent deadlocks
	for (const auto& [category, key, setting] : settingsToWrite) {
		SaveSettingToFile(filePath, category, key, setting);
	}

	// Flush Windows .ini cache to disk
	WritePrivateProfileStringA(NULL, NULL, NULL, filePath.c_str());
}

/**
 * @brief Saves weather-specific settings for all known weather files.
 *
 * Queries the WeatherManager for available (weatherKey, filePath) pairs, deduplicates entries by file path (keeping the first weatherKey encountered for each path), and invokes SaveWeatherSettings for each unique file path to persist its weather settings.
 */
void SettingManager::SaveAllWeatherSettings()
{
	auto& weatherManager = WeatherManager::GetSingleton();
	const auto& weatherFiles = weatherManager.GetWeatherFiles();

	// Deduplicate by file path to avoid redundant IO and overwrite bugs
	std::unordered_map<std::string, std::string> uniqueFiles;
	for (const auto& [weatherKey, filePath] : weatherFiles) {
		if (uniqueFiles.find(filePath) == uniqueFiles.end()) {
			uniqueFiles[filePath] = weatherKey;
		}
	}

	for (const auto& [filePath, weatherKey] : uniqueFiles) {
		SaveWeatherSettings(weatherKey, filePath);
	}
}

/**
 * @brief Clears cached weather data and forces a reload of all weather files.
 *
 * Acquires the manager mutex to remove in-memory per-weather value arrays and
 * stored weather-file write timestamps, then triggers reinitialization of the
 * weather subsystem so weather files will be re-read and repopulated.
 */
void SettingManager::ReloadAllWeatherSettings()
{
	{
		std::unique_lock lock(mutex);
		weatherData.clear();
		weatherFileWriteTimes.clear();  // Force re-read of all weather files
	}
	auto& weatherManager = WeatherManager::GetSingleton();
	weatherManager.Initialize();
}

/**
 * @brief Update the inputs used for time-of-day interpolation.
 *
 * Replaces the stored time-of-day weight arrays and the interior blending factor
 * that are later used by ComputeTimeOfDayInterpolation and
 * ComputeColorTimeOfDayInterpolation.
 *
 * @param newTimeOfDay1 Four-element array of floats representing the primary
 *        time-of-day weight set (used for Dawn, Sunrise, Day, Sunset and for
 *        interior weighting).
 * @param newTimeOfDay2 Four-element array of floats representing the secondary
 *        time-of-day weight set (used for Dusk, Night and ancillary exterior
 *        blending).
 * @param newInteriorFactor Blend factor in [0,1] that controls interior vs
 *        exterior interpolation behavior (interior path is used when this
 *        value is greater than 0.5).
 */
void SettingManager::SetTimeOfDayData(const float newTimeOfDay1[4], const float newTimeOfDay2[4], float newInteriorFactor)
{
	std::unique_lock lock(mutex);
	memcpy(timeOfDay1, newTimeOfDay1, sizeof(timeOfDay1));
	memcpy(timeOfDay2, newTimeOfDay2, sizeof(timeOfDay2));
	interiorFactor = newInteriorFactor;
}

/**
 * @brief Loads main settings and per-category weather-ignore flags from an INI file.
 *
 * Reads settings from the given INI file and updates the manager's stored settings only when the file has changed.
 * For each registered setting the function reads its persisted value and updates the setting's last-saved value.
 * For categories containing weather-supported settings it also reads the `IgnoreWeatherSystem` and
 * `IgnoreWeatherSystemInterior` flags and applies them atomically with the settings update.
 * If the file does not exist or its last-write time matches the last loaded time, no changes are made.
 *
 * @param filePath Path to the INI file to load (may be relative; resolved to an absolute path).
 */
void SettingManager::LoadFromFile(const std::string& filePath)
{
	std::filesystem::path absPath = std::filesystem::absolute(filePath);

	if (!std::filesystem::exists(absPath)) {
		logger::warn("[SettingManager] Settings file not found: {}, using defaults", absPath.string());
		return;
	}

	auto writeTime = std::filesystem::last_write_time(absPath);

	// Snapshot settings and identify weather categories under brief shared lock
	std::vector<Setting> settingsCopy;
	std::vector<std::string> weatherCategories;
	{
		std::shared_lock lock(mutex);
		if (writeTime == lastMainIniWriteTime) {
			return;  // File unchanged since last load
		}
		settingsCopy = allSettings;
		for (const auto& [catName, catData] : categories) {
			for (const auto& [key, settingID] : catData.settings) {
				if (allSettings[settingID].hasWeatherSupport) {
					weatherCategories.push_back(catName);
					break;
				}
			}
		}
	}

	// Do all file I/O without holding any lock
	std::string absPathStr = absPath.string();
	for (auto& setting : settingsCopy) {
		LoadSettingFromFile(absPathStr, setting.category, setting.key, setting);
		setting.lastSavedValue = setting.currentValue;
	}

	// Load weather ignore settings (I/O without lock)
	struct WeatherIgnoreData
	{
		std::string category;
		bool ignoreWeatherSystem = false;
		bool ignoreWeatherSystemInterior = true;
	};
	std::vector<WeatherIgnoreData> weatherIgnoreResults;
	for (const auto& category : weatherCategories) {
		WeatherIgnoreData data;
		data.category = category;

		char buffer[256];
		GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem", "false", buffer, sizeof(buffer), absPathStr.c_str());
		bool parsed;
		if (TryParseBool(buffer, parsed)) {
			data.ignoreWeatherSystem = parsed;
		}

		GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior", "true", buffer, sizeof(buffer), absPathStr.c_str());
		if (TryParseBool(buffer, parsed)) {
			data.ignoreWeatherSystemInterior = parsed;
		}

		weatherIgnoreResults.push_back(std::move(data));
	}

	// Store results under unique lock
	{
		std::unique_lock lock(mutex);
		lastMainIniWriteTime = writeTime;
		allSettings = std::move(settingsCopy);

		// Apply weather ignore settings
		for (const auto& data : weatherIgnoreResults) {
			auto catIt = categories.find(data.category);
			if (catIt != categories.end()) {
				catIt->second.ignoreWeatherSystem = data.ignoreWeatherSystem;
				catIt->second.ignoreWeatherSystemInterior = data.ignoreWeatherSystemInterior;
			}
		}

		// Sync lastSaved state for all categories
		for (auto& [catName, catData] : categories) {
			catData.lastSavedIgnoreWeatherSystem = catData.ignoreWeatherSystem;
			catData.lastSavedIgnoreWeatherSystemInterior = catData.ignoreWeatherSystemInterior;
		}

		lastSavedWeatherData = weatherData;
	}
}

/**
 * @brief Writes changed settings and updated weather-ignore flags to an INI file.
 *
 * Collects settings whose current value differs from their last-saved value and categories
 * whose weather-ignore flags have changed, updates the in-memory "last saved" markers,
 * and writes those changes to the given INI file. If there are no changes to persist,
 * the function returns without modifying the file. The INI cache is flushed after writing.
 *
 * @param filePath Path to the INI file to write.
 */
void SettingManager::SaveToFile(const std::string& filePath)
{
	std::vector<std::tuple<std::string, std::string, Setting>> settingsToWrite;
	std::vector<std::tuple<std::string, bool, bool>> weatherSupportFlags;

	{
		std::unique_lock lock(mutex);
		for (auto& setting : allSettings) {
			if (!(setting.currentValue == setting.lastSavedValue)) {
				settingsToWrite.emplace_back(setting.category, setting.key, setting);
				setting.lastSavedValue = setting.currentValue;
			}
		}

		for (const auto& categoryName : categoryOrder) {
			auto& categoryData = categories.at(categoryName);
			bool hasWeatherSupport = false;
			for (const auto& [key, settingID] : categoryData.settings) {
				if (allSettings[settingID].hasWeatherSupport) {
					hasWeatherSupport = true;
					break;
				}
			}

			if (hasWeatherSupport) {
				if (categoryData.ignoreWeatherSystem != categoryData.lastSavedIgnoreWeatherSystem ||
					categoryData.ignoreWeatherSystemInterior != categoryData.lastSavedIgnoreWeatherSystemInterior) {
					weatherSupportFlags.emplace_back(categoryName, categoryData.ignoreWeatherSystem, categoryData.ignoreWeatherSystemInterior);
					categoryData.lastSavedIgnoreWeatherSystem = categoryData.ignoreWeatherSystem;
					categoryData.lastSavedIgnoreWeatherSystemInterior = categoryData.ignoreWeatherSystemInterior;
				}
			}
		}
	}

	if (settingsToWrite.empty() && weatherSupportFlags.empty()) {
		return;
	}

	// Perform IO outside of lock
	for (const auto& [category, key, setting] : settingsToWrite) {
		SaveSettingToFile(filePath, category, key, setting);
	}

	for (const auto& [category, ignoreOut, ignoreIn] : weatherSupportFlags) {
		WritePrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem",
			ignoreOut ? "true" : "false", filePath.c_str());
		WritePrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior",
			ignoreIn ? "true" : "false", filePath.c_str());
	}

	// Flush cache
	WritePrivateProfileStringA(NULL, NULL, NULL, filePath.c_str());
}

/**
 * @brief Interpolate between two setting values according to a blend factor.
 *
 * If the two values hold different underlying types, the function selects
 * `a` when `t <= 0.5` or `b` when `t > 0.5`. For matching types the behavior is:
 * - `bool`: selects `a` when `t <= 0.5` or `b` when `t > 0.5`.
 * - `float`: linear interpolation `a + t*(b - a)`.
 * - `TimeOfDayValue`: element-wise linear interpolation across the 8 time-of-day entries.
 * - `ColorTimeOfDayValue`: element-wise linear interpolation across the 8 color entries.
 *
 * @param a The first value (returned when t <= 0.5 for type-mismatch or thresholded types).
 * @param b The second value (returned when t > 0.5 for type-mismatch or thresholded types).
 * @param t Blend factor in [0,1], where 0 yields `a` and 1 yields `b` for interpolatable types.
 * @return SettingValue The interpolated or selected setting value as described above.
 */
SettingValue SettingManager::InterpolateValues(const SettingValue& a, const SettingValue& b, float t) const
{
	if (a.index() != b.index()) {
		return t > 0.5f ? b : a;
	}

	return std::visit([&](auto&& valA) -> SettingValue {
		using T = std::decay_t<decltype(valA)>;
		const T& valB = std::get<T>(b);

		if constexpr (std::is_same_v<T, bool>) {
			return t > 0.5f ? valB : valA;
		} else if constexpr (std::is_same_v<T, float>) {
			return valA + t * (valB - valA);
		} else if constexpr (std::is_same_v<T, TimeOfDayValue>) {
			TimeOfDayValue result;
			for (int i = 0; i < 8; ++i) {
				result.values[i] = valA.values[i] + t * (valB.values[i] - valA.values[i]);
			}
			return result;
		} else if constexpr (std::is_same_v<T, ColorTimeOfDayValue>) {
			ColorTimeOfDayValue result;
			for (int i = 0; i < 8; ++i) {
				result.values[i] = valA.values[i] + t * (valB.values[i] - valA.values[i]);
			}
			return result;
		}
		return valB;
	},
		a);
}

/**
 * @brief Computes an interpolated scalar from a time-of-day sample using the manager's current blend state.
 *
 * Chooses between an interior interpolation and an exterior weighted sum based on the current interior factor.
 *
 * @param value TimeOfDayValue containing samples for the eight time-of-day slots (Dawn, Sunrise, Day, Sunset, Dusk, Night, InteriorDay, InteriorNight).
 * @return float Interpolated value: when the interior factor is greater than 0.5, returns a blend between `InteriorNight` and `InteriorDay` using interior weights; otherwise returns a weighted sum of `Dawn`, `Sunrise`, `Day`, `Sunset`, `Dusk`, and `Night` using the exterior time-of-day weights.
 */
float SettingManager::ComputeTimeOfDayInterpolation(const TimeOfDayValue& value) const
{
	if (interiorFactor > 0.5f) {
		float dayNightFactor = (timeOfDay1[2] + timeOfDay1[1] + timeOfDay1[0] * 0.5f + timeOfDay1[3] * 0.5f);
		return value.values[TimeOfDayValue::InteriorNight] + dayNightFactor *
		                                                         (value.values[TimeOfDayValue::InteriorDay] - value.values[TimeOfDayValue::InteriorNight]);
	}

	return timeOfDay1[0] * value.values[TimeOfDayValue::Dawn] +
	       timeOfDay1[1] * value.values[TimeOfDayValue::Sunrise] +
	       timeOfDay1[2] * value.values[TimeOfDayValue::Day] +
	       timeOfDay1[3] * value.values[TimeOfDayValue::Sunset] +
	       timeOfDay2[0] * value.values[TimeOfDayValue::Dusk] +
	       timeOfDay2[1] * value.values[TimeOfDayValue::Night];
}

/**
 * @brief Compute the scene color for the current time-of-day using configured time/interior blend factors.
 *
 * If the interior factor is greater than 0.5, the result is a blend between the `InteriorNight` and
 * `InteriorDay` entries weighted by a derived day/night factor. Otherwise the result is the weighted
 * sum of the exterior time-of-day entries (Dawn, Sunrise, Day, Sunset, Dusk, Night) using the
 * current `timeOfDay1` and `timeOfDay2` weights.
 *
 * @param value ColorTimeOfDayValue containing per-time-of-day color entries (Dawn, Sunrise, Day, Sunset, Dusk, Night, InteriorDay, InteriorNight).
 * @return float3 The interpolated RGB color for the current time-of-day and interior/exterior blend.
 */
float3 SettingManager::ComputeColorTimeOfDayInterpolation(const ColorTimeOfDayValue& value) const
{
	if (interiorFactor > 0.5f) {
		float dayNightFactor = (timeOfDay1[2] + timeOfDay1[1] + timeOfDay1[0] * 0.5f + timeOfDay1[3] * 0.5f);
		float3 interiorNight = value.values[ColorTimeOfDayValue::InteriorNight];
		float3 interiorDay = value.values[ColorTimeOfDayValue::InteriorDay];
		return interiorNight + dayNightFactor * (interiorDay - interiorNight);
	}

	return timeOfDay1[0] * value.values[ColorTimeOfDayValue::Dawn] +
	       timeOfDay1[1] * value.values[ColorTimeOfDayValue::Sunrise] +
	       timeOfDay1[2] * value.values[ColorTimeOfDayValue::Day] +
	       timeOfDay1[3] * value.values[ColorTimeOfDayValue::Sunset] +
	       timeOfDay2[0] * value.values[ColorTimeOfDayValue::Dusk] +
	       timeOfDay2[1] * value.values[ColorTimeOfDayValue::Night];
}

/**
 * @brief Load a setting's value(s) from an INI file and update the provided Setting.
 *
 * Reads the specified section/key(s) from filePath and updates setting.currentValue according to setting.type:
 * - Bool: accepts `"true"`/`"1"` or `"false"`/`"0"`, falling back to the setting's default on parse failure.
 * - Float: parses a float and clamps it to [minValue, maxValue], falling back to the default on parse failure.
 * - TimeOfDay: reads eight keys formed by appending the time-of-day suffixes to the base key, parses each as a float, clamps to [minValue, maxValue], and updates the corresponding elements; elements with invalid values keep their defaults.
 * - ColorTimeOfDay: reads eight keys formed by appending the time-of-day suffixes to the base key, parses comma-separated `x, y, z` floats, clamps components to [minValue, maxValue], and updates an element only when exactly three components parse successfully.
 *
 * @param filePath Path to the INI file to read.
 * @param section INI section (category) containing the key(s).
 * @param key Base key name; time-of-day variants are formed by appending known suffixes.
 * @param setting Setting object whose currentValue will be updated in-place.
 */
void SettingManager::LoadSettingFromFile(const std::string& filePath, const std::string& section, const std::string& key, Setting& setting)
{
	switch (setting.type) {
	case SettingType::Bool:
		{
			bool defaultVal = std::get<bool>(setting.defaultValue);
			char buffer[256];
			GetPrivateProfileStringA(section.c_str(), key.c_str(), defaultVal ? "true" : "false", buffer, sizeof(buffer), filePath.c_str());
			bool parsed;
			if (TryParseBool(buffer, parsed)) {
				setting.currentValue = parsed;
			} else {
				setting.currentValue = defaultVal;
			}
			break;
		}
	case SettingType::Float:
		{
			float defaultVal = std::get<float>(setting.defaultValue);
			char buffer[256];
			GetPrivateProfileStringA(section.c_str(), key.c_str(), std::to_string(defaultVal).c_str(), buffer, sizeof(buffer), filePath.c_str());
			float parsed;
			if (TryParseFloat(buffer, parsed)) {
				setting.currentValue = std::clamp(parsed, setting.minValue, setting.maxValue);
			} else {
				setting.currentValue = defaultVal;
			}
			break;
		}
	case SettingType::TimeOfDay:
		{
			TimeOfDayValue timeOfDayValue = std::get<TimeOfDayValue>(setting.defaultValue);

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				char buffer[256];
				std::string defaultStr = std::to_string(timeOfDayValue.values[i]);
				GetPrivateProfileStringA(section.c_str(), fullKey.c_str(), defaultStr.c_str(), buffer, sizeof(buffer), filePath.c_str());
				float parsed;
				if (TryParseFloat(buffer, parsed)) {
					timeOfDayValue.values[i] = std::clamp(parsed, setting.minValue, setting.maxValue);
				}
			}

			setting.currentValue = timeOfDayValue;
			break;
		}
	case SettingType::ColorTimeOfDay:
		{
			ColorTimeOfDayValue colorTimeOfDayValue = std::get<ColorTimeOfDayValue>(setting.defaultValue);

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				char buffer[256];
				float3 defaultColor = colorTimeOfDayValue.values[i];
				std::string defaultStr = std::to_string(defaultColor.x) + ", " +
				                         std::to_string(defaultColor.y) + ", " +
				                         std::to_string(defaultColor.z);

				GetPrivateProfileStringA(section.c_str(), fullKey.c_str(), defaultStr.c_str(), buffer, sizeof(buffer), filePath.c_str());
				std::string valueStr = buffer;

				// Parse comma-separated float3 values
				std::stringstream ss(valueStr);
				std::string item;
				std::vector<float> components;
				bool success = true;

				while (std::getline(ss, item, ',')) {
					float parsed;
					if (TryParseFloat(item, parsed)) {
						components.push_back(std::clamp(parsed, setting.minValue, setting.maxValue));
					} else {
						success = false;
						break;
					}
				}

				// Ensure we have exactly 3 components and parsing was successful
				if (success && components.size() == 3) {
					colorTimeOfDayValue.values[i].x = components[0];
					colorTimeOfDayValue.values[i].y = components[1];
					colorTimeOfDayValue.values[i].z = components[2];
				}
			}

			setting.currentValue = colorTimeOfDayValue;
			break;
		}
	}
}

/**
 * @brief Writes a single setting to the specified INI file section.
 *
 * Writes the setting's current value(s) into the INI file at filePath under the given section and key.
 * - Bool settings are written as "true" or "false".
 * - Float settings are written with up to three decimal places, trailing zeros removed, and at least one decimal digit preserved.
 * - TimeOfDay settings write eight separate keys formed by appending time-of-day suffixes (Dawn, Sunrise, Day, Sunset, Dusk, Night, InteriorDay, InteriorNight) to `key`, each with a formatted float value.
 * - ColorTimeOfDay settings write eight keys (same suffixes) with comma-separated `x, y, z` components formatted as floats.
 *
 * @param filePath Path to the INI file to write.
 * @param section INI section (category) under which the key(s) will be written.
 * @param key Base key name; for time-of-day variants this is used as a prefix for the eight suffixed keys.
 * @param setting Setting object whose `currentValue` will be persisted.
 */
void SettingManager::SaveSettingToFile(const std::string& filePath, const std::string& section, const std::string& key, const Setting& setting)
{
	auto formatFloat = [](float value) -> std::string {
		char temp[32];
		sprintf_s(temp, "%.3f", value);
		std::string result = temp;

		// Remove trailing zeros
		while (result.length() > 1 && result.back() == '0') {
			result.pop_back();
		}

		// Ensure at least one decimal place (add .0 if needed)
		if (result.back() == '.') {
			result += '0';
		}

		return result;
	};

	switch (setting.type) {
	case SettingType::Bool:
		{
			bool value = std::get<bool>(setting.currentValue);
			WritePrivateProfileStringA(section.c_str(), key.c_str(), value ? "true" : "false", filePath.c_str());
			break;
		}
	case SettingType::Float:
		{
			float value = std::get<float>(setting.currentValue);
			std::string formatted = formatFloat(value);
			WritePrivateProfileStringA(section.c_str(), key.c_str(), formatted.c_str(), filePath.c_str());
			break;
		}
	case SettingType::TimeOfDay:
		{
			const TimeOfDayValue& timeOfDayValue = std::get<TimeOfDayValue>(setting.currentValue);

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				std::string formatted = formatFloat(timeOfDayValue.values[i]);
				WritePrivateProfileStringA(section.c_str(), fullKey.c_str(), formatted.c_str(), filePath.c_str());
			}
			break;
		}
	case SettingType::ColorTimeOfDay:
		{
			const ColorTimeOfDayValue& colorTimeOfDayValue = std::get<ColorTimeOfDayValue>(setting.currentValue);

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				const auto& color = colorTimeOfDayValue.values[i];
				std::string formatted = formatFloat(color.x) + ", " + formatFloat(color.y) + ", " + formatFloat(color.z);
				WritePrivateProfileStringA(section.c_str(), fullKey.c_str(), formatted.c_str(), filePath.c_str());
			}
			break;
		}
	}
}

/**
 * @brief Loads per-category weather-ignore flags from an INI file and updates category state.
 *
 * Reads the "IgnoreWeatherSystem" and "IgnoreWeatherSystemInterior" keys for each category that contains
 * at least one weather-supported setting and updates the category's ignore flags and their last-saved copies.
 * Intended to be called while already holding the manager's unique lock.
 *
 * @param filePath Path to the INI file to read.
 */
void SettingManager::LoadWeatherIgnoreSettings(const std::string& filePath)
{
	// Internal helper, called from methods already holding a unique lock
	for (auto& [category, categoryData] : categories) {
		bool hasWeatherSupport = false;
		for (const auto& [key, settingID] : categoryData.settings) {
			if (allSettings[settingID].hasWeatherSupport) {
				hasWeatherSupport = true;
				break;
			}
		}

		if (hasWeatherSupport) {
			char buffer1[256];
			GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem", "false", buffer1, sizeof(buffer1), filePath.c_str());
			bool parsed;
			if (TryParseBool(buffer1, parsed)) {
				categoryData.ignoreWeatherSystem = parsed;
			}

			char buffer2[256];
			GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior", "true", buffer2, sizeof(buffer2), filePath.c_str());
			if (TryParseBool(buffer2, parsed)) {
				categoryData.ignoreWeatherSystemInterior = parsed;
			}
		}

		categoryData.lastSavedIgnoreWeatherSystem = categoryData.ignoreWeatherSystem;
		categoryData.lastSavedIgnoreWeatherSystemInterior = categoryData.ignoreWeatherSystemInterior;
	}
}

/**
 * @brief Checks whether the specified category is configured to ignore the weather system.
 *
 * @param category Category name to query.
 * @return `true` if the category exists and its IgnoreWeatherSystem flag is set, `false` otherwise.
 */
bool SettingManager::GetIgnoreWeatherSystem(const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() ? categoryIt->second.ignoreWeatherSystem : false;
}

/**
 * @brief Query whether the specified category ignores weather effects while the player is interior.
 *
 * @param category Category name to query.
 * @return true if the category's IgnoreWeatherSystemInterior flag is set or the category is missing, `false` otherwise.
 */
bool SettingManager::GetIgnoreWeatherSystemInterior(const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() ? categoryIt->second.ignoreWeatherSystemInterior : true;
}

/**
 * @brief Set whether the weather system should be ignored for a settings category.
 *
 * Updates the category's IgnoreWeatherSystem flag. If the category does not exist, it will be created.
 *
 * @param category Name of the settings category.
 * @param ignore `true` to ignore the weather system for this category, `false` to allow weather effects.
 */
void SettingManager::SetIgnoreWeatherSystem(const std::string& category, bool ignore)
{
	std::unique_lock lock(mutex);
	categories[category].ignoreWeatherSystem = ignore;
}

/**
 * @brief Set whether a category ignores the weather system for interior blending.
 *
 * @param category Name of the setting category to modify.
 * @param ignore `true` to have the category ignore weather when interior blending is active, `false` to apply weather normally.
 */
void SettingManager::SetIgnoreWeatherSystemInterior(const std::string& category, bool ignore)
{
	std::unique_lock lock(mutex);
	categories[category].ignoreWeatherSystemInterior = ignore;
}

/**
 * @brief Reloads the main configuration and refreshes weather-specific settings.
 *
 * Reads and applies settings from the primary INI ("enbseries.ini"), updates
 * internal state tracking for main-file persistence, and clears/reloads any
 * cached per-weather data so weather-linked settings are refreshed from disk.
 */
void SettingManager::Load()
{
	LoadFromFile("enbseries.ini");
	ReloadAllWeatherSettings();
}

/**
 * @brief Persist all current settings to disk.
 *
 * Writes non-weather settings to "enbseries.ini" and saves all weather-linked settings
 * to their respective weather files.
 */
void SettingManager::Save()
{
	SaveToFile("enbseries.ini");
	SaveAllWeatherSettings();
}

// Explicit template instantiations
template bool SettingManager::GetValue<bool>(const std::string& key, const std::string& category, bool rawValue);
template float SettingManager::GetValue<float>(const std::string& key, const std::string& category, bool rawValue);
template TimeOfDayValue SettingManager::GetValue<TimeOfDayValue>(const std::string& key, const std::string& category, bool rawValue);
template ColorTimeOfDayValue SettingManager::GetValue<ColorTimeOfDayValue>(const std::string& key, const std::string& category, bool rawValue);

template void SettingManager::SetValue<bool>(const std::string& key, const std::string& category, const bool& value);
template void SettingManager::SetValue<float>(const std::string& key, const std::string& category, const float& value);
template void SettingManager::SetValue<TimeOfDayValue>(const std::string& key, const std::string& category, const TimeOfDayValue& value);
template void SettingManager::SetValue<ColorTimeOfDayValue>(const std::string& key, const std::string& category, const ColorTimeOfDayValue& value);

template bool SettingManager::GetValue<bool>(uint32_t id, bool rawValue);
template float SettingManager::GetValue<float>(uint32_t id, bool rawValue);
template TimeOfDayValue SettingManager::GetValue<TimeOfDayValue>(uint32_t id, bool rawValue);
template ColorTimeOfDayValue SettingManager::GetValue<ColorTimeOfDayValue>(uint32_t id, bool rawValue);

template void SettingManager::SetValue<bool>(uint32_t id, const bool& value);
template void SettingManager::SetValue<float>(uint32_t id, const float& value);
template void SettingManager::SetValue<TimeOfDayValue>(uint32_t id, const TimeOfDayValue& value);
template void SettingManager::SetValue<ColorTimeOfDayValue>(uint32_t id, const ColorTimeOfDayValue& value);

template bool SettingManager::GetValueInternal<bool>(uint32_t id, bool rawValue) const;
template float SettingManager::GetValueInternal<float>(uint32_t id, bool rawValue) const;
template TimeOfDayValue SettingManager::GetValueInternal<TimeOfDayValue>(uint32_t id, bool rawValue) const;
template ColorTimeOfDayValue SettingManager::GetValueInternal<ColorTimeOfDayValue>(uint32_t id, bool rawValue) const;

template void SettingManager::SetValueInternal<bool>(uint32_t id, const bool& value);
template void SettingManager::SetValueInternal<float>(uint32_t id, const float& value);
template void SettingManager::SetValueInternal<TimeOfDayValue>(uint32_t id, const TimeOfDayValue& value);
template void SettingManager::SetValueInternal<ColorTimeOfDayValue>(uint32_t id, const ColorTimeOfDayValue& value);