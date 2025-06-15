#include "FileSystem.h"

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
				return gamePath / "Data";
			} catch (const std::exception& e) {
				// Fallback to current_path if Windows API method fails
				logger::warn("Failed to get game path via Windows API, falling back to current_path: {}", e.what());
				return std::filesystem::current_path() / "Data";
			}
		}

		std::filesystem::path GetShadersPath()
		{
			return GetDataPath() / "Shaders";
		}

		std::filesystem::path GetFeaturesPath()
		{
			return GetShadersPath() / "Features";
		}

		std::filesystem::path GetFeatureIniPath(const std::string& featureName)
		{
			return GetFeaturesPath() / (featureName + ".ini");
		}

		std::filesystem::path GetFeatureShaderPath(const std::string& featureName)
		{
			return GetShadersPath() / featureName;
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
	}
}
