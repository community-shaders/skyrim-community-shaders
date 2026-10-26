#pragma once

#include "Utils/FileSystem.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class PresetExportTransaction
{
public:
	explicit PresetExportTransaction(std::filesystem::path destination) :
		target(std::move(destination)) {}
	PresetExportTransaction(const PresetExportTransaction&) = delete;
	PresetExportTransaction& operator=(const PresetExportTransaction&) = delete;

	~PresetExportTransaction()
	{
		if (targetTouched)
			Rollback();
		if (workspace.empty())
			return;
		std::error_code ec;
		if (!preserveBackup)
			std::filesystem::remove_all(workspace, ec);
		else
			std::ofstream(workspace / kRetainedMarker).put('\n');
	}

	bool Prepare()
	{
		try {
			if (std::filesystem::is_symlink(target))
				return false;
			const auto tempRoot = std::filesystem::temp_directory_path();
			RemoveStaleWorkspaces(tempRoot);
			static std::atomic_uint64_t sequence{ 0 };
			const auto candidate = tempRoot / std::format("{}{:x}_{:x}_{:x}", kWorkspacePrefix,
												  ::GetCurrentProcessId(), std::chrono::steady_clock::now().time_since_epoch().count(), sequence++);
			if (!std::filesystem::create_directory(candidate))
				return false;
			workspace = candidate;
			staged = workspace / "staged" / target.filename();
			backup = workspace / "backup";
			std::filesystem::create_directories(staged);
			std::filesystem::create_directory(backup);
			targetExisted = std::filesystem::exists(target);
			if (targetExisted) {
				for (const auto& entry : std::filesystem::recursive_directory_iterator(target))
					if (entry.is_directory())
						originalDirectories.insert(entry.path().lexically_relative(target));
				const auto files = ListFiles(target);
				for (const auto& relative : files) {
					std::filesystem::create_directories((backup / relative).parent_path());
					std::filesystem::copy_file(target / relative, backup / relative);
					std::filesystem::create_directories((staged / relative).parent_path());
					std::filesystem::copy_file(backup / relative, staged / relative);
				}
			}
			return true;
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Could not stage preset '{}': {}", Util::PathHelpers::ToUtf8(target), e.what());
			return false;
		}
	}

	const std::filesystem::path& Root() const { return staged; }

	void TrackExternalWrites() { targetTouched = true; }

	bool Commit()
	{
		try {
			auto files = ListFiles(staged);
			files.merge(ListFiles(backup));
			for (const auto& relative : files) {
				const bool existed = std::filesystem::exists(backup / relative);
				const bool exists = std::filesystem::exists(staged / relative);
				const auto contents = exists ? Read(staged / relative) : std::string{};
				if (existed && exists && Read(backup / relative) == contents)
					continue;
				targetTouched = true;
				if (exists) {
					std::filesystem::create_directories((target / relative).parent_path());
					if (!Util::FileHelpers::WriteFileAtomically(target / relative, contents, "preset export"))
						throw std::runtime_error("Could not replace " + Util::PathHelpers::ToUtf8(relative));
				} else {
					std::filesystem::remove(target / relative);
				}
			}
			targetTouched = false;
			return true;
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Preset '{}' commit failed: {}", Util::PathHelpers::ToUtf8(target), e.what());
		}
		if (targetTouched)
			Rollback();
		return false;
	}

private:
	void Rollback()
	{
		targetTouched = false;
		try {
			auto files = ListFiles(backup);
			if (std::filesystem::exists(target))
				files.merge(ListFiles(target));
			for (const auto& relative : files) {
				try {
					if (std::filesystem::exists(backup / relative)) {
						const auto original = Read(backup / relative);
						if (std::filesystem::exists(target / relative) && Read(target / relative) == original)
							continue;
						std::filesystem::create_directories((target / relative).parent_path());
						if (!Util::FileHelpers::WriteFileAtomically(target / relative, original, "preset rollback"))
							throw std::runtime_error("Could not restore " + Util::PathHelpers::ToUtf8(relative));
					} else {
						std::filesystem::remove(target / relative);
					}
				} catch (const std::exception& e) {
					preserveBackup = true;
					logger::error("[SceneSettings] Preset rollback failed: {}. Backup retained at '{}'", e.what(), Util::PathHelpers::ToUtf8(backup));
				}
			}
		} catch (const std::exception& e) {
			preserveBackup = true;
			logger::error("[SceneSettings] Could not enumerate preset rollback files: {}. Backup retained at '{}'", e.what(), Util::PathHelpers::ToUtf8(backup));
		}
		RemoveCreatedDirectories();
	}

	/** @brief Removes the directories the commit created, which the rollback left empty. */
	void RemoveCreatedDirectories() const
	{
		try {
			std::error_code ec;
			std::vector<std::filesystem::path> created;
			if (std::filesystem::exists(target))
				for (const auto& entry : std::filesystem::recursive_directory_iterator(target))
					if (entry.is_directory() && !originalDirectories.contains(entry.path().lexically_relative(target)))
						created.push_back(entry.path());
			// Deepest first, so each parent is empty by the time it is reached.
			std::ranges::sort(created, std::greater<>{});
			for (const auto& directory : created)
				if (std::filesystem::is_empty(directory, ec))
					std::filesystem::remove(directory, ec);
			if (!targetExisted && std::filesystem::is_empty(target, ec))
				std::filesystem::remove(target, ec);
		} catch (const std::exception& e) {
			logger::warn("[SceneSettings] Could not tidy '{}' after a rollback: {}", Util::PathHelpers::ToUtf8(target), e.what());
		}
	}

	/** @brief Deletes workspaces a crashed export left in the temp folder, keeping any that hold a retained backup. */
	static void RemoveStaleWorkspaces(const std::filesystem::path& tempRoot)
	{
		// This process's own workspaces may still be in use.
		const auto ownPrefix = std::format("{}{:x}_", kWorkspacePrefix, ::GetCurrentProcessId());
		try {
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(tempRoot)) {
				const auto name = Util::PathHelpers::ToUtf8(entry.path().filename());
				if (name.starts_with(kWorkspacePrefix) && !name.starts_with(ownPrefix) &&
					!std::filesystem::exists(entry.path() / kRetainedMarker, ec))
					std::filesystem::remove_all(entry.path(), ec);
			}
		} catch (const std::exception& e) {
			logger::warn("[SceneSettings] Could not clear stale preset export workspaces: {}", e.what());
		}
	}

	static std::set<std::filesystem::path> ListFiles(const std::filesystem::path& root)
	{
		std::set<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
			if (entry.is_symlink())
				throw std::runtime_error("Preset contains a symbolic link: " + Util::PathHelpers::ToUtf8(entry.path()));
			if (entry.is_regular_file())
				files.insert(entry.path().lexically_relative(root));
		}
		return files;
	}

	static std::string Read(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			throw std::runtime_error("Could not read " + Util::PathHelpers::ToUtf8(path));
		std::string contents{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		if (file.bad())
			throw std::runtime_error("Could not read " + Util::PathHelpers::ToUtf8(path));
		return contents;
	}

	static constexpr std::string_view kWorkspacePrefix = "cspex_";
	/// Marks a workspace whose backup a failed rollback still needs, so later exports leave it alone.
	static constexpr const char* kRetainedMarker = "backup-retained";

	std::filesystem::path target;
	std::filesystem::path workspace;
	std::filesystem::path staged;
	std::filesystem::path backup;
	/// Directories under the target before the export, relative to it; the rest are the commit's.
	std::set<std::filesystem::path> originalDirectories;
	bool targetExisted = false;
	bool preserveBackup = false;
	bool targetTouched = false;
};
