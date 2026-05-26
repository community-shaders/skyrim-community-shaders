#include "LightEditor.h"
#include "../Features/InverseSquareLighting.h"
#include "../Features/LightLimitFix.h"
#include "../Menu.h"
#include "../Utils/UI.h"
#include "EditorWindow.h"
#include "WeatherUtils.h"
#include "RE/B/BSLight.h"
#include "RE/B/BSShadowLight.h"
#include "RE/E/ExtraEmittanceSource.h"

#include <array>
#include <filesystem>
#include <numbers>
#include <fstream>
#include <regex>
#include <sstream>

std::vector<std::pair<std::string, RE::TESObjectLIGH*>> LightEditor::s_lighFormList;
std::vector<std::pair<std::string, RE::TESForm*>> LightEditor::s_emittanceFormList;

// Returns the named array member of a JSON object, or nullptr if missing / not an array.
static const nlohmann::ordered_json* GetArrayMember(const nlohmann::ordered_json& obj, const char* key)
{
	const auto it = obj.find(key);
	return (it != obj.end() && it->is_array()) ? &*it : nullptr;
}

// Downcast a BSLight to BSShadowLight only when it actually is one. Returns nullptr otherwise.
static RE::BSShadowLight* AsShadowLight(RE::BSLight* light)
{
	return (light && light->IsShadowLight()) ? static_cast<RE::BSShadowLight*>(light) : nullptr;
}

static void ScheduleConsoleCommand(std::string cmd, RE::TESObjectREFR* refr = nullptr)
{
	if (auto* taskInterface = SKSE::GetTaskInterface()) {
		taskInterface->AddTask([cmd = std::move(cmd), refr]() {
			// Write the console selected-ref global directly (same RELOCATION_ID as
			// CommonLibSSE-NG/src/RE/C/Console.cpp:32 Console::GetSelectedRefHandle).
			// Console::SetSelectedRef requires a Console instance that only exists
			// while the console UI is open, so we write the global directly.
			// We also pass refr to CompileAndRun as thisObj to cover command handlers
			// that read from the script parameter rather than the console global.
			static REL::Relocation<RE::ObjectRefHandle*> selectedRef{
				RELOCATION_ID(519394, AE_CHECK(SKSE::RUNTIME_SSE_1_6_1130, 405935, 504099))
			};

			RE::ObjectRefHandle prevHandle;
			if (refr) {
				prevHandle = *selectedRef;
				*selectedRef = RE::ObjectRefHandle(refr);
			}

			const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			if (auto* script = factory ? static_cast<RE::Script*>(factory->Create()) : nullptr) {
				script->SetCommand(cmd);
				script->CompileAndRun(refr);
				delete script;
			}

			if (refr)
				*selectedRef = prevHandle;
		});
	}
}

void LightEditor::EnsureEmittanceFormListBuilt()
{
	if (!s_emittanceFormList.empty())
		return;
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh)
		return;
	auto containsCaseInsensitive = [](const std::string& str, std::string_view needle) {
		return std::ranges::search(str, needle, [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		}).begin() != str.end();
	};

	auto addForms = [&](auto& formArray, RE::FormType expectedType) {
		for (auto* form : formArray) {
			if (!form || form->formID == 0 || form->GetFormType() != expectedType)
				continue;
			std::string edid = clib_util::editorID::get_editorID(form);
			if (edid.empty())
				continue;
			if (!containsCaseInsensitive(edid, "fx") && !containsCaseInsensitive(edid, "weather"))
				continue;
			s_emittanceFormList.emplace_back(std::move(edid), static_cast<RE::TESForm*>(form));
		}
	};
	addForms(dh->GetFormArray<RE::TESRegion>(), RE::FormType::Region);
	std::ranges::sort(s_emittanceFormList, [](const auto& a, const auto& b) { return a.first < b.first; });
}


RE::FormID LightEditor::ResolveFormEntry(const std::string& entry)
{
	const auto tildePos = entry.find('~');
	const bool hasPrefix = entry.starts_with("0x") || entry.starts_with("0X");
	if (tildePos == std::string::npos) {
		if (!hasPrefix)
			return 0;
		try { return static_cast<RE::FormID>(std::stoul(entry.substr(2), nullptr, 16)); }
		catch (...) { return 0; }
	}
	const auto hexStart = hasPrefix ? 2 : 0;
	RE::FormID relativeID;
	try { relativeID = static_cast<RE::FormID>(std::stoul(entry.substr(hexStart, tildePos - hexStart), nullptr, 16)); }
	catch (...) { return 0; }
	auto* dh = RE::TESDataHandler::GetSingleton();
	auto* form = dh ? dh->LookupForm(relativeID, entry.substr(tildePos + 1)) : nullptr;
	return form ? form->GetFormID() : 0;
}

void LightEditor::ApplyLighFormData(const RE::TESObjectLIGH* ligh)
{
	current.data.lighFormId = ligh->formID;

	current.data.flags.reset(LightLimitFix::LightFlags::InverseSquare);
	current.data.flags.reset(LightLimitFix::LightFlags::Linear);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kInverseSquare)))
		current.data.flags.set(LightLimitFix::LightFlags::InverseSquare);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kLinear)))
		current.data.flags.set(LightLimitFix::LightFlags::Linear);

	const float size = ligh->data.fov >= 50.f ? std::numbers::sqrt2_v<float> : ligh->data.fov;
	current.data.size = std::clamp(size, 0.01f, 50.f);
	current.data.cutoffOverride = std::clamp(ligh->data.fallofExponent, 0.01f, 1.f);
	current.data.radius = static_cast<float>(ligh->data.radius);
	current.data.fade = ligh->fade;
	current.data.diffuse.red   = ligh->data.color.red   / 255.f;
	current.data.diffuse.green = ligh->data.color.green / 255.f;
	current.data.diffuse.blue  = ligh->data.color.blue  / 255.f;
}

void LightEditor::EnsureLighFormListBuilt()
{
	if (!s_lighFormList.empty())
		return;
	if (auto* dh = RE::TESDataHandler::GetSingleton()) {
		for (auto* form : dh->GetFormArray<RE::TESObjectLIGH>()) {
			if (!form || form->formID == 0)
				continue;
			std::string edid = clib_util::editorID::get_editorID(form);
			if (!edid.empty())
				s_lighFormList.emplace_back(std::move(edid), form);
		}
		std::ranges::sort(s_lighFormList, [](const auto& a, const auto& b) { return a.first < b.first; });
	}
}

void LightEditor::DrawSettings()
{
	// Header
	ImGui::Text("Light Editor");
	ImGui::Separator();

	ImGui::Checkbox("Disable Regular Falloff Lights", &disableRegularLights);
	ImGui::Checkbox("Disable Inverse Square Falloff Lights", &disableInvSqLights);

	if (ImGui::Button("Toggle All LP Lights")) {
		ScheduleConsoleCommand("tlp 0");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Toggle all Light Placer lights on/off (tlp 0).");
	}

	ImGui::SameLine();
	if (ImGui::Button("Toggle LP Markers")) {
		ScheduleConsoleCommand("tlp 1");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Toggle Light Placer debug markers (tlp 1).");
	}

	ImGui::SameLine();
	if (ImGui::Button("Reload LP")) {
		RestoreOriginal();
		previous = {};
		waitFrames = 3;
		ScheduleConsoleCommand("reloadlp");
		EditorWindow::GetSingleton()->ShowNotification("Reloading Light Placer configs...", Util::Colors::GetInfo());
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Reload all Light Placer JSON configs in-game (reloadlp).");
	}

	ImGui::Separator();
	
	ImGui::Text("Total Lights: %u", totalLightCount);
	ImGui::Text("Active Shadow Lights: %u", activeShadowLightCount);
	ImGui::Separator();

	{
		const auto& style = ImGui::GetStyle();
		const float arrowWidth = ImGui::GetFrameHeight();
		const float filterComboWidth = ImGui::CalcTextSize("Attached Lights").x + style.FramePadding.x * 2 + arrowWidth;
		const float sortComboWidth = ImGui::CalcTextSize("EditorID").x + style.FramePadding.x * 2 + arrowWidth;

		ImGui::SetNextItemWidth(filterComboWidth);
		int selectedFilter = static_cast<int>(filterOption);
		if (ImGui::Combo("##Type", &selectedFilter, FilterOptionLabels, static_cast<int>(FilterOption::Count))) {
			filterOption = static_cast<FilterOption>(selectedFilter);
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(sortComboWidth);
		int selectedSort = static_cast<int>(sortOption);
		if (ImGui::Combo("##Sorting", &selectedSort, SortOptionLabels, static_cast<int>(SortOption::Count))) {
			sortOption = static_cast<SortOption>(selectedSort);
		}

		ImGui::SameLine();
		ImGui::Checkbox("Shadows Only", &shadowsOnly);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Only show lights with HemiShadow or OmniShadow flags.");
		}
	}

	static constexpr const char* kLightsComboId = "LightsCombo";
	if (ImGui::BeginCombo("Lights", selected.isSelected ? GetLightName(selected).c_str() : "Select a light")) {
		auto searchText = Util::DrawComboSearchInput(kLightsComboId);
		for (auto& light : lights) {
			const auto displayName = GetLightName(light);
			if (!searchText.empty() && !Util::StringMatchesSearch(displayName, searchText))
				continue;
			const bool isSelected = light == selected;
			if (ImGui::Selectable(displayName.c_str(), isSelected)) {
				selected = light;
				Util::ClearComboSearch(kLightsComboId);
			}
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	} else {
		Util::ClearComboSearch(kLightsComboId);
	}

	ImGui::Separator();

	if (!selected.isSelected)
		return;

	if (selected.isRef || selected.isAttached) {
		ImGui::Text("Owner: 0x%08X | %s", selected.id, displayInfo.ownerEditorId.c_str());
		ImGui::Text("Owner last edited by: %s", displayInfo.ownerLastEditedBy.c_str());
		ImGui::Text("Base Object: 0x%08X | %s", displayInfo.baseObjectFormId, selected.name.c_str());
		ImGui::Text("LIGH: 0x%08X | %s", displayInfo.lighFormId, displayInfo.lighEditorId.c_str());
		ImGui::Text("Cell: 0x%08X | %s", displayInfo.cellFormId, displayInfo.cellEditorId.c_str());
	} else {
		ImGui::Text("Memory Address: %p", selected.ptr);
		ImGui::Text("NiLight Name: %s", selected.name.c_str());
	}

	ImGui::Separator();

	if (ImGui::Button("Reset")) {
		current = original;
		shadowDepthBias = originalShadowDepthBias;
		ApplyShadowDepthBias();
		waitFrames = 1;
	}

	ImGui::SameLine();
	if (ImGui::Button("Toggle Light")) {
		if (lpInfo.isLPLight && activeRefr)
			ScheduleConsoleCommand("tlp 0", activeRefr);
		else if (current.data.fade == 0.0f)
			current.data.fade = cachedFadeBeforeToggle;
		else {
			cachedFadeBeforeToggle = current.data.fade;
			current.data.fade = 0.0f;
		}
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (lpInfo.isLPLight)
			ImGui::Text("Toggle this reference's LP-placed lights on/off (tlp 0).");
		else
			ImGui::Text("Toggle this light on/off.");
	}

	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		{
			auto _style = Util::StatusButtonStyle(lpMatchFound ? Util::Colors::GetSuccess() : Util::Colors::GetError());
			if (ImGui::Button("Save to Light Placer")) {
				const bool ok = SaveToLightPlacer(saveColorToLP);
				if (ok) {
					ScheduleConsoleCommand("reloadlp");
					previous = {};
					waitFrames = 3;
					lpMatchFound = true;
				}
				EditorWindow::GetSingleton()->ShowNotification(
					ok ? fmt::format("Saved to {}", lpInfo.configPath) : "Save failed — see log",
					ok ? Util::Colors::GetSuccess() : Util::Colors::GetError());
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (lpMatchFound)
				ImGui::Text("Matching entry found in %s.\nSave current settings to the Light Placer JSON.", lpInfo.configPath.c_str());
			else
				ImGui::Text("No matching entry found in %s.\nSaving will fail.", lpInfo.configPath.c_str());
		}
	}
	ImGui::SameLine();
	ImGui::Checkbox("Log Mode", &extendedLogMode);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Extend slider ranges and use a logarithmic scale.");
	}

	ImGui::Spacing();

	if (selected.isAttached) {
		EnsureLighFormListBuilt();
		if (lpInfo.isLPLight && useExternalEmittance)
			EnsureEmittanceFormListBuilt();
		const char* previewEdid = "(Original)";
		for (auto& [edid, ligh] : s_lighFormList)
			if (ligh->GetFormID() == current.data.lighFormId) { previewEdid = edid.c_str(); break; }

		static constexpr const char* kLighOverrideId = "LighFormOverride";
		if (ImGui::BeginCombo("Bulb type##combo", previewEdid)) {
			auto searchText = Util::DrawComboSearchInput(kLighOverrideId);
			if (searchText.empty() || Util::StringMatchesSearch("(Original)", searchText)) {
				if (ImGui::Selectable("(Original)", current.data.lighFormId == original.data.lighFormId)) {
					current.data = original.data;
					Util::ClearComboSearch(kLighOverrideId);
				}
				if (current.data.lighFormId == original.data.lighFormId)
					ImGui::SetItemDefaultFocus();
			}
			for (auto& [edid, ligh] : s_lighFormList) {
				if (!searchText.empty() && !Util::StringMatchesSearch(edid, searchText))
					continue;
				const bool isCurrent = ligh->GetFormID() == current.data.lighFormId;
				if (ImGui::Selectable(edid.c_str(), isCurrent)) {
					const float savedFade     = current.data.fade;
					const float savedRadius   = current.data.radius;
					const float savedSize     = current.data.size;
					const float savedCutoff   = current.data.cutoffOverride;
					ApplyLighFormData(ligh);
					current.data.fade          = savedFade;
					current.data.radius        = savedRadius;
					current.data.size          = savedSize;
					current.data.cutoffOverride = savedCutoff;
					Util::ClearComboSearch(kLighOverrideId);
				}
				if (isCurrent)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		} else {
			Util::ClearComboSearch(kLighOverrideId);
		}

		if (lpInfo.isLPLight && useExternalEmittance) {
			static constexpr const char* kEmittanceComboId = "EmittanceFormCombo";
			const char* preview = externalEmittanceEdid.empty() ? "(None)" : externalEmittanceEdid.c_str();
			if (ImGui::BeginCombo("External Emittance##combo", preview)) {
				auto searchText = Util::DrawComboSearchInput(kEmittanceComboId);
				if (searchText.empty() || Util::StringMatchesSearch("(None)", searchText)) {
					if (ImGui::Selectable("(None)", externalEmittanceEdid.empty())) {
						externalEmittanceEdid = {};
						Util::ClearComboSearch(kEmittanceComboId);
					}
					if (externalEmittanceEdid.empty())
						ImGui::SetItemDefaultFocus();
				}
				for (auto& [edid, form] : s_emittanceFormList) {
					if (!searchText.empty() && !Util::StringMatchesSearch(edid, searchText))
						continue;
					const bool isCurrent = edid == externalEmittanceEdid;
					if (ImGui::Selectable(edid.c_str(), isCurrent)) {
						externalEmittanceEdid = edid;
						Util::ClearComboSearch(kEmittanceComboId);
					}
					if (isCurrent)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			} else {
				Util::ClearComboSearch(kEmittanceComboId);
			}
		}
	}

	ImGui::Spacing();

	WeatherUtils::DrawColorEdit("Color", reinterpret_cast<float3&>(current.data.diffuse));
	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		ImGui::Checkbox("Save##color", &saveColorToLP);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Include color when saving to Light Placer.\nWhen unchecked, the light falls back to the LIGH form color.");
		}
	}

	// Dispatches to logarithmic+extended or normal slider based on extendedLogMode.
	// Log scale requires min > 0, so extended mins are nudged above zero where needed.
	auto drawSlider = [&](const char* label, float& value,
	                       float normalMin, float normalMax,
	                       float extMin, float extMax,
	                       const char* format) -> bool {
		if (extendedLogMode)
			return ImGui::SliderFloat(label, &value, extMin, extMax, format, ImGuiSliderFlags_Logarithmic);
		return static_cast<bool>(WeatherUtils::DrawSliderFloat(label, value, normalMin, normalMax, nullptr, format));
	};

	drawSlider("Intensity", current.data.fade, 0.01f, 16.f, 0.01f, 1024.f, "%.3f");

	const auto isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);

	if (isInvSq)
		ImGui::BeginDisabled();
	drawSlider("Radius", current.data.radius, 2.f, 8096.f, 2.f, 65536.f, "%.0f");
	if (isInvSq)
		ImGui::EndDisabled();

	if (isInvSq) {
		drawSlider("Size", current.data.size, 0.01f, 10.0f, 0.001f, 100.f, "%.3f");
		WeatherUtils::DrawSliderFloat("Cutoff", current.data.cutoffOverride, 0.01f, 1.f, nullptr, "%.3f");
	}

	if (HasShadowFlags(current.tesFlags.underlying())) {
		if (drawSlider("Shadow Depth Bias", shadowDepthBias, 0.0f, 10.0f, 0.01f, 50.f, "%.2f"))
			ApplyShadowDepthBias();
	}

	ImGui::Spacing();

	if (!selected.isOther && current.data.lighFormId != 0 && selected.hasPosition) {
		ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f", displayInfo.pos.x, displayInfo.pos.y, displayInfo.pos.z);
		ImGui::Spacing();
		ImGui::SliderFloat3("Position", &current.pos.x, -1000.f, 1000.f, "%.0f");

		ImGui::Spacing();

		auto* flags = reinterpret_cast<uint32_t*>(&current.tesFlags);
		auto* runtimeFlags = reinterpret_cast<uint32_t*>(&current.data.flags);
		ImGui::Text("Light Flags");

		// Inverse Square is disabled for spotlights since they have their own falloff model.
		ImGui::BeginDisabled(selected.isSpotlight);
		ImGui::CheckboxFlags("Inverse Square", runtimeFlags, static_cast<uint32_t>(LightLimitFix::LightFlags::InverseSquare));
		ImGui::EndDisabled();
		ImGui::CheckboxFlags("Linear", runtimeFlags, static_cast<uint32_t>(LightLimitFix::LightFlags::Linear));

		static constexpr std::pair<const char*, RE::TES_LIGHT_FLAGS> kTesFlagCheckboxes[] = {
			{ "Dynamic",       RE::TES_LIGHT_FLAGS::kDynamic      },
			{ "Negative",      RE::TES_LIGHT_FLAGS::kNegative     },
			{ "Flicker",       RE::TES_LIGHT_FLAGS::kFlicker      },
			{ "Flicker Slow",  RE::TES_LIGHT_FLAGS::kFlickerSlow  },
			{ "Pulse",         RE::TES_LIGHT_FLAGS::kPulse        },
			{ "Pulse Slow",    RE::TES_LIGHT_FLAGS::kPulseSlow    },
			{ "Hemi Shadow",   RE::TES_LIGHT_FLAGS::kHemiShadow   },
			{ "Omni Shadow",   RE::TES_LIGHT_FLAGS::kOmniShadow   },
			{ "Portal Strict", RE::TES_LIGHT_FLAGS::kPortalStrict },
		};
		for (const auto& [label, flag] : kTesFlagCheckboxes)
			ImGui::CheckboxFlags(label, flags, static_cast<uint32_t>(flag));

		if (lpInfo.isLPLight)
			ImGui::Checkbox("External Emittance##flag", &useExternalEmittance);
	}
}

bool LightEditor::HasShadowFlags(uint32_t tesFlags)
{
	return (tesFlags & (static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow) |
	                    static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow) |
	                    static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kSpotShadow))) != 0;
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

	if (!selected.isSelected && savedSelection.isSelected) {
		selected = savedSelection;
		savedSelection = {};
	}

	// Skip a few frames after disruptive operations (reloadlp, Disable/Enable, position change)
	// so the game has time to rebuild the light list before we resample it.
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

		if (!current.isRef && runtimeData->lighFormId != 0) {
			if (auto* lighForm = RE::TESForm::LookupByID(runtimeData->lighFormId))
				ligh = lighForm->As<RE::TESObjectLIGH>();
		}

		current.isSpotlight = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow);
		const bool isShadow = ligh && HasShadowFlags(ligh->data.flags.underlying());

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
		UpdateSelectedLight(refr, ligh, niLight, bsLight);
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
	if (selected.isSelected)
		savedSelection = selected;
	RestoreOriginal();
	selected = {};
	previous = {};
}

void LightEditor::ApplyShadowDepthBias()
{
	if (auto* shadowLight = AsShadowLight(activeBsLight.get()))
		shadowLight->GetRuntimeData().shadowBiasScale = shadowDepthBias;
}

void LightEditor::UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight, RE::BSLight* bsLight)
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	auto tesFlags = ligh ? &ligh->data.flags : nullptr;

	// Per-selection initialization: snapshots the light's original state, populates lpInfo,
	// and runs a dry-run save to determine whether a matching LP JSON entry exists.
	if (previous != selected) {
		RestoreOriginal();

		original.tesFlags = tesFlags ? static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(tesFlags->underlying()) : static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(0);
		original.data = *runtimeData;
		original.pos = selected.isRef ? refr->GetPosition() : (niLight->parent ? niLight->parent->local.translate : RE::NiPoint3{});

		current = original;

		auto* originalShadowLight = AsShadowLight(bsLight);
		originalShadowDepthBias = originalShadowLight ? originalShadowLight->GetRuntimeData().shadowBiasScale : 0.0f;
		shadowDepthBias = originalShadowDepthBias;
		cachedFadeBeforeToggle = 0.0f;

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

		lpMatchFound = lpInfo.isLPLight && SaveToLightPlacer(false, true);

		externalEmittanceEdid = {};
		useExternalEmittance = false;
		if (refr) {
			if (const auto* extra = refr->extraList.GetByType<RE::ExtraEmittanceSource>())
				if (extra->source)
					externalEmittanceEdid = clib_util::editorID::get_editorID(extra->source);
			useExternalEmittance = !externalEmittanceEdid.empty();
		}

		previous = selected;
	}

	activeNiLight.reset(niLight);
	activeBsLight.reset(bsLight);

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		const bool isShadow = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kHemiShadow, RE::TES_LIGHT_FLAGS::kOmniShadow);
		current.data.radius = InverseSquareLighting::CalculateRadius(
			current.data.fade * 4.f, isShadow,
			std::clamp(current.data.cutoffOverride, 0.01f, 1.0f),
			std::clamp(current.data.size, 0.1f, 50.0f));
	}

	if (selected.isRef) {
		const auto currentPos = refr->GetPosition();
		if (currentPos != current.pos) {
			refr->SetPosition(current.pos);
			waitFrames = 1;
		}
		displayInfo.pos = current.pos;
	} else if (selected.isAttached) {
		if (niLight->parent) {
			const auto currentPos = niLight->parent->local.translate;
			if (currentPos != current.pos) {
				niLight->parent->local.translate = current.pos;
				RE::NiUpdateData updateData;
				niLight->parent->Update(updateData);
				waitFrames = 1;
			}
			displayInfo.pos = current.pos;
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
	displayInfo.cellFormId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormID() : 0;
	displayInfo.cellEditorId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormEditorID() : "Unknown";
	displayInfo.lighFormId = ligh ? ligh->GetFormID() : 0;
	displayInfo.lighEditorId = ligh ? clib_util::editorID::get_editorID(ligh) : "Unknown";
}

bool LightEditor::ApplyOverrides(RE::NiLight* niLight, ISLCommon::RuntimeLightDataExt* runtimeData) const
{
	if (niLight != activeNiLight.get())
		return false;

	runtimeData->lighFormId = current.data.lighFormId;
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

	if (auto* shadowLight = AsShadowLight(activeBsLight.get()))
		shadowLight->GetRuntimeData().shadowBiasScale = originalShadowDepthBias;

	activeNiLight.reset();
	activeBsLight.reset();
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

std::string LightEditor::UpdateLPFlags(const std::string& existingFlags, bool inverseSquare, bool linear, bool flicker, bool portalStrict, bool shadow)
{
	static constexpr std::array managed = { "InverseSquare", "Linear", "Flicker", "PortalStrict", "Shadow" };

	std::vector<std::string> flags;
	if (!existingFlags.empty()) {
		std::istringstream ss(existingFlags);
		std::string flag;
		while (std::getline(ss, flag, '|')) {
			if (std::find(managed.begin(), managed.end(), flag) == managed.end())
				flags.push_back(flag);
		}
	}
	if (inverseSquare)
		flags.push_back("InverseSquare");
	if (linear)
		flags.push_back("Linear");
	if (flicker)
		flags.push_back("Flicker");
	if (portalStrict)
		flags.push_back("PortalStrict");
	if (shadow)
		flags.push_back("Shadow");

	std::string result;
	for (size_t i = 0; i < flags.size(); ++i) {
		if (i > 0)
			result += "|";
		result += flags[i];
	}
	return result;
}

bool LightEditor::MatchesLPFilters(const nlohmann::ordered_json& lightEntry, RE::TESObjectREFR* refr)
{
	if (!refr)
		return true;

	auto matchesEntry = [&](const std::string& entry) -> bool {
		const bool hasPrefix = entry.starts_with("0x") || entry.starts_with("0X");
		if (entry.find('~') != std::string::npos || hasPrefix) {
			const RE::FormID resolvedId = ResolveFormEntry(entry);
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

	auto anyMatches = [&](const nlohmann::ordered_json& list) {
		for (const auto& item : list)
			if (item.is_string() && matchesEntry(item.get<std::string>()))
				return true;
		return false;
	};

	if (auto* wl = GetArrayMember(lightEntry, "whiteList"); wl && !anyMatches(*wl))
		return false;
	if (auto* bl = GetArrayMember(lightEntry, "blackList"); bl && anyMatches(*bl))
		return false;

	return true;
}

bool LightEditor::SaveToLightPlacer(bool includeColor, bool dryRun)
{
	if (!lpInfo.isLPLight)
		return false;

	std::filesystem::path filePath = std::filesystem::path("Data\\LightPlacer") / (lpInfo.configPath + ".json");
	if (!std::filesystem::exists(filePath)) {
		logger::warn("[LightEditor] Light Placer config not found: {}", filePath.string());
		return false;
	}

	nlohmann::ordered_json configArray;
	{
		std::ifstream inFile(filePath);
		if (!inFile.is_open()) {
			logger::warn("[LightEditor] Failed to open Light Placer config: {}", filePath.string());
			return false;
		}
		try {
			inFile >> configArray;
		} catch (const nlohmann::json::parse_error& e) {
			logger::warn("[LightEditor] Failed to parse Light Placer config: {} - {}", filePath.string(), e.what());
			return false;
		}
	}

	if (!configArray.is_array())
		return false;

	static constexpr std::array managedDataKeys = {
		"color", "light", "fade", "radius", "size", "cutoff", "shadowDepthBias", "externalEmittance", "flags", "offset", "rotation"
	};
	static constexpr std::array managedEntryKeys = { "data", "points", "nodes", "whiteList", "blackList" };

	bool found = false;

	auto normalizePath = [](std::string path) -> std::string {
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	};

	auto arrayContainsString = [](const nlohmann::ordered_json& arr, const std::function<bool(const std::string&)>& pred) -> bool {
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

		bool entryMatches = false;
		if (auto* models = GetArrayMember(entry, "models"); !normalizedOwner.empty() && models)
			entryMatches = arrayContainsString(*models, [&](const std::string& s) { return normalizePath(s) == normalizedOwner; });
		if (!entryMatches) {
			if (auto* formIDs = GetArrayMember(entry, "formIDs"); formIDs) {
				const RE::FormID baseFormId = activeRefr && activeRefr->GetObjectReference()
				                                  ? activeRefr->GetObjectReference()->formID
				                                  : 0;
				entryMatches = arrayContainsString(*formIDs, [&](const std::string& s) -> bool {
					const bool hasPrefix = s.starts_with("0x") || s.starts_with("0X");
					if (s.find('~') == std::string::npos && !hasPrefix)
						return !lpInfo.ownerEditorId.empty() && s == lpInfo.ownerEditorId;
					if (baseFormId == 0)
						return false;
					const RE::FormID resolved = ResolveFormEntry(s);
					return resolved != 0 && resolved == baseFormId;
				});
			}
		}

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

			if (dryRun)
				return true;

			const bool isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);
			const bool isLinear = current.data.flags.any(LightLimitFix::LightFlags::Linear);
			const uint32_t tesUnderlying = current.tesFlags.underlying();
			const bool isFlicker = (tesUnderlying & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker)) != 0;
			const bool isPortalStrict = (tesUnderlying & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict)) != 0;
			const bool isOmniShadow = (tesUnderlying & static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow)) != 0;
			const std::string newFlags = UpdateLPFlags(data.value("flags", std::string{}), isInvSq, isLinear, isFlicker, isPortalStrict, isOmniShadow);

			nlohmann::ordered_json newData;
			if (includeColor || data.contains("color"))
				newData["color"] = { current.data.diffuse.red, current.data.diffuse.green, current.data.diffuse.blue };
			newData["light"] = data["light"];
			newData["fade"] = current.data.fade;
			if (isInvSq) {
				newData["size"] = current.data.size;
				newData["cutoff"] = current.data.cutoffOverride;
			} else {
				newData["radius"] = current.data.radius;
			}
			if (data.contains("shadowDepthBias"))
				newData["shadowDepthBias"] = shadowDepthBias;
			if (useExternalEmittance && !externalEmittanceEdid.empty())
				newData["externalEmittance"] = externalEmittanceEdid;
			else if (!useExternalEmittance && data.contains("externalEmittance"))
				newData["externalEmittance"] = data["externalEmittance"];
			if (!newFlags.empty())
				newData["flags"] = newFlags;
			if (data.contains("offset"))
				newData["offset"] = data["offset"];
			if (data.contains("rotation"))
				newData["rotation"] = data["rotation"];
			for (auto& [key, val] : data.items())
				if (std::ranges::find(managedDataKeys, key) == managedDataKeys.end())
					newData[key] = val;
			data = std::move(newData);

			auto getPointsKey = [&]() -> const char* {
				return lightEntry.contains("points") ? "points" : (lightEntry.contains("nodes") ? "nodes" : nullptr);
			};
			if (const char* key = getPointsKey()) {
				auto& pts = lightEntry[key];
				if (pts.is_array() && !pts.empty() && pts[0].is_array() && pts[0].size() >= 3)
					pts[0] = nlohmann::ordered_json::array({ static_cast<int>(current.pos.x), static_cast<int>(current.pos.y), static_cast<int>(current.pos.z) });
			}

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

	// Normalise key order for every data and lightEntry in the file so the whole config is consistent.
	auto normalizeData = [](nlohmann::ordered_json& d) {
		nlohmann::ordered_json nd;
		if (d.contains("color"))           nd["color"]           = d["color"];
		if (d.contains("light"))           nd["light"]           = d["light"];
		if (d.contains("fade"))            nd["fade"]            = d["fade"];
		if (d.contains("radius"))          nd["radius"]          = d["radius"];
		if (d.contains("size"))            nd["size"]            = d["size"];
		if (d.contains("cutoff"))          nd["cutoff"]          = d["cutoff"];
		if (d.contains("shadowDepthBias"))   nd["shadowDepthBias"]   = d["shadowDepthBias"];
		if (d.contains("externalEmittance")) nd["externalEmittance"] = d["externalEmittance"];
		if (d.contains("flags"))             nd["flags"]             = d["flags"];
		if (d.contains("offset"))          nd["offset"]          = d["offset"];
		if (d.contains("rotation"))        nd["rotation"]        = d["rotation"];
		for (auto& [key, val] : d.items())
			if (std::ranges::find(managedDataKeys, key) == managedDataKeys.end())
				nd[key] = val;
		d = std::move(nd);
	};

	auto normalizeLightEntry = [&normalizeData](nlohmann::ordered_json& le) {
		if (le.contains("data"))
			normalizeData(le["data"]);
		nlohmann::ordered_json newEntry;
		if (le.contains("data"))       newEntry["data"]      = le["data"];
		if (le.contains("points"))     newEntry["points"]    = le["points"];
		else if (le.contains("nodes")) newEntry["nodes"]     = le["nodes"];
		if (le.contains("whiteList"))  newEntry["whiteList"] = le["whiteList"];
		if (le.contains("blackList"))  newEntry["blackList"] = le["blackList"];
		for (auto& [key, val] : le.items())
			if (std::ranges::find(managedEntryKeys, key) == managedEntryKeys.end())
				newEntry[key] = val;
		le = std::move(newEntry);
	};

	for (auto& topEntry : configArray) {
		auto lightsIt = topEntry.find("lights");
		if (lightsIt == topEntry.end() || !lightsIt->is_array())
			continue;
		for (auto& le : *lightsIt)
			normalizeLightEntry(le);
	}

	{
		std::ofstream outFile(filePath);
		if (!outFile.is_open()) {
			logger::warn("[LightEditor] Failed to write Light Placer config: {}", filePath.string());
			return false;
		}
		std::string output = configArray.dump(1, '\t');

		// Inline vec3 arrays onto a single line.
		static const std::regex vec3Pattern(R"(\[\n\s*([-\d.eE+]+),\n\s*([-\d.eE+]+),\n\s*([-\d.eE+]+)\n\s*\])");
		output = std::regex_replace(output, vec3Pattern, "[$1, $2, $3]");

		// Re-round all floats to 4 decimal places and strip trailing zeros.
		// nlohmann serializes floats at full binary precision, so e.g. 0.498f becomes
		// 0.49799999594688416; this pass restores the intended rounded representation.
		{
			static const std::regex floatPattern(R"(-?\d+\.\d+)");
			std::string rounded;
			rounded.reserve(output.size());
			std::sregex_iterator it(output.begin(), output.end(), floatPattern), end;
			size_t pos = 0;
			for (; it != end; ++it) {
				rounded += output.substr(pos, it->position() - pos);
				double v = std::round(std::stod(it->str()) * 10000.0) / 10000.0;
				char buf[32];
				std::snprintf(buf, sizeof(buf), "%.4f", v);
				std::string s(buf);
				s.erase(s.find_last_not_of('0') + 1);
				if (s.back() == '.') s.pop_back();
				rounded += s;
				pos = it->position() + it->length();
			}
			rounded += output.substr(pos);
			output = std::move(rounded);
		}

		outFile << output;
		outFile.flush();
		if (outFile.fail()) {
			logger::warn("[LightEditor] Failed to write Light Placer config to {}: stream error", filePath.string());
			return false;
		}
	}

	if (!dryRun)
		original.pos = current.pos;

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
			return std::tie(a.id, a.index) < std::tie(b.id, b.index);
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
