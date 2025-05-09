#include "PostProcessing.h"

#include "IconsFontAwesome5.h"
#include "imgui_stdlib.h"

#include "State.h"

void PostProcessing::DrawSettings()
{
	// 0 for list of feats
	// 1 for feat settings
	static int pageNum = 0;
	static int featIdx = 0;
	static int presetIdx = -1;
	const float _iconButtonSize = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.x;
	const ImVec2 iconButtonSize{ _iconButtonSize, _iconButtonSize };

	//if (ImGui::BeginTable("Page Select", 2)) {
	//	ImGui::TableNextColumn();
	//	ImGui::RadioButton("Effect List", &pageNum, 0);
	//	ImGui::TableNextColumn();
	//	ImGui::RadioButton("Effect Settings", &pageNum, 1);

	//	ImGui::EndTable();
	//}

	ImGui::BeginGroup();
	std::string currentPreset = (presetIdx >= 0 && presetIdx < presets.size()) 
                                ? presets[presetIdx] 
                                : "Select a preset";

	if (ImGui::BeginCombo("##PresetCombo", currentPreset.c_str())) {
		presets = LoadPresets();

		for (int i = 0; i < presets.size(); ++i) {
			bool isSelected = presetIdx == i;
			if (ImGui::Selectable(presets[i].c_str(), isSelected))
				presetIdx = i;
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button("Load")) {
		if (presetIdx >= 0 && presetIdx < presets.size())
		{
			LoadPresetFrom(presets[presetIdx]);
		}
	}

	ImGui::EndGroup();
	ImGui::BeginGroup();
	static std::string newPresetName = "";
	ImGui::InputText("##NewPresetName", &newPresetName);

	ImGui::SameLine();
	if (ImGui::Button("Save")) {
		if (!newPresetName.empty())
			SavePresetTo(newPresetName);
	}

	ImGui::EndGroup();

	ImGui::Separator();
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	if (pageNum == 0) {
		// Effect List

		if (ImGui::Button(ICON_FA_PLUS, iconButtonSize))
			ImGui::OpenPopup("New Feature");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Add a new effect.");

		if (ImGui::BeginPopup("New Feature", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar)) {
			bool doClose = false;
			if (ImGui::BeginListBox("##Feature List")) {
				const auto& featConstructors = PostProcessFeatureConstructor::GetFeatureConstructors();

				for (auto& [id, featCon] : featConstructors) {
					if (ImGui::Selectable(featCon.name.c_str())) {
						feats.push_back(std::unique_ptr<PostProcessFeature>{ featCon.fn() });
						feats.back()->name = feats.back()->GetType();

						auto bogey = json::object();
						feats.back()->LoadSettings(bogey);
						feats.back()->SetupResources();

						featIdx = (int)feats.size() - 1;

						doClose = true;
					}
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text(featCon.desc.c_str());
				}

				ImGui::EndListBox();
			}
			if (doClose)
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		ImGui::Dummy({ ImGui::GetTextLineHeightWithSpacing() * 2, 1 });
		ImGui::SameLine();

		ImGui::Checkbox("Bypass", &bypass);

		ImGui::Spacing();

		int markedFeat = -1;
		int actionType = -1;  // 0 - remove, 1 - move up, 2 - move down
		if (ImGui::BeginListBox("##Features", { -FLT_MIN, -FLT_MIN })) {
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));  // I hate this
			for (int i = 0; i < feats.size(); ++i) {
				ImGui::PushID(i);

				auto& feat = feats[i];

				bool nonVR = REL::Module::IsVR() && !feat->SupportsVR();

				ImGui::Checkbox("##Enabled", &feat->enabled);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Enabled/Bypassed");

				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_TIMES, iconButtonSize)) {
					markedFeat = i;
					actionType = 0;
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Remove the selected effect.");

				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_ARROW_UP, iconButtonSize) && (i != 0)) {
					markedFeat = i;
					actionType = 1;
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Move the selected effect up.");

				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_ARROW_DOWN, iconButtonSize) && (i < feats.size() - 1)) {
					markedFeat = i;
					actionType = 2;
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Move the selected effect down.");

				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_BARS, iconButtonSize)) {
					markedFeat = i;
					actionType = 3;
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Edit the selected effect.");

				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();

				if (nonVR)
					ImGui::BeginDisabled();
				if (ImGui::Selectable(std::format("{} ({})", feat->name, feat->GetType()).c_str(), featIdx == i))
					featIdx = i;
				if (nonVR)
					ImGui::EndDisabled();

				if (auto _tt = Util::HoverTooltipWrapper())
					if (nonVR)
						ImGui::Text("Bypassed due to no VR support.");
					else
						ImGui::Text(feat->GetDesc().c_str());

				ImGui::PopID();
			}
			ImGui::PopStyleColor();

			ImGui::EndListBox();
		}

		if (markedFeat >= 0 && actionType >= 0) {
			switch (actionType) {
			case 0:
				feats.erase(feats.begin() + markedFeat);
				break;
			case 1:
				std::iter_swap(feats.begin() + markedFeat, feats.begin() + markedFeat - 1);
				if (markedFeat == featIdx)
					featIdx--;
				else if (markedFeat - 1 == featIdx)
					featIdx++;
				break;
			case 2:
				std::iter_swap(feats.begin() + markedFeat, feats.begin() + markedFeat + 1);
				if (markedFeat == featIdx)
					featIdx++;
				else if (markedFeat + 1 == featIdx)
					featIdx--;
				break;
			case 3:
				featIdx = markedFeat;
				pageNum = 1;
				break;
			default:
				break;
			}
		}

	} else if (pageNum == 1) {
		// Effect Settings

		if (featIdx < feats.size()) {
			auto& feat = feats[featIdx];
			if (ImGui::Button(ICON_FA_ARROW_LEFT, iconButtonSize)) {
				pageNum = 0;
			}

			ImGui::Spacing();

			ImGui::InputText("Name", &feat->name);
	
			ImGui::SeparatorText(std::format("{} ({})", feat->name, feat->GetType()).c_str());

			ImGui::TextWrapped(feat->GetDesc().c_str());

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushID(featIdx);
			feat->DrawSettings();
			ImGui::PopID();
		} else {
			ImGui::TextDisabled("Please select an effect in the effect list to continue.");
		}
	}
}

void PostProcessing::LoadSettings(json& o_json)
{
	const auto& featConstructors = PostProcessFeatureConstructor::GetFeatureConstructors();

	logger::info("Loading post processing settings...");

	auto effects = o_json["effects"];
	if (!effects.is_array()) {
		RestoreDefaultSettings();
		logger::warn("Invalid post processing settings, restoring defaults.");
		return;
	}

	feats.clear();

	for (auto& item : effects) {
		auto currFeatCount = feats.size();
		try {
			auto itemType = item.value<std::string>("type", "UNSPECIFIED");
			if (featConstructors.contains(itemType)) {
				PostProcessFeature* feat = featConstructors.at(itemType).fn();
				feat->name = item.value<std::string>("name", feat->GetType());
				feat->enabled = item.value<bool>("enabled", true);
				feat->LoadSettings(item["settings"]);
				if (loaded)
					feat->SetupResources();  // to prevent double setup before loaded

				feats.push_back(std::unique_ptr<PostProcessFeature>{ feat });

				logger::info("Loaded {}({}).", feat->name, feat->GetType());
			} else {
				logger::warn("Invalid post processing feature type \"{}\" detected in settings.", itemType);
			}
		} catch (json::exception& e) {
			logger::error("Error occured while parsing post processing settings: {}", e.what());
			if (feats.size() > currFeatCount)
				feats.pop_back();
		}
	}
}

void PostProcessing::SaveSettings(json& o_json)
{
	auto arr = json::array();

	for (auto& feat : feats) {
		json temp_json{};
		feat->SaveSettings(temp_json);
		arr.push_back({
			{ "type", feat->GetType() },
			{ "name", feat->name },
			{ "enabled", feat->enabled },
			{ "settings", temp_json },
		});
	}

	o_json["effects"] = arr;
}

std::vector<std::string> PostProcessing::LoadPresets()
{
	std::vector<std::string> o_presets = {};

	try {
		std::filesystem::create_directories(ppPresetPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating preset directory during Load ({}) : {}\n", ppPresetPath, e.what());
		return o_presets;
	}

	for (const auto& entry : std::filesystem::directory_iterator(ppPresetPath)) {
		if (entry.is_regular_file() && entry.path().extension() == ".json") {
			o_presets.push_back(entry.path().stem().string());
		}
	}

	return o_presets;
}

void PostProcessing::LoadPresetFrom(std::string a_name)
{
	json a_presets = {};

	// if the name has .json, remove it
	if (a_name.ends_with(".json"))
		a_name = a_name.substr(0, a_name.size() - 5);

	try {
		std::ifstream i{ std::format("{}\\{}.json", ppPresetPath, a_name) };
		i >> a_presets;
	} catch (const std::exception& e) {
		logger::warn("Failed to load preset: {}. Error: {}", a_name, e.what());
		return;
	}

	LoadSettings(a_presets);
}

void PostProcessing::SavePresetTo(std::string a_name)
{
	json a_presets = {};
	SaveSettings(a_presets);
	a_presets["preset_name"] = a_name;

	// Check if the name is valid
	if (a_name.empty()) {
		logger::warn("Invalid preset name.");
		return;
	}

	try {
		std::filesystem::create_directories(ppPresetPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating preset directory during Save ({}) : {}\n", ppPresetPath, e.what());
		return;
	}

	std::string presetPath = std::format("{}\\{}.json", ppPresetPath, a_name);
	std::ofstream o{ presetPath };

	try {
		o << std::setw(4) << a_presets;
		logger::info("Saving preset to {}", presetPath);
	} catch (const std::exception& e) {
		logger::warn("Failed to write preset to file: {}. Error: {}", presetPath, e.what());
	}
}

void PostProcessing::RestoreDefaultSettings()
{
	LoadPresetFrom("default");
}

void PostProcessing::ClearShaderCache()
{
	for (auto& feat : feats)
		if (!REL::Module::IsVR() || feat->SupportsVR())
			feat->ClearShaderCache();
}

void PostProcessing::SetupResources()
{
	{
		auto renderer = globals::game::renderer;
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texCopy = eastl::make_unique<Texture2D>(texDesc);
		texCopy->CreateUAV(uavDesc);
	}

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PostProcessing\\copy.cs.hlsl", {}, "cs_5_0")))
		copyCS.attach(rawPtr);

	for (auto& feat : feats)
		if (!REL::Module::IsVR() || feat->SupportsVR())
			feat->SetupResources();
}

void PostProcessing::Reset()
{
	for (auto& feat : feats)
		if (!REL::Module::IsVR() || feat->SupportsVR())
			feat->Reset();
}

void PostProcessing::PreProcess()
{
	if (bypass)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto gameTexMain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	PostProcessFeature::TextureInfo lastTexColor = { gameTexMain.texture, gameTexMain.SRV };

	// go through each fx
	for (auto& feat : feats)
		if (feat->enabled && (!REL::Module::IsVR() || feat->SupportsVR()))
			feat->Draw(lastTexColor);

	D3D11_TEXTURE2D_DESC desc;
	lastTexColor.tex->GetDesc(&desc);
	if (desc.Format == texCopy->desc.Format) {
		// either MAIN_COPY or MAIN is used as input for HDR pass
		// so we copy to both so whatever the game wants we're not failing it
		context->CopySubresourceRegion(gameTexMain.texture, 0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
		context->CopySubresourceRegion(
			renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY].texture,
			0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
	} else {
		ID3D11ShaderResourceView* srv = lastTexColor.srv;
		ID3D11UnorderedAccessView* uav = texCopy->uav.get();

		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetShader(copyCS.get(), nullptr, 0);
		context->Dispatch((texCopy->desc.Width + 7) >> 3, (texCopy->desc.Height + 7) >> 3, 1);

		srv = nullptr;
		uav = nullptr;

		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetShader(nullptr, nullptr, 0);

		context->CopySubresourceRegion(gameTexMain.texture, 0, 0, 0, 0, texCopy->resource.get(), 0, nullptr);
		context->CopySubresourceRegion(
			renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY].texture,
			0, 0, 0, 0, texCopy->resource.get(), 0, nullptr);
	}
}

void PostProcessing::PostPostLoad()
{
	logger::info("Hooking preprocess passes");
	stl::write_vfunc<0x2, BSImagespaceShaderHDRTonemapBlendCinematic_SetupTechnique>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[0]);
	stl::write_vfunc<0x2, BSImagespaceShaderHDRTonemapBlendCinematicFade_SetupTechnique>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[0]);
}