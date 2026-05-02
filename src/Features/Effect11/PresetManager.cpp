#include "PresetManager.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include "Globals.h"

PresetManager& PresetManager::GetSingleton()
{
	static PresetManager instance;
	return instance;
}

void PresetManager::Initialize()
{
	DiscoverPresets();
	LoadPersistedChoice();
}

void PresetManager::DiscoverPresets()
{
	presets.clear();
	activePresetIndex = -1;

	// 1. Scan Data\enbpresets\ subfolders (highest priority)
	std::filesystem::path presetsDir = "Data\\enbpresets";
	if (std::filesystem::exists(presetsDir) && std::filesystem::is_directory(presetsDir)) {
		std::vector<std::string> folders;
		for (auto& entry : std::filesystem::directory_iterator(presetsDir)) {
			if (!entry.is_directory())
				continue;
			if (std::filesystem::exists(entry.path() / "enbseries.ini"))
				folders.push_back(entry.path().filename().string());
		}

		std::sort(folders.begin(), folders.end(), [](const std::string& a, const std::string& b) {
			std::string la = a, lb = b;
			std::transform(la.begin(), la.end(), la.begin(), ::tolower);
			std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
			return la < lb;
		});

		for (auto& folder : folders) {
			PresetInfo info;
			info.folderName = folder;
			info.displayName = folder;
			info.basePath = presetsDir / folder;
			info.isRoot = false;
			LoadPresetMetadata(info);
			LoadThumbnail(info);
			presets.push_back(std::move(info));
		}
	}

	// 2. Check Data folder (enbseries.ini in Data\)
	if (std::filesystem::exists("Data\\enbseries.ini")) {
		PresetInfo data;
		data.folderName = "__data__";
		data.displayName = "Data (enbseries)";
		data.basePath = "Data";
		data.isRoot = false;
		presets.push_back(std::move(data));
	}

	// 3. Check root folder (enbseries.ini in game root, lowest priority)
	if (std::filesystem::exists("enbseries.ini")) {
		PresetInfo root;
		root.folderName = "";
		root.displayName = "Root (enbseries)";
		root.basePath = "";
		root.isRoot = true;
		presets.push_back(std::move(root));
	}

	if (presets.empty()) {
		logger::warn("[PresetManager] No ENB presets found");
	} else {
		logger::info("[PresetManager] Discovered {} preset(s)", presets.size());
	}
}

void PresetManager::LoadPresetMetadata(PresetInfo& preset)
{
	std::filesystem::path metaPath;
	if (preset.isRoot) {
		metaPath = "enbseries/preset.json";
	} else {
		metaPath = preset.basePath / "preset.json";
	}

	if (!std::filesystem::exists(metaPath))
		return;

	std::ifstream ifs(metaPath);
	if (!ifs.is_open())
		return;

	try {
		nlohmann::json root = nlohmann::json::parse(ifs);

		if (root.contains("name") && root["name"].is_string())
			preset.displayName = root["name"].get<std::string>();

		if (root.contains("description") && root["description"].is_string())
			preset.description = root["description"].get<std::string>();

		if (root.contains("thumbnail") && root["thumbnail"].is_string()) {
			std::string thumb = root["thumbnail"].get<std::string>();
			if (preset.isRoot) {
				preset.thumbnailPath = (std::filesystem::path("enbseries") / thumb).string();
			} else {
				preset.thumbnailPath = (preset.basePath / thumb).string();
			}
		}

		if (root.contains("requiredPlugins") && root["requiredPlugins"].is_array()) {
			for (auto& p : root["requiredPlugins"]) {
				if (p.is_string())
					preset.requiredPlugins.push_back(p.get<std::string>());
			}
		}
	} catch (const std::exception& e) {
		logger::error("[PresetManager] Failed to parse {}: {}", metaPath.string(), e.what());
	}
}

void PresetManager::LoadThumbnail(PresetInfo& preset)
{
	if (preset.thumbnailPath.empty())
		return;

	if (!std::filesystem::exists(preset.thumbnailPath))
		return;

	int width = 0, height = 0;
	unsigned char* data = stbi_load(preset.thumbnailPath.c_str(), &width, &height, nullptr, 4);
	if (!data)
		return;

	auto* device = globals::d3d::device;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = data;
	initData.SysMemPitch = width * 4;

	winrt::com_ptr<ID3D11Texture2D> texture;
	HRESULT hr = device->CreateTexture2D(&desc, &initData, texture.put());
	stbi_image_free(data);

	if (FAILED(hr))
		return;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	hr = device->CreateShaderResourceView(texture.get(), &srvDesc, preset.thumbnailSRV.put());
	if (SUCCEEDED(hr)) {
		preset.thumbnailWidth = static_cast<float>(width);
		preset.thumbnailHeight = static_cast<float>(height);
		logger::debug("[PresetManager] Loaded thumbnail: {}", preset.thumbnailPath);
	}
}

void PresetManager::LoadPersistedChoice()
{
	if (presets.empty())
		return;

	std::filesystem::path iniPath = "Data/SKSE/Plugins/CommunityShaders_Effect11.ini";

	if (std::filesystem::exists(iniPath)) {
		char buffer[256] = {};
		GetPrivateProfileStringA("Presets", "ActivePreset", "", buffer, sizeof(buffer), iniPath.string().c_str());
		std::string saved(buffer);

		if (!saved.empty()) {
			for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
				bool match = presets[i].isRoot ? saved == "__root__" : presets[i].folderName == saved;
				if (match) {
					activePresetIndex = i;
					logger::info("[PresetManager] Restored preset: {}", presets[i].displayName);
					return;
				}
			}
			logger::warn("[PresetManager] Persisted preset '{}' not found, falling back", saved);
		}
	}

	// Default: root preset if present, otherwise first alphabetically
	activePresetIndex = 0;
	logger::info("[PresetManager] Defaulting to preset: {}", presets[0].displayName);
}

void PresetManager::SavePersistedChoice()
{
	if (activePresetIndex < 0 || activePresetIndex >= static_cast<int>(presets.size()))
		return;

	std::filesystem::path iniPath = "Data/SKSE/Plugins/CommunityShaders_Effect11.ini";

	const auto& preset = presets[activePresetIndex];
	std::string value = preset.isRoot ? "__root__" : preset.folderName;

	WritePrivateProfileStringA("Presets", "ActivePreset", value.c_str(), iniPath.string().c_str());
	WritePrivateProfileStringA(NULL, NULL, NULL, iniPath.string().c_str());
}

const PresetManager::PresetInfo* PresetManager::GetActivePreset() const
{
	if (activePresetIndex >= 0 && activePresetIndex < static_cast<int>(presets.size()))
		return &presets[activePresetIndex];
	return nullptr;
}

std::filesystem::path PresetManager::GetENBSeriesPath() const
{
	auto* preset = GetActivePreset();
	if (!preset || preset->isRoot)
		return "enbseries";
	return preset->basePath / "enbseries";
}

std::filesystem::path PresetManager::GetENBSeriesIniPath() const
{
	auto* preset = GetActivePreset();
	if (!preset || preset->isRoot)
		return "enbseries.ini";
	return preset->basePath / "enbseries.ini";
}

void PresetManager::SetActivePreset(int index)
{
	if (index < 0 || index >= static_cast<int>(presets.size()))
		return;
	activePresetIndex = index;
	SavePersistedChoice();
	logger::info("[PresetManager] Switched to preset: {}", presets[index].displayName);
}

bool PresetManager::AreRequirementsMet(const PresetInfo& preset) const
{
	if (preset.requiredPlugins.empty())
		return true;

	auto handler = RE::TESDataHandler::GetSingleton();
	if (!handler)
		return false;

	for (const auto& plugin : preset.requiredPlugins) {
		if (!handler->LookupModByName(plugin))
			return false;
	}
	return true;
}
