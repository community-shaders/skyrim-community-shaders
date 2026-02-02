#pragma once

#include "Utils/Format.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SIE
{

	// Dependency tracking: .hlsl/.hlsli relationships
	class ShaderFileDependencyTracker
	{
	public:
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

	private:
		// Map: .hlsl file -> set of included files (usually .hlsli)
		std::unordered_map<std::string, std::unordered_set<std::string>> hlslToIncludes;
		// Map: .hlsli file -> set of dependent .hlsl files
		std::unordered_map<std::string, std::unordered_set<std::string>> hlsliToHlsl;
		std::mutex mutex;
	};

}  // namespace SIE
