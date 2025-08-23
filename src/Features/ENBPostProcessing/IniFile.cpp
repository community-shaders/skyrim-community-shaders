#include "IniFile.h"
#include <algorithm>
#include <fstream>
#include <sstream>

IniFile::IniFile(const std::string& filePath)
{
	Load(filePath);
}

void IniFile::Load(const std::string& filePath)
{
	Clear();
	loadedFilePath = filePath;

	if (!std::filesystem::exists(filePath)) {
		return;  // File doesn't exist, start with empty data
	}

	std::ifstream file(filePath);
	if (!file.is_open()) {
		return;
	}

	std::string line;
	std::string currentSection;

	while (std::getline(file, line)) {
		ParseLine(line, currentSection);
	}
}

void IniFile::Save(const std::string& filePath)
{
	// Create directory if it doesn't exist
	std::filesystem::create_directories(std::filesystem::path(filePath).parent_path());

	std::ofstream file(filePath);
	if (!file.is_open()) {
		return;
	}

	// Write sections in consistent order
	for (const auto& [sectionName, keyValueMap] : sections) {
		if (!keyValueMap.empty()) {
			file << "[" << sectionName << "]\n";

			// Write key-value pairs in consistent order
			for (const auto& [key, value] : keyValueMap) {
				file << key << "=" << value << "\n";
			}
			file << "\n";  // Empty line after each section
		}
	}
}

void IniFile::Save()
{
	if (!loadedFilePath.empty()) {
		Save(loadedFilePath);
	}
}

std::string IniFile::GetString(const std::string& section, const std::string& key, const std::string& defaultValue) const
{
	auto sectionIt = sections.find(section);
	if (sectionIt == sections.end()) {
		return defaultValue;
	}

	auto keyIt = sectionIt->second.find(key);
	if (keyIt == sectionIt->second.end()) {
		return defaultValue;
	}

	return keyIt->second;
}

void IniFile::SetString(const std::string& section, const std::string& key, const std::string& value)
{
	sections[section][key] = value;
}

bool IniFile::HasSection(const std::string& section) const
{
	return sections.find(section) != sections.end();
}

bool IniFile::HasKey(const std::string& section, const std::string& key) const
{
	auto sectionIt = sections.find(section);
	if (sectionIt == sections.end()) {
		return false;
	}
	return sectionIt->second.find(key) != sectionIt->second.end();
}

void IniFile::Clear()
{
	sections.clear();
	loadedFilePath.clear();
}

void IniFile::ParseLine(const std::string& line, std::string& currentSection)
{
	std::string trimmedLine = Trim(line);

	// Skip empty lines and comments
	if (trimmedLine.empty() || trimmedLine[0] == ';' || trimmedLine[0] == '#') {
		return;
	}

	// Check for section header
	if (trimmedLine[0] == '[' && trimmedLine.back() == ']') {
		currentSection = trimmedLine.substr(1, trimmedLine.length() - 2);
		currentSection = Trim(currentSection);
		return;
	}

	// Parse key=value pair
	size_t equalPos = trimmedLine.find('=');
	if (equalPos != std::string::npos && !currentSection.empty()) {
		std::string key = Trim(trimmedLine.substr(0, equalPos));
		std::string value = Trim(trimmedLine.substr(equalPos + 1));

		if (!key.empty()) {
			sections[currentSection][key] = value;
		}
	}
}

std::string IniFile::Trim(const std::string& str) const
{
	const std::string whitespace = " \t\r\n";
	size_t start = str.find_first_not_of(whitespace);
	if (start == std::string::npos) {
		return "";
	}
	size_t end = str.find_last_not_of(whitespace);
	return str.substr(start, end - start + 1);
}