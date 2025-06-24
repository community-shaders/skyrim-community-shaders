// string and printing related helpers

#pragma once

namespace Util
{
	std::string GetFormattedVersion(const REL::Version& version);
	/**
	 * @brief Gets the minimum required version for a feature.
	 *
	 * This function looks up the minimum required version for a feature
	 * from FeatureVersions::FEATURE_MINIMAL_VERSIONS and returns it as a
	 * formatted string. Returns "unknown" if the feature is not found.
	 *
	 * @param shortName The short name of the feature.
	 * @return The formatted minimum required version string, or "unknown" if not found.
	 */
	std::string GetFeatureRequiredVersion(const std::string& shortName);

	/**
	 * @brief Checks if a feature has a minimum required version defined.
	 *
	 * This function checks if a feature exists in the FeatureVersions::FEATURE_MINIMAL_VERSIONS
	 * map and optionally returns the version.
	 *
	 * @param shortName The short name of the feature.
	 * @param outVersion Pointer to REL::Version to store the version if found (optional).
	 * @return True if the feature is found, false otherwise.
	 */
	
	bool IsFeatureKnown(const std::string& shortName, REL::Version* outVersion = nullptr);

	std::string DefinesToString(const std::vector<std::pair<const char*, const char*>>& defines);
	std::string DefinesToString(const std::vector<D3D_SHADER_MACRO>& defines);

	/**
	 * @brief Normalizes a file path by replacing backslashes with forward slashes,
	 *        removing redundant slashes, and converting all characters to lowercase.
	 *
	 * This function ensures that the file path uses consistent forward slashes
	 * (`/`), eliminates consecutive slashes (`//`), and converts all characters
	 * in the path to lowercase for case-insensitive comparisons.
	 *
	 * @param a_path The original file path to be normalized.
	 * @return A normalized file path as a lowercase string with single forward slashes.
	 */
	std::string FixFilePath(const std::string& a_path);
	std::string WStringToString(const std::wstring& wideString);
}  // namespace Util