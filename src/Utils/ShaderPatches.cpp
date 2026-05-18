#include "ShaderPatches.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace Util::ShaderPatches
{
	static std::vector<Entry> entries;
	static bool loaded = false;

	void Load()
	{
		entries.clear();
		loaded = true;

		std::filesystem::path path = "Data\\Shaders\\Effect11\\ShaderPatches.json";
		std::ifstream ifs(path);
		if (!ifs.is_open())
			return;

		try {
			nlohmann::json root = nlohmann::json::parse(ifs);
			for (auto& item : root) {
				Entry entry;
				entry.file = item.at("file").get<std::string>();
				for (auto& r : item.at("patches")) {
					Replacement rep;
					rep.find = r.at("find").get<std::string>();
					rep.replace = r.at("replace").get<std::string>();
					entry.replacements.push_back(std::move(rep));
				}
				entries.push_back(std::move(entry));
			}
		} catch (const std::exception& e) {
			logger::error("[ShaderPatches] Failed to parse {}: {}", path.string(), e.what());
		}

		if (!entries.empty())
			logger::info("[ShaderPatches] Loaded {} entries", entries.size());
	}

	static bool FilenameMatches(const char* includePath, const std::string& pattern)
	{
		std::string path(includePath);
		for (auto& c : path)
			if (c == '/')
				c = '\\';

		std::string pat = pattern;
		for (auto& c : pat)
			if (c == '/')
				c = '\\';

		if (pat.size() > path.size())
			return false;

		auto pathEnd = path.end();
		auto pathStart = pathEnd - static_cast<ptrdiff_t>(pat.size());
		if (pathStart != path.begin() && *(pathStart - 1) != '\\')
			return false;

		return std::equal(pathStart, pathEnd, pat.begin(), pat.end(), [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
	}

	bool Apply(const char* filename, std::string& content)
	{
		if (!loaded)
			Load();

		bool modified = false;
		for (auto& entry : entries) {
			if (!FilenameMatches(filename, entry.file))
				continue;
			for (auto& rep : entry.replacements) {
				size_t pos = 0;
				while ((pos = content.find(rep.find, pos)) != std::string::npos) {
					content.replace(pos, rep.find.size(), rep.replace);
					pos += rep.replace.size();
					modified = true;
				}
			}
		}

		if (modified)
			logger::debug("[ShaderPatches] Patched {}", filename);

		return modified;
	}

	bool Apply(const char* filename, std::vector<char>& buffer)
	{
		std::string content(buffer.begin(), buffer.end());
		if (Apply(filename, content)) {
			buffer.assign(content.begin(), content.end());
			return true;
		}
		return false;
	}
}
