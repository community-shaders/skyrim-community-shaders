#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

/** @brief The descriptive fields of a unified pack manifest, shared by the Presets catalog that reads them and the
 *  CS Editor's preset export that writes them. Artwork paths are relative to the pack folder. */
struct PackManifest
{
	static constexpr const char* kNameKey = "name";
	static constexpr const char* kVersionKey = "version";
	static constexpr const char* kAuthorKey = "author";
	static constexpr const char* kDescriptionKey = "description";
	static constexpr const char* kNexusUrlKey = "nexusUrl";
	static constexpr const char* kTagsKey = "tags";
	static constexpr const char* kCsVersionKey = "csVersion";
	static constexpr const char* kRequiredFeaturesKey = "requiredFeatures";
	static constexpr const char* kRequiredPluginsKey = "requiredPlugins";
	static constexpr const char* kLogoKey = "logo";
	static constexpr const char* kCoverKey = "cover";
	static constexpr const char* kScreenshotsKey = "screenshots";

	/// Version a new pack starts at.
	static constexpr const char* kDefaultVersion = "1.0.0";

	std::string name;
	std::string version;
	std::string author;
	std::string description;
	/// Shown as a link when non-empty.
	std::string nexusUrl;
	std::vector<std::string> tags;
	/// Community Shaders version the pack was made for (MAJOR.MINOR.PATCH); empty = unspecified.
	std::string csVersion;
	/// Feature short names the pack expects loaded; missing ones surface as warnings in the browser.
	std::vector<std::string> requiredFeatures;
	/// Plugin file names the pack was made with; missing ones surface as soft warnings in the browser.
	std::vector<std::string> requiredPlugins;
	std::string logo;
	std::string cover;
	std::vector<std::string> screenshots;

	/**
	 * @brief Reads the fields from a manifest object; absent or mistyped fields stay empty.
	 * @param fallbackName Name used when the manifest has none, normally the pack folder name.
	 */
	static PackManifest Read(const nlohmann::json& manifest, std::string_view fallbackName);

	/** @brief Writes version, author, description, tags, csVersion and the requirements, erasing empty ones.
	 *  Name, nexusUrl and artwork are left to the caller, which decides how they merge with what the file holds. */
	void WriteDetails(nlohmann::json& manifest) const;
};
