#include "Features/InverseSquareLighting/LightEditor.h"
#include "Features/LightLimitFix.h"
#include "Menu.h"
#include "Util.h"
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <sstream>

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

	if (ImGui::Button("Export All Lights to JSON")) {
		ExportLightsToJson();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export all visible lights with metadata to JSON file with RefID, timestamp, and light data");
	}

	ImGui::SameLine();
	if (ImGui::Button("Export Selected Light")) {
		ExportSelectedLightToJson();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export only the currently selected light to JSON file");
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

	// Create Light Placer compatible format: array of light configurations
	json exportArray = json::array();

	// Group lights by model/reference to create proper Light Placer structure
	std::map<std::string, std::vector<const LightInfo*>> lightsByModel;

	int metadataLightCount = 0;
	for (const auto& light : lights) {
		// Only export lights that have metadata (isRef or isAttached)
		if (light.isRef || light.isAttached) {
			// Use a model identifier - for actual game objects this would be the model path
			// For now, group by owner/type for demo purposes
			std::string modelKey = fmt::format("ISL_Export_Group_{}",
				light.isRef ? "Reference" : "Attached");
			lightsByModel[modelKey].push_back(&light);
			metadataLightCount++;
		}
	}

	// Create Light Placer entries for each model group
	for (const auto& [modelKey, modelLights] : lightsByModel) {
		json modelEntry;

		// Add ISL metadata as a comment (not part of Light Placer spec)
		const auto now = std::chrono::system_clock::now();
		const auto time_t = std::chrono::system_clock::to_time_t(now);
		std::stringstream ss;
		ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");

		// Get current cell info for context
		const auto* tes = RE::TES::GetSingleton();
		const auto* currentCell = tes ? tes->interiorCell : nullptr;
		if (!currentCell && tes) {
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (player) {
				currentCell = player->GetParentCell();
			}
		}

		// Models array - in real usage this would be actual .nif paths
		modelEntry["models"] = json::array({ modelKey + ".nif" });

		// Add export metadata (custom extension)
		modelEntry["_islExportInfo"] = {
			{ "timestamp", ss.str() },
			{ "cellEditorID", currentCell && currentCell->GetFormEditorID() ? currentCell->GetFormEditorID() : "Unknown" },
			{ "filterOption", FilterOptionLabels[static_cast<int>(filterOption)] },
			{ "sortOption", SortOptionLabels[static_cast<int>(sortOption)] }
		};

		// Lights array
		modelEntry["lights"] = json::array();

		for (const auto* light : modelLights) {
			modelEntry["lights"].push_back(CreateLightJsonData(*light));
		}

		exportArray.push_back(modelEntry);
	}

	// Generate filename with timestamp
	const auto exportPath = Util::PathHelpers::GetCommunityShaderPath() / "LightExports";
	try {
		std::filesystem::create_directories(exportPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Failed to create export directory: {}", e.what());
		return;
	}

	const auto now = std::chrono::system_clock::now();
	const auto time_t = std::chrono::system_clock::to_time_t(now);
	std::stringstream timeStream;
	timeStream << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
	const auto filename = fmt::format("ISL_Export_{}.json", timeStream.str());
	const auto filePath = exportPath / filename;

	std::ofstream outFile(filePath);
	if (!outFile.is_open()) {
		logger::warn("Failed to create export file: {}", filePath.string());
		return;
	}

	outFile << exportArray.dump(2);  // Use 2-space indent like the example
	outFile.close();

	logger::info("Successfully exported {} lights with metadata to: {}", metadataLightCount, filePath.string());
}

json LightEditor::CreateLightJsonData(const LightInfo& lightInfo)
{
	// Create Light Placer compatible format
	json lightEntry;

	// Light data section
	json lightData;

	// Basic light properties - using display info when available
	if (lightInfo.isRef || lightInfo.isAttached) {
		lightData["light"] = displayInfo.lighEditorId.empty() ? "DefaultPointLight01" : displayInfo.lighEditorId;
	} else {
		lightData["light"] = "DefaultPointLight01";  // Default for non-ref lights
	}

	// Color in Light Placer format [r, g, b] as 0-1 normalized values
	lightData["color"] = {
		current.data.diffuse.red,
		current.data.diffuse.green,
		current.data.diffuse.blue
	};

	// Radius and fade
	lightData["radius"] = current.data.radius;
	lightData["fade"] = current.data.fade;

	// Add custom metadata for ISL tracking (non-standard but useful)
	lightData["_islMetadata"] = {
		{ "refID", fmt::format("0x{:08X}", lightInfo.id) },
		{ "editorID", lightInfo.name },
		{ "type", lightInfo.isRef ? "Reference" : (lightInfo.isAttached ? "Attached" : "Other") },
		{ "memoryAddress", fmt::format("{:p}", lightInfo.ptr) }
	};

	// Additional settings if this is the selected light
	if (lightInfo == selected && lightInfo.isSelected) {
		// Add size and cutoff if different from default
		if (current.data.size != 0.0f) {
			lightData["size"] = current.data.size;
		}

		// Position offset
		if (current.pos.x != 0.0f || current.pos.y != 0.0f || current.pos.z != 0.0f) {
			lightData["offset"] = {
				current.pos.x,
				current.pos.y,
				current.pos.z
			};
		}

		// Flags in Light Placer format
		std::vector<std::string> flags;
		if (static_cast<bool>(*reinterpret_cast<const uint32_t*>(&current.data.flags) & static_cast<uint32_t>(LightLimitFix::LightFlags::InverseSquare))) {
			// Note: InverseSquare is not a standard Light Placer flag
			lightData["_islMetadata"]["isInverseSquare"] = true;
		}

		// TES flags converted to Light Placer equivalents where possible
		if (!lightInfo.isOther && displayInfo.ownerFormId != 0) {
			auto flagsValue = *reinterpret_cast<const uint32_t*>(&current.tesFlags);
			if (flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict)) {
				flags.push_back("PortalStrict");
			}
			if (flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow)) {
				flags.push_back("Shadow");
			}
			// Store other TES flags in metadata
			lightData["_islMetadata"]["tesFlags"] = {
				{ "dynamic", static_cast<bool>(flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDynamic)) },
				{ "negative", static_cast<bool>(flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kNegative)) },
				{ "flicker", static_cast<bool>(flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker)) },
				{ "flickerSlow", static_cast<bool>(flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlickerSlow)) },
				{ "pulse", static_cast<bool>(flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulse)) },
				{ "pulseSlow", static_cast<bool>(flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulseSlow)) },
				{ "hemiShadow", static_cast<bool>(flagsValue & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow)) }
			};
		}

		if (!flags.empty()) {
			std::string flagsStr;
			for (size_t i = 0; i < flags.size(); ++i) {
				if (i > 0)
					flagsStr += "|";
				flagsStr += flags[i];
			}
			lightData["flags"] = flagsStr;
		}

		// Additional reference metadata
		if (lightInfo.isRef || lightInfo.isAttached) {
			lightData["_islMetadata"]["ownerInfo"] = {
				{ "ownerFormID", fmt::format("0x{:08X}", displayInfo.ownerFormId) },
				{ "ownerEditorID", displayInfo.ownerEditorId },
				{ "baseObjectFormID", fmt::format("0x{:08X}", displayInfo.baseObjectFormId) },
				{ "ownerLastEditedBy", displayInfo.ownerLastEditedBy },
				{ "cellEditorID", displayInfo.cellEditorId }
			};
		}
	}

	// Create the light entry with points array (Light Placer format)
	lightEntry["data"] = lightData;
	lightEntry["points"] = json::array({ json::array({ lightInfo.position.x,
		lightInfo.position.y,
		lightInfo.position.z }) });

	return lightEntry;
}

void LightEditor::ExportSelectedLightToJson()
{
	if (!selected.isSelected) {
		logger::warn("No light is currently selected for export");
		return;
	}

	// Create Light Placer compatible format: array with single entry
	json exportArray = json::array();

	// Add timestamp and context metadata
	const auto now = std::chrono::system_clock::now();
	const auto time_t = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
	ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");

	// Get current cell info for context
	const auto* tes = RE::TES::GetSingleton();
	const auto* currentCell = tes ? tes->interiorCell : nullptr;
	if (!currentCell && tes) {
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			currentCell = player->GetParentCell();
		}
	}

	// Create single model entry for selected light
	json modelEntry;

	// Use a descriptive model name for the selected light
	std::string modelKey = fmt::format("ISL_Selected_Light_Export_{}_{}",
		selected.isRef ? "Reference" : (selected.isAttached ? "Attached" : "Other"),
		selected.id);

	modelEntry["models"] = json::array({ modelKey + ".nif" });

	// Add export metadata (custom extension)
	modelEntry["_islExportInfo"] = {
		{ "timestamp", ss.str() },
		{ "exportType", "selected_light" },
		{ "cellEditorID", currentCell && currentCell->GetFormEditorID() ? currentCell->GetFormEditorID() : "Unknown" },
		{ "selectedLightInfo", { { "refID", fmt::format("0x{:08X}", selected.id) },
								   { "name", selected.name },
								   { "type", selected.isRef ? "Reference" : (selected.isAttached ? "Attached" : "Other") } } }
	};

	// Add player position for reference
	const auto* player = RE::PlayerCharacter::GetSingleton();
	if (player) {
		const auto playerPos = player->GetPosition();
		modelEntry["_islExportInfo"]["playerPosition"] = {
			{ "x", playerPos.x },
			{ "y", playerPos.y },
			{ "z", playerPos.z }
		};
	}

	// Lights array with single light
	modelEntry["lights"] = json::array();
	modelEntry["lights"].push_back(CreateLightJsonData(selected));

	exportArray.push_back(modelEntry);

	// Generate filename with timestamp
	const auto exportPath = Util::PathHelpers::GetCommunityShaderPath() / "LightExports";
	try {
		std::filesystem::create_directories(exportPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Failed to create export directory: {}", e.what());
		return;
	}

	std::stringstream timeStream;
	timeStream << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
	const auto filename = fmt::format("ISL_Selected_{}.json", timeStream.str());
	const auto filePath = exportPath / filename;

	std::ofstream outFile(filePath);
	if (!outFile.is_open()) {
		logger::warn("Failed to create export file: {}", filePath.string());
		return;
	}

	outFile << exportArray.dump(2);  // Use 2-space indent like the example
	outFile.close();

	logger::info("Successfully exported selected light to: {}", filePath.string());
}