#include <fstream>
#include <iostream>
#include "FormIdParser.h"

std::string FormIdParser::trim(const std::string& str)
{
	const std::string whitespace = " \t\r\n";
	auto start = str.find_first_not_of(whitespace);
	if (start == std::string::npos) {
		return "";
	}
	auto end = str.find_last_not_of(whitespace);
	return str.substr(start, end - start + 1);
}

std::unordered_set<std::uint32_t> FormIdParser::parseHexFile(const std::string& filename)
{
	std::unordered_set<std::uint32_t> hexMap;
	std::ifstream file(filename);
	if (!file.is_open()) {
		logger::error("[FormIdParser] failed to open the file: {}", filename);
		return hexMap;
	}

	std::string line;
	size_t lineNumber = 0;

	while (std::getline(file, line)) {
		std::string trimmed = trim(line);
		if (!trimmed.empty()) {
			try {
				size_t pos = 0;
				std::uint32_t value = (std::uint32_t)std::stoull(trimmed, &pos, 16);
				if (pos != trimmed.length()) {
					logger::error("[FormIdParser] not a hexadecimal number: {}", trimmed);
				}
				hexMap.insert(value);
			} catch (const std::invalid_argument& e) {
				logger::error("[FormIdParser] invalid line: {}: {}", lineNumber, e.what());
			} catch (const std::out_of_range& e) {
				logger::error("[FormIdParser] invalid line: {}: {}", lineNumber, e.what());
			}
		}
		++lineNumber;
	}

	return hexMap;
}
