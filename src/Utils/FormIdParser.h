#pragma once


#include <string>
#include <unordered_set>

struct FormIdParser {
	static std::string trim(const std::string& str);
	static std::unordered_set<std::uint32_t> parseHexFile(const std::string& filename);
};