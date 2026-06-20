#pragma once

#include "Utils/Format.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SIE
{

	/** @brief Tracks include dependencies between .hlsl and .hlsli files for hot-reload invalidation. */
	class ShaderFileDependencyTracker
	{
	public:
		/**
		 * @brief Records which includes a compiled .hlsl file depends on.
		 * @param hlslFile Path to the compiled .hlsl file.
		 * @param includes Paths to all files included during compilation.
		 */
		void RegisterDependencies(const std::string& hlslFile, const std::vector<std::string>& includes)
		{
			std::lock_guard lock(mutex);
			std::string normalizedHlsl = Util::FixFilePath(hlslFile);
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

		/**
		 * @brief Removes all dependency records for a .hlsl file.
		 * @param hlslFile Path to the .hlsl file being removed or recompiled.
		 */
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

		/**
		 * @brief Returns all .hlsl files that directly include the given file.
		 * @param hlsliFile Path to the include file.
		 * @return Paths of .hlsl files that depend on hlsliFile.
		 */
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

		/** @brief Removes all tracked dependencies. */
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
