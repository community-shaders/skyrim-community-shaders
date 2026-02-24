#pragma once

namespace Util
{

	/**
	 * @brief Dumps the names of all settings from the specified setting collections and the game setting collection.
	 *
	 * This function retrieves settings from two INI setting collections (`INISettingCollection` and `INIPrefSettingCollection`)
	 * and logs their names. It also retrieves and logs settings from the game setting collection (`GameSettingCollection`).
	 *
	 * The output is logged using the `logger::info` method, and it includes the collection name for better clarity on where
	 * each setting belongs.
	 */
	void DumpSettingsOptions();

	struct GameSetting
	{
		std::string friendlyName;
		std::string description;
		std::uintptr_t offset;
		std::variant<bool, std::int32_t, std::uint32_t, float> defaultValue;
		std::variant<bool, std::int32_t, std::uint32_t, float> minValue;
		std::variant<bool, std::int32_t, std::uint32_t, float> maxValue;
	};

	/**
	 * @brief Updates boolean settings in the provided map, setting them to the specified value.
	 *
	 * @param settingsMap A map containing settings to update, where each setting is associated with its name.
	 * @param featureName The name of the feature being enabled or disabled, used for logging purposes.
	 * @param a_value The value to set for the boolean settings.
	 */
	void SetBooleanSettings(const std::map<std::string, GameSetting>& settingsMap, const std::string& featureName, bool a_value);

	/**
	 * @brief Updates boolean settings in the provided map, setting them to true.
	 *
	 * This function is intended to be used when features are disabled in Skyrim by default.
	 * It logs the changes and supports customizing the feature name in the log output.
	 *
	 * @param settingsMap A map containing settings to update, where each setting is associated with its name.
	 * @param featureName The name of the feature being enabled, used for logging purposes.
	 */
	void EnableBooleanSettings(const std::map<std::string, GameSetting>& settingsMap, const std::string& featureName);

	/**
	 * @brief Updates boolean settings in the provided map, setting them to false.
	 *
	 * @param settingsMap A map containing settings to update, where each setting is associated with its name.
	 * @param featureName The name of the feature being disabled, used for logging purposes.
	 */
	void DisableBooleanSettings(const std::map<std::string, GameSetting>& settingsMap, const std::string& featureName);

	/**
	* @brief Resets game settings to their default values if the current values do not match the defaults.
	*
	* This function iterates through all game settings in the provided map and checks whether each setting's
	* current value is equal to its default value. If they differ, the current value is updated to the default value.
	*
	* The function assumes that the `gameSetting` structure contains the following fields:
	* - `defaultValue`: The default value for the setting.
	* - `currentValue`: The current value of the setting.
	*
	* @param settingsMap A map of setting names to `gameSetting` objects, where each object contains
	*        the information for a specific game setting. The map is modified in place.
	*/
	void ResetGameSettingsToDefaults(std::map<std::string, GameSetting>& settingsMap);

	/**
	 * @brief Renders a tree of ImGui elements for the specified settings.
	 *
	 * This function generates ImGui UI elements for the settings provided in the map.
	 *
	 * @param settingsMap The map of settings to be rendered.
	 * @param tableName A unique identifier for the ImGui tree.
	 */
	void RenderImGuiSettingsTree(const std::map<std::string, GameSetting>& settingsMap, const std::string& tableName);

	/**
	 * @brief Renders an appropriate ImGui element (e.g., checkbox, slider) based on the variable type.
	 *
	 * This function determines the type of setting from its name and renders the corresponding UI element.
	 *
	 * @tparam T The type of the setting value.
	 * @param settingName The name of the setting.
	 * @param settingData The metadata of the setting.
	 * @param valuePtr Pointer to the value (either from an RE::Setting or direct memory access).
	 * @param collectionName (Optional) The name of the collection for logging purposes.
	 */
	template <typename T>
	void RenderImGuiElement(const std::string& settingName, const GameSetting& settingData, T* valuePtr, const std::string& collectionName = "");

	/**
	 * @brief Renders an appropriate ImGui element for RE::Setting data.
	 *
	 * This function creates UI elements for settings that are tied to the RE::Setting structure.
	 *
	 * @param settingName The name of the setting.
	 * @param settingData The metadata of the setting.
	 * @param setting Pointer to the RE::Setting.
	 * @param collectionName (Optional) The name of the collection for logging purposes.
	 */
	void RenderImGuiElement(const std::string& settingName, const GameSetting& settingData, RE::Setting* setting, const std::string& collectionName = "");

	/**
	 * @brief Gets the value of a game setting, transparently handling INI-based and offset-based settings.
	 *
	 * For INI-based settings (offset == 0), tries INISettingCollection, INIPrefSettingCollection,
	 * then GameSettingCollection. For offset-based settings (e.g., VR where INI entries don't exist),
	 * reads directly from the memory address.
	 *
	 * @tparam T The expected value type (bool, float, std::int32_t, or std::uint32_t).
	 * @param settingName The setting name (e.g., "iNumSplits:Display").
	 * @param settingData The GameSetting metadata containing the offset and default value.
	 * @return The current value of the setting, or the default value if not found.
	 */
	template <typename T>
	T GetGameSettingValue(const std::string& settingName, const GameSetting& settingData)
	{
		if (settingData.offset == 0) {
			auto* setting = RE::GetINISetting(settingName.c_str());
			if (!setting) {
				if (auto* collection = globals::game::gameSettingCollection)
					setting = collection->GetSetting(settingName.data());
			}
			if (setting) {
				T value{};
				if constexpr (std::is_same_v<T, bool>)
					value = setting->data.b;
				else if constexpr (std::is_same_v<T, float>)
					value = setting->data.f;
				else if constexpr (std::is_same_v<T, std::int32_t>)
					value = setting->data.i;
				else if constexpr (std::is_same_v<T, std::uint32_t>)
					value = setting->data.u;
				logger::debug("GetGameSettingValue: '{}' = {} (from INI)", settingName, value);
				return value;
			}
			auto defaultValue = std::get<T>(settingData.defaultValue);
			logger::warn("GetGameSettingValue: '{}' not found in INI or GameSettingCollection, using default: {}", settingName, defaultValue);
			return defaultValue;
		} else {
			auto address = REL::Offset{ settingData.offset }.address();
			T value = *reinterpret_cast<T*>(address);
			logger::debug("GetGameSettingValue: '{}' = {} (from offset 0x{:X})", settingName, value, settingData.offset);
			return value;
		}
	}

	/**
	 * @brief Sets the value of a game setting, transparently handling INI-based and offset-based settings.
	 *
	 * For INI-based settings (offset == 0), tries INISettingCollection, INIPrefSettingCollection,
	 * then GameSettingCollection. For offset-based settings (e.g., VR where INI entries don't exist),
	 * writes directly to the memory address.
	 *
	 * @tparam T The value type (bool, float, std::int32_t, or std::uint32_t).
	 * @param settingName The setting name (e.g., "iNumFocusShadow:Display").
	 * @param settingData The GameSetting metadata containing the offset.
	 * @param a_value The value to set.
	 */
	template <typename T>
	void SetGameSettingValue(const std::string& settingName, const GameSetting& settingData, T a_value)
	{
		if (settingData.offset == 0) {
			auto* setting = RE::GetINISetting(settingName.c_str());
			if (!setting) {
				if (auto* collection = globals::game::gameSettingCollection)
					setting = collection->GetSetting(settingName.data());
			}
			if (setting) {
				if constexpr (std::is_same_v<T, bool>)
					setting->data.b = a_value;
				else if constexpr (std::is_same_v<T, float>)
					setting->data.f = a_value;
				else if constexpr (std::is_same_v<T, std::int32_t>)
					setting->data.i = a_value;
				else if constexpr (std::is_same_v<T, std::uint32_t>)
					setting->data.u = a_value;
				logger::debug("SetGameSettingValue: '{}' = {} (from INI)", settingName, a_value);
			} else {
				logger::warn("SetGameSettingValue: '{}' not found in INI or GameSettingCollection", settingName);
			}
		} else {
			auto address = REL::Offset{ settingData.offset }.address();
			*reinterpret_cast<T*>(address) = a_value;
			logger::debug("SetGameSettingValue: '{}' = {} (from offset 0x{:X})", settingName, a_value, settingData.offset);
		}
	}

	/**
	 * @brief Saves the provided game settings to the INI file.
	 *
	 * This function iterates through the settings map and saves settings that are managed via
	 * the INISettingCollection (i.e., those without an offset) by calling `WriteSetting`.
	 *
	 * @param settingsMap A map of game settings to be saved.
	 */
	void SaveGameSettings(const std::map<std::string, GameSetting>& settingsMap);

	/**
	 * @brief Loads the provided game settings from the INI file.
	 *
	 * This function iterates through the settings map and loads settings that are managed via
	 * the INISettingCollection (i.e., those without an offset) by calling `ReadSetting`.
	 *
	 * @param settingsMap A map of game settings to be loaded.
	 */
	void LoadGameSettings(const std::map<std::string, GameSetting>& settingsMap);
}  // namespace Util
