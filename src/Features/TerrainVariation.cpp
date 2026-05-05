#include "TerrainVariation.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "Util.h"

#include <algorithm>
#include <cctype>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableLODTerrainTilingFix,
	meshUvScale,
	meshVariationStrength,
	meshParallaxShadowStrength)

std::string TerrainVariation::CanonicalizeTextureIdentity(std::string_view identity)
{
	std::string normalized(identity);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return normalized;
}

void TerrainVariation::RebuildOptInSet()
{
	pbrTextureOptInSet.clear();
	for (const auto& id : pbrTextureOptInIds) {
		auto normalized = CanonicalizeTextureIdentity(id);
		if (!normalized.empty()) {
			pbrTextureOptInSet.insert(std::move(normalized));
		}
	}
}

void TerrainVariation::RegisterObservedPBRTextureIdentity(std::string_view identity)
{
	auto normalized = CanonicalizeTextureIdentity(identity);
	if (normalized.empty()) {
		return;
	}

	const std::unique_lock lock(textureIdMutex);
	if (pbrObservedTextureSet.insert(normalized).second) {
		pbrObservedTextureIds.push_back(std::move(normalized));
		std::sort(pbrObservedTextureIds.begin(), pbrObservedTextureIds.end());
	}
}

bool TerrainVariation::IsPBRTextureIdentityOptedIn(std::string_view identity) const
{
	auto normalized = CanonicalizeTextureIdentity(identity);
	if (normalized.empty()) {
		return false;
	}

	const std::shared_lock lock(textureIdMutex);
	return pbrTextureOptInSet.contains(normalized);
}

void TerrainVariation::DrawSettings()
{
	{
		MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
		ImGui::TextWrapped(
			"Terrain variation is always enabled when installed. Use disable at boot to turn off.");
	}

	ImGui::Spacing();

	bool lodTilingFix = settings.enableLODTerrainTilingFix != 0;
	if (ImGui::Checkbox("Apply to LOD Terrain", &lodTilingFix)) {
		settings.enableLODTerrainTilingFix = lodTilingFix ? 1u : 0u;
		logger::info("TerrainVariation LOD setting changed to: {}", settings.enableLODTerrainTilingFix != 0);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Applies the tiling fix to LOD terrain objects.\n"
			"This helps reduce the visible tiling effect on distant terrain.");
	}

	if (ImGui::SliderFloat("Mesh UV Scale", &settings.meshUvScale, 1.0f, 256.0f, "%.2f")) {
		settings.meshUvScale = std::max(settings.meshUvScale, 0.001f);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Controls stochastic cell size on non-landscape PBR meshes.");
	}

	if (ImGui::SliderFloat("Mesh Variation Strength", &settings.meshVariationStrength, 0.0f, 2.0f, "%.2f")) {
		settings.meshVariationStrength = std::clamp(settings.meshVariationStrength, 0.0f, 2.0f);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Blends between vanilla sampling (0) and full stochastic sampling (1).");
	}

	if (ImGui::SliderFloat("Mesh Parallax Shadow Strength", &settings.meshParallaxShadowStrength, 0.0f, 2.0f, "%.2f")) {
		settings.meshParallaxShadowStrength = std::clamp(settings.meshParallaxShadowStrength, 0.0f, 2.0f);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Controls stochastic influence on parallax height/shadow sampling for opted-in meshes.");
	}

	ImGui::Spacing();
	ImGui::SeparatorText("PBR Mesh Texture Opt-In");
	ImGui::TextWrapped("This list controls non-landscape PBR meshes only. Landscape TV behavior is unchanged.");

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Move texture identities into TV ON to apply TV on PBR meshes that use them.");
	}

	std::vector<std::string> tvOff;
	std::vector<std::string> tvOn;
	{
		const std::shared_lock lock(textureIdMutex);
		tvOn.assign(pbrTextureOptInSet.begin(), pbrTextureOptInSet.end());
		for (const auto& id : pbrObservedTextureIds) {
			if (!pbrTextureOptInSet.contains(id)) {
				tvOff.push_back(id);
			}
		}
	}
	std::sort(tvOff.begin(), tvOff.end());
	std::sort(tvOn.begin(), tvOn.end());

	ImGui::PushID("TVPBRTextureOptInTable");
	if (ImGui::BeginTable("TVPBRTextureOptInTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 300.0f))) {
		ImGui::TableSetupColumn("TV OFF");
		ImGui::TableSetupColumn("TV ON");
		ImGui::TableHeadersRow();

		const int maxRows = std::max(static_cast<int>(tvOff.size()), static_cast<int>(tvOn.size()));
		for (int i = 0; i < maxRows; ++i) {
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			if (i < static_cast<int>(tvOff.size())) {
				if (ImGui::Selectable(std::format("{}##off{}", tvOff[i], i).c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
					const std::unique_lock lock(textureIdMutex);
					const auto& selectedId = tvOff[i];
					if (pbrTextureOptInSet.insert(selectedId).second) {
						pbrTextureOptInIds.push_back(selectedId);
						std::sort(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end());
						pbrTextureOptInIds.erase(std::unique(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end()), pbrTextureOptInIds.end());
					}
				}
			}

			ImGui::TableSetColumnIndex(1);
			if (i < static_cast<int>(tvOn.size())) {
				if (ImGui::Selectable(std::format("{}##on{}", tvOn[i], i).c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
					const std::unique_lock lock(textureIdMutex);
					const auto& removeId = tvOn[i];
					pbrTextureOptInSet.erase(removeId);
					pbrTextureOptInIds.erase(
						std::remove(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end(), removeId),
						pbrTextureOptInIds.end());
				}
			}
		}
		ImGui::EndTable();
	}
	ImGui::PopID();
}

void TerrainVariation::PostPostLoad()
{
	logger::info("TerrainVariation: Feature initialized");
}

void TerrainVariation::LoadSettings(json& o_json)
{
	settings = o_json;
	const std::unique_lock lock(textureIdMutex);
	pbrTextureOptInIds.clear();
	if (o_json.contains("pbrTextureOptInIds")) {
		pbrTextureOptInIds = o_json["pbrTextureOptInIds"].get<std::vector<std::string>>();
	}
	for (auto& id : pbrTextureOptInIds) {
		id = CanonicalizeTextureIdentity(id);
	}
	std::sort(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end());
	pbrTextureOptInIds.erase(std::unique(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end()), pbrTextureOptInIds.end());
	RebuildOptInSet();
}

void TerrainVariation::SaveSettings(json& o_json)
{
	{
		const std::shared_lock lock(textureIdMutex);
		pbrTextureOptInIds.assign(pbrTextureOptInSet.begin(), pbrTextureOptInSet.end());
	}
	std::sort(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end());
	o_json = settings;
	o_json["pbrTextureOptInIds"] = pbrTextureOptInIds;
}

void TerrainVariation::RestoreDefaultSettings()
{
	const std::unique_lock lock(textureIdMutex);
	settings = {};
	pbrTextureOptInIds.clear();
	pbrTextureOptInSet.clear();
}

bool TerrainVariation::DrawFailLoadMessage() const
{
	return false;
}