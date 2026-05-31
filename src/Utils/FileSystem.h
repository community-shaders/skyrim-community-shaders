#pragma once

#include "Format.h"
#include "WinApi.h"
#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>
#include <tuple>
#include <vector>

struct SettingsDiffEntry
{
	std::string path;
	std::string aValue;
	std::string bValue;
};

namespace Util
{
	/**
	 * Path construction utilities for consistent file system path handling.
	 * Reduces repeated path construction and provides consistent path handling.
	 */
	namespace PathHelpers
	{
		/**
		 * Gets the base Data directory path
		 * @return Current working directory / "Data"
		 */
		std::filesystem::path GetDataPath();

		/**
		 * Gets the CommunityShaders plugin directory path
		 * @return Data / "SKSE" / "Plugins" / "CommunityShaders"
		 */
		std::filesystem::path GetCommunityShaderPath();

		/**
		 * Gets the CommunityShaders_ImGui.ini file path
		 * @return Data / "SKSE" / "Plugins" / "CommunityShaders_ImGui.ini"
		 */
		std::filesystem::path GetImGuiIniPath();

		/**
		 * Gets the CommunityShaders Interface directory path
		 * @return Data / "Interface" / "CommunityShaders"
		 */
		std::filesystem::path GetInterfacePath();

		/**
		 * Gets the CommunityShaders Fonts directory path
		 * @return Interface / "Fonts"
		 */
		std::filesystem::path GetFontsPath();

		/**
		 * Gets the CommunityShaders Icons directory path
		 * @return Interface / "Icons"
		 */
		std::filesystem::path GetIconsPath();

		/**
		 * Gets the SettingsUser.json file path
		 * @return CommunityShaderPath / "SettingsUser.json"
		 */
		std::filesystem::path GetSettingsUserPath();

		/**
		 * Gets the SettingsTest.json file path
		 * @return CommunityShaderPath / "SettingsTest.json"
		 */
		std::filesystem::path GetSettingsTestPath();

		/**
		 * Gets the SettingsDefault.json file path
		 * @return CommunityShaderPath / "SettingsDefault.json"
		 */
		std::filesystem::path GetSettingsDefaultPath();

		/**
		 * Gets the SettingsTheme.json file path
		 * @return CommunityShaderPath / "SettingsTheme.json"
		 */
		std::filesystem::path GetSettingsThemePath();

		/**
		 * Gets the Themes directory path
		 * @return CommunityShaderPath / "Themes"
		 */
		std::filesystem::path GetThemesPath();

		/**
		 * Gets the Overrides directory path
		 * @return CommunityShaderPath / "Overrides"
		 */
		std::filesystem::path GetOverridesPath();

		/**
		 * Gets the User Overrides directory path (for user modifications to overrides)
		 * @return CommunityShaderPath / "Overrides" / "User"
		 */
		std::filesystem::path GetUserOverridesPath();

		/**
		 * Gets the AppliedOverrides.json file path
		 * @return CommunityShaderPath / "AppliedOverrides.json"
		 */
		std::filesystem::path GetAppliedOverridesPath();

		/**
		 * Gets the SceneSettings directory path
		 * @return CommunityShaderPath / "SceneSettings"
		 */
		std::filesystem::path GetSceneSettingsPath();

		/**
		 * Gets the main Shaders directory path
		 * @return Data / "Shaders"
		 */
		std::filesystem::path GetShadersPath();

		/**
		 * Gets the Features directory path where INI files are stored
		 * @return Data / "Shaders" / "Features"
		 */
		std::filesystem::path GetFeaturesPath();

		/**
		 * Gets the deployed INI file path for a feature
		 * @param featureName The feature name
		 * @return Features / "{featureName}.ini"
		 */
		std::filesystem::path GetFeatureIniPath(const std::string& featureName);

		/**
		 * Gets the deployed shader directory path for a feature
		 * @param featureName The feature name
		 * @return Shaders / "{featureName}"
		 */
		std::filesystem::path GetFeatureShaderPath(const std::string& featureName);

		/**
		 * Returns the real file system path to the current DLL module.
		 *
		 * This is useful when running under Mod Organizer 2 (MO2), which uses a virtual file system (VFS).
		 * Accessing files relative to the game's Data directory via VFS (e.g. "Data/Shaders") may not work
		 * outside the game process (e.g. from Windows Explorer or ShellExecute), since those paths don't
		 * exist on disk. This function bypasses VFS and returns the actual DLL path on disk.
		 *
		 * @return Absolute file system path to the current DLL module.
		 */
		std::filesystem::path GetCurrentModuleRealPath();

		/**
		 * Returns the real root directory of the mod, relative to the DLL path.
		  * @return <mod_root>.
		 */
		std::filesystem::path GetRootRealPath();

		/**
		 * Returns the real path to the Shaders directory located in the mod's root folder.
		 * @return  <mod_root> / "Shaders"
		 */
		std::filesystem::path GetShadersRealPath();

		/**
		 * Returns the real path to the Themes directory containing theme JSON files.
		 * @return  <mod_root> / "SKSE" / "Plugins" / "CommunityShaders" / "Themes"
		 */
		std::filesystem::path GetThemesRealPath();

		/**
		 * Returns the real path to the Features directory containing feature INI files.
		 * @return  <mod_root> / "Shaders" / "Features"
		 */
		std::filesystem::path GetFeaturesRealPath();

		/**
		 * Resolves a game-root-relative path to a real filesystem path, bypassing MO2's VFS.
		 *
		 * MO2 maps the game's Data directory to a mod's physical root folder via its virtual
		 * file system. Paths like "Data\Shaders\Upscaling\FidelityFX" don't exist on disk when
		 * passed to ShellExecute outside the game process. This function strips the leading
		 * "Data\" component and prepends GetRootRealPath() to produce the real on-disk path.
		 *
		 * @param dataRelativePath Path relative to the game root (e.g. L"Data\\Shaders\\Upscaling\\FidelityFX")
		 * @return Real filesystem path, or empty path if GetRootRealPath() fails
		 */
		std::filesystem::path GetRealPathFromDataRelative(const std::filesystem::path& dataRelativePath);

		/**
		 * Returns the path to the Community Shaders log file in the default SKSE logging folder.
		 * @return Documents / "My Games" / "Skyrim..." / "SKSE" / "CommunityShaders.log"
		 */
		std::filesystem::path GetLogPath();

	}

	/**
	 * File system utilities for safe file operations
	 */
	namespace FileHelpers
	{
		/**
		 * Result of a file deletion operation
		 */
		struct DeletionResult
		{
			bool success;
			std::string errorMessage;
			std::string deletedDescription;
		};

		/**
		 * Safely deletes a file or directory with proper error handling and logging
		 * @param path The path to delete
		 * @param description Human-readable description for logging
		 * @return DeletionResult with success status and details
		 */
		DeletionResult SafeDelete(const std::string& path, const std::string& description);

		/**
		 * Ensures a directory exists, creating it if necessary with proper error handling
		 * @param path The directory path to ensure exists
		 */
		void EnsureDirectoryExists(const std::filesystem::path& path);

		/**
		 * Replaces Windows-invalid filename characters with underscore.
		 * @param name Filename or path component to sanitize
		 * @return Sanitized string safe for use as a filename
		 */
		std::string SanitizeFileName(std::string name);
	}

	/**
	 * Enumerates all DLLs in a directory and returns a vector of (name, version string) pairs.
	 */
	inline std::vector<std::pair<std::string, std::string>> EnumerateDllVersions(const std::filesystem::path& dir)
	{
		std::vector<std::pair<std::string, std::string>> result;
		try {
			for (const auto& entry : std::filesystem::directory_iterator(dir)) {
				if (entry.is_regular_file() && entry.path().extension() == L".dll") {
					const auto& path = entry.path();
					auto version = Util::GetDllVersion(path.c_str());
					auto name = path.filename().string();
					std::string versionStr = version ? Util::GetFormattedVersion(*version) : "Unknown";
					result.emplace_back(name, versionStr);
				}
			}
		} catch (const std::filesystem::filesystem_error& e) {
			// Log error but return empty vector to avoid crashing
			logger::warn("Failed to enumerate DLL versions in {}: {}", dir.string(), e.what());
		}
		return result;
	}

	namespace FileSystem
	{
		/**
		 * Compares two JSON objects and returns a list of differences
		 * Core diffing logic shared between file-based and in-memory JSON comparisons
		 * @param userJson First JSON object (USER/baseline variant)
		 * @param testJson Second JSON object (TEST variant)
		 * @param epsilon Tolerance for floating-point comparisons (default: 0.0001f filters precision noise while preserving meaningful changes >0.01%)
		 * @return Vector of differences between the two JSON objects
		 */
		std::vector<SettingsDiffEntry> DiffJson(const nlohmann::json& userJson, const nlohmann::json& testJson, float epsilon = 0.0001f);

		/**
		 * Loads and compares two JSON files, returning a list of differences
		 * @param userPath Path to the first JSON file (USER variant)
		 * @param testPath Path to the second JSON file (TEST variant)
		 * @param epsilon Tolerance for floating-point comparisons (default: 0.0001f)
		 * @return Vector of differences between the two JSON files
		 */
		std::vector<SettingsDiffEntry> LoadJsonDiff(const std::filesystem::path& userPath, const std::filesystem::path& testPath, float epsilon = 0.0001f);
	}
}
