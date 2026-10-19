#include "TerrainVariation.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "State.h"
#include "../Util.h"

#include <cctype>

#define I18N_KEY_PREFIX "feature.terrain_variation."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableLODTerrainTilingFix,
	enableMeshSupport)

void TerrainVariation::DrawSettings()
{
	{
		MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
		ImGui::TextWrapped("%s", T(TKEY("always_enabled_note"),
			"Terrain Variation is always enabled when installed. To turn it off, use Disable at Boot."));
	}

	ImGui::Spacing();

	bool lodTilingFix = settings.enableLODTerrainTilingFix != 0;
	if (ImGui::Checkbox(T(TKEY("apply_to_lod_terrain"), "Apply to LOD Terrain"), &lodTilingFix)) {
		settings.enableLODTerrainTilingFix = lodTilingFix ? 1u : 0u;
		logger::info("TerrainVariation LOD setting changed to: {}", settings.enableLODTerrainTilingFix != 0);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("apply_to_lod_terrain_tooltip"),
							  "Applies the tiling fix to LOD terrain objects.\nThis helps reduce the visible tiling effect on distant terrain."));
	}

	bool meshSupport = settings.enableMeshSupport != 0;
	if (ImGui::Checkbox(T(TKEY("enable_mesh_support"), "Enable Mesh Support"), &meshSupport)) {
		settings.enableMeshSupport = meshSupport ? 1u : 0u;
		logger::info("TerrainVariation mesh support setting changed to: {}", settings.enableMeshSupport != 0);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_mesh_support_tooltip"),
							  "Applies the tiling fix to meshes that share a texture with the landscape surface, such as dirt cliffs, rocks and mountain slabs.\nTree branches, roots, foliage and decals are never affected."));
	}
}

#undef I18N_KEY_PREFIX

namespace
{
	constexpr std::string_view LandscapeDirectory = "landscape/";

	std::string CanonicaliseTexturePath(std::string_view a_path)
	{
		std::string canonical(a_path);
		for (auto& character : canonical) {
			character = character == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}

		if (canonical.starts_with("data/")) {
			canonical.erase(0, 5);
		}
		if (canonical.starts_with("textures/")) {
			canonical.erase(0, 9);
		}

		return canonical;
	}

	// Covers textures/landscape/ and prefixed copies such as textures/pbr/landscape/
	bool IsInLandscapeDirectory(std::string_view a_canonical)
	{
		return a_canonical.starts_with(LandscapeDirectory) || a_canonical.find("/landscape/") != std::string_view::npos;
	}

	std::string_view GetFileName(std::string_view a_canonical)
	{
		const auto separator = a_canonical.rfind('/');
		return separator == std::string_view::npos ? a_canonical : a_canonical.substr(separator + 1);
	}
}

void TerrainVariation::DataLoaded()
{
	auto dataHandler = RE::TESDataHandler::GetSingleton();
	if (dataHandler == nullptr) {
		logger::warn("TerrainVariation: No data handler, landscape texture path list unavailable; mesh support will not match any meshes");
		return;
	}

	const auto& landTextures = dataHandler->GetFormArray<RE::TESLandTexture>();

	const std::unique_lock lock(meshTextureMutex);

	landscapeDiffusePaths.clear();
	landscapeDiffuseFileNames.clear();
	meshTextureCache.clear();
	meshTextureKeepAlive.clear();

	auto addTextureSet = [this](RE::BGSTextureSet* textureSet) {
		if (textureSet == nullptr) {
			return;
		}

		const auto path = textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
		if (path == nullptr || *path == '\0') {
			return;
		}

		auto canonical = CanonicaliseTexturePath(path);
		landscapeDiffuseFileNames.emplace(GetFileName(canonical));
		landscapeDiffusePaths.insert(std::move(canonical));
	};

	for (auto landTexture : landTextures) {
		if (landTexture == nullptr) {
			continue;
		}

		addTextureSet(landTexture->textureSet);
		addTextureSet(Util::GetSeasonalSwap(landTexture->textureSet));
	}

	logger::info("TerrainVariation: Collected {} landscape diffuse texture paths ({} file names) from {} land texture records", landscapeDiffusePaths.size(), landscapeDiffuseFileNames.size(), landTextures.size());
}

bool TerrainVariation::IsLandscapeDiffuseTexture(const RE::BSFixedString& a_name)
{
	const auto key = a_name.c_str();
	if (key == nullptr || *key == '\0') {
		return false;
	}

	{
		const std::shared_lock lock(meshTextureMutex);
		if (auto it = meshTextureCache.find(key); it != meshTextureCache.end()) {
			return it->second;
		}
	}

	const std::unique_lock lock(meshTextureMutex);
	auto [it, inserted] = meshTextureCache.try_emplace(key, false);
	if (inserted) {
		// Only textures painted on the landscape surface qualify, wherever they live, so branches and roots
		// sharing a landscape/ subfolder with a ground texture are left alone. A mesh copy of a ground texture
		// in another landscape/ subfolder (landscape/mountains/mountainslab01.dds) still matches by file name.
		const auto canonical = CanonicaliseTexturePath(key);
		it->second = landscapeDiffusePaths.contains(canonical) ||
		             (IsInLandscapeDirectory(canonical) && landscapeDiffuseFileNames.contains(std::string(GetFileName(canonical))));
		meshTextureKeepAlive.push_back(a_name);
		if (it->second) {
			logger::debug("TerrainVariation: Mesh variation enabled for texture {}", key);
		}
	}

	return it->second;
}

void TerrainVariation::BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass)
{
	auto& descriptor = globals::state->permutationData.ExtraFeatureDescriptor;
	descriptor &= ~uint(State::ExtraFeatureDescriptors::TVMeshVariation);

	if (!loaded || settings.enableMeshSupport == 0 || a_pass == nullptr || a_pass->geometry == nullptr) {
		return;
	}

	auto& runtimeData = a_pass->geometry->GetGeometryRuntimeData();

	// Alpha tested draws are foliage cards and decals, where shifting UVs would be destructive
	auto& property0 = runtimeData.alphaProperty;
	if (property0 && property0->GetRTTI() == globals::rtti::NiAlphaPropertyRTTI.get() && static_cast<RE::NiAlphaProperty*>(property0.get())->GetAlphaTesting()) {
		return;
	}

	auto& property1 = runtimeData.shaderProperty;
	auto lightProperty = property1 && property1->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(property1.get()) : nullptr;
	if (lightProperty == nullptr) {
		return;
	}

	using enum RE::BSShaderProperty::EShaderPropertyFlag;
	if (lightProperty->flags.any(kMultiTextureLandscape, kLODLandscape, kDecal, kDynamicDecal)) {
		return;
	}

	// Offsetting a UV only makes sense under wrap addressing
	auto material = static_cast<const RE::BSLightingShaderMaterialBase*>(lightProperty->GetBaseMaterial());
	if (material == nullptr || material->textureClampMode != static_cast<std::int32_t>(RE::BSGraphics::TextureAddressMode::kWrapSWrapT)) {
		return;
	}

	auto baseTexture = lightProperty->GetBaseTexture();
	if (baseTexture == nullptr) {
		return;
	}

	if (IsLandscapeDiffuseTexture(baseTexture->name)) {
		descriptor |= uint(State::ExtraFeatureDescriptors::TVMeshVariation);
	}
}

struct TerrainVariation::Hooks
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			globals::features::terrainVariation.BSLightingShader_SetupGeometry(Pass);
			func(This, Pass, RenderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		logger::info("TerrainVariation: Installed hooks - BSLightingShader_SetupGeometry");
	}
};

void TerrainVariation::PostPostLoad()
{
	Hooks::Install();

	logger::info("TerrainVariation: Feature initialized");
}

void TerrainVariation::LoadSettings(json& o_json)
{
	settings = o_json;
}

void TerrainVariation::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainVariation::RestoreDefaultSettings()
{
	settings = {};
}

bool TerrainVariation::DrawFailLoadMessage() const
{
	return false;
}
