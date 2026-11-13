#include "PackManifest.h"

using json = nlohmann::json;

namespace
{
	std::string ReadString(const json& manifest, const char* key)
	{
		const auto it = manifest.find(key);
		return it != manifest.end() && it->is_string() ? it->get<std::string>() : std::string{};
	}

	std::vector<std::string> ReadStrings(const json& manifest, const char* key)
	{
		std::vector<std::string> values;
		if (const auto it = manifest.find(key); it != manifest.end() && it->is_array()) {
			for (const auto& value : *it) {
				if (value.is_string())
					values.push_back(value.get<std::string>());
			}
		}
		return values;
	}

	template <class T>
	void SetOrErase(json& manifest, const char* key, const T& value)
	{
		if (value.empty())
			manifest.erase(key);
		else
			manifest[key] = value;
	}
}

PackManifest PackManifest::Read(const json& manifest, std::string_view fallbackName)
{
	assert(manifest.is_object());
	PackManifest result{ .name = ReadString(manifest, kNameKey),
		.version = ReadString(manifest, kVersionKey),
		.author = ReadString(manifest, kAuthorKey),
		.description = ReadString(manifest, kDescriptionKey),
		.nexusUrl = ReadString(manifest, kNexusUrlKey),
		.tags = ReadStrings(manifest, kTagsKey),
		.csVersion = ReadString(manifest, kCsVersionKey),
		.requiredFeatures = ReadStrings(manifest, kRequiredFeaturesKey),
		.requiredPlugins = ReadStrings(manifest, kRequiredPluginsKey),
		.logo = ReadString(manifest, kLogoKey),
		.cover = ReadString(manifest, kCoverKey),
		.screenshots = ReadStrings(manifest, kScreenshotsKey) };
	if (result.name.empty())
		result.name = fallbackName;
	return result;
}

void PackManifest::WriteDetails(json& manifest) const
{
	SetOrErase(manifest, kVersionKey, version);
	SetOrErase(manifest, kAuthorKey, author);
	SetOrErase(manifest, kDescriptionKey, description);
	SetOrErase(manifest, kTagsKey, tags);
	SetOrErase(manifest, kCsVersionKey, csVersion);
	SetOrErase(manifest, kRequiredFeaturesKey, requiredFeatures);
	SetOrErase(manifest, kRequiredPluginsKey, requiredPlugins);
}
