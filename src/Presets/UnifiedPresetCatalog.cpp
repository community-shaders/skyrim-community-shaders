#include "UnifiedPresetCatalog.h"
#include "PCH.h"

#include "BaselineManager.h"
#include "CSEditor/FormEditSources.h"
#include "CSEditor/SceneManager/SceneManager.h"
#include "CSEditor/SceneManager/SceneSettingsManager.h"
#include "CSEditor/WeatherUtils.h"
#include "Feature.h"
#include "Features/CSEditor.h"
#include "Features/Effects11.h"
#include "Features/Effects11/PresetManager.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "PostProcessingMode.h"
#include "Presets/PostProcessingPresets.h"
#include "Presets/PresetCompatibility.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <span>
#include <stb_image.h>
#include <vector>

using json = nlohmann::json;

namespace
{
	/// Where older builds kept the active pack state, read once if SettingsUser.json has none yet.
	constexpr const char* kLegacyActiveStateFileName = "_active.json";
	/// Matches State::Save, so a state write leaves the file formatted as the user config always is.
	constexpr int kUserSettingsIndent = 1;
	constexpr const char* kBaselinePackIdsKey = "baselinePackIds";
	constexpr const char* kLegacyMetaFileName = "preset.json";
	constexpr const char* kEffects11PackSubdir = UnifiedPresetCatalog::kEffects11PackSubdir;
	// The colon keeps it out of the pack folder namespace, like orphan "e11:" ids.
	constexpr const char* kEffects11LegacyPackId = "legacy:effects11";
	constexpr std::string_view kEffects11OrphanPrefix = "e11:";
	constexpr size_t kMaxDescriptionLength = 400;
	constexpr std::string_view kTruncationSuffix = "...";
	constexpr size_t kMaxScreenshots = 12;
	constexpr int kRgbaChannels = 4;

	using Util::ToLower;

	constexpr const char* kPipelineKey = "pipeline";
	constexpr const char* kPipelineRevisionKey = "pipelineRevision";

	/** @brief The Effects 11 preset id a pack switches to, read off the form of its id. */
	std::string GetEffects11PresetId(const std::string& packId)
	{
		if (packId == kEffects11LegacyPackId)
			return PresetManager::kLegacyPresetId;
		if (packId.starts_with(kEffects11OrphanPrefix))
			return packId.substr(kEffects11OrphanPrefix.size());
		return PresetManager::MakeUnifiedPackPresetId(packId);
	}

	std::filesystem::path ResolveRelative(const std::filesystem::path& root, const std::string& rel)
	{
		if (rel.empty())
			return {};
		auto path = root / Util::PathHelpers::FromUtf8(rel);
		return std::filesystem::exists(path) ? path : std::filesystem::path{};
	}

	/** @brief The first candidate that exists under root, or an empty path. */
	std::filesystem::path FindFirstExisting(const std::filesystem::path& root, std::span<const char* const> candidates)
	{
		for (const char* name : candidates) {
			if (auto path = ResolveRelative(root, name); !path.empty())
				return path;
		}
		return {};
	}

	/** @brief The manifest's declared preset type, matched case-insensitively; null when absent or unrecognized. */
	std::optional<UnifiedPresetCatalog::PresetType> ReadPresetType(const json& manifest, const std::string& packId)
	{
		using PresetType = UnifiedPresetCatalog::PresetType;
		const auto it = manifest.find(UnifiedPresetCatalog::kPresetTypeKey);
		if (it == manifest.end())
			return std::nullopt;
		if (it->is_string()) {
			for (auto type : { PresetType::CS, PresetType::E11, PresetType::Baseline })
				if (Util::IEquals(it->get_ref<const std::string&>(), UnifiedPresetCatalog::GetPresetTypeName(type)))
					return type;
		}
		logger::warn("[Presets] Pack '{}' has unknown type '{}'; grouping by detected payloads", packId, it->dump());
		return std::nullopt;
	}

	/** @brief Whether an id from persisted state is a plain pack folder name, never a path or an orphan id. */
	bool IsPackFolderId(const std::string& id)
	{
		return !id.empty() && id != "." && id != ".." && id.find_first_of("/\\:") == std::string::npos;
	}

	/** @brief A pack folder, the Legacy Effects 11 install, or an orphan Effects 11 preset. */
	bool IsActivePackId(const std::string& id)
	{
		return IsPackFolderId(id) || id == kEffects11LegacyPackId ||
		       (id.starts_with(kEffects11OrphanPrefix) && IsPackFolderId(id.substr(kEffects11OrphanPrefix.size())));
	}

	/** @brief Feature short names set by a pack's Baseline folder, sorted and de-duplicated. */
	std::vector<std::string> ListBaselineFeatures(const std::string& packId)
	{
		std::vector<std::string> features;
		for (const auto& packPath : Util::PathHelpers::GetUnifiedPackPaths(packId)) {
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(packPath / UnifiedPresetCatalog::kBaselineSubdir, ec)) {
				std::error_code typeEc;
				if (!entry.is_regular_file(typeEc))
					continue;
				auto name = BaselineManager::ParseBaselineFeatureName(entry.path());
				if (!name.empty())
					features.push_back(std::move(name));
			}
		}
		std::ranges::sort(features);
		features.erase(std::unique(features.begin(), features.end()), features.end());
		return features;
	}

	bool IsImageExtension(const std::filesystem::path& path)
	{
		static constexpr std::array<std::string_view, 6> kImageExtensions = { ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".dds" };
		return std::ranges::contains(kImageExtensions, ToLower(Util::PathHelpers::ToUtf8(path.extension())));
	}

	std::string InferDescriptionFromReadme(const std::filesystem::path& packRoot)
	{
		static constexpr const char* kReadmeNames[] = {
			"ReadMe.txt", "README.txt", "readme.txt", "ReadMe.md", "README.md", "readme.md"
		};
		for (const char* name : kReadmeNames) {
			const auto path = packRoot / name;
			std::ifstream in(path);
			if (!in)
				continue;
			std::string line;
			std::string result;
			while (std::getline(in, line)) {
				// Trim trailing CR from Windows text files.
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty()) {
					if (!result.empty())
						break;
					continue;
				}
				if (!result.empty())
					result += ' ';
				result += line;
				if (result.size() > kMaxDescriptionLength)
					break;
			}
			if (!result.empty()) {
				if (result.size() > kMaxDescriptionLength) {
					result.resize(kMaxDescriptionLength - kTruncationSuffix.size());
					result += kTruncationSuffix;
				}
				return result;
			}
		}
		return {};
	}

	/** @brief Fill unset logo, cover and screenshots from conventional file and folder names in the pack root. */
	void InferMissingArtwork(UnifiedPresetCatalog::PackInfo& pack)
	{
		static constexpr const char* kLogoCandidates[] = {
			"logo.png", "logo.jpg", "logo.jpeg", "logo.webp", "logo.dds",
			"preview.png", "preview.jpg", "thumbnail.png", "thumbnail.jpg", "icon.png"
		};
		static constexpr const char* kCoverCandidates[] = {
			"cover.png", "cover.jpg", "cover.jpeg", "cover.webp", "cover.dds",
			"banner.png", "banner.jpg", "header.png", "header.jpg"
		};
		static constexpr const char* kShotDirs[] = {
			"gallery", "screenshots", "Screenshots", "images", "Images", "previews", "Previews"
		};

		if (pack.logoPath.empty())
			pack.logoPath = FindFirstExisting(pack.rootPath, kLogoCandidates);
		if (pack.coverPath.empty())
			pack.coverPath = FindFirstExisting(pack.rootPath, kCoverCandidates);

		if (!pack.screenshotPaths.empty())
			return;

		std::error_code ec;
		for (const char* dirName : kShotDirs) {
			const auto dir = pack.rootPath / dirName;
			if (!std::filesystem::is_directory(dir, ec))
				continue;
			for (const auto& file : std::filesystem::directory_iterator(dir, ec)) {
				if (ec || !file.is_regular_file(ec) || !IsImageExtension(file.path()))
					continue;
				pack.screenshotPaths.push_back(file.path());
			}
		}

		// Loose images at pack root (skip logo/cover already chosen).
		for (const auto& file : std::filesystem::directory_iterator(pack.rootPath, ec)) {
			if (ec || !file.is_regular_file(ec) || !IsImageExtension(file.path()))
				continue;
			const auto path = file.path();
			if ((!pack.logoPath.empty() && path == pack.logoPath) ||
				(!pack.coverPath.empty() && path == pack.coverPath))
				continue;
			pack.screenshotPaths.push_back(path);
		}

		std::sort(pack.screenshotPaths.begin(), pack.screenshotPaths.end());
		if (pack.screenshotPaths.size() > kMaxScreenshots)
			pack.screenshotPaths.resize(kMaxScreenshots);
	}
}

UnifiedPresetCatalog& UnifiedPresetCatalog::GetSingleton()
{
	static UnifiedPresetCatalog instance;
	return instance;
}

std::filesystem::path UnifiedPresetCatalog::GetPackManifestPath(const std::filesystem::path& packRoot)
{
	auto manifestPath = packRoot / packRoot.filename();
	manifestPath += ".json";
	std::error_code ec;
	if (!std::filesystem::exists(manifestPath, ec) && std::filesystem::exists(packRoot / kLegacyMetaFileName, ec))
		return packRoot / kLegacyMetaFileName;
	return manifestPath;
}

const char* UnifiedPresetCatalog::GetPresetTypeName(PresetType type)
{
	switch (type) {
	case PresetType::E11:
		return "E11";
	case PresetType::Baseline:
		return "Baseline";
	default:
		return "CS";
	}
}

json UnifiedPresetCatalog::ReadPackManifest(const std::filesystem::path& packRoot, std::string* outError)
{
	const auto manifestPath = GetPackManifestPath(packRoot);
	std::string error;
	if (auto manifest = Util::FileHelpers::ReadJsonFile(manifestPath, "preset pack manifest", 0, &error)) {
		if (manifest->is_object())
			return std::move(*manifest);
		error = T("menu.presets.manifest_not_object", "not a JSON object");
	}
	if (outError && !error.empty())
		*outError = I18n::GetSingleton()->Format("menu.presets.invalid_manifest",
			{ { "file", Util::PathHelpers::ToUtf8(manifestPath.filename()) }, { "error", error } }, "Invalid {file}: {error}");
	return json::object();
}

std::filesystem::path UnifiedPresetCatalog::ResolveEffects11Root(const std::filesystem::path& packRoot, const json& manifest)
{
	assert(manifest.is_object());
	if (const auto backends = manifest.find("backends"); backends != manifest.end() && backends->is_object()) {
		if (const auto enabled = backends->find("effects11");
			enabled != backends->end() && enabled->is_boolean() && !enabled->get<bool>())
			return {};
	}

	std::filesystem::path preferredRoot;
	if (const auto effects11 = manifest.find("effects11"); effects11 != manifest.end() && effects11->is_object()) {
		if (const auto path = effects11->find("path"); path != effects11->end() && path->is_string())
			preferredRoot = packRoot / Util::PathHelpers::FromUtf8(path->get_ref<const std::string&>());
	}

	// Classic ENB drops (NAT.ENB-style) put enbseries at the pack root instead of effects11/.
	for (const auto& candidate : { preferredRoot, packRoot / kEffects11PackSubdir, packRoot }) {
		std::string reason;
		if (!candidate.empty() && PresetManager::ValidateLibraryPreset(candidate, reason))
			return candidate;
	}
	return {};
}

std::filesystem::path UnifiedPresetCatalog::GetActivePackRoot() const
{
	// Orphan ids carry a "source:" prefix, which no pack folder name can contain.
	if (activePackId.empty() || activePackId.contains(':'))
		return {};
	return Util::PathHelpers::GetUnifiedPackPath(activePackId);
}

std::filesystem::path UnifiedPresetCatalog::ResolveActivePackPath(const std::filesystem::path& path) const
{
	std::error_code ec;
	if (path.empty() || path.is_absolute() || std::filesystem::exists(path, ec))
		return path;
	const auto packRoot = GetActivePackRoot();
	if (auto packPath = packRoot / path; !packRoot.empty() && std::filesystem::exists(packPath, ec))
		return packPath;
	return path;
}

void UnifiedPresetCatalog::LoadActiveState()
{
	activePackId.clear();
	baselinePackIds.clear();
	activePipeline.reset();
	pipelineRevision = 0;
	sceneControlOverrides = json::object();
	// Older builds kept the state beside the presets, where updating the mod deleted it.
	std::optional<json> state;
	if (const auto userSettings = Util::FileHelpers::ReadJsonObject(Util::PathHelpers::GetSettingsUserPath(), "user config")) {
		if (const auto section = userSettings->find(kUserSettingsKey); section != userSettings->end() && section->is_object())
			state = *section;
	}
	if (!state)
		state = Util::FileHelpers::ReadJsonObject(Util::PathHelpers::GetUnifiedPresetsRealPath() / kLegacyActiveStateFileName, "active pack state");
	if (!state)
		return;
	try {
		if (const auto overrides = state->find(kSceneControlKey); overrides != state->end() && overrides->is_object()) {
			for (const auto& [packId, choices] : overrides->items())
				if (IsActivePackId(packId) && choices.is_object())
					sceneControlOverrides[packId] = choices;
		}
		activePackId = state->value("activePackId", "");
		if (!IsActivePackId(activePackId))
			activePackId.clear();
		if (const auto pipeline = state->find(kPipelineKey); pipeline != state->end() && pipeline->is_string())
			activePipeline = magic_enum::enum_cast<PostProcessingMode::Mode>(pipeline->get<std::string>());
		if (const auto revision = state->find(kPipelineRevisionKey); revision != state->end() && revision->is_number_unsigned())
			pipelineRevision = revision->get<uint32_t>();
		if (const auto ids = state->find(kBaselinePackIdsKey); ids != state->end() && ids->is_array()) {
			for (const auto& id : *ids) {
				if (id.is_string() && IsPackFolderId(id.get_ref<const std::string&>()) && !IsBaselineEnabled(id.get<std::string>()))
					baselinePackIds.push_back(id.get<std::string>());
			}
		}
	} catch (...) {
		activePackId.clear();
		baselinePackIds.clear();
		activePipeline.reset();
		pipelineRevision = 0;
		sceneControlOverrides = json::object();
	}
}

json UnifiedPresetCatalog::SerializeActiveState() const
{
	json state;
	state["activePackId"] = activePackId;
	state[kBaselinePackIdsKey] = baselinePackIds;
	if (activePipeline) {
		state[kPipelineKey] = magic_enum::enum_name(*activePipeline);
		state[kPipelineRevisionKey] = pipelineRevision;
	}
	if (!sceneControlOverrides.empty())
		state[kSceneControlKey] = sceneControlOverrides;
	return state;
}

bool UnifiedPresetCatalog::SaveActiveState() const
{
	// Only this section is written now; the rest of the user config waits for Save Settings.
	const auto path = Util::PathHelpers::GetSettingsUserPath();
	std::error_code ec;
	auto document = std::filesystem::exists(path, ec) ? Util::FileHelpers::ReadJsonObject(path, "user config") :
	                                                    std::optional<json>(json::object());
	if (!document) {
		logger::warn("[Presets] Active pack state not saved: {} could not be read", Util::PathHelpers::ToUtf8(path));
		return false;
	}
	try {
		(*document)[kUserSettingsKey] = SerializeActiveState();
		if (!Util::FileHelpers::WriteJsonAtomically(path, *document, kUserSettingsIndent, "user config")) {
			logger::warn("[Presets] Failed to save active pack state to {}", Util::PathHelpers::ToUtf8(path));
			return false;
		}
	} catch (const std::exception& e) {
		logger::warn("[Presets] Failed to save active pack state: {}", e.what());
		return false;
	}
	// Left behind, the old file would come back as the state the next time the user config goes missing.
	std::filesystem::remove(Util::PathHelpers::GetUnifiedPresetsRealPath() / kLegacyActiveStateFileName, ec);
	return true;
}

void UnifiedPresetCatalog::SetActivePackId(const std::string& id)
{
	activePackId = id;
	SaveActiveState();
}

std::string UnifiedPresetCatalog::GetActiveEffects11PresetId() const
{
	return activePackId.empty() ? std::string() : GetEffects11PresetId(activePackId);
}

bool UnifiedPresetCatalog::UsePackPipeline(PostProcessingMode::Mode mode)
{
	// Only a pipeline that took effect is recorded, so the next boot runs what this session does.
	if (!PostProcessingMode::Set(mode))
		return false;
	activePipeline = mode;
	++pipelineRevision;
	return true;
}

UnifiedPresetCatalog::PackInfo* UnifiedPresetCatalog::FindPack(const std::string& id)
{
	return const_cast<PackInfo*>(std::as_const(*this).FindPack(id));
}

const UnifiedPresetCatalog::PackInfo* UnifiedPresetCatalog::FindPack(const std::string& id) const
{
	for (const auto& pack : packs) {
		if (pack.id == id)
			return &pack;
	}
	return nullptr;
}

void UnifiedPresetCatalog::DiscoverUnifiedPacks()
{
	for (const auto& packRoot : Util::PathHelpers::ListCommunityShaderEntries(Util::PathHelpers::kUnifiedPresetsSubdir, true)) {
		const auto packId = Util::PathHelpers::ToUtf8(packRoot.filename());

		std::string manifestError;
		const auto meta = ReadPackManifest(packRoot, &manifestError);
		PackInfo pack{ PackManifest::Read(meta, packId) };
		pack.id = packId;
		pack.source = SourceKind::UnifiedPack;
		pack.rootPath = packRoot;
		pack.valid = manifestError.empty();
		pack.invalidReason = std::move(manifestError);
		pack.type = ReadPresetType(meta, packId);
		if (const auto disableAtBoot = meta.find(kDisableAtBootKey); disableAtBoot != meta.end() && disableAtBoot->is_object()) {
			for (auto it = disableAtBoot->begin(); it != disableAtBoot->end(); ++it) {
				if (it.value().is_boolean())
					pack.disableAtBoot[it.key()] = it.value().get<bool>();
			}
		}

		pack.logoPath = ResolveRelative(packRoot, pack.logo);
		pack.coverPath = ResolveRelative(packRoot, pack.cover);
		for (const auto& shot : pack.screenshots) {
			if (auto path = ResolveRelative(packRoot, shot); !path.empty())
				pack.screenshotPaths.push_back(std::move(path));
		}
		pack.compat = PresetCompatibility::Evaluate(pack.csVersion, pack.requiredFeatures, pack.requiredPlugins);

		pack.effects11Root = ResolveEffects11Root(packRoot, meta);
		pack.hasEffects11 = !pack.effects11Root.empty();
		pack.hasCSPresets = SceneSettingsManager::HasScenePayload(packRoot);
		pack.hasFormEdits = FormEditSources::HasPackFormFiles(packRoot);
		pack.baselineFeatures = ListBaselineFeatures(packId);
		pack.hasBaseline = !pack.baselineFeatures.empty() || !pack.disableAtBoot.empty() ||
		                   (pack.type && *pack.type == PresetType::Baseline);

		if (pack.description.empty())
			pack.description = InferDescriptionFromReadme(packRoot);
		InferMissingArtwork(pack);

		if (!pack.hasEffects11 && !pack.hasCSPresets && !pack.hasFormEdits && !pack.hasBaseline) {
			pack.valid = false;
			if (pack.invalidReason.empty())
				pack.invalidReason = T("menu.presets.missing_payload", "No Effects11, CS Presets, Forms or Baseline payload found");
		}

		packs.push_back(std::move(pack));
	}
}

void UnifiedPresetCatalog::DiscoverEffects11Presets()
{
	// Unified packs list their own effects11 folder, so only Legacy and the library are scanned here.
	for (const auto& preset : PresetManager::GetSingleton().ScanPresets(false)) {
		if (preset.isLegacy ? !preset.valid : FindPack(preset.id) != nullptr)
			continue;

		PackInfo pack;
		pack.rootPath = preset.rootPath;
		pack.effects11Root = preset.rootPath;
		pack.hasEffects11 = pack.valid = preset.valid;
		if (preset.isLegacy) {
			pack.id = kEffects11LegacyPackId;
			pack.name = T("menu.presets.legacy_name", "Effects 11 Preset (Legacy)");
			pack.source = SourceKind::Effects11Legacy;
			pack.description = T("menu.presets.legacy_description", "Effects 11 preset installed in the game root or Data folder.");
		} else {
			pack.id = std::string(kEffects11OrphanPrefix) + preset.id;
			pack.name = preset.id;
			pack.source = SourceKind::Effects11Orphan;
			pack.description = T("menu.presets.effects11_library_description", "Effects11 library preset");
			if (!preset.valid)
				pack.invalidReason = T("menu.presets.missing_effects11_files", "Missing enbseries.ini, enbseries or enbseries/enbeffect.fx");
		}
		packs.push_back(std::move(pack));
	}
}

void UnifiedPresetCatalog::Discover()
{
	ReleaseAllArtwork();
	packs.clear();
	Util::FileHelpers::EnsureDirectoryExists(Util::PathHelpers::GetUnifiedPresetsRealPath());
	LoadActiveState();

	// Before the scan, so the packs it creates are listed by it.
	PostProcessingPresets::MigrateLegacyPresets();
	DiscoverUnifiedPacks();
	DiscoverEffects11Presets();

	// With nothing saved, the Legacy install still runs by default; show it as the active pack without persisting.
	if (activePackId.empty() && !activePipeline && PresetManager::GetSingleton().IsLegacyActive() &&
		globals::features::effects11.IsActive() && FindPack(kEffects11LegacyPackId))
		activePackId = kEffects11LegacyPackId;

	std::sort(packs.begin(), packs.end(), [](const PackInfo& a, const PackInfo& b) {
		return ToLower(a.name) < ToLower(b.name);
	});

	logger::info("[Presets] Discovered {} pack(s); active='{}'", packs.size(), activePackId.empty() ? "(none)" : activePackId);
}

std::vector<size_t> UnifiedPresetCatalog::Query(std::optional<PresetType> typeFilter, const std::string& search) const
{
	std::vector<size_t> indices;
	indices.reserve(packs.size());

	for (size_t i = 0; i < packs.size(); ++i) {
		const auto& pack = packs[i];
		if (typeFilter && !pack.IsType(*typeFilter))
			continue;

		if (!search.empty()) {
			const bool match = ContainsStringIgnoreCase(pack.name, search) || ContainsStringIgnoreCase(pack.author, search) ||
			                   ContainsStringIgnoreCase(pack.description, search) ||
			                   std::ranges::any_of(pack.tags, [&](const auto& tag) { return ContainsStringIgnoreCase(tag, search); });
			if (!match)
				continue;
		}

		indices.push_back(i);
	}
	return indices;
}

bool UnifiedPresetCatalog::LoadTextureSRV(const std::filesystem::path& path, winrt::com_ptr<ID3D11ShaderResourceView>& outSRV)
{
	outSRV = nullptr;
	if (path.empty() || !std::filesystem::exists(path) || !globals::d3d::device)
		return false;

	// Read via filesystem::path so MO2 / Unicode paths work; stbi_load(char*) uses fopen and can fail.
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		logger::warn("[Presets] Failed to open artwork '{}'", Util::PathHelpers::ToUtf8(path));
		return false;
	}
	const auto fileSize = file.tellg();
	if (fileSize <= 0) {
		logger::warn("[Presets] Empty artwork '{}'", Util::PathHelpers::ToUtf8(path));
		return false;
	}
	file.seekg(0, std::ios::beg);
	std::vector<unsigned char> bytes(static_cast<size_t>(fileSize));
	if (!file.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
		logger::warn("[Presets] Failed to read artwork '{}'", Util::PathHelpers::ToUtf8(path));
		return false;
	}

	ID3D11ShaderResourceView* srv = nullptr;
	ImVec2 size{};
	const auto ext = ToLower(Util::PathHelpers::ToUtf8(path.extension()));
	bool ok = false;
	if (ext == ".dds") {
		ok = Util::LoadDDSTextureFromFile(globals::d3d::device, Util::PathHelpers::ToUtf8(path).c_str(), &srv, size);
	} else {
		int width = 0, height = 0;
		unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, nullptr, kRgbaChannels);
		if (!pixels) {
			logger::warn("[Presets] stbi failed for '{}': {}", Util::PathHelpers::ToUtf8(path), stbi_failure_reason() ? stbi_failure_reason() : "unknown");
			return false;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA init = {};
		init.pSysMem = pixels;
		init.SysMemPitch = static_cast<UINT>(width * kRgbaChannels);

		winrt::com_ptr<ID3D11Texture2D> texture;
		const HRESULT hrTex = globals::d3d::device->CreateTexture2D(&desc, &init, texture.put());
		stbi_image_free(pixels);
		if (FAILED(hrTex) || !texture) {
			logger::warn("[Presets] CreateTexture2D failed for '{}' ({:x})", Util::PathHelpers::ToUtf8(path), static_cast<unsigned>(hrTex));
			return false;
		}
		Util::SetResourceName(texture.get(), "UnifiedPresetCatalog::Artwork");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		const HRESULT hrSrv = globals::d3d::device->CreateShaderResourceView(texture.get(), &srvDesc, &srv);
		if (FAILED(hrSrv) || !srv) {
			logger::warn("[Presets] CreateSRV failed for '{}' ({:x})", Util::PathHelpers::ToUtf8(path), static_cast<unsigned>(hrSrv));
			return false;
		}
		Util::SetResourceName(srv, "UnifiedPresetCatalog::Artwork SRV");
		size = ImVec2(static_cast<float>(width), static_cast<float>(height));
		ok = true;
	}
	if (!ok || !srv)
		return false;

	outSRV.attach(srv);
	return true;
}

bool UnifiedPresetCatalog::EnsureArtwork(PackInfo& pack)
{
	if (pack.artworkLoaded)
		return true;

	LoadTextureSRV(pack.logoPath, pack.logoSRV);
	LoadTextureSRV(pack.coverPath, pack.coverSRV);
	// List thumbs need something: fall back to cover when logo is missing/unloadable.
	if (!pack.logoSRV && pack.coverSRV)
		pack.logoSRV = pack.coverSRV;

	pack.screenshotSRVs.clear();
	pack.screenshotSRVs.reserve(pack.screenshotPaths.size());
	for (const auto& shot : pack.screenshotPaths) {
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (LoadTextureSRV(shot, srv))
			pack.screenshotSRVs.push_back(std::move(srv));
	}
	pack.artworkLoaded = true;
	return true;
}

void UnifiedPresetCatalog::ReleaseAllArtwork()
{
	for (auto& pack : packs) {
		pack.logoSRV = nullptr;
		pack.coverSRV = nullptr;
		pack.screenshotSRVs.clear();
		pack.artworkLoaded = false;
	}
}

bool UnifiedPresetCatalog::ApplyPack(const std::string& id, bool saveEffects11Current)
{
	auto* pack = FindPack(id);
	if (!pack || !pack->valid)
		return false;

	bool appliedAny = false;
	bool effects11Applied = false;

	if (pack->hasEffects11) {
		auto& presetManager = PresetManager::GetSingleton();
		presetManager.DiscoverPresets();

		const auto e11Id = GetEffects11PresetId(pack->id);
		if (presetManager.SwitchPreset(e11Id, saveEffects11Current)) {
			appliedAny = effects11Applied = true;
		} else {
			logger::warn("[Presets] Failed to switch Effects11 preset '{}'", e11Id);
		}
	}

	if (pack->hasCSPresets || pack->hasFormEdits)
		appliedAny = true;

	// A Baseline-only pack is its own layer, so it must not displace the active Effects 11 / CS pack.
	if (appliedAny) {
		// The scene layer always follows the active pack, so a pack without scene files clears it.
		activePackId = id;
		globals::features::sceneManager.ReloadOverwrites();
		CSEditor::ReloadFormEdits();
		// Applying a pack, even again, re-selects the pipeline it was authored for. An Effects 11 pack that cannot
		// run falls back to Post Processing; without that either, the current pipeline stays.
		using PostProcessingMode::Mode;
		if (!(pack->IsE11() && effects11Applied && UsePackPipeline(Mode::Effects11)))
			UsePackPipeline(Mode::PostProcessing);
		SaveActiveState();
	}

	if (pack->hasBaseline) {
		EnableBaseline(*pack);
		appliedAny = true;
	}

	return appliedAny;
}

void UnifiedPresetCatalog::DisableActivePack()
{
	assert(!activePackId.empty());
	if (const auto* pack = FindPack(activePackId); pack && pack->hasEffects11)
		UsePackPipeline(PostProcessingMode::Mode::Vanilla);
	SetActivePackId("");
	globals::features::sceneManager.ReloadOverwrites();
	CSEditor::ReloadFormEdits();
}

bool UnifiedPresetCatalog::IsBaselineEnabled(const std::string& id) const
{
	return std::ranges::find(baselinePackIds, id) != baselinePackIds.end();
}

void UnifiedPresetCatalog::ReapplyIfInUse(const std::string& id)
{
	// A refresh, not ApplyPack: that would also re-pick the pipeline and move the pack to the top of the Baseline order.
	if (activePackId == id) {
		globals::features::sceneManager.ReloadOverwrites();
		CSEditor::ReloadFormEdits();
	}
	if (IsBaselineEnabled(id)) {
		BaselineManager::GetSingleton()->Refresh();
		globals::state->ReapplySettingsLayers();
	}
}

void UnifiedPresetCatalog::EnableBaseline(const PackInfo& pack)
{
	// Re-enabling moves the pack to the end so it wins conflicts against the other Baseline packs.
	std::erase(baselinePackIds, pack.id);
	baselinePackIds.push_back(pack.id);
	SaveActiveState();

	BaselineManager::GetSingleton()->Refresh();
	// The user's edits stay on top; features that are not loaded pick the pack up when they load.
	globals::state->ReapplySettingsLayers();
	if (const auto edited = globals::state->FindEditsUnderPack(pack.id); !edited.empty())
		QueueBaselineOverlap(pack.id, edited);

	// Disable-at-boot entries take effect on the next game start. Like a boot toggle they are saved with the
	// rest of Settings, so enabling a pack never writes the user's unsaved edits.
	if (!pack.disableAtBoot.empty()) {
		auto* state = globals::state;
		bool changed = false;
		for (const auto& [featureName, disabled] : pack.disableAtBoot) {
			if (state->IsFeatureDisabled(featureName) == disabled)
				continue;
			state->SetFeatureDisabled(featureName, disabled);
			changed = true;
			logger::info("[Presets] Baseline '{}': {} '{}' at boot", pack.id, disabled ? "disable" : "enable", featureName);
		}
		if (changed)
			state->CheckCoreSettingsChanged();
	}
}

void UnifiedPresetCatalog::LoadSceneControl() const
{
	const auto packRoot = GetActivePackRoot();
	const auto manifest = packRoot.empty() ? json::object() : ReadPackManifest(packRoot);
	auto sceneControl = manifest.value(kSceneControlKey, json::object());
	if (!sceneControl.is_object())
		sceneControl = json::object();
	// The pack's choices are its defaults; the user's own win.
	if (const auto overrides = sceneControlOverrides.find(activePackId); overrides != sceneControlOverrides.end())
		sceneControl.update(*overrides);
	E11Handoff::ApplyManifest(sceneControl);
}

void UnifiedPresetCatalog::SetSceneControl(E11Handoff::Feature feature, bool handedOff)
{
	globals::features::effects11.SetHandedOff(feature, handedOff);
	if (activePackId.empty())
		return;

	// The pack's manifest stays its author's; the user's choice is kept per pack in the active pack state.
	const std::string shortName(E11Handoff::GetShortName(feature));
	sceneControlOverrides[activePackId][shortName] = handedOff;
	SaveActiveState();
	logger::info("[Presets] Pack '{}': sceneControl '{}' = {}", activePackId, shortName, handedOff);
}

void UnifiedPresetCatalog::QueueBaselineOverlap(const std::string& packId, const std::vector<Feature*>& features)
{
	if (std::ranges::find(overlapPackIds, packId) == overlapPackIds.end())
		overlapPackIds.push_back(packId);
	for (auto* feature : features)
		if (auto name = feature->GetDisplayName(); std::ranges::find(overlapFeatureNames, name) == overlapFeatureNames.end())
			overlapFeatureNames.push_back(std::move(name));

	std::string packNames;
	for (const auto& id : overlapPackIds) {
		const auto* pack = FindPack(id);
		packNames += (packNames.empty() ? "" : ", ") + (pack && !pack->name.empty() ? pack->name : id);
	}
	std::string names;
	for (const auto& name : overlapFeatureNames)
		names += (names.empty() ? "" : ", ") + name;
	const auto count = overlapFeatureNames.size();

	overlapPrompt.title = T("menu.presets.overlap_title", "Baseline Preset Over Your Changes");
	overlapPrompt.message = std::vformat(T("menu.presets.overlap_message",
											 "{} sets {} features you have changed: {}.\n\n"
											 "Your changes are kept and still win. Use the preset's values to drop them for the settings it sets."),
		std::make_format_args(packNames, count, names));
	overlapPrompt.confirmLabel = T("menu.presets.overlap_use_preset", "Use Preset Values");
	overlapPrompt.cancelLabel = T("menu.presets.overlap_keep", "Keep My Changes");
	overlapPrompt.focus = Util::ConfirmationPopup::Focus::Cancel;
	overlapPrompt.Request();
}

void UnifiedPresetCatalog::DrawBaselineOverlapPrompt()
{
	if (overlapPrompt.Draw())
		for (const auto& packId : overlapPackIds)
			globals::state->ClearEditsUnderPack(packId);
	if (!overlapPrompt.IsOpen()) {
		overlapPackIds.clear();
		overlapFeatureNames.clear();
	}
}

bool UnifiedPresetCatalog::RemoveBaseline(const std::string& id)
{
	if (std::erase(baselinePackIds, id) == 0)
		return false;
	SaveActiveState();

	BaselineManager::GetSingleton()->Refresh();
	globals::state->ReapplySettingsLayers();
	logger::info("[Presets] Removed Baseline pack '{}'", id);
	return true;
}

bool UnifiedPresetCatalog::MoveBaseline(const std::string& id, bool raise)
{
	const auto found = std::ranges::find(baselinePackIds, id);
	if (found == baselinePackIds.end())
		return false;
	const auto index = static_cast<size_t>(found - baselinePackIds.begin());
	if (raise ? index + 1 >= baselinePackIds.size() : index == 0)
		return false;
	const size_t target = raise ? index + 1 : index - 1;
	std::swap(baselinePackIds[index], baselinePackIds[target]);
	// An order that is not saved would revert at the next start, so keep showing the one on disk.
	if (!SaveActiveState()) {
		std::swap(baselinePackIds[index], baselinePackIds[target]);
		return false;
	}

	BaselineManager::GetSingleton()->Refresh();
	globals::state->ReapplySettingsLayers();
	logger::info("[Presets] Moved Baseline pack '{}' {}", id, raise ? "up" : "down");
	return true;
}

bool UnifiedPresetCatalog::EnsureBaselineManifest(const std::filesystem::path& packRoot, const std::string& displayName)
{
	const auto manifestPath = GetPackManifestPath(packRoot);
	std::error_code ec;
	if (std::filesystem::exists(manifestPath, ec))
		return true;

	json manifest{ { PackManifest::kNameKey, displayName }, { kPresetTypeKey, GetPresetTypeName(PresetType::Baseline) } };
	PackManifest{ .version = PackManifest::kDefaultVersion, .csVersion = PresetCompatibility::CurrentCsVersionString() }.WriteDetails(manifest);
	return Util::FileHelpers::WriteJsonAtomically(manifestPath, manifest, 4, "preset pack manifest");
}

bool UnifiedPresetCatalog::OpenPresetsFolder() const
{
	const auto path = Util::PathHelpers::GetUnifiedPresetsRealPath();
	Util::FileHelpers::EnsureDirectoryExists(path);
	return Util::FileHelpers::OpenWithShell(path);
}

bool UnifiedPresetCatalog::OpenPackFolder(const std::string& id) const
{
	const auto* pack = FindPack(id);
	if (!pack)
		return false;

	std::filesystem::path path = pack->rootPath;
	// Explorer runs outside MO2's VFS, so open the physical copy when this mod folder has one.
	// A Legacy install belongs to another mod (or the game root), never the CS mod folder.
	if (const auto dataRelative = path.lexically_relative(Util::PathHelpers::GetDataPath());
		pack->source != SourceKind::Effects11Legacy && !dataRelative.empty() && *dataRelative.begin() != "..") {
		std::error_code ec;
		if (auto realPath = Util::PathHelpers::GetRealPathFromDataRelative(dataRelative); !realPath.empty() && std::filesystem::exists(realPath, ec))
			path = std::move(realPath);
	}

	return Util::FileHelpers::OpenWithShell(path);
}
