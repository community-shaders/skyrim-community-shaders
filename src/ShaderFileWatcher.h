#pragma once

#include "Utils/Format.h"
#include <efsw/efsw.hpp>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SIE
{

	// Forward declaration
	template <typename T>
	class ThreadPool;
	class ShaderCache;

	// Dependency tracking: .hlsl/.hlsli relationships
	class ShaderFileDependencyTracker
	{
	public:
		// Map: .hlsl file -> set of included files (usually .hlsli)
		std::unordered_map<std::string, std::unordered_set<std::string>> hlslToIncludes;
		// Map: .hlsli file -> set of dependent .hlsl files
		std::unordered_map<std::string, std::unordered_set<std::string>> hlsliToHlsl;
		std::mutex mutex;

		// Called after compiling a .hlsl file, with the set of includes used
		void RegisterDependencies(const std::string& hlslFile, const std::vector<std::string>& includes)
		{
			std::lock_guard lock(mutex);
			// Normalize paths
			std::string normalizedHlsl = Util::FixFilePath(hlslFile);
			// Remove any previous dependencies for this hlslFile
			auto it = hlslToIncludes.find(normalizedHlsl);
			if (it != hlslToIncludes.end()) {
				for (const auto& oldInc : it->second) {
					hlsliToHlsl[oldInc].erase(normalizedHlsl);
					if (hlsliToHlsl[oldInc].empty())
						hlsliToHlsl.erase(oldInc);
				}
			}
			hlslToIncludes[normalizedHlsl].clear();
			for (const auto& inc : includes) {
				std::string normalizedInc = Util::FixFilePath(inc);
				hlslToIncludes[normalizedHlsl].insert(normalizedInc);
				hlsliToHlsl[normalizedInc].insert(normalizedHlsl);
			}
		}

		// Called when a .hlsl file is deleted or recompiled (to clean up)
		void UnregisterDependencies(const std::string& hlslFile)
		{
			std::lock_guard lock(mutex);
			std::string normalizedHlsl = Util::FixFilePath(hlslFile);
			auto it = hlslToIncludes.find(normalizedHlsl);
			if (it != hlslToIncludes.end()) {
				for (const auto& inc : it->second) {
					hlsliToHlsl[inc].erase(normalizedHlsl);
					if (hlsliToHlsl[inc].empty())
						hlsliToHlsl.erase(inc);
				}
				hlslToIncludes.erase(it);
			}
		}

		// Get all .hlsl files that depend on a given .hlsli
		std::vector<std::string> GetDependents(const std::string& hlsliFile)
		{
			std::lock_guard lock(mutex);
			std::vector<std::string> result;
			std::string normalizedInc = Util::FixFilePath(hlsliFile);
			auto it = hlsliToHlsl.find(normalizedInc);
			if (it != hlsliToHlsl.end()) {
				result.assign(it->second.begin(), it->second.end());
			}
			return result;
		}

		void Clear()
		{
			std::lock_guard lock(mutex);
			hlslToIncludes.clear();
			hlsliToHlsl.clear();
		}
	};

	// Filewatcher listener for shaders
	class ShaderFileWatcher : public efsw::FileWatchListener
	{
	public:
		ShaderFileWatcher(ShaderCache* cache, ShaderFileDependencyTracker* deps);
		void handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action, std::string oldFilename) override;
		void processQueue();
		void StartWatching(const std::string& root);
		void StopWatching();

	private:
		ShaderCache* cache;
		ShaderFileDependencyTracker* deps;
		efsw::FileWatcher* fileWatcher = nullptr;
		efsw::WatchID watchID = 0;
		std::mutex queueMutex;
		struct FileAction
		{
			efsw::WatchID watchid;
			std::string dir;
			std::string filename;
			efsw::Action action;
			std::string oldFilename;
		};
		std::vector<FileAction> queue;
		bool running = false;
	};

}  // namespace SIE
