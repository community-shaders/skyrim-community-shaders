#include "Features/InverseSquareLighting/LightEditor.h"
#include "Features/LightLimitFix.h"
#include "Menu.h"
#include "Util.h"
#include <chrono>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <format>

void LightEditor::DrawSettings()
{
	ImGui::Checkbox("Enable Light Editor", &enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Allows for modifying lights in real-time to preview changes. "
			"Changes cannot be saved directly and it is not intended for gameplay use.");
	}

	if (!enabled)
		return;

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Checkbox("Disable Regular Falloff Lights", &disableRegularLights);
	ImGui::Checkbox("Disable Inverse Square Falloff Lights", &disableInvSqLights);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	int selectedFilter = static_cast<int>(filterOption);
	if (ImGui::Combo("Filter By", &selectedFilter, FilterOptionLabels, static_cast<int>(FilterOption::Count))) {
		filterOption = static_cast<FilterOption>(selectedFilter);
	}

	int selectedSort = static_cast<int>(sortOption);
	if (ImGui::Combo("Sort By", &selectedSort, SortOptionLabels, static_cast<int>(SortOption::Count))) {
		sortOption = static_cast<SortOption>(selectedSort);
	}

	if (ImGui::Button("Export Lights to JSON")) {
		ExportLightsToJson();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export all visible lights to JSON file with RefID, timestamp, and light data");
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

	ImGui::Spacing();
	ImGui::Spacing();

	ImGui::CheckboxFlags("Inverse Square Light", reinterpret_cast<uint32_t*>(&current.data.flags), static_cast<uint32_t>(LightLimitFix::LightFlags::InverseSquare));

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

	if (!selected.isOther && current.data.lighFormId != 0) {
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
	if (!enabled || !Menu::GetSingleton()->ShouldSwallowInput())
		return;

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

		if (ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow))
			return;

		current.isOther = ligh == nullptr;
		current.isAttached = refr && !current.isRef && !current.isOther;

		const bool isRefMatch = current.isRef && filterOption == FilterOption::RefLights;
		const bool isAttachedMatch = current.isAttached && filterOption == FilterOption::AttachedLights;
		const bool isOtherMatch = current.isOther && filterOption == FilterOption::OtherLights;

		if (!(isRefMatch || isAttachedMatch || isOtherMatch))
			return;

		if (current.isRef) {
			current.position = refr->GetPosition();
		} else if (current.isAttached) {
			current.position = niLight->parent->world.translate;
		}
		if (current.isOther) {
			current.ptr = reinterpret_cast<void*>(niLight);
			current.name = niLight->name;
			current.position = niLight->parent->world.translate;
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
		previous = selected;
		selected = {};
	}

	SortLights();
}

void LightEditor::UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight)
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	auto tesFlags = ligh ? &ligh->data.flags : nullptr;

	if (previous != selected) {
		original.tesFlags = tesFlags ? static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(tesFlags->underlying()) : static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(0);
		original.data = *runtimeData;
		original.pos = selected.isRef ? refr->GetPosition() : niLight->parent->local.translate;
		current = original;
		current.pos = { 0, 0, 0 };
		previous = selected;
	}

	runtimeData->diffuse = current.data.diffuse;
	runtimeData->fade = current.data.fade;

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		current.data.radius = runtimeData->radius;
		runtimeData->cutoffOverride = std::clamp(current.data.cutoffOverride, 0.01f, 1.0f);
		runtimeData->size = std::clamp(current.data.size, 0.1f, 50.0f);
	} else {
		runtimeData->radius = current.data.radius;
		runtimeData->cutoffOverride = current.data.cutoffOverride;
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
		const auto currentPos = niLight->parent->local.translate;
		const auto newPos = original.pos + current.pos;
		if (currentPos != newPos) {
			niLight->parent->local.translate = newPos;
			RE::NiUpdateData updateData;
			niLight->parent->Update(updateData);
			waitFrames = 1;
		}
		displayInfo.pos = newPos;
	}

	if (!selected.isOther && refr && tesFlags && current.tesFlags.underlying() != tesFlags->underlying()) {
		*tesFlags = static_cast<RE::TES_LIGHT_FLAGS>(current.tesFlags.underlying());
		refr->Disable();
		refr->Enable(false);
		waitFrames = 1;
	}

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare))
		runtimeData->flags.set(LightLimitFix::LightFlags::InverseSquare);
	else
		runtimeData->flags.reset(LightLimitFix::LightFlags::InverseSquare);

	displayInfo.ownerFormId = refr ? refr->GetFormID() : 0;
	displayInfo.ownerEditorId = refr ? clib_util::editorID::get_editorID(refr) : "Unknown";
	displayInfo.baseObjectFormId = refr && refr->GetBaseObject() ? refr->GetBaseObject()->formID : 0;
	displayInfo.ownerLastEditedBy = refr && refr->GetDescriptionOwnerFile() ? refr->GetDescriptionOwnerFile()->fileName : "Unknown";
	displayInfo.cellEditorId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormEditorID() : "Unknown";
	displayInfo.lighFormId = ligh ? ligh->GetFormID() : 0;
	displayInfo.lighEditorId = ligh ? clib_util::editorID::get_editorID(ligh) : "Unknown";
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

void LightEditor::ExportLightsToJson()
{
	if (lights.empty()) {
		logger::warn("No lights available to export");
		return;
	}

	json exportData;
	
	// Add timestamp
	const auto now = std::chrono::system_clock::now();
	const auto time_t = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
	ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
	exportData["timestamp"] = ss.str();

	// Add current scene context
	const auto* tes = RE::TES::GetSingleton();
	const auto* currentCell = tes ? tes->interiorCell : nullptr;
	if (!currentCell && tes) {
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			currentCell = player->GetParentCell();
		}
	}
	
	// Cell information
	if (currentCell) {
		exportData["cell"] = {
			{"formID", fmt::format("0x{:08X}", currentCell->GetFormID())},
			{"editorID", currentCell->GetFormEditorID() ? currentCell->GetFormEditorID() : "Unknown"},
			{"isInterior", currentCell->IsInteriorCell()}
		};
	} else {
		exportData["cell"] = {
			{"formID", "0x00000000"},
			{"editorID", "Unknown"},
			{"isInterior", false}
		};
	}

	// Player position for reference
	const auto* player = RE::PlayerCharacter::GetSingleton();
	if (player) {
		const auto playerPos = player->GetPosition();
		exportData["playerPosition"] = {
			{"x", playerPos.x},
			{"y", playerPos.y},
			{"z", playerPos.z}
		};
	}

	// Filter and sort settings used for this export
	exportData["exportSettings"] = {
		{"filterOption", FilterOptionLabels[static_cast<int>(filterOption)]},
		{"sortOption", SortOptionLabels[static_cast<int>(sortOption)]},
		{"lightCount", lights.size()}
	};

	// Add light data
	exportData["lights"] = json::array();
	for (const auto& light : lights) {
		exportData["lights"].push_back(CreateLightJsonData(light));
	}

	// Generate filename with timestamp
	const auto exportPath = Util::PathHelpers::GetCommunityShaderPath() / "LightExports";
	try {
		std::filesystem::create_directories(exportPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Failed to create export directory: {}", e.what());
		return;
	}

	const auto filename = fmt::format("lights_export_{:%Y%m%d_%H%M%S}.json", 
		std::chrono::system_clock::now());
	const auto filePath = exportPath / filename;

	std::ofstream outFile(filePath);
	if (!outFile.is_open()) {
		logger::warn("Failed to create export file: {}", filePath.string());
		return;
	}

	outFile << exportData.dump(4);
	outFile.close();

	logger::info("Successfully exported {} lights to: {}", lights.size(), filePath.string());
}

json LightEditor::CreateLightJsonData(const LightInfo& lightInfo)
{
	json lightData;

	// Basic light info - using refID as the main identifier for compatibility
	lightData["refID"] = fmt::format("0x{:08X}", lightInfo.id);
	lightData["editorID"] = lightInfo.name;
	lightData["index"] = lightInfo.index;
	lightData["type"] = lightInfo.isRef ? "Reference" : (lightInfo.isAttached ? "Attached" : "Other");
	lightData["memoryAddress"] = fmt::format("{:p}", lightInfo.ptr);

	// Position - using standard light placer format
	lightData["position"] = {
		{"x", lightInfo.position.x},
		{"y", lightInfo.position.y},
		{"z", lightInfo.position.z}
	};

	// If this is the selected light, include detailed settings
	if (lightInfo == selected && lightInfo.isSelected) {
		lightData["settings"] = {
			{"color", {
				{"r", current.data.diffuse.red},
				{"g", current.data.diffuse.green},
				{"b", current.data.diffuse.blue}
			}},
			{"intensity", current.data.fade},
			{"radius", current.data.radius},
			{"size", current.data.size},
			{"cutoffOverride", current.data.cutoffOverride},
			{"isInverseSquare", current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)}
		};

		// Position offset if applicable
		if (!lightInfo.isOther) {
			lightData["settings"]["positionOffset"] = {
				{"x", current.pos.x},
				{"y", current.pos.y},
				{"z", current.pos.z}
			};
		}

		// TES flags if applicable
		if (!lightInfo.isOther && displayInfo.ownerFormId != 0) {
			lightData["settings"]["tesFlags"] = {
				{"dynamic", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kDynamic)},
				{"negative", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kNegative)},
				{"flicker", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kFlicker)},
				{"flickerSlow", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kFlickerSlow)},
				{"pulse", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kPulse)},
				{"pulseSlow", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kPulseSlow)},
				{"hemiShadow", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kHemiShadow)},
				{"omniShadow", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kOmniShadow)},
				{"portalStrict", current.tesFlags.any(RE::TES_LIGHT_FLAGS::kPortalStrict)}
			};
		}

		// Additional metadata for reference/attached lights
		if (lightInfo.isRef || lightInfo.isAttached) {
			lightData["metadata"] = {
				{"ownerFormID", fmt::format("0x{:08X}", displayInfo.ownerFormId)},
				{"ownerEditorID", displayInfo.ownerEditorId},
				{"baseObjectFormID", fmt::format("0x{:08X}", displayInfo.baseObjectFormId)},
				{"ownerLastEditedBy", displayInfo.ownerLastEditedBy},
				{"cellEditorID", displayInfo.cellEditorId},
				{"lightFormID", fmt::format("0x{:08X}", displayInfo.lighFormId)},
				{"lightEditorID", displayInfo.lighEditorId}
			};
		}
	}

	return lightData;
}