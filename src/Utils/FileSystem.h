#pragma once

#include "Format.h"
#include "WinApi.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <optional>
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
		/** @brief Preset library folders, relative to the CommunityShaders plugin directory. */
		constexpr const char* kUnifiedPresetsSubdir = "Presets";
		constexpr const char* kEffects11PresetsSubdir = "Effects11/Presets";

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
		 * Gets the CommunityShaders Icons directory path (UI textures / glyphs).
		 * @return Interface / "Icons"
		 */
		std::filesystem::path GetIconsPath();

		/**
		 * Gets the deployed icon-font directory (FA / Tabler / Game Icons + licenses).
		 * Kept under Icons so it stays out of the user Fonts catalog.
		 * @return Icons / "glyphs"
		 */
		std::filesystem::path GetIconFontsPath();

		/**
		 * Gets the CommunityShaders Cursors directory path
		 * @return Interface / "Cursors"
		 */
		std::filesystem::path GetCursorsPath();

		/**
		 * Gets the SettingsUser.json file path
		 * @return CommunityShaderPath / "SettingsUser.json"
		 */
		std::filesystem::path GetSettingsUserPath();

		/**
		 * Gets the Themes directory path
		 * @return CommunityShaderPath / "Themes"
		 */
		std::filesystem::path GetThemesPath();

		/**
		 * Gets the Translations directory path for i18n locale files
		 * @return CommunityShaderPath / "Translations"
		 */
		std::filesystem::path GetTranslationsPath();

		/**
		 * Gets the SceneSettings directory path
		 * @return CommunityShaderPath / "SceneSettings"
		 */
		std::filesystem::path GetSceneSettingsPath();

		/**
		 * Gets the UnifiedWaterCache directory path
		 * @return CommunityShaderPath / "UnifiedWaterCache"
		 */
		std::filesystem::path GetUnifiedWaterCachePath();

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
		 * Returns the real path to the Effects11 presets library (for Explorer / create_directories).
		 * @return  <mod_root> / "SKSE" / "Plugins" / "CommunityShaders" / "Effects11" / "Presets", or the in-game
		 *          path when the mod root is unknown
		 */
		std::filesystem::path GetEffects11PresetsRealPath();

		/**
		 * Returns the real path to the unified presets library.
		 * @return  <mod_root> / "SKSE" / "Plugins" / "CommunityShaders" / "Presets", or the in-game path when
		 *          the mod root is unknown
		 */
		std::filesystem::path GetUnifiedPresetsRealPath();

		/**
		 * @brief Directories to scan for a CommunityShaders subfolder: the Data (VFS) path, then the mod's
		 * real folder when distinct, since MO2 hides folders created there mid-session from the VFS.
		 * @param relativePath Path under CommunityShaders, e.g. "Presets".
		 */
		std::vector<std::filesystem::path> GetCommunityShaderScanRoots(const std::filesystem::path& relativePath);

		/** @brief A path as UTF-8. path::string() converts through the ANSI code page, which mangles or throws
		 *  on names outside it, while pack ids, JSON and ImGui all carry UTF-8. */
		std::string ToUtf8(const std::filesystem::path& path);
		/** @brief ToUtf8 with '/' separators, for paths stored in JSON. */
		std::string ToGenericUtf8(const std::filesystem::path& path);
		/** @brief A path from UTF-8 text; a path built from std::string reads it in the ANSI code page. */
		std::filesystem::path FromUtf8(std::string_view text);

		/** @brief Whether a library folder or file is hidden from preset scans ('_' or '.' prefix). */
		bool IsHiddenLibraryEntry(std::string_view name);
		/** @brief Path overload: reads the native name, since path::string() throws outside the ANSI code page. */
		bool IsHiddenLibraryEntry(const std::filesystem::path& name);

		/** @brief Visible child folders (or files) of a CommunityShaders subfolder across every scan root; first root wins. */
		std::vector<std::filesystem::path> ListCommunityShaderEntries(const std::filesystem::path& relativePath, bool directories);

		/** @brief Folder of a unified preset pack: the first scan root holding it, else the Data (VFS) path,
		 *  which MO2 routes to its overwrite folder. */
		std::filesystem::path GetUnifiedPackPath(const std::string& packId);

		/** @brief Every scan root's folder for a pack, Data (VFS) first. MO2 can split one pack across the
		 *  overwrite folder and the mod's real folder, where exports land. */
		std::vector<std::filesystem::path> GetUnifiedPackPaths(const std::string& packId);

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

		/** @brief Opens a file, folder or URL with its default handler, Explorer for folders. @return Whether the shell accepted it. */
		bool OpenWithShell(const std::filesystem::path& path);

		/**
		 * Replaces Windows-invalid filename characters with underscore.
		 * @param name Filename or path component to sanitize
		 * @return Sanitized string safe for use as a filename
		 */
		std::string SanitizeFileName(std::string name);

		/**
		 * @brief Writes to a sibling temporary, then replaces the target, so a failure never leaves a truncated file.
		 * @param context Human-readable description used in log messages
		 * @return true when the target now holds the new contents
		 */
		bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content, std::string_view context);

		/** @brief Serializes JSON and writes it through WriteFileAtomically; a serialization failure leaves the target untouched. */
		bool WriteJsonAtomically(const std::filesystem::path& path, const nlohmann::json& data, int indent, std::string_view context);

		/**
		 * @brief Parses a JSON file (instantiated for nlohmann::json and nlohmann::ordered_json).
		 * @param context Human-readable description used in the log message
		 * @param maxBytes Larger files are rejected; 0 means unbounded
		 * @param error Receives the reason when the file exists but is rejected; untouched when it cannot be opened
		 * @return The document, or nullopt when the file cannot be opened, is too large, or cannot be parsed
		 */
		template <class Json = nlohmann::json>
		std::optional<Json> ReadJsonFile(const std::filesystem::path& path, std::string_view context, std::uintmax_t maxBytes = 0, std::string* error = nullptr);

		/** @brief ReadJsonFile that also rejects documents whose root is not an object. */
		std::optional<nlohmann::json> ReadJsonObject(const std::filesystem::path& path, std::string_view context, std::uintmax_t maxBytes = 0, std::string* error = nullptr);
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

	}
}
