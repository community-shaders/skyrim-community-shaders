#include "TerrainVariation.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "Util.h"

#include <algorithm>
#include <cctype>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableLODTerrainTilingFix)

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

	ImGui::Spacing();
	ImGui::SeparatorText("PBR Mesh Texture Opt-In");
	ImGui::TextWrapped("This list controls non-landscape PBR meshes only. Landscape TV behavior is unchanged.");

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Move texture identities into TV ON to apply TV on PBR meshes that use them.");
	}

	std::vector<std::string> allIds;
	std::unordered_set<std::string> optedInSetCopy;
	{
		const std::shared_lock lock(textureIdMutex);
		optedInSetCopy = pbrTextureOptInSet;
		std::unordered_set<std::string> allSet = pbrTextureOptInSet;
		for (const auto& id : pbrObservedTextureIds) {
			allSet.insert(id);
		}
		allIds.assign(allSet.begin(), allSet.end());
	}
	std::sort(allIds.begin(), allIds.end());

	ImGui::PushID("TVPBRTextureOptInTable");
	if (ImGui::BeginTable("TVPBRTextureOptInTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 300.0f))) {
		ImGui::TableSetupColumn("TV OFF");
		ImGui::TableSetupColumn("TV ON");
		ImGui::TableHeadersRow();

		for (int i = 0; i < static_cast<int>(allIds.size()); ++i) {
			ImGui::TableNextRow();
			const auto& id = allIds[i];
			const bool isOn = optedInSetCopy.contains(id);

			ImGui::TableSetColumnIndex(0);
			if (!isOn) {
				if (ImGui::Selectable(std::format("{}##off{}", id, i).c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
					const std::unique_lock lock(textureIdMutex);
					if (pbrTextureOptInSet.insert(id).second) {
						pbrTextureOptInIds.push_back(id);
						std::sort(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end());
						pbrTextureOptInIds.erase(std::unique(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end()), pbrTextureOptInIds.end());
					}
				}
			}

			ImGui::TableSetColumnIndex(1);
			if (isOn) {
				if (ImGui::Selectable(std::format("{}##on{}", id, i).c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
					const std::unique_lock lock(textureIdMutex);
					pbrTextureOptInSet.erase(id);
					pbrTextureOptInIds.erase(
						std::remove(pbrTextureOptInIds.begin(), pbrTextureOptInIds.end(), id),
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