#include "WeatherManager.h"

#include "State.h"

WeatherManager::CurrentWeathers WeatherManager::GetCurrentWeathers()
{
	CurrentWeathers result;

	auto sky = RE::Sky::GetSingleton();
	if (!sky) {
		return result;
	}

	result.currentWeather = sky->currentWeather;
	result.lerpFactor = sky->currentWeatherPct;

	// Update cache: store current lastWeather if it exists, otherwise keep the cached one
	if (sky->lastWeather) {
		cachedLastWeather = sky->lastWeather;
	}

	// Use cached last weather if sky->lastWeather is null
	result.lastWeather = sky->lastWeather ? sky->lastWeather : cachedLastWeather;

	return result;
}

void WeatherManager::LoadPerWeatherSettingsFromDisk()
{
	const std::string weathersPath = std::format("{}\\Weathers", Util::PathHelpers::GetCommunityShaderPath().string());

	if (!std::filesystem::exists(weathersPath) || !std::filesystem::is_directory(weathersPath)) {
		logger::info("Weathers directory does not exist: {}", weathersPath);
		return;
	}

	logger::info("Loading per-weather settings from: {}", weathersPath);

	for (const auto& entry : std::filesystem::directory_iterator(weathersPath)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".json") {
			continue;
		}

		std::string weatherKey = entry.path().stem().string();
		std::ifstream settingsFile(entry.path());

		if (!settingsFile.good() || !settingsFile.is_open()) {
			logger::warn("Failed to open weather settings file: {}", entry.path().string());
			continue;
		}

		try {
			json weatherData;
			settingsFile >> weatherData;
			settingsFile.close();

			// Store the entire weather settings in cache
			// The structure is expected to be: { "featureSettings": { "FeatureName": { settings }, ... }
			if (weatherData.is_object() && weatherData.contains("featureSettings") && weatherData["featureSettings"].is_object()) {
				for (auto& [featureName, featureSettings] : weatherData["featureSettings"].items()) {
					perWeatherSettingsCache[weatherKey][featureName] = featureSettings;
				}
				logger::info("Loaded settings for weather: {}", weatherKey);
			}
		} catch (const nlohmann::json::parse_error& e) {
			logger::warn("Error parsing weather settings file ({}): {}", entry.path().string(), e.what());
		}
	}

	logger::info("Finished loading per-weather settings. Total weathers: {}", perWeatherSettingsCache.size());
}

void WeatherManager::UpdateFeatures()
{
	auto currentWeathers = GetCurrentWeathers();

	// Check if weather state has changed
	bool weatherChanged = (currentWeathers.currentWeather != lastKnownWeather.currentWeather) ||
	                      (currentWeathers.lastWeather != lastKnownWeather.lastWeather);

	// Detect if a new transition is starting
	bool transitionStarting = weatherChanged && currentWeathers.lerpFactor < 1.0f;

	// Detect if transition just completed
	bool transitionEnding = lastKnownWeather.lerpFactor < 1.0f && currentWeathers.lerpFactor >= 1.0f;

	// Always update if lerp factor changes or weather changed
	if (weatherChanged || std::abs(currentWeathers.lerpFactor - lastKnownWeather.lerpFactor) > 0.001f) {
		auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();

		// Get all features and update those that have registered weather variables
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature || !feature->loaded) {
				continue;
			}

			std::string featureName = feature->GetShortName();

			// Check if feature has registered weather variables
			if (globalRegistry->HasWeatherSupport(featureName)) {
				json currWeatherSettings;
				json nextWeatherSettings;

				// Load settings for last weather (from)
				if (currentWeathers.lastWeather && currentWeathers.lerpFactor < 1.0f) {
					LoadSettingsFromWeather(currentWeathers.lastWeather, featureName, currWeatherSettings);
				}

				// Load settings for current weather (to)
				if (currentWeathers.currentWeather) {
					LoadSettingsFromWeather(currentWeathers.currentWeather, featureName, nextWeatherSettings);
				}

				// Handle transition lifecycle
				if (transitionStarting) {
					// Begin new transition - cache the "from" values
					globalRegistry->BeginFeatureTransition(featureName, currWeatherSettings);
				}

				// Update feature variables
				if (currentWeathers.lerpFactor >= 1.0f && nextWeatherSettings.empty()) {
					// Transition complete, no override on destination - reset to user settings
					globalRegistry->EndFeatureTransition(featureName);
					auto* featureRegistry = globalRegistry->GetFeatureRegistry(featureName);
					if (featureRegistry) {
						for (const auto& var : featureRegistry->GetVariables()) {
							var->SetToUserSettings();
						}
					}
				} else {
					// In transition or has override - interpolate
					globalRegistry->UpdateFeatureFromWeathers(featureName, currWeatherSettings, nextWeatherSettings, currentWeathers.lerpFactor);
					if (transitionEnding) {
						globalRegistry->EndFeatureTransition(featureName);
					}
				}
			}
		}

		lastKnownWeather = currentWeathers;
	}
}

void WeatherManager::SaveSettingsToWeather(RE::TESWeather* weather, const std::string& featureName, const json& settings)
{
	if (!weather) {
		return;
	}

	std::string weatherKey = GetWeatherKey(weather);

	// Update cache: if settings is empty, remove the feature entry; otherwise set it
	if (settings.is_object() && settings.empty()) {
		auto wkIt = perWeatherSettingsCache.find(weatherKey);
		if (wkIt != perWeatherSettingsCache.end()) {
			wkIt->second.erase(featureName);
			if (wkIt->second.empty()) {
				perWeatherSettingsCache.erase(wkIt);
			}
		}
	} else {
		perWeatherSettingsCache[weatherKey][featureName] = settings;
	}

	// Save to disk
	const std::string weathersPath = std::format("{}\\Weathers", Util::PathHelpers::GetCommunityShaderPath().string());
	const std::string filePath = std::format("{}\\{}.json", weathersPath, weatherKey);

	// Create directory if needed
	if (!std::filesystem::exists(weathersPath)) {
		try {
			std::filesystem::create_directories(weathersPath);
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error creating Weathers directory ({}): {}", weathersPath, e.what());
			return;
		}
	}

	// Load existing weather data if it exists
	json weatherData;
	if (std::filesystem::exists(filePath)) {
		std::ifstream existingFile(filePath);
		if (existingFile.good() && existingFile.is_open()) {
			try {
				existingFile >> weatherData;
			} catch (const nlohmann::json::parse_error& e) {
				logger::warn("Error parsing existing weather file ({}): {}", filePath, e.what());
				weatherData = json::object();
			}
			existingFile.close();
		}
	}

	// Ensure weatherData is an object and has featureSettings
	if (!weatherData.is_object()) {
		weatherData = json::object();
	}
	if (!weatherData.contains("featureSettings") || !weatherData["featureSettings"].is_object()) {
		weatherData["featureSettings"] = json::object();
	}
	auto& featureSettings = weatherData["featureSettings"];

	// Update with new feature settings or remove feature entry if settings empty
	if (settings.is_object() && settings.empty()) {
		// Remove feature entry from loaded JSON
		featureSettings.erase(featureName);
	} else {
		featureSettings[featureName] = settings;
	}

	// Write back to disk
	// Only delete file if featureSettings is empty AND weatherData contains only featureSettings (no other data)
	if (featureSettings.empty() && weatherData.size() == 1 && weatherData.contains("featureSettings")) {
		std::error_code ec;
		if (std::filesystem::remove(filePath, ec)) {
			logger::info("Removed weather settings file (no data remain): {}", filePath);
			return;
		}
		if (ec == std::errc::no_such_file_or_directory) {
			return;
		}
		logger::warn("Failed to remove empty weather settings file ({}): {}", filePath, ec.message());
		// fall through to write updated JSON as a best-effort fallback
	}

	std::ofstream settingsFile(filePath);
	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to open weather settings file for writing: {}", filePath);
		return;
	}

	try {
		settingsFile << weatherData.dump(1);
		settingsFile.close();
		logger::info("Saved {} settings for weather: {}", featureName, weatherKey);
	} catch (const std::exception& e) {
		logger::warn("Error writing weather settings file ({}): {}", filePath, e.what());
	}
}

bool WeatherManager::LoadSettingsFromWeather(RE::TESWeather* weather, const std::string& featureName, json& o_json)
{
	if (!weather) {
		return false;
	}

	std::string weatherKey = GetWeatherKey(weather);

	// Check cache first
	auto weatherIt = perWeatherSettingsCache.find(weatherKey);
	if (weatherIt != perWeatherSettingsCache.end()) {
		auto featureIt = weatherIt->second.find(featureName);
		if (featureIt != weatherIt->second.end()) {
			const json& featureJson = featureIt->second;

			// Check if weather-specific overrides are enabled
			bool enabled = featureJson.value("__enabled", false);
			if (!enabled) {
				// Settings exist but are disabled, return empty
				return false;
			}

			o_json = featureJson;
			return true;
		}
	}

	return false;
}

std::string WeatherManager::GetWeatherKey(RE::TESWeather* weather)
{
	if (!weather) {
		return "None";
	}

	const char* editorID = weather->GetFormEditorID();
	if (editorID && editorID[0] != '\0') {
		return std::string(editorID);
	}

	// Fallback to FormID if no EditorID
	return std::format("{:08X}", weather->GetFormID());
}

bool WeatherManager::HasWeatherSettings(RE::TESWeather* weather) const
{
	if (!weather) {
		return false;
	}

	std::string weatherKey = GetWeatherKey(weather);
	return perWeatherSettingsCache.find(weatherKey) != perWeatherSettingsCache.end();
}

void WeatherManager::ClearCache()
{
	perWeatherSettingsCache.clear();
	lastKnownWeather = CurrentWeathers();
	cachedLastWeather = nullptr;
	logger::info("Cleared WeatherManager cache");
}
