#include "IniFileCache.h"
#include <algorithm>
#include <filesystem>

IniFileCache& IniFileCache::GetSingleton()
{
	static IniFileCache instance;
	return instance;
}

IniFile& IniFileCache::GetFile(const std::string& filePath)
{
	std::string normalizedPath = NormalizePath(filePath);

	auto it = cache.find(normalizedPath);
	if (it != cache.end()) {
		return *it->second;
	}

	// Load new file
	auto iniFile = std::make_unique<IniFile>(normalizedPath);
	auto& ref = *iniFile;
	cache[normalizedPath] = std::move(iniFile);
	return ref;
}

void IniFileCache::SaveAll()
{
	for (const auto& [path, iniFile] : cache) {
		iniFile->Save(path);
	}
}

void IniFileCache::SaveFile(const std::string& filePath)
{
	std::string normalizedPath = NormalizePath(filePath);
	auto it = cache.find(normalizedPath);
	if (it != cache.end()) {
		it->second->Save(normalizedPath);
	}
}

void IniFileCache::Clear()
{
	cache.clear();
}

void IniFileCache::RemoveFile(const std::string& filePath)
{
	std::string normalizedPath = NormalizePath(filePath);
	cache.erase(normalizedPath);
}

std::string IniFileCache::NormalizePath(const std::string& path) const
{
	try {
		std::filesystem::path fsPath(path);
		fsPath = std::filesystem::absolute(fsPath);
		std::string result = fsPath.string();

		// Convert to lowercase for consistent caching on Windows
		std::transform(result.begin(), result.end(), result.begin(), ::tolower);
		return result;
	} catch (...) {
		// If filesystem operations fail, use the original path
		std::string result = path;
		std::transform(result.begin(), result.end(), result.begin(), ::tolower);
		return result;
	}
}

namespace IniAPI
{
	std::string GetPrivateProfileString(const std::string& section, const std::string& key,
		const std::string& defaultValue, const std::string& filePath)
	{
		auto& cache = IniFileCache::GetSingleton();
		IniFile& iniFile = cache.GetFile(filePath);
		return iniFile.GetString(section, key, defaultValue);
	}

	void WritePrivateProfileString(const std::string& section, const std::string& key,
		const std::string& value, const std::string& filePath)
	{
		auto& cache = IniFileCache::GetSingleton();
		IniFile& iniFile = cache.GetFile(filePath);
		iniFile.SetString(section, key, value);
	}

	void FlushAll()
	{
		auto& cache = IniFileCache::GetSingleton();
		cache.SaveAll();
	}
}