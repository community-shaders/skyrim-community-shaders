#include "LightEditor.h"
#include "../Features/InverseSquareLighting.h"
#include "../Features/LightLimitFix.h"
#include "../Menu.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

void LightEditor::DrawSettings()
{
	ImGui::Checkbox("Disable Regular Falloff Lights", &disableRegularLights);
	ImGui::Checkbox("Disable Inverse Square Falloff Lights", &disableInvSqLights);

	ImGui::Spacing();
	ImGui::Text("Total Lights: %u", totalLightCount);
	ImGui::Text("Active Shadow Lights: %u", activeShadowLightCount);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Checkbox("Shadows Only", &shadowsOnly);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Only show lights with HemiShadow or OmniShadow flags.");
	}

	int selectedFilter = static_cast<int>(filterOption);
	if (ImGui::Combo("Filter By", &selectedFilter, FilterOptionLabels, static_cast<int>(FilterOption::Count))) {
		filterOption = static_cast<FilterOption>(selectedFilter);
	}

	int selectedSort = static_cast<int>(sortOption);
	if (ImGui::Combo("Sort By", &selectedSort, SortOptionLabels, static_cast<int>(SortOption::Count))) {
		sortOption = static_cast<SortOption>(selectedSort);
	}

	if (ImGui::BeginCombo("Lights", selected.isSelected ? GetLightName(selected).c_str() : "Select a light")) {
		for (auto& light : lights) {
			const auto displayName = GetLightName(light);
			const bool isSelected = light == selected;

			if (ImGui::Selectable(displayName.c_str(), isSelected))
				selected = light;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (!selected.isSelected)
		return;

	if (selected.isRef || selected.isAttached) {
		ImGui::Text("Owner: 0x%08X | %s", selected.id, displayInfo.ownerEditorId.c_str());
		ImGui::Text("Owner last edited by: %s", displayInfo.ownerLastEditedBy.c_str());
		ImGui::Text("Base Object: 0x%08X | %s", displayInfo.baseObjectFormId, selected.name.c_str());
		ImGui::Text("LIGH: 0x%08X | %s", displayInfo.lighFormId, displayInfo.lighEditorId.c_str());
		ImGui::Text("Cell: %s", displayInfo.cellEditorId.c_str());
	} else {
		ImGui::Text("Memory Address: %p", selected.ptr);
		ImGui::Text("NiLight Name: %s", selected.name.c_str());
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button("Revert Changes")) {
		current = original;
		current.pos = { 0, 0, 0 };
		waitFrames = 1;
	}

	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		if (ImGui::Button("Save to Light Placer")) {
			SaveToLightPlacer();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Save current settings to the Light Placer JSON.");
		}
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (selected.isSpotlight)
		ImGui::TextDisabled("Spotlight: ISL light type flags not applicable");
	ImGui::BeginDisabled(selected.isSpotlight);
	ImGui::CheckboxFlags("Inverse Square Light", reinterpret_cast<uint32_t*>(&current.data.flags), static_cast<uint32_t>(LightLimitFix::LightFlags::InverseSquare));
	ImGui::EndDisabled();
	ImGui::CheckboxFlags("Linear Light", reinterpret_cast<uint32_t*>(&current.data.flags), static_cast<uint32_t>(LightLimitFix::LightFlags::Linear));

	ImGui::Spacing();
	ImGui::Spacing();

	ImGui::ColorEdit3("Color", &current.data.diffuse.red);
	ImGui::SliderFloat("Intensity", &current.data.fade, 0.01f, 16.f, "%.3f");

	const auto isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);

	if (isInvSq)
		ImGui::BeginDisabled();
	ImGui::SliderFloat("Radius", &current.data.radius, 2.f, 8096.f, "%.0f");
	if (isInvSq)
		ImGui::EndDisabled();

	if (isInvSq) {
		ImGui::SliderFloat("Size", &current.data.size, 0.01f, 10.0f, "%.3f");
		ImGui::SliderFloat("Cutoff", &current.data.cutoffOverride, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (!selected.isOther && current.data.lighFormId != 0 && selected.hasPosition) {
		ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f", displayInfo.pos.x, displayInfo.pos.y, displayInfo.pos.z);
		ImGui::Spacing();
		ImGui::SliderFloat3("Position Offset", &current.pos.x, -500.f, 500.f, "%.0f");

		ImGui::Spacing();
		ImGui::Spacing();

		auto* flags = reinterpret_cast<uint32_t*>(&current.tesFlags);
		ImGui::Spacing();
		ImGui::Text("Light Flags");
		ImGui::CheckboxFlags("Dynamic", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDynamic));
		ImGui::CheckboxFlags("Negative", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kNegative));
		ImGui::CheckboxFlags("Flicker", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker));
		ImGui::CheckboxFlags("Flicker Slow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlickerSlow));
		ImGui::CheckboxFlags("Pulse", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulse));
		ImGui::CheckboxFlags("Pulse Slow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulseSlow));
		ImGui::CheckboxFlags("Hemi Shadow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow));
		ImGui::CheckboxFlags("Omni Shadow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow));
		ImGui::CheckboxFlags("Portal Strict", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict));
	}
}

std::string LightEditor::GetLightName(LightInfo& lightInfo)
{
	if (lightInfo.isRef)
		return fmt::format("0x{:08X} - {}", lightInfo.id, lightInfo.name.c_str());
	if (lightInfo.isAttached)
		return fmt::format("0x{:08X}|{} - {}", lightInfo.id, lightInfo.index, lightInfo.name.c_str());
	return fmt::format("{:p} - {}", lightInfo.ptr, lightInfo.name.c_str());
}

void LightEditor::GatherLights()
{
	if (!Menu::GetSingleton()->ShouldSwallowInput()) {
		ResetOverrides();
		return;
	}

	if (waitFrames > 0) {
		waitFrames--;
		return;
	}

	bool foundSelected = false;

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& light) {
		const auto bsLight = light.get();
		if (!bsLight)
			return;

		const auto niLight = bsLight->light.get();
		if (!niLight)
			return;

		LightInfo current;
		RE::TESObjectLIGH* ligh = nullptr;

		const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
		const auto refr = niLight->GetUserData();
		if (refr) {
			if (refr->IsDisabled())
				return;
			if (auto* objRef = refr->GetObjectReference()) {
				if (objRef->GetFormType() == RE::FormType::Light)
					ligh = objRef->As<RE::TESObjectLIGH>();
				current.id = refr->GetFormID();
				current.name = clib_util::editorID::get_editorID(objRef);
				current.index = lightsAttached[refr]++;
			}
		}

		current.isRef = ligh != nullptr;

		if (!current.isRef && runtimeData->lighFormId != 0)
			ligh = RE::TESForm::LookupByID(runtimeData->lighFormId)->As<RE::TESObjectLIGH>();

		current.isSpotlight = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow);
		const bool isShadow = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kHemiShadow, RE::TES_LIGHT_FLAGS::kOmniShadow, RE::TES_LIGHT_FLAGS::kSpotShadow);

		totalLightCount++;
		if (isShadow)
			activeShadowLightCount++;

		if ((shadowsOnly) && (!ligh || !isShadow)) {
			return;
		}

		current.isAttached = !current.isRef && refr != nullptr;
		current.isOther = (!current.isRef && !current.isAttached) || (current.isSpotlight);

		const bool isRefMatch = (current.isRef && !current.isSpotlight) && filterOption == FilterOption::RefLights;
		const bool isAttachedMatch = current.isAttached && filterOption == FilterOption::AttachedLights;
		const bool isOtherMatch = current.isOther && filterOption == FilterOption::OtherLights;

		if (!(isRefMatch || isAttachedMatch || isOtherMatch))
			return;

		if (current.isRef) {
			current.position = refr->GetPosition();
			current.hasPosition = true;
		} else if (niLight->parent) {
			current.position = niLight->parent->world.translate;
			current.hasPosition = true;
		}
		if (current.isOther) {
			current.ptr = reinterpret_cast<void*>(niLight);
			if (current.name.empty())
				current.name = niLight->name.c_str();
			current.index = 0;
		}

		current.isSelected = selected == current;

		lights.push_back(current);

		if (!current.isSelected)
			return;
		selected = current;
		foundSelected = true;
		UpdateSelectedLight(refr, ligh, niLight);
	};

	lights.clear();
	lightsAttached.clear();
	totalLightCount = 0;
	activeShadowLightCount = 0;
	const auto smState = globals::game::smState;
	const auto shadowSceneNode = smState->shadowSceneNode[0];

	const auto& activeLights = shadowSceneNode->GetRuntimeData().activeLights;

	for (auto& light : activeLights) {
		addLight(light);
	}

	const auto& activeShadowLights = shadowSceneNode->GetRuntimeData().activeShadowLights;

	for (auto& light : activeShadowLights) {
		addLight(light);
	}

	if (!foundSelected) {
		RestoreOriginal();
		previous = {};
		selected = {};
	}

	SortLights();
}

void LightEditor::ResetOverrides()
{
	RestoreOriginal();
	selected = {};
	previous = {};
}

void LightEditor::UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight)
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	auto tesFlags = ligh ? &ligh->data.flags : nullptr;

	if (previous != selected) {
		RestoreOriginal();

		original.tesFlags = tesFlags ? static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(tesFlags->underlying()) : static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(0);
		original.data = *runtimeData;
		original.pos = selected.isRef ? refr->GetPosition() : (niLight->parent ? niLight->parent->local.translate : RE::NiPoint3{});

		current = original;
		current.pos = { 0, 0, 0 };

		lpInfo = selected.isAttached ? ParseLPLightName(niLight->name.c_str()) : LPLightInfo{};
		if (lpInfo.isLPLight && refr) {
			if (auto* baseObj = refr->GetObjectReference()) {
				lpInfo.ownerEditorId = clib_util::editorID::get_editorID(baseObj);
				if (auto* model = baseObj->As<RE::TESModel>()) {
					if (const char* path = model->GetModel())
						lpInfo.ownerModelPath = path;
				}
			}
		}

		activeIsRef = selected.isRef;
		activeRefr = refr;
		activeLigh = ligh;

		previous = selected;
	}

	activeNiLight.reset(niLight);

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		const bool isShadow = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kHemiShadow, RE::TES_LIGHT_FLAGS::kOmniShadow);
		current.data.radius = InverseSquareLighting::CalculateRadius(
			current.data.fade * 4.f, isShadow,
			std::clamp(current.data.cutoffOverride, 0.01f, 1.0f),
			std::clamp(current.data.size, 0.1f, 50.0f));
	}

	if (selected.isRef) {
		const auto currentPos = refr->GetPosition();
		const auto newPos = original.pos + current.pos;
		if (currentPos != newPos) {
			refr->SetPosition(newPos);
			waitFrames = 1;
		}
		displayInfo.pos = newPos;
	} else if (selected.isAttached) {
		if (niLight->parent) {
			const auto currentPos = niLight->parent->local.translate;
			const auto newPos = original.pos + current.pos;
			if (currentPos != newPos) {
				niLight->parent->local.translate = newPos;
				RE::NiUpdateData updateData;
				niLight->parent->Update(updateData);
				waitFrames = 1;
			}
			displayInfo.pos = newPos;
		} else {
			displayInfo.pos = {};
		}
	}

	if (!selected.isOther && refr && tesFlags && current.tesFlags.underlying() != tesFlags->underlying()) {
		*tesFlags = static_cast<RE::TES_LIGHT_FLAGS>(current.tesFlags.underlying());
		refr->Disable();
		refr->Enable(false);
		waitFrames = 1;
	}

	displayInfo.ownerFormId = refr ? refr->GetFormID() : 0;
	displayInfo.ownerEditorId = refr ? clib_util::editorID::get_editorID(refr) : "Unknown";
	displayInfo.baseObjectFormId = refr && refr->GetBaseObject() ? refr->GetBaseObject()->formID : 0;
	displayInfo.ownerLastEditedBy = refr && refr->GetDescriptionOwnerFile() ? refr->GetDescriptionOwnerFile()->fileName : "Unknown";
	displayInfo.cellEditorId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormEditorID() : "Unknown";
	displayInfo.lighFormId = ligh ? ligh->GetFormID() : 0;
	displayInfo.lighEditorId = ligh ? clib_util::editorID::get_editorID(ligh) : "Unknown";
}

bool LightEditor::ApplyOverrides(RE::NiLight* niLight, ISLCommon::RuntimeLightDataExt* runtimeData) const
{
	if (niLight != activeNiLight.get())
		return false;

	runtimeData->diffuse = current.data.diffuse;
	runtimeData->fade = current.data.fade;
	runtimeData->cutoffOverride = current.data.cutoffOverride;
	runtimeData->size = current.data.size;

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare))
		runtimeData->flags.set(LightLimitFix::LightFlags::InverseSquare);
	else
		runtimeData->flags.reset(LightLimitFix::LightFlags::InverseSquare);

	if (current.data.flags.any(LightLimitFix::LightFlags::Linear))
		runtimeData->flags.set(LightLimitFix::LightFlags::Linear);
	else
		runtimeData->flags.reset(LightLimitFix::LightFlags::Linear);

	return true;
}

void LightEditor::RestoreOriginal()
{
	if (!activeNiLight)
		return;

	auto* runtimeData = ISLCommon::RuntimeLightDataExt::Get(activeNiLight.get());
	*runtimeData = original.data;

	if (activeIsRef && activeRefr) {
		activeRefr->SetPosition(original.pos);
	} else if (activeNiLight->parent) {
		activeNiLight->parent->local.translate = original.pos;
		RE::NiUpdateData updateData;
		activeNiLight->parent->Update(updateData);
	}

	if (activeLigh && activeRefr && current.tesFlags.underlying() != original.tesFlags.underlying()) {
		activeLigh->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(original.tesFlags.underlying());
		activeRefr->Disable();
		activeRefr->Enable(false);
	}

	activeNiLight.reset();
	activeRefr = nullptr;
	activeLigh = nullptr;
	activeIsRef = false;
}

LightEditor::LPLightInfo LightEditor::ParseLPLightName(const std::string& name)
{
	LPLightInfo info;

	constexpr std::string_view prefix = "LP_Light[";
	if (!name.starts_with(prefix))
		return info;

	auto bracketEnd = name.find(']');
	if (bracketEnd == std::string::npos)
		return info;

	auto inner = name.substr(prefix.size(), bracketEnd - prefix.size());
	auto pipePos = inner.find('|');
	if (pipePos == std::string::npos)
		return info;

	info.configPath = inner.substr(0, pipePos);
	info.lightEDID = inner.substr(pipePos + 1);

	if (info.configPath.find("..") != std::string::npos) {
		logger::warn("[LightEditor] Rejected LP light name with path traversal: {}", name);
		return info;
	}

	info.isLPLight = true;
	return info;
}

std::string LightEditor::UpdateLPFlags(const std::string& existingFlags, bool inverseSquare, bool linear)
{
	std::vector<std::string> flags;
	if (!existingFlags.empty()) {
		std::istringstream ss(existingFlags);
		std::string flag;
		while (std::getline(ss, flag, '|')) {
			if (flag != "InverseSquare" && flag != "Linear")
				flags.push_back(flag);
		}
	}
	if (inverseSquare)
		flags.push_back("InverseSquare");
	if (linear)
		flags.push_back("Linear");

	std::string result;
	for (size_t i = 0; i < flags.size(); ++i) {
		if (i > 0)
			result += "|";
		result += flags[i];
	}
	return result;
}

bool LightEditor::MatchesLPFilters(const json& lightEntry, RE::TESObjectREFR* refr)
{
	if (!refr)
		return true;

	auto resolveFilterEntry = [](const std::string& entry) -> RE::FormID {
		auto tildePos = entry.find('~');
		if (tildePos == std::string::npos || !entry.starts_with("0x"))
			return 0;
		RE::FormID relativeID;
		try {
			relativeID = static_cast<RE::FormID>(std::stoul(entry.substr(2, tildePos - 2), nullptr, 16));
		} catch (...) {
			return 0;
		}
		std::string plugin = entry.substr(tildePos + 1);
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
			return 0;
		auto* form = dataHandler->LookupForm(relativeID, plugin);
		return form ? form->GetFormID() : 0;
	};

	auto matchesEntry = [&](const std::string& entry) -> bool {
		if (entry.find('~') != std::string::npos) {
			RE::FormID resolvedId = resolveFilterEntry(entry);
			return resolvedId != 0 && resolvedId == refr->GetFormID();
		}
		if (auto* cell = refr->GetParentCell())
			if (entry == cell->GetFormEditorID())
				return true;
		if (auto* worldspace = refr->GetWorldspace()) {
			auto wsEdid = clib_util::editorID::get_editorID(worldspace);
			if (entry == wsEdid)
				return true;
		}
		return false;
	};

	auto getArray = [&](const char* key) -> const json* {
		auto it = lightEntry.find(key);
		return (it != lightEntry.end() && it->is_array()) ? &*it : nullptr;
	};

	auto anyMatches = [&](const json& list) {
		for (const auto& item : list)
			if (item.is_string() && matchesEntry(item.get<std::string>()))
				return true;
		return false;
	};

	if (auto* wl = getArray("whiteList"); wl && !anyMatches(*wl))
		return false;
	if (auto* bl = getArray("blackList"); bl && anyMatches(*bl))
		return false;

	return true;
}

std::array<float, 3> LightEditor::GetJsonVec3(const json& data, const char* key)
{
	auto it = data.find(key);
	if (it != data.end() && it->is_array() && it->size() >= 3 && (*it)[0].is_number() && (*it)[1].is_number() && (*it)[2].is_number())
		return { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
	return { 0.f, 0.f, 0.f };
}

bool LightEditor::SaveToLightPlacer()
{
	if (!lpInfo.isLPLight)
		return false;

	std::filesystem::path filePath = std::filesystem::path("Data\\LightPlacer") / (lpInfo.configPath + ".json");
	if (!std::filesystem::exists(filePath)) {
		logger::warn("[LightEditor] Light Placer config not found: {}", filePath.string());
		return false;
	}

	json configArray;
	{
		std::ifstream inFile(filePath);
		if (!inFile.is_open()) {
			logger::warn("[LightEditor] Failed to open Light Placer config: {}", filePath.string());
			return false;
		}
		try {
			inFile >> configArray;
		} catch (const json::parse_error& e) {
			logger::warn("[LightEditor] Failed to parse Light Placer config: {} - {}", filePath.string(), e.what());
			return false;
		}
	}

	if (!configArray.is_array())
		return false;

	bool found = false;

	auto normalizePath = [](std::string path) -> std::string {
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	};

	auto arrayContainsString = [](const json& arr, const std::function<bool(const std::string&)>& pred) -> bool {
		for (const auto& elem : arr)
			if (elem.is_string() && pred(elem.get<std::string>()))
				return true;
		return false;
	};

	std::string normalizedOwner = normalizePath(lpInfo.ownerModelPath);

	for (auto& entry : configArray) {
		auto lightsIt = entry.find("lights");
		if (lightsIt == entry.end() || !lightsIt->is_array())
			continue;

		auto getArray = [&](const char* key) -> const json* {
			auto it = entry.find(key);
			return (it != entry.end() && it->is_array()) ? &*it : nullptr;
		};

		bool entryMatches = false;
		if (auto* models = getArray("models"); !normalizedOwner.empty() && models)
			entryMatches = arrayContainsString(*models, [&](const std::string& s) { return normalizePath(s) == normalizedOwner; });
		if (!entryMatches)
			if (auto* formIDs = getArray("formIDs"); !lpInfo.ownerEditorId.empty() && formIDs)
				entryMatches = arrayContainsString(*formIDs, [&](const std::string& s) { return s == lpInfo.ownerEditorId; });

		if (!entryMatches)
			continue;

		for (auto& lightEntry : entry["lights"]) {
			if (!lightEntry.contains("data"))
				continue;
			auto& data = lightEntry["data"];
			if (!data.contains("light") || !data["light"].is_string())
				continue;

			std::string edid = data["light"].get<std::string>();
			if (edid != lpInfo.lightEDID)
				continue;

			if (!MatchesLPFilters(lightEntry, activeRefr))
				continue;

			data["color"] = { current.data.diffuse.red, current.data.diffuse.green, current.data.diffuse.blue };
			data["fade"] = current.data.fade;
			data["radius"] = current.data.radius;
			data["cutoff"] = current.data.cutoffOverride;
			data["size"] = current.data.size;

			auto offset = GetJsonVec3(data, "offset");
			data["offset"] = {
				offset[0] + current.pos.x,
				offset[1] + current.pos.y,
				offset[2] + current.pos.z
			};

			std::string existingFlags = data.value("flags", std::string{});
			bool isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);
			bool isLinear = current.data.flags.any(LightLimitFix::LightFlags::Linear);
			std::string newFlags = UpdateLPFlags(existingFlags, isInvSq, isLinear);
			if (!newFlags.empty())
				data["flags"] = newFlags;
			else
				data.erase("flags");

			found = true;
			break;
		}
		if (found)
			break;
	}

	if (!found) {
		logger::warn("[LightEditor] No matching entry found for model '{}' with light EDID '{}' in {}", lpInfo.ownerModelPath, lpInfo.lightEDID, filePath.string());
		return false;
	}

	{
		std::ofstream outFile(filePath);
		if (!outFile.is_open()) {
			logger::warn("[LightEditor] Failed to write Light Placer config: {}", filePath.string());
			return false;
		}
		outFile << configArray.dump(1, '\t');
		outFile.flush();
		if (outFile.fail()) {
			logger::warn("[LightEditor] Failed to write Light Placer config to {}: stream error", filePath.string());
			return false;
		}
	}

	original.pos = original.pos + current.pos;
	current.pos = { 0, 0, 0 };

	logger::info("[LightEditor] Saved light settings to {}", filePath.string());
	return true;
}

void LightEditor::SortLights()
{
	if (filterOption == FilterOption::OtherLights && (sortOption == SortOption::FormID || sortOption == SortOption::EditorID))
		sortOption = SortOption::None;

	switch (sortOption) {
	case SortOption::Distance:
		{
			const auto playerPos = RE::PlayerCharacter::GetSingleton()->GetPosition();
			std::ranges::sort(lights, [&](const LightInfo& a, const LightInfo& b) {
				if (a.hasPosition != b.hasPosition)
					return a.hasPosition;
				return a.position.GetSquaredDistance(playerPos) < b.position.GetSquaredDistance(playerPos);
			});
			break;
		}
	case SortOption::FormID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return (a.id * 10 + a.index) < (b.id * 10 + b.index);
		});
		break;
	case SortOption::EditorID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return a.name < b.name;
		});
		break;
	case SortOption::None:
	default:
		break;
	}
}
