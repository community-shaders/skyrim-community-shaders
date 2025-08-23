#pragma once

#include "IniFile.h"
#include <future>
#include <memory>
#include <unordered_map>
#include <vector>

class IniFileCache
{
public:
	static IniFileCache& GetSingleton();

	// Get cached INI file (loads if not cached)
	IniFile& GetFile(const std::string& filePath);

	// Save all cached files
	void SaveAll();

	// Save specific file
	void SaveFile(const std::string& filePath);

	// Clear cache
	void Clear();

	// Remove specific file from cache
	void RemoveFile(const std::string& filePath);

private:
	std::unordered_map<std::string, std::unique_ptr<IniFile>> cache;

	std::string NormalizePath(const std::string& path) const;
};

// Convenience functions that match the Windows API but use our cache
namespace IniAPI
{
	std::string GetPrivateProfileString(const std::string& section, const std::string& key,
		const std::string& defaultValue, const std::string& filePath);

	void WritePrivateProfileString(const std::string& section, const std::string& key,
		const std::string& value, const std::string& filePath);

	// Batch save all cached files (call this after multiple writes)
	void FlushAll();
}