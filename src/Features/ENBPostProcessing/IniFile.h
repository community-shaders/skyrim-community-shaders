#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

class IniFile
{
public:
	IniFile() = default;
	explicit IniFile(const std::string& filePath);

	// Load from file
	void Load(const std::string& filePath);

	// Save to file
	void Save(const std::string& filePath);
	void Save();  // Save to the file it was loaded from

	// Get value (returns defaultValue if not found)
	std::string GetString(const std::string& section, const std::string& key, const std::string& defaultValue = "") const;

	// Set value
	void SetString(const std::string& section, const std::string& key, const std::string& value);

	// Check if section or key exists
	bool HasSection(const std::string& section) const;
	bool HasKey(const std::string& section, const std::string& key) const;

	// Clear all data
	void Clear();

private:
	using KeyValueMap = std::unordered_map<std::string, std::string>;
	using SectionMap = std::unordered_map<std::string, KeyValueMap>;

	SectionMap sections;
	std::string loadedFilePath;

	void ParseLine(const std::string& line, std::string& currentSection);
	std::string Trim(const std::string& str) const;
};