#include "OverlayPanel.h"

#include <algorithm>
#include <format>
#include <imgui_stdlib.h>
#include <tuple>

#include "EditorWidgets.h"
#include "Features/Effects11/Overlays/PresetOverlays.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu/MenuIcons.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.effects11.overlays."

namespace
{
	/** @brief A value for tooltips: On/Off, an int, or up to 4 floats with 3 decimals. */
	std::string FormatValue(const Overlays::OverlayValue& a_value)
	{
		switch (a_value.kind) {
		case Overlays::ValueKind::Bool:
			return a_value.values[0] != 0.0f ? T(TKEY("on"), "On") : T(TKEY("off"), "Off");
		case Overlays::ValueKind::Int:
			return std::format("{}", static_cast<int>(a_value.values[0]));
		default:
			{
				std::string text;
				for (int c = 0; c < a_value.components; ++c)
					text += std::format("{}{:.3f}", c ? ", " : "", a_value.values[c]);
				return text;
			}
		}
	}
}

void OverlayPanel::DrawRowTooltip(const Overlays::RowInfo& a_row)
{
	const auto& overlays = Overlays::Manager::GetSingleton().GetOverlays();
	ImGui::Separator();
	Util::Text::Info("%s", T(TKEY("changed_by"), "Changed by preset overlays:"));
	for (const auto& contribution : a_row.contributions) {
		const auto line = std::format("{}: {} -> {}", overlays[contribution.overlay].DisplayName(), FormatValue(contribution.before), FormatValue(contribution.after));
		ImGui::TextUnformatted(line.c_str());
	}
	if (a_row.locked)
		Util::Text::WrappedWarning("%s", T(TKEY("row_locked"), "Locked: make the last overlay listed the edit target in Preset Overlays, or turn it off, to edit this."));
}

namespace
{
	/** @brief Width of the overlay name field, in font heights. */
	constexpr float kNameFieldWidthEm = 18.0f;
}

void OverlayPanel::Draw(bool& a_open, bool& a_dirty, const Callbacks& a_callbacks)
{
	a_callbacks.placeWindow();
	const std::string title = std::format("{}###Effects11Overlays", T(TKEY("window_title"), "Preset Overlays"));
	if (Util::BeginWithRoundedClose(title.c_str(), &a_open, ImGuiWindowFlags_NoScrollbar)) {
		auto& manager = Overlays::Manager::GetSingleton();
		ImGui::PushTextWrapPos(0.0f);
		if (!manager.HasFolder())
			Util::TextUnformattedDisabled(T(TKEY("no_folder"), "This preset has no enbpresetoverlays folder. Use New overlay to start one."));
		if (const auto& failedFile = manager.GetFailedWriteFile(); !failedFile.empty()) {
			const auto text = I18n::GetSingleton()->Format(TKEY("write_failed"), { { "file", failedFile } }, "Could not write {file}");
			Util::Text::WrappedError("%s", text.c_str());
		}
		if (const auto& failedFile = manager.GetFailedReadFile(); !failedFile.empty()) {
			const auto text = I18n::GetSingleton()->Format(TKEY("amount_read_failed"), { { "file", failedFile } }, "Could not read {file}, so its Amount was not saved");
			Util::Text::WrappedError("%s", text.c_str());
		}
		ImGui::PopTextWrapPos();

		DrawEditTarget();
		ImGui::Separator();
		a_callbacks.searchBar(filter, focusSearch);
		if (ImGui::BeginChild("##overlays", ImVec2(0.0f, 0.0f))) {
			DrawOverlayList();
			DrawActiveOrder();
			ImGui::Separator();
			DrawCreateButtons();
		}
		ImGui::EndChild();
		DrawNamePopup(a_dirty, a_callbacks);
		if (manager.HasDirtyOverlays())
			a_dirty = true;
	}
	ImGui::End();
}

void OverlayPanel::DrawEditTarget()
{
	auto& manager = Overlays::Manager::GetSingleton();
	const auto* current = manager.FindOverlay(manager.GetEditTarget());
	const std::string preview = current ? current->DisplayName() : T(TKEY("target_base"), "Base preset");

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(T(TKEY("edit_target"), "Edits go to"));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##editTarget", preview.c_str())) {
		if (Util::Selectable(T(TKEY("target_base"), "Base preset"), !current))
			manager.SetEditTarget({});
		for (const auto& overlay : manager.GetOverlays()) {
			if (!overlay.error.empty())
				continue;
			ImGui::PushID(overlay.fileName.c_str());
			if (Util::Selectable(overlay.DisplayName().c_str(), current == &overlay))
				manager.SetEditTarget(overlay.fileName);
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	Util::AddTooltip(T(TKEY("edit_target_tip"), "Where edits in the Effects 11 and Shader Parameters panels are saved.\nChoosing an overlay turns it on."));

	if (current) {
		const auto text = I18n::GetSingleton()->Format(TKEY("editing_overlay"), { { "overlay", current->DisplayName() } }, "Edits go to overlay {overlay}");
		Util::Text::WrappedWarning("%s", text.c_str());
	}
	if (manager.AllRowsLocked())
		Util::Text::WrappedWarning("%s", T(TKEY("amount_zero"), "This overlay's Amount is 0, so edits cannot be recorded. Raise its Amount to edit."));
}

void OverlayPanel::RefreshOrder()
{
	auto& manager = Overlays::Manager::GetSingleton();
	if (orderGeneration == manager.GetGeneration() && orderFilter == filter)
		return;
	orderGeneration = manager.GetGeneration();
	orderFilter = filter;

	const auto& overlays = manager.GetOverlays();
	order.clear();
	for (size_t i = 0; i < overlays.size(); ++i) {
		const auto& overlay = overlays[i];
		if (Effects11UI::ContainsNoCase(overlay.DisplayName(), filter) || Effects11UI::ContainsNoCase(overlay.uiGroups, filter) ||
			Effects11UI::ContainsNoCase(overlay.uiDesc, filter) || Effects11UI::ContainsNoCase(overlay.fileName, filter))
			order.push_back(i);
	}
	std::sort(order.begin(), order.end(), [&](size_t a_left, size_t a_right) {
		const auto& left = overlays[a_left];
		const auto& right = overlays[a_right];
		return std::tie(left.uiGroups, left.uiOrdering, left.fileName) < std::tie(right.uiGroups, right.uiOrdering, right.fileName);
	});
}

void OverlayPanel::DrawOverlayList()
{
	RefreshOrder();
	const auto& overlays = Overlays::Manager::GetSingleton().GetOverlays();
	if (!filter.empty() && order.empty()) {
		const auto text = I18n::GetSingleton()->Format(TKEY("no_match"), { { "search", filter } }, "No overlays match \"{search}\".");
		Util::TextUnformattedDisabled(text.c_str());
		return;
	}

	for (size_t start = 0; start < order.size();) {
		const auto& group = overlays[order[start]].uiGroups;
		size_t end = start;
		while (end < order.size() && overlays[order[end]].uiGroups == group)
			++end;

		const std::string label = std::format("{}###grp_{}", group.empty() ? T(TKEY("ungrouped"), "Other overlays") : group.c_str(), group);
		if (!filter.empty())
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			for (size_t i = start; i < end; ++i)
				DrawOverlayRow(order[i]);
		}
		start = end;
	}
}

void OverlayPanel::DrawOverlayRow(size_t a_index)
{
	auto& manager = Overlays::Manager::GetSingleton();
	const auto& overlay = manager.GetOverlays()[a_index];
	ImGui::PushID(overlay.fileName.c_str());

	const bool failed = !overlay.error.empty();
	bool enabled = manager.IsActive(overlay.fileName);
	const std::string label = failed ? overlay.DisplayName() + " " + T(TKEY("failed"), "(failed to load)") : overlay.DisplayName();
	{
		const Util::DisableGuard disable(failed);
		if (ImGui::Checkbox(label.c_str(), &enabled))
			manager.SetEnabled(overlay.fileName, enabled);
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		DrawOverlayTooltip(a_index);

	if (enabled && overlay.HasAmountSlider()) {
		float amount = overlay.amount;
		const std::string format = Effects11UI::MakeFormat(Effects11UI::DecimalsForStep(overlay.amountStep), T(TKEY("amount"), "Amount"));
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (Effects11UI::FloatValue("##amount", &amount, overlay.amountMin, overlay.amountMax, overlay.amountStep, format.c_str()))
			manager.SetAmount(overlay.fileName, amount);
		if (ImGui::IsItemDeactivatedAfterEdit())
			manager.SaveAmount(overlay.fileName);
	}
	ImGui::PopID();
}

void OverlayPanel::DrawOverlayTooltip(size_t a_index)
{
	auto& manager = Overlays::Manager::GetSingleton();
	const auto& overlay = manager.GetOverlays()[a_index];
	if (!ImGui::BeginTooltip())
		return;
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * Effects11UI::kTooltipWrapEm);
	ImGui::TextUnformatted(overlay.DisplayName().c_str());
	Util::TextUnformattedDisabled(overlay.fileName.c_str());
	if (!overlay.uiDesc.empty())
		ImGui::TextUnformatted(overlay.uiDesc.c_str());
	if (!overlay.incompatible.empty()) {
		const auto text = I18n::GetSingleton()->Format(TKEY("incompatible"), { { "overlays", Overlays::JoinList(overlay.incompatible) } }, "Incompatible with: {overlays}. Turning this on turns those off.");
		Util::Text::WrappedWarning("%s", text.c_str());
	}
	if (const size_t unused = manager.UnmappedCount(a_index)) {
		const auto text = I18n::GetSingleton()->Format(TKEY("unused"), { { "count", std::to_string(unused) }, { "total", std::to_string(overlay.ops.size()) } },
			"{count} of {total} parameters are not used by Effects 11.");
		Util::TextUnformattedDisabled(text.c_str());
	}
	if (!overlay.error.empty())
		Util::Text::WrappedError("%s", T(TKEY("read_failed"), "Could not read the file."));
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

void OverlayPanel::DrawActiveOrder()
{
	auto& manager = Overlays::Manager::GetSingleton();
	const auto& active = manager.GetActive();
	if (active.empty())
		return;
	ImGui::SeparatorText(T(TKEY("active_order"), "Active order (the last one wins)"));

	std::string moveName;
	int moveDelta = 0;
	for (size_t i = 0; i < active.size(); ++i) {
		ImGui::PushID(active[i].c_str());
		{
			const Util::DisableGuard disable(i == 0);
			if (Util::IconButton("##up", Icons::TI(ICON_TI_CHEVRON_UP), T(TKEY("move_up"), "Apply earlier"))) {
				moveName = active[i];
				moveDelta = -1;
			}
		}
		ImGui::SameLine();
		{
			const Util::DisableGuard disable(i + 1 == active.size());
			if (Util::IconButton("##down", Icons::TI(ICON_TI_CHEVRON_DOWN), T(TKEY("move_down"), "Apply later"))) {
				moveName = active[i];
				moveDelta = 1;
			}
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		const auto* overlay = manager.FindOverlay(active[i]);
		ImGui::TextUnformatted(overlay ? overlay->DisplayName().c_str() : active[i].c_str());
		ImGui::PopID();
	}
	if (!moveName.empty())
		manager.Move(moveName, moveDelta);  // after the loop, since it reorders the list being drawn
}

void OverlayPanel::DrawCreateButtons()
{
	if (Util::Button(T(TKEY("new_overlay"), "New overlay"))) {
		pendingFromChanges = false;
		openNamePopup = true;
	}
	Util::AddTooltip(T(TKEY("new_overlay_tip"), "Creates an empty overlay and makes it the edit target."));
	ImGui::SameLine();
	if (Util::Button(T(TKEY("new_from_changes"), "New overlay from unsaved changes"))) {
		pendingFromChanges = true;
		openNamePopup = true;
	}
	Util::AddTooltip(T(TKEY("new_from_changes_tip"),
		"Moves your unsaved edits out of the base preset into a new overlay.\nThe look stays the same and the preset files keep their saved values.\n"
		"Unsaved technique choices and per-weather (IgnoreWeatherSystem) toggles are discarded."));
}

void OverlayPanel::DrawNamePopup(bool& a_dirty, const Callbacks& a_callbacks)
{
	const std::string title = std::format("{}###OverlayName", T(TKEY("name_title"), "New overlay"));
	if (openNamePopup) {
		pendingName.clear();
		ImGui::OpenPopup(title.c_str());
		openNamePopup = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::TextUnformatted(T(TKEY("name_prompt"), "Overlay name:"));
	if (ImGui::IsWindowAppearing())
		ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * kNameFieldWidthEm);
	const bool submitted = ImGui::InputText("##name", &pendingName, ImGuiInputTextFlags_EnterReturnsTrue);
	const bool valid = pendingName.find_first_not_of(" \t") != std::string::npos;

	bool create = false;
	{
		const Util::DisableGuard disable(!valid);
		create = Util::SuccessButton(T(TKEY("create"), "Create")) || (submitted && valid);
	}
	if (create) {
		auto& manager = Overlays::Manager::GetSingleton();
		if (pendingFromChanges)
			a_dirty |= !manager.CreateOverlayFromChanges(pendingName, a_callbacks.revert);  // the revert cleared the flag
		else
			manager.CreateOverlay(pendingName);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (Util::Button(T(TKEY("cancel"), "Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

#undef I18N_KEY_PREFIX
