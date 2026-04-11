#include "Features/Subrect/Subrect.h"

#include <algorithm>
#include <imgui.h>

namespace
{
	Subrect::UVRegion ClampUV(Subrect::UVRegion uv)
	{
		uv.x = std::clamp(uv.x, 0.0f, 1.0f);
		uv.y = std::clamp(uv.y, 0.0f, 1.0f);
		uv.w = std::clamp(uv.w, 0.01f, 1.0f);
		uv.h = std::clamp(uv.h, 0.01f, 1.0f);

		if (uv.x + uv.w > 1.0f) {
			uv.w = 1.0f - uv.x;
		}
		if (uv.y + uv.h > 1.0f) {
			uv.h = 1.0f - uv.y;
		}

		return uv;
	}

	Subrect::UVRegion DefaultUV()
	{
		return {};
	}

	Subrect::UVRegion LoadUVFromJson(const json& value, const char* legacyKey = nullptr)
	{
		Subrect::UVRegion uv = DefaultUV();
		if (value.is_array() && value.size() == 4) {
			uv.x = value[0];
			uv.y = value[1];
			uv.w = value[2];
			uv.h = value[3];
		} else if (legacyKey != nullptr && value.contains(legacyKey) && value[legacyKey].is_array() && value[legacyKey].size() == 4) {
			uv.x = value[legacyKey][0];
			uv.y = value[legacyKey][1];
			uv.w = value[legacyKey][2];
			uv.h = value[legacyKey][3];
		}
		return ClampUV(uv);
	}

	json SaveUVToJson(const Subrect::UVRegion& uv)
	{
		return { uv.x, uv.y, uv.w, uv.h };
	}

	Subrect::PixelRegion UVToPixelRegion(const Subrect::UVRegion& uv, uint32_t eyeWidth, uint32_t eyeHeight)
	{
		Subrect::PixelRegion result;
		result.x = std::min<uint32_t>(eyeWidth - 1, static_cast<uint32_t>(uv.x * eyeWidth));
		result.y = std::min<uint32_t>(eyeHeight - 1, static_cast<uint32_t>(uv.y * eyeHeight));
		result.w = std::max<uint32_t>(1, static_cast<uint32_t>(uv.w * eyeWidth));
		result.h = std::max<uint32_t>(1, static_cast<uint32_t>(uv.h * eyeHeight));
		result.w = std::min<uint32_t>(result.w, eyeWidth - result.x);
		result.h = std::min<uint32_t>(result.h, eyeHeight - result.y);
		return result;
	}
}

namespace Subrect
{
	void Controller::LoadSettings(const json& a_json)
	{
		if (a_json.contains("CropX")) currentLeftEyeUV.x = a_json["CropX"];
		if (a_json.contains("CropY")) currentLeftEyeUV.y = a_json["CropY"];
		if (a_json.contains("CropW")) currentLeftEyeUV.w = a_json["CropW"];
		if (a_json.contains("CropH")) currentLeftEyeUV.h = a_json["CropH"];

		if (a_json.contains("CropRightX")) currentRightEyeUV.x = a_json["CropRightX"];
		if (a_json.contains("CropRightY")) currentRightEyeUV.y = a_json["CropRightY"];
		if (a_json.contains("CropRightW")) currentRightEyeUV.w = a_json["CropRightW"];
		if (a_json.contains("CropRightH")) currentRightEyeUV.h = a_json["CropRightH"];

		if (a_json.contains("CropPresets") && a_json["CropPresets"].is_array()) {
			presets.clear();
			for (auto& entry : a_json["CropPresets"]) {
				Preset preset;
				preset.name = entry.value("name", "Unknown");
				if (entry.contains("left_uv")) {
					preset.leftEye = LoadUVFromJson(entry["left_uv"]);
				} else {
					preset.leftEye = LoadUVFromJson(entry, "uv");
				}

				if (entry.contains("right_uv")) {
					preset.rightEye = LoadUVFromJson(entry["right_uv"]);
				} else {
					preset.rightEye = preset.leftEye;
				}

				presets.push_back(std::move(preset));
			}
		}

		EnsureDefaultPreset();
		ClampCurrentUV();
		if (!a_json.contains("CropRightX") && !a_json.contains("CropRightY") && !a_json.contains("CropRightW") && !a_json.contains("CropRightH")) {
			SyncRightEyeUV();
		}

		if (a_json.contains("SelectedPresetIndex")) {
			selectedPresetIndex = a_json["SelectedPresetIndex"];
			if (selectedPresetIndex >= 0 && selectedPresetIndex < static_cast<int>(presets.size())) {
				ApplyPreset(selectedPresetIndex);
			} else {
				selectedPresetIndex = -1;
			}
		}
	}

	void Controller::SaveSettings(json& a_json) const
	{
		a_json["CropX"] = currentLeftEyeUV.x;
		a_json["CropY"] = currentLeftEyeUV.y;
		a_json["CropW"] = currentLeftEyeUV.w;
		a_json["CropH"] = currentLeftEyeUV.h;
		a_json["CropRightX"] = currentRightEyeUV.x;
		a_json["CropRightY"] = currentRightEyeUV.y;
		a_json["CropRightW"] = currentRightEyeUV.w;
		a_json["CropRightH"] = currentRightEyeUV.h;

		json presetsJson = json::array();
		for (const auto& preset : presets) {
			json entry;
			entry["name"] = preset.name;
			entry["uv"] = SaveUVToJson(preset.leftEye);
			entry["left_uv"] = SaveUVToJson(preset.leftEye);
			entry["right_uv"] = SaveUVToJson(preset.rightEye);
			presetsJson.push_back(std::move(entry));
		}
		a_json["CropPresets"] = presetsJson;
		a_json["SelectedPresetIndex"] = selectedPresetIndex;
	}

	void Controller::DrawEditor(ID3D11ShaderResourceView* previewSrv, ID3D11Texture2D* previewTexture, float eyeRatio)
	{
		ImGui::Text("=== VR Capture Cropping (Left Eye Relative) ===");

		std::string currentPreview =
			(selectedPresetIndex >= 0 && selectedPresetIndex < static_cast<int>(presets.size()))
				? presets[selectedPresetIndex].name
				: "(Custom)";

		if (ImGui::BeginCombo("Crop Preset", currentPreview.c_str())) {
			for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
				const bool isSelected = selectedPresetIndex == i;
				if (ImGui::Selectable(presets[i].name.c_str(), isSelected)) {
					ApplyPreset(i);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::InputText("Save As", newPresetName, sizeof(newPresetName));
		ImGui::SameLine();
		if (ImGui::Button("Save Preset")) {
			std::string presetName = newPresetName;
			if (!presetName.empty()) {
				presets.push_back(Preset{
					.name = presetName,
					.leftEye = currentLeftEyeUV,
					.rightEye = currentRightEyeUV,
				});
				selectedPresetIndex = static_cast<int>(presets.size()) - 1;
				newPresetName[0] = '\0';
			}
		}

		if (selectedPresetIndex > 0) {
			ImGui::SameLine();
			if (ImGui::Button("Delete Preset")) {
				presets.erase(presets.begin() + selectedPresetIndex);
				ApplyPreset(0);
			}
		}

		ImGui::Spacing();
		ImGui::PushItemWidth(250.0f);
		bool changed = false;
		changed |= ImGui::SliderFloat2("Position UV (X, Y)", &currentLeftEyeUV.x, 0.0f, 1.0f, "%.3f");
		changed |= ImGui::SliderFloat2("Size UV (W, H)", &currentLeftEyeUV.w, 0.01f, 1.0f, "%.3f");
		ImGui::PopItemWidth();

		if (changed) {
			selectedPresetIndex = -1;
			ClampCurrentUV();
			SyncRightEyeUV();
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Right eye UV mirrors the left-eye preset.");
		ImGui::Text("Interactive Cropping (Drag on the image to select)");

		if (!previewSrv || !previewTexture) {
			ImGui::TextDisabled("Preview unavailable.");
			return;
		}

		D3D11_TEXTURE2D_DESC desc{};
		previewTexture->GetDesc(&desc);
		float maxWidth = std::min(400.0f, ImGui::GetContentRegionAvail().x);
		float aspectRatio = (static_cast<float>(desc.Width) * eyeRatio) / static_cast<float>(desc.Height);
		ImVec2 imageSize(maxWidth, maxWidth / aspectRatio);
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		ImGui::Image(reinterpret_cast<ImTextureID>(previewSrv), imageSize, ImVec2(0.0f, 0.0f), ImVec2(eyeRatio, 1.0f));

		ImGui::SetCursorScreenPos(cursorPos);
		ImGui::SetNextItemAllowOverlap();
		ImGui::InvisibleButton("##subrectCanvas", imageSize);

		ImVec2 mousePos = ImGui::GetIO().MousePos;
		ImVec2 relativeMouseP(mousePos.x - cursorPos.x, mousePos.y - cursorPos.y);
		float mouseUVX = std::clamp(relativeMouseP.x / imageSize.x, 0.0f, 1.0f);
		float mouseUVY = std::clamp(relativeMouseP.y / imageSize.y, 0.0f, 1.0f);

		if (ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			isDraggingCrop = true;
			selectedPresetIndex = -1;
			dragStartUV[0] = mouseUVX;
			dragStartUV[1] = mouseUVY;
			currentLeftEyeUV.x = mouseUVX;
			currentLeftEyeUV.y = mouseUVY;
			currentLeftEyeUV.w = 0.0f;
			currentLeftEyeUV.h = 0.0f;
		}

		if (isDraggingCrop) {
			float minX = std::min(dragStartUV[0], mouseUVX);
			float minY = std::min(dragStartUV[1], mouseUVY);
			float maxX = std::max(dragStartUV[0], mouseUVX);
			float maxY = std::max(dragStartUV[1], mouseUVY);

			currentLeftEyeUV.x = minX;
			currentLeftEyeUV.y = minY;
			currentLeftEyeUV.w = maxX - minX;
			currentLeftEyeUV.h = maxY - minY;
			ClampCurrentUV();
			SyncRightEyeUV();

			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				isDraggingCrop = false;
			}
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pMin(cursorPos.x + currentLeftEyeUV.x * imageSize.x, cursorPos.y + currentLeftEyeUV.y * imageSize.y);
		ImVec2 pMax(cursorPos.x + (currentLeftEyeUV.x + currentLeftEyeUV.w) * imageSize.x,
			cursorPos.y + (currentLeftEyeUV.y + currentLeftEyeUV.h) * imageSize.y);
		drawList->AddRect(pMin, pMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
	}

	PixelRegion Controller::GetLeftEyePixelRegion(uint32_t fullTextureWidth, uint32_t fullTextureHeight) const
	{
		return UVToPixelRegion(currentLeftEyeUV, fullTextureWidth / 2, fullTextureHeight);
	}

	StereoPixelRegions Controller::GetStereoPixelRegions(uint32_t fullTextureWidth, uint32_t fullTextureHeight) const
	{
		StereoPixelRegions regions;
		regions.leftEye = UVToPixelRegion(currentLeftEyeUV, fullTextureWidth / 2, fullTextureHeight);
		regions.rightEye = UVToPixelRegion(currentRightEyeUV, fullTextureWidth / 2, fullTextureHeight);
		return regions;
	}

	void Controller::EnsureDefaultPreset()
	{
		if (presets.empty()) {
			Preset preset;
			preset.name = "Full Left Eye";
			preset.leftEye = DefaultUV();
			preset.rightEye = DefaultUV();
			presets.push_back(std::move(preset));
		}
	}

	void Controller::SyncRightEyeUV()
	{
		// Mirror horizontally: left-eye overlap is toward the nose (right),
		// right-eye overlap is toward the nose (left).
		currentRightEyeUV.y = currentLeftEyeUV.y;
		currentRightEyeUV.w = currentLeftEyeUV.w;
		currentRightEyeUV.h = currentLeftEyeUV.h;
		currentRightEyeUV.x = 1.0f - currentLeftEyeUV.x - currentLeftEyeUV.w;
	}

	void Controller::ClampCurrentUV()
	{
		currentLeftEyeUV = ClampUV(currentLeftEyeUV);
		currentRightEyeUV = ClampUV(currentRightEyeUV);
	}

	void Controller::ApplyPreset(int index)
	{
		EnsureDefaultPreset();
		selectedPresetIndex = std::clamp(index, 0, static_cast<int>(presets.size()) - 1);
		currentLeftEyeUV = presets[selectedPresetIndex].leftEye;
		currentRightEyeUV = presets[selectedPresetIndex].rightEye;
		ClampCurrentUV();
	}
}