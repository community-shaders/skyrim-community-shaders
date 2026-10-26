#include "FormEditSources.h"

#include "Presets/UnifiedPresetCatalog.h"
#include "SceneManager/SceneSettingsInternal.h"
#include "Utils/FileSystem.h"
#include "Widget.h"

#include <functional>
#include <optional>

namespace FormEditSources
{
	namespace
	{
		std::optional<FormKeySet> packKeys;

		/** @brief Visits each .json file in every save folder under @p formsRoot. */
		void ForEachFormFile(const std::filesystem::path& formsRoot,
			const std::function<void(std::string_view folder, const std::filesystem::path& file)>& visit)
		{
			for (const auto folder : Widget::kSaveFolderNames) {
				const auto directory = formsRoot / std::filesystem::path(folder);
				std::error_code ec;
				if (!std::filesystem::is_directory(directory, ec))
					continue;
				for (const auto& file : SceneSettingsInternal::GetSortedJsonFiles(directory, "form edit files"))
					visit(folder, file);
			}
		}
	}

	std::filesystem::path GetPackFormsRoot(const std::filesystem::path& packRoot)
	{
		return packRoot / kFormsSubdir;
	}

	std::filesystem::path GetFilePath(const std::filesystem::path& formsRoot, const FormKey& key)
	{
		return formsRoot / key.first / (key.second + ".json");
	}

	FormKeySet ScanKeys(const std::filesystem::path& formsRoot)
	{
		FormKeySet keys;
		ForEachFormFile(formsRoot, [&](std::string_view folder, const std::filesystem::path& file) {
			keys.emplace(std::string(folder), Util::PathHelpers::ToUtf8(file.stem()));
		});
		return keys;
	}

	std::vector<std::filesystem::path> ListPackFormFiles(const std::filesystem::path& packRoot)
	{
		std::vector<std::filesystem::path> files;
		ForEachFormFile(GetPackFormsRoot(packRoot), [&](std::string_view, const std::filesystem::path& file) {
			files.push_back(file);
		});
		return files;
	}

	bool HasPackFormFiles(const std::filesystem::path& packRoot)
	{
		return !ScanKeys(GetPackFormsRoot(packRoot)).empty();
	}

	std::filesystem::path ResolveFile(const FormKey& key)
	{
		std::error_code ec;
		if (auto userFile = GetFilePath(Util::PathHelpers::GetCommunityShaderPath(), key); std::filesystem::exists(userFile, ec))
			return userFile;
		if (!GetPackFormKeys().contains(key))
			return {};
		return GetFilePath(GetPackFormsRoot(UnifiedPresetCatalog::GetSingleton().GetActivePackRoot()), key);
	}

	const FormKeySet& GetPackFormKeys()
	{
		if (!packKeys) {
			const auto packRoot = UnifiedPresetCatalog::GetSingleton().GetActivePackRoot();
			packKeys = packRoot.empty() ? FormKeySet{} : ScanKeys(GetPackFormsRoot(packRoot));
		}
		return *packKeys;
	}

	FormKeySet Refresh()
	{
		auto changed = GetPackFormKeys();
		packKeys.reset();
		const auto& current = GetPackFormKeys();
		changed.insert(current.begin(), current.end());
		return changed;
	}

	bool HasPackFormEdits()
	{
		return !GetPackFormKeys().empty();
	}

	std::string GetActivePackName()
	{
		const auto& catalog = UnifiedPresetCatalog::GetSingleton();
		const auto* pack = catalog.FindPack(catalog.GetActivePackId());
		return pack ? pack->name : catalog.GetActivePackId();
	}

	FormKeySet GetEffectiveKeys()
	{
		auto keys = ScanKeys(Util::PathHelpers::GetCommunityShaderPath());
		const auto& pack = GetPackFormKeys();
		keys.insert(pack.begin(), pack.end());
		return keys;
	}

	bool ReadEffectiveFormEdits(std::vector<FormEdit>& out)
	{
		bool readAll = true;
		for (const auto& key : GetEffectiveKeys()) {
			const auto path = ResolveFile(key);
			if (auto edit = Util::FileHelpers::ReadJsonFile(path, "form edit"))
				out.push_back({ key, std::move(*edit) });
			else
				readAll = false;
		}
		return readAll;
	}
}
