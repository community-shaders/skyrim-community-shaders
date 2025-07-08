#pragma once

#include "FileSystem.h"
#include "Format.h"
#include "Winapi.h"
#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <tuple>
#include <vector>

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

}
