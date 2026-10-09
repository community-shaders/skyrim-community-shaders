#pragma once

#include "Utils/ContentHash.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

/// Persistent store of compiled shader blobs, addressed by Util::CompileDedupe::MakeKey (a hash of the
/// preprocessed source plus everything else that changes the bytecode).
/// Lives in Data/ShaderCache/ContentStore, which cache invalidation leaves in place.
namespace Util::ShaderContentStore
{
	/// Subdirectory of Data/ShaderCache holding the store; cache invalidation leaves it in place.
	inline constexpr std::wstring_view kDirName = L"ContentStore";

	/// How many blobs a store holds and how much disk they use.
	struct Usage
	{
		uint64_t blobs = 0;
		uint64_t bytes = 0;
	};

	/// Totals the blobs under a store root without opening the store.
	inline Usage MeasureUsage(const std::filesystem::path& a_root)
	{
		Usage usage;
		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(a_root, ec), end; !ec && it != end; it.increment(ec)) {
			if (!it->is_regular_file(ec) || it->path().extension() != ".bin")
				continue;
			const auto size = it->file_size(ec);
			if (ec)
				continue;
			++usage.blobs;
			usage.bytes += size;
		}
		return usage;
	}

	/// Sharded on-disk blob store addressed by a CompileDedupe key; safe for concurrent use by compile threads.
	class Store
	{
	public:
		/// @param a_maxBytes Size cap enforced by Put once a sixteenth of it has been written since the last trim; 0 disables.
		explicit Store(std::filesystem::path a_root, uint64_t a_maxBytes = 0) :
			root(std::move(a_root)), maxBytes(a_maxBytes) {}

		/// Changes the cap Put enforces; call Trim to apply a lower one immediately.
		void SetMaxBytes(uint64_t a_maxBytes) { maxBytes.store(a_maxBytes, std::memory_order_relaxed); }

		/// On-disk location of the blob for a key.
		std::filesystem::path PathFor(const ContentHash::Hash128& a_key) const
		{
			const auto hex = a_key.ToHex();
			return root / hex.substr(0, 2) / (hex + ".bin");
		}

		/// Returns the stored blob, or an empty vector on a miss or unreadable entry. A hit counts as a use for Trim.
		std::vector<char> Get(const ContentHash::Hash128& a_key) const
		{
			const auto path = PathFor(a_key);
			std::vector<char> bytes;
			{
				std::ifstream ifs(path, std::ios::binary);
				if (!ifs.is_open())
					return {};
				bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
				if (ifs.bad())
					return {};
			}
			Touch(a_key);
			return bytes;
		}

		/// Marks a blob as just used, so Trim evicts it last.
		void Touch(const ContentHash::Hash128& a_key) const
		{
			std::error_code ec;
			std::filesystem::last_write_time(PathFor(a_key), std::filesystem::file_time_type::clock::now(), ec);
		}

		/// Writes via a temp file and rename so a reader never sees a partial blob.
		bool Put(const ContentHash::Hash128& a_key, const void* a_data, size_t a_size) const
		{
			if (!a_data || a_size == 0)
				return false;
			const auto path = PathFor(a_key);
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			if (ec)
				return false;
			// Concurrent compile threads can finish identical shaders, so each write needs its own temp name.
			static std::atomic<uint64_t> tempCounter{ 0 };
			auto tmp = path;
			tmp += std::format(".{}.tmp", tempCounter.fetch_add(1, std::memory_order_relaxed));
			{
				std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
				if (!ofs.is_open())
					return false;
				ofs.write(static_cast<const char*>(a_data), static_cast<std::streamsize>(a_size));
				if (!ofs.good()) {
					ofs.close();
					std::filesystem::remove(tmp, ec);
					return false;
				}
			}
			std::filesystem::rename(tmp, path, ec);
			if (ec) {
				std::filesystem::remove(tmp, ec);
				return false;
			}
			// A writer that finds another trim running skips this one; the next write past the threshold retries.
			const auto cap = maxBytes.load(std::memory_order_relaxed);
			if (cap && (bytesSinceTrim += a_size) >= cap / 16 && trimMutex.try_lock()) {
				std::scoped_lock lock(std::adopt_lock, trimMutex);
				bytesSinceTrim = 0;
				TrimLocked(cap, false, nullptr);
			}
			return true;
		}

		/// Deletes every stored blob; later Put calls recreate the directories.
		bool Clear(std::error_code& a_ec) const
		{
			std::scoped_lock lock(trimMutex);
			bytesSinceTrim = 0;
			std::filesystem::remove_all(root, a_ec);
			return !a_ec;
		}

		/**
		 * @brief Evicts least-recently-used entries until the store is at most a_maxBytes, in one directory walk.
		 * @param a_removeTemps Also delete temp files of unfinished writes; only safe while no Put is running.
		 * @param a_remaining Receives what the store holds afterwards.
		 * @return The number of entries removed.
		 */
		size_t Trim(uint64_t a_maxBytes, bool a_removeTemps = false, Usage* a_remaining = nullptr) const
		{
			std::scoped_lock lock(trimMutex);
			return TrimLocked(a_maxBytes, a_removeTemps, a_remaining);
		}

	private:
		size_t TrimLocked(uint64_t a_maxBytes, bool a_removeTemps, Usage* a_remaining) const
		{
			struct Entry
			{
				std::filesystem::path path;
				uint64_t size;
				std::filesystem::file_time_type time;
			};
			std::vector<Entry> entries;
			std::vector<std::filesystem::path> temps;
			uint64_t total = 0;
			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec))
					continue;
				const auto extension = it->path().extension();
				if (a_removeTemps && extension == ".tmp") {
					temps.push_back(it->path());
					continue;
				}
				if (extension != ".bin")
					continue;
				const auto size = it->file_size(ec);
				const auto time = it->last_write_time(ec);
				if (ec)
					continue;
				entries.push_back({ it->path(), size, time });
				total += size;
			}
			for (const auto& temp : temps)
				std::filesystem::remove(temp, ec);

			size_t removed = 0;
			uint64_t remainingBlobs = entries.size();
			if (total > a_maxBytes) {
				std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
				for (const auto& e : entries) {
					if (total <= a_maxBytes)
						break;
					const bool deleted = std::filesystem::remove(e.path, ec);
					// A file that is already gone frees its space too; one still open (being read) is skipped.
					if (!ec) {
						total -= e.size;
						--remainingBlobs;
						removed += deleted ? 1 : 0;
					}
				}
			}
			if (a_remaining)
				*a_remaining = { remainingBlobs, total };
			return removed;
		}

		std::filesystem::path root;
		std::atomic<uint64_t> maxBytes;
		mutable std::atomic<uint64_t> bytesSinceTrim{ 0 };
		mutable std::mutex trimMutex;
	};
}
