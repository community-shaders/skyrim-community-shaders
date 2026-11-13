#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

/** @brief Resolves CS Editor form edit files between the user's folders and the active preset pack; the user file wins. */
namespace FormEditSources
{
	/** @brief Pack subfolder mirroring the user's form edit folders. */
	constexpr const char* kFormsSubdir = "Forms";

	/** @brief A form edit's save folder and save key. */
	using FormKey = std::pair<std::string, std::string>;
	using FormKeySet = std::set<FormKey>;

	/** @brief A parsed form edit, ready to be written into a pack. */
	struct FormEdit
	{
		FormKey key;
		nlohmann::json content;
	};

	/** @brief A pack's Forms folder. */
	std::filesystem::path GetPackFormsRoot(const std::filesystem::path& packRoot);
	/** @brief A form edit file under a forms root: the user's CommunityShaders folder or a pack's Forms folder. */
	std::filesystem::path GetFilePath(const std::filesystem::path& formsRoot, const FormKey& key);

	/** @brief The keys of every form edit file under a forms root. */
	FormKeySet ScanKeys(const std::filesystem::path& formsRoot);
	/** @brief Every form edit file a pack ships. */
	std::vector<std::filesystem::path> ListPackFormFiles(const std::filesystem::path& packRoot);
	/** @brief Whether a pack ships any form edit file. */
	bool HasPackFormFiles(const std::filesystem::path& packRoot);

	/** @brief The user file when it exists, else the active pack's file, else an empty path. */
	std::filesystem::path ResolveFile(const FormKey& key);
	/** @brief The active pack's form edit keys, scanned once and cached until Refresh. */
	const FormKeySet& GetPackFormKeys();
	/** @brief Rescans the active pack.
	 *  @return The previous and new pack keys combined: every form whose source may have changed. */
	FormKeySet Refresh();
	/** @brief Whether the active pack ships any form edit. */
	bool HasPackFormEdits();
	/** @brief The active pack's display name, falling back to its id before the catalog has discovered it. */
	std::string GetActivePackName();

	/** @brief The user's form edit keys plus the active pack's. */
	FormKeySet GetEffectiveKeys();
	/** @brief Parses every effective form edit through ResolveFile.
	 *  @return False when any source could not be read; the readable ones are still appended to @p out. */
	bool ReadEffectiveFormEdits(std::vector<FormEdit>& out);
}
