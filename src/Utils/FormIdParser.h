#pragma once

#include <string>
#include <unordered_set>

struct FormIdParser
{
	static std::string trim(const std::string& str);
	// Parses a text file of numbers in hexadecimal format into a set. One number per line. A # symbol can be used for one-line comments.
	static std::unordered_set<std::uint32_t> parseHexFile(const std::filesystem::path&);
	// Parses a text file of any line of text without surrounding whitespace into a set. Used to parse Trishape node names. A # symbol can be used for one-line comments.
	static std::unordered_set<std::uint64_t> parseTriNameFile(const std::filesystem::path&);
	static std::uint64_t fnv_hash(const char* key);
};