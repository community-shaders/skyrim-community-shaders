#include "SceneTransitionField.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>  // GetActiveID
#include <imgui_stdlib.h>

#include "SceneSettingsManager.h"
#include "Utils/UI.h"

namespace
{
	/// Text-metrics basis only: sizing the field for the widest expected value stops it resizing as
	/// digits are typed. It tracks kMaxLocationTransitionSeconds by eye, and drift costs a few pixels.
	constexpr const char* kWidestValue = "300.0";

	/// Live text per field, so a partially typed value survives the frame without being overwritten
	/// by the stored one. One entry per unique id and never removed, as Util::DrawComboSearchInput does.
	std::unordered_map<ImGuiID, std::string> fieldBuffers;

	std::string FormatSeconds(float a_seconds)
	{
		char text[16]{};
		std::snprintf(text, sizeof(text), "%.1f", a_seconds);
		return text;
	}
}

float SceneTransitionField::GetWidth()
{
	return ImGui::CalcTextSize(kWidestValue).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

bool SceneTransitionField::Draw(const char* a_id, std::optional<float>& a_value, float a_inherited, bool a_editable)
{
	ImGui::PushID(a_id);

	const auto fieldId = ImGui::GetID("##TransitionSeconds");
	auto& buffer = fieldBuffers[fieldId];
	// Re-seeding while the field is active would fight the user mid-edit.
	if (ImGui::GetActiveID() != fieldId)
		buffer = FormatSeconds(a_value.value_or(a_inherited));

	// Dim, not disabled: an inherited value still has to be clickable to override it.
	const bool inheriting = !a_value;
	if (inheriting)
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetDisabled());
	ImGui::BeginDisabled(!a_editable);
	ImGui::SetNextItemWidth(GetWidth());
	ImGui::InputText("##TransitionSeconds", &buffer, ImGuiInputTextFlags_CharsDecimal);
	ImGui::EndDisabled();
	if (inheriting)
		ImGui::PopStyleColor();

	bool changed = false;
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		std::optional<float> parsed;
		const std::string_view typed{ buffer };
		float seconds = 0.0f;
		if (typed.empty()) {
			// An emptied field means "inherit", which no float widget can express.
			parsed.reset();
		} else if (const auto result = std::from_chars(typed.data(), typed.data() + typed.size(), seconds);
			result.ec == std::errc{} && result.ptr == typed.data() + typed.size()) {
			parsed = std::clamp(seconds, 0.0f, SceneSettingsManager::kMaxLocationTransitionSeconds);
		} else {
			// Unparseable text leaves the stored value alone rather than silently clearing it.
			ImGui::PopID();
			return false;
		}
		changed = parsed != a_value;
		a_value = parsed;
	}

	ImGui::PopID();
	return changed;
}
