#include "FileSystem.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <psapi.h>

namespace Util
{
	// Path helper utilities implementation
	namespace PathHelpers
	{
		std::filesystem::path GetDataPath()
		{
			try {
				// Get the current process (game) executable path
				wchar_t buffer[MAX_PATH];
				DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
				if (length == 0 || length == MAX_PATH) {
					throw std::runtime_error("Failed to get module filename");
				}

				auto executablePath = std::filesystem::path(buffer);

				auto gamePath = executablePath.parent_path();
				auto dataPath = gamePath / "Data";
				return dataPath;
			} catch (const std::exception& e) {
				// Fallback to current_path if Windows API method fails
				logger::warn("Failed to get game path via Windows API, falling back to current_path: {}", e.what());
				return std::filesystem::current_path() / "Data";
			}
		}

		std::filesystem::path GetCommunityShaderPath()
		{
			return GetDataPath() / "SKSE" / "Plugins" / "CommunityShaders";
		}

		std::filesystem::path GetImGuiIniPath()
		{
			return GetDataPath() / "SKSE" / "Plugins" / "CommunityShaders_ImGui.ini";
		}

		std::filesystem::path GetInterfacePath()
		{
			return GetDataPath() / "Interface" / "CommunityShaders";
		}

		std::filesystem::path GetFontsPath()
		{
			return GetInterfacePath() / "Fonts";
		}

		std::filesystem::path GetIconsPath()
		{
			return GetInterfacePath() / "Icons";
		}

		std::filesystem::path GetSettingsUserPath()
		{
			return GetCommunityShaderPath() / "SettingsUser.json";
		}

		std::filesystem::path GetSettingsTestPath()
		{
			return GetCommunityShaderPath() / "SettingsTest.json";
		}

		std::filesystem::path GetSettingsDefaultPath()
		{
			return GetCommunityShaderPath() / "SettingsDefault.json";
		}

		std::filesystem::path GetSettingsThemePath()
		{
			return GetCommunityShaderPath() / "SettingsTheme.json";
		}

		std::filesystem::path GetThemesPath()
		{
			return GetCommunityShaderPath() / "Themes";
		}

		std::filesystem::path GetOverridesPath()
		{
			return GetCommunityShaderPath() / "Overrides";
		}

		std::filesystem::path GetAppliedOverridesPath()
		{
			return GetCommunityShaderPath() / "AppliedOverrides.json";
		}

		std::filesystem::path GetShadersPath()
		{
			return GetDataPath() / "Shaders";
		}

		std::filesystem::path GetFeaturesPath()
		{
			return GetShadersPath() / "Features";
		}

		std::filesystem::path GetCurrentModuleRealPath()
		{
			try {
				HMODULE selfModule = nullptr;
				if (!GetModuleHandleExW(
						GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCWSTR>(&GetCurrentModuleRealPath),
						&selfModule)) {
					return {};
				}
				wchar_t buffer[MAX_PATH]{};
				DWORD size = GetModuleFileNameExW(GetCurrentProcess(), selfModule, buffer, MAX_PATH);
				if (size == 0 || size == MAX_PATH) {
					throw std::runtime_error("Failed to get module filename");
				}
				return std::filesystem::path(buffer);
			} catch (const std::exception& e) {
				logger::error("GetCurrentModuleRealPath: Exception caught: {}", e.what());
				return {};
			}
		}

		std::filesystem::path GetRootRealPath()
		{
			static std::filesystem::path cachedPath = []() {
				std::filesystem::path dllPath = GetCurrentModuleRealPath();
				if (dllPath.empty())
					return std::filesystem::path{};
				return dllPath.parent_path().parent_path().parent_path();
			}();
			return cachedPath;
		}

		std::filesystem::path GetShadersRealPath()
		{
			return GetRootRealPath() / "Shaders";
		}

		std::filesystem::path GetThemesRealPath()
		{
			return GetRootRealPath() / "SKSE" / "Plugins" / "CommunityShaders" / "Themes";
		}

		std::filesystem::path GetFeaturesRealPath()
		{
			return GetShadersRealPath() / "Features";
		}

		std::filesystem::path GetFeatureIniPath(const std::string& featureName)
		{
			return GetFeaturesPath() / (featureName + ".ini");
		}

		std::filesystem::path GetFeatureShaderPath(const std::string& featureName)
		{
			return GetFeaturesPath() / featureName;
		}
	}

	// File system utilities implementation
	namespace FileHelpers
	{
		DeletionResult SafeDelete(const std::string& path, const std::string& description)
		{
			DeletionResult result;
			result.deletedDescription = description + ": " + path;

			if (path.empty() || !std::filesystem::exists(path)) {
				result.success = true;  // Consider non-existent files as successfully "deleted"
				return result;
			}

			try {
				if (std::filesystem::is_directory(path)) {
					std::filesystem::remove_all(path);
				} else {
					std::filesystem::remove(path);
				}
				result.success = true;
				logger::info("Deleted {}: {}", description, path);
			} catch (const std::filesystem::filesystem_error& e) {
				result.success = false;
				result.errorMessage = e.what();
				logger::error("Failed to delete {}: {} - {}", description, path, e.what());
			}

			return result;
		}

		void EnsureDirectoryExists(const std::filesystem::path& path)
		{
			std::error_code ec;
			std::filesystem::create_directories(path, ec);
			if (ec) {
				logger::warn("Failed to create directory '{}': {}", path.string(), ec.message());
			}
		}
	}
}

std::vector<SettingsDiffEntry> Util::FileSystem::LoadJsonDiff(const std::filesystem::path& userPath, const std::filesystem::path& testPath)
{
	std::vector<SettingsDiffEntry> diffEntries;

	try {
		if (!std::filesystem::exists(userPath) || !std::filesystem::exists(testPath)) {
			return diffEntries;
		}

		std::ifstream userFile(userPath);
		std::ifstream testFile(testPath);

		if (!userFile.is_open() || !testFile.is_open()) {
			return diffEntries;
		}

		nlohmann::json userJson, testJson;
		userFile >> userJson;
		testFile >> testJson;

		auto diff = nlohmann::json::diff(userJson, testJson);

		for (const auto& change : diff) {
			std::string op = change.value("op", "");
			std::string path = change.value("path", "");
			std::string aVal, bVal;

			if (op == "replace") {
				aVal = userJson.at(nlohmann::json::json_pointer(path)).dump();
				bVal = testJson.at(nlohmann::json::json_pointer(path)).dump();
			} else if (op == "add") {
				aVal = "(none)";
				bVal = testJson.at(nlohmann::json::json_pointer(path)).dump();
			} else if (op == "remove") {
				aVal = userJson.at(nlohmann::json::json_pointer(path)).dump();
				bVal = "(none)";
			}

			diffEntries.push_back({ path, aVal, bVal });
		}
	} catch (const std::exception& e) {
		logger::warn("Failed to load JSON diff: {}", e.what());
	}

	return diffEntries;
}
