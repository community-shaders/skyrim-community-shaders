#include "Utils/Subrect.h"

#include <algorithm>
#include <cmath>
#include <d3d11.h>
#include <imgui.h>

namespace
{
	Util::Subrect::UVRegion ClampUV(Util::Subrect::UVRegion uv)
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

	Util::Subrect::UVRegion DefaultUV()
	{
		return {};
	}

	Util::Subrect::UVRegion LoadUVArray(const json& arr)
	{
		Util::Subrect::UVRegion uv = DefaultUV();
		if (arr.is_array() && arr.size() == 4) {
			uv.x = arr[0];
			uv.y = arr[1];
			uv.w = arr[2];
			uv.h = arr[3];
		}
		return ClampUV(uv);
	}

	Util::Subrect::UVRegion MirrorUVHorizontal(const Util::Subrect::UVRegion& uv)
	{
		// HMD nose-side overlap: left-eye nose-side region is on the right
		// half of the eye texture; mirror around x=0.5 maps it to the
		// right-eye's left half.
		Util::Subrect::UVRegion mirrored = uv;
		mirrored.x = 1.0f - uv.x - uv.w;
		return ClampUV(mirrored);
	}

	json SaveUVToJson(const Util::Subrect::UVRegion& uv)
	{
		return { uv.x, uv.y, uv.w, uv.h };
	}

	Util::Subrect::PixelRegion UVToPixelRegion(const Util::Subrect::UVRegion& uv, uint32_t width, uint32_t height)
	{
		Util::Subrect::PixelRegion result;
		result.x = std::min<uint32_t>(width - 1, static_cast<uint32_t>(uv.x * width));
		result.y = std::min<uint32_t>(height - 1, static_cast<uint32_t>(uv.y * height));
		result.w = std::max<uint32_t>(1, static_cast<uint32_t>(uv.w * width));
		result.h = std::max<uint32_t>(1, static_cast<uint32_t>(uv.h * height));
		result.w = std::min<uint32_t>(result.w, width - result.x);
		result.h = std::min<uint32_t>(result.h, height - result.y);
		return result;
	}
}

namespace Util::Subrect
{
	void Controller::LoadSettings(const json& a_json)
	{
		if (a_json.contains("CropX"))
			currentUV.x = a_json["CropX"];
		if (a_json.contains("CropY"))
			currentUV.y = a_json["CropY"];
		if (a_json.contains("CropW"))
			currentUV.w = a_json["CropW"];
		if (a_json.contains("CropH"))
			currentUV.h = a_json["CropH"];

		const bool hasExplicitLeft =
			a_json.contains("CropX") || a_json.contains("CropY") ||
			a_json.contains("CropW") || a_json.contains("CropH");
		const bool hasExplicitRight =
			a_json.contains("CropRightX") || a_json.contains("CropRightY") ||
			a_json.contains("CropRightW") || a_json.contains("CropRightH");
		if (a_json.contains("CropRightX"))
			currentRightUV.x = a_json["CropRightX"];
		if (a_json.contains("CropRightY"))
			currentRightUV.y = a_json["CropRightY"];
		if (a_json.contains("CropRightW"))
			currentRightUV.w = a_json["CropRightW"];
		if (a_json.contains("CropRightH"))
			currentRightUV.h = a_json["CropRightH"];

		if (a_json.contains("CropPresets") && a_json["CropPresets"].is_array()) {
			presets.clear();
			for (auto& entry : a_json["CropPresets"]) {
				Preset preset;
				preset.name = entry.value("name", "Unknown");
				if (entry.contains("uv")) {
					preset.uv = LoadUVArray(entry["uv"]);
				}
				// Right-eye UV is optional. If absent, callers in stereo mode
				// fall back to the mirrored left-eye via ApplyPreset.
				if (entry.contains("right_uv")) {
					preset.rightUV = LoadUVArray(entry["right_uv"]);
				} else {
					preset.rightUV = MirrorUVHorizontal(preset.uv);
				}
				presets.push_back(std::move(preset));
			}
		}

		EnsureDefaultPreset();
		ClampCurrentUV();

		// Legacy upgrade: if the JSON has the mono crop keys but no right-eye
		// keys, mirror left → right so existing user settings transition
		// cleanly. If neither side is present, leave currentRightUV alone so
		// EnsureDefaultPreset's seeded right-eye value survives.
		if (stereoEnabled && hasExplicitLeft && !hasExplicitRight) {
			SyncRightUV();
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
		a_json["CropX"] = currentUV.x;
		a_json["CropY"] = currentUV.y;
		a_json["CropW"] = currentUV.w;
		a_json["CropH"] = currentUV.h;

		if (stereoEnabled) {
			a_json["CropRightX"] = currentRightUV.x;
			a_json["CropRightY"] = currentRightUV.y;
			a_json["CropRightW"] = currentRightUV.w;
			a_json["CropRightH"] = currentRightUV.h;
		}

		json presetsJson = json::array();
		for (const auto& preset : presets) {
			json entry;
			entry["name"] = preset.name;
			entry["uv"] = SaveUVToJson(preset.uv);
			if (stereoEnabled) {
				entry["right_uv"] = SaveUVToJson(preset.rightUV);
			}
			presetsJson.push_back(std::move(entry));
		}
		a_json["CropPresets"] = presetsJson;
		a_json["SelectedPresetIndex"] = selectedPresetIndex;
	}

	void Controller::SeedDefaultPresets(std::vector<Preset> defaults)
	{
		seededDefaults = std::move(defaults);
	}

	void Controller::SetStereoEnabled(bool enabled)
	{
		if (stereoEnabled == enabled) {
			return;
		}
		stereoEnabled = enabled;
		if (stereoEnabled) {
			SyncRightUV();
		}
	}

	void Controller::DrawEditor(ID3D11ShaderResourceView* previewSrv, ID3D11Texture2D* previewTexture, float uvVisibleWidth, float uvStartX, ImDrawCallback imageRenderCallback)
	{
		// Hosts that render without first calling LoadSettings would otherwise
		// see an empty presets vector and the combo would mislabel as "(Custom)".
		EnsureDefaultPreset();
		if (selectedPresetIndex < 0 || selectedPresetIndex >= static_cast<int>(presets.size())) {
			selectedPresetIndex = 0;
		}

		std::string currentPreview =
			(selectedPresetIndex >= 0 && selectedPresetIndex < static_cast<int>(presets.size())) ? presets[selectedPresetIndex].name : "(Custom)";

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
				presets.push_back(Preset{ .name = presetName, .uv = currentUV });
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

		ImGui::SameLine();
		if (ImGui::Button("Reset Crop")) {
			ApplyPreset(0);
		}

		ImGui::Spacing();
		ImGui::PushItemWidth(250.0f);
		bool changed = false;
		changed |= ImGui::SliderFloat2("Position UV (X, Y)", &currentUV.x, 0.0f, 1.0f, "%.3f");
		changed |= ImGui::SliderFloat2("Size UV (W, H)", &currentUV.w, 0.01f, 1.0f, "%.3f");
		ImGui::PopItemWidth();

		if (changed) {
			selectedPresetIndex = -1;
			ClampCurrentUV();
			if (stereoEnabled) {
				SyncRightUV();
			}
		}

		ImGui::Spacing();
		ImGui::Text("Interactive Cropping (Drag on the image to select)");

		if (!previewSrv || !previewTexture) {
			ImGui::TextDisabled("Preview unavailable.");
			return;
		}

		D3D11_TEXTURE2D_DESC desc{};
		previewTexture->GetDesc(&desc);
		float maxWidth = std::min(400.0f, ImGui::GetContentRegionAvail().x);
		float aspectRatio = (static_cast<float>(desc.Width) * uvVisibleWidth) / static_cast<float>(desc.Height);
		ImVec2 imageSize(maxWidth, maxWidth / aspectRatio);
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		ImDrawList* hostDrawList = ImGui::GetWindowDrawList();
		if (imageRenderCallback) {
			hostDrawList->AddCallback(imageRenderCallback, nullptr);
		}
		ImGui::Image(reinterpret_cast<ImTextureID>(previewSrv), imageSize,
			ImVec2(uvStartX, 0.0f), ImVec2(uvStartX + uvVisibleWidth, 1.0f));
		if (imageRenderCallback) {
			hostDrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		}

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
			currentUV.x = mouseUVX;
			currentUV.y = mouseUVY;
			currentUV.w = 0.0f;
			currentUV.h = 0.0f;
		}

		if (isDraggingCrop) {
			float minX = std::min(dragStartUV[0], mouseUVX);
			float minY = std::min(dragStartUV[1], mouseUVY);
			float maxX = std::max(dragStartUV[0], mouseUVX);
			float maxY = std::max(dragStartUV[1], mouseUVY);

			currentUV.x = minX;
			currentUV.y = minY;
			currentUV.w = maxX - minX;
			currentUV.h = maxY - minY;
			ClampCurrentUV();
			if (stereoEnabled) {
				SyncRightUV();
			}

			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				isDraggingCrop = false;
			}
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pMin(cursorPos.x + currentUV.x * imageSize.x, cursorPos.y + currentUV.y * imageSize.y);
		ImVec2 pMax(cursorPos.x + (currentUV.x + currentUV.w) * imageSize.x,
			cursorPos.y + (currentUV.y + currentUV.h) * imageSize.y);
		drawList->AddRect(pMin, pMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
	}

	PixelRegion Controller::GetPixelRegion(uint32_t width, uint32_t height) const
	{
		return UVToPixelRegion(currentUV, width, height);
	}

	StereoPixelRegions Controller::GetStereoPixelRegions(uint32_t fullWidth, uint32_t fullHeight) const
	{
		// Each eye occupies half the SBS texture width. In mono mode, both
		// eyes report the same region so callers don't need to branch.
		const uint32_t eyeWidth = fullWidth / 2;
		StereoPixelRegions regions;
		regions.leftEye = UVToPixelRegion(currentUV, eyeWidth, fullHeight);
		regions.rightEye = UVToPixelRegion(stereoEnabled ? currentRightUV : currentUV, eyeWidth, fullHeight);
		return regions;
	}

	void Controller::EnsureDefaultPreset()
	{
		if (!presets.empty()) {
			return;
		}
		if (!seededDefaults.empty()) {
			presets = seededDefaults;
			// currentUV must match what the combo shows as selected; otherwise
			// the first preset appears chosen but the crop region stays full-frame.
			currentUV = presets[0].uv;
			currentRightUV = presets[0].rightUV;
			selectedPresetIndex = 0;
		} else {
			presets.push_back(Preset{ .name = "Full Frame", .uv = DefaultUV() });
		}
	}

	void Controller::ClampCurrentUV()
	{
		currentUV = ClampUV(currentUV);
		currentRightUV = ClampUV(currentRightUV);
	}

	void Controller::ApplyPreset(int index)
	{
		EnsureDefaultPreset();
		selectedPresetIndex = std::clamp(index, 0, static_cast<int>(presets.size()) - 1);
		currentUV = presets[selectedPresetIndex].uv;
		currentRightUV = presets[selectedPresetIndex].rightUV;
		ClampCurrentUV();
	}

	void Controller::SyncRightUV()
	{
		currentRightUV = MirrorUVHorizontal(currentUV);
	}
}  // namespace Util::Subrect
