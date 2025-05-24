#include "Format.h"

namespace Util
{
	std::string GetFormattedVersion(const REL::Version& version)
	{
		const auto& v = version.string(".");
		return v.substr(0, v.find_last_of("."));
	}

	std::string DefinesToString(const std::vector<std::pair<const char*, const char*>>& defines)
	{
		std::string result;
		for (const auto& def : defines) {
			if (def.first != nullptr) {
				result += def.first;
				if (def.second != nullptr && !std::string(def.second).empty()) {
					result += "=";
					result += def.second;
				}
				result += ' ';
			} else {
				break;
			}
		}
		return result;
	}

	std::string DefinesToString(const std::vector<D3D_SHADER_MACRO>& defines)
	{
		std::string result;
		for (const auto& def : defines) {
			if (def.Name != nullptr) {
				result += def.Name;
				if (def.Definition != nullptr && !std::string(def.Definition).empty()) {
					result += "=";
					result += def.Definition;
				}
				result += ' ';
			} else {
				break;
			}
		}
		return result;
	}

	std::string FixFilePath(const std::string& a_path)
	{
		std::string lowerFilePath = a_path;

		// Replace all backslashes with forward slashes
		std::replace(lowerFilePath.begin(), lowerFilePath.end(), '\\', '/');

		// Remove consecutive forward slashes
		std::string::iterator newEnd = std::unique(lowerFilePath.begin(), lowerFilePath.end(),
			[](char a, char b) { return a == '/' && b == '/'; });
		lowerFilePath.erase(newEnd, lowerFilePath.end());

		// Convert all characters to lowercase
		std::transform(lowerFilePath.begin(), lowerFilePath.end(), lowerFilePath.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		return lowerFilePath;
	}

	std::string WStringToString(const std::wstring& wideString)
	{
		std::string result;
		std::transform(wideString.begin(), wideString.end(), std::back_inserter(result), [](wchar_t c) {
			return (char)c;
		});
		return result;
	}
}  // namespace Util
