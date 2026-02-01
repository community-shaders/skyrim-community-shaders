#include "ShaderFileWatcher.h"
#include "ShaderCache.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <thread>

namespace SIE
{

	ShaderFileWatcher::ShaderFileWatcher(ShaderCache* cache_, ShaderFileDependencyTracker* deps_) :
		cache(cache_), deps(deps_) {}

	void ShaderFileWatcher::handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action, std::string oldFilename)
	{
		std::lock_guard lock(queueMutex);
		queue.push_back({ watchid, dir, filename, action, oldFilename });
	}

	void ShaderFileWatcher::processQueue()
	{
		running = true;
		while (running) {
			std::vector<FileAction> localQueue;
			{
				std::lock_guard lock(queueMutex);
				if (queue.empty()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}
				localQueue.swap(queue);
			}
			for (const auto& action : localQueue) {
				std::filesystem::path filePath = std::filesystem::path(action.dir) / action.filename;
				std::string ext = filePath.extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
				if (ext == ".hlsl") {
					cache->Clear(filePath.string());
				} else if (ext == ".hlsli") {
					// Invalidate all .hlsl files that depend on this .hlsli
					auto dependents = deps->GetDependents(filePath.string());
					for (const auto& hlsl : dependents) {
						cache->Clear(hlsl);
					}
				}
			}
		}
	}

	void ShaderFileWatcher::StartWatching(const std::string& root)
	{
		if (!fileWatcher) {
			fileWatcher = new efsw::FileWatcher();
			fileWatcher->addWatch(root, this, true);
			fileWatcher->watch();
			running = true;
			std::thread([this] { this->processQueue(); }).detach();
		}
	}

	void ShaderFileWatcher::StopWatching()
	{
		running = false;
		if (fileWatcher) {
			fileWatcher->removeWatch(watchID);
			delete fileWatcher;
			fileWatcher = nullptr;
		}
	}

}  // namespace SIE
