#include "Widget.h"
#include "Menu/IconLoader.h"

#include <format>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/WeatherTypeIcons.h"
#include "SaveManager.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"
#include "WeatherUtils.h"
#include "imgui_internal.h"

#define I18N_KEY_PREFIX "cs_editor."

void Widget::Save()
{
	SaveSettings();
	const auto file = GetSaveFilePath();

	// Checked before touching the file so a null document can never replace saved data.
	if (js.is_null()) {
		logger::warn("{}: Cannot save - JSON data is null", GetEditorID());
		return;
	}

	try {
		if (!Util::FileHelpers::WriteJsonAtomically(file, js, 2, "editor widget settings")) {
			logger::error("{}: Failed to write settings file: {}", GetEditorID(), file);
			return;
		}
		EditorWindow::GetSingleton()->OnWidgetJsonAttachmentChanged(this);
	} catch (const std::exception& e) {
		logger::error("{}: Unexpected error saving settings file: {}", GetEditorID(), e.what());
	}
}

void Widget::Load(bool showNotification)
{
	const std::string filePath = FormEditSources::ResolveFile(GetFormKey()).string();

	if (filePath.empty()) {
		js = json();
		LoadSettings();

		if (showNotification) {
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("No saved file - reset {} to vanilla values", GetEditorID()),
				Util::Colors::GetInfo(),
				3.0f);
		}
		return;
	}

	const auto notify = [&](const std::string& message, const ImVec4& color) {
		if (showNotification)
			EditorWindow::GetSingleton()->ShowNotification(message, color);
	};

	std::string error;
	auto document = Util::FileHelpers::ReadJsonFile(filePath, "editor widget settings", 0, &error);
	if (!document && error.empty()) {
		logger::warn("Failed to open settings file: {}", filePath);
		notify(std::format("Failed to open file for {}", GetEditorID()), Util::Colors::GetWarning());
		return;
	}

	const bool loaded = document && !document->is_null();
	js = loaded ? std::move(*document) : json();
	try {
		LoadSettings();
	} catch (const std::exception& e) {
		logger::error("Unexpected error loading settings file ({}) : {}", filePath, e.what());
		notify(std::format("Error loading {} - resetting to vanilla", GetEditorID()), Util::Colors::GetError());
		js = json();
		LoadSettings();
		return;
	}

	if (loaded)
		notify(std::format("Loaded saved settings for {}", GetEditorID()), Util::Colors::GetSuccess());
	else
		notify(std::format("Parse error for {} - resetting to vanilla", GetEditorID()), Util::Colors::GetError());
}

void Widget::Delete()
{
	std::string filePath = GetSaveFilePath();

	if (!std::filesystem::exists(filePath)) {
		return;
	}

	try {
		std::filesystem::remove(filePath);

		// The active preset's file, when it has one, takes over from the deleted user file.
		Load(false);
		ApplyChanges();

		EditorWindow::GetSingleton()->OnWidgetJsonAttachmentChanged(this);

		auto editorId = GetEditorID();
		const char* message = FormEditSources::GetPackFormKeys().contains(GetFormKey()) ?
		                          T(TKEY("deleted_reverted_to_preset"), "Deleted {} - reverted to preset values") :
		                          T(TKEY("deleted_reverted_to_vanilla"), "Deleted {} - reverted to vanilla values");
		EditorWindow::GetSingleton()->ShowNotification(
			std::vformat(message, std::make_format_args(editorId)),
			Util::Colors::GetSuccess(),
			3.0f);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error deleting settings file ({}) : {}\n", filePath, e.what());
	}
}

bool Widget::HasSavedFile() const
{
	return std::filesystem::exists(GetSaveFilePath());
}

std::optional<Widget::PackBadge> Widget::GetPackBadge() const
{
	if (!FormEditSources::GetPackFormKeys().contains(GetFormKey()))
		return std::nullopt;
	assert(globals::menu);
	const auto& statusPalette = globals::menu->GetTheme().StatusPalette;
	auto packName = FormEditSources::GetActivePackName();
	if (HasSavedFile())
		return PackBadge{ statusPalette.Warning,
			std::vformat(T(TKEY("pack_override_tooltip"), "Your saved file overrides preset '{}'. Delete it to use the preset's values."),
				std::make_format_args(packName)) };
	return PackBadge{ statusPalette.InfoColor,
		std::vformat(T(TKEY("pack_values_tooltip"), "Values from preset '{}'."), std::make_format_args(packName)) };
}

void Widget::DrawPackSourceBadge() const
{
	const auto packBadge = GetPackBadge();
	if (!packBadge)
		return;
	ImGui::SameLine();
	{
		Icons::FontGuard font(Icons::Family::Tabler);
		ImGui::TextColored(packBadge->color, "%s", ICON_TI_STACK_2);
	}
	Util::AddTooltip(packBadge->tooltip.c_str());
}

void Widget::DrawMenu()
{
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu(T(TKEY("menu"), "Menu"))) {
			if (ImGui::MenuItem(T(TKEY("save"), "Save"))) {
				SaveManager::SaveEditor(SaveManager::Trigger::User);
			}
			if (ImGui::MenuItem(T(TKEY("load"), "Load"))) {
				Load();
			}
			if (ImGui::MenuItem(T(TKEY("delete_saved_file"), "Delete Saved File"))) {
				ImGui::OpenPopup("DeleteConfirmation");
			}
			if (ImGui::MenuItem(T(TKEY("revert_to_game_values"), "Revert to Game Values"))) {
				RevertChanges();
			}

			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	DrawDeleteConfirmationModal();
}

void Widget::DrawDeleteConfirmationModal(const char* popupId)
{
	if (!ImGui::IsPopupOpen(popupId))
		return;
	if (deleteConfirmationFrame == ImGui::GetFrameCount())
		return;

	if (auto popup = Util::CenteredPopupModal(popupId)) {
		deleteConfirmationFrame = ImGui::GetFrameCount();
		ImGui::Text("%s", T(TKEY("confirm_delete_saved_file"), "Are you sure you want to delete the saved settings file?"));
		ImGui::Separator();

		const float scale = Util::GetUIScale();
		const float buttonWidth = 120.0f * scale;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float totalWidth = (buttonWidth * 2) + spacing;
		const float cursorX = (ImGui::GetWindowWidth() - totalWidth) / 2.0f;

		ImGui::SetCursorPosX(cursorX);

		if (ImGui::Button(T(TKEY("yes_delete"), "Yes, Delete"), ImVec2(buttonWidth, 0))) {
			Delete();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();

		if (ImGui::Button(T(TKEY("cancel"), "Cancel"), ImVec2(buttonWidth, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
	}
}

std::string Widget::GetSaveFilePath() const
{
	return FormEditSources::GetFilePath(Util::PathHelpers::GetCommunityShaderPath(), GetFormKey()).string();
}

std::string Widget::GetFolderName() const
{
	switch (form->GetFormType()) {
	case RE::FormType::Weather:
		return std::string(kWeatherFolderName);
	case RE::FormType::LightingMaster:
		return std::string(kLightingTemplateFolderName);
	case RE::FormType::ImageSpace:
		return std::string(kImageSpaceFolderName);
	case RE::FormType::VolumetricLighting:
		return std::string(kVolumetricLightingFolderName);
	case RE::FormType::ShaderParticleGeometryData:
		return std::string(kPrecipitationFolderName);
	case RE::FormType::ReferenceEffect:
		return std::string(kVisualEffectsFolderName);
	case RE::FormType::Cell:
		return std::string(kCellLightingFolderName);
	case RE::FormType::LensFlare:
		return std::string(kLensFlareFolderName);
	default:
		return std::string(kOtherEditorWidgetsFolderName);
	}
}

bool Widget::BeginWidgetWindow(bool showApply, bool showSaveLoadRevert, bool showForceWeather, RE::TESWeather* weather, const char* searchId)
{
	SetupWidgetWindowDefaults(GetWidgetTypeName());
	if (m_pendingFocus) {
		ImGui::SetNextWindowFocus();
		m_pendingFocus = false;
	}
	m_customHeaderActionsDrawn = false;
	m_titleBarSearchDrawn = false;

	// Weather type icon sits left of the title via drawLeading (standalone icon fonts).
	std::string title = GetWindowTitle();
	const auto typeIcon = WeatherTypeIcons::Resolve(weather);
	const bool hasLeadingIcon = typeIcon.has_value();

	bool result = Util::BeginWithCustomHeader(title.c_str(), &open, [this, showApply, showSaveLoadRevert, showForceWeather, weather, searchId]() { m_customHeaderActionsDrawn = DrawTitleBarActions(showApply, showSaveLoadRevert, showForceWeather, weather, true, searchId); }, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags, hasLeadingIcon ? [typeIcon](ImVec2 iconMin, float iconSize) { WeatherTypeIcons::Draw(typeIcon, ImGui::GetWindowDrawList(), iconMin, iconSize,
																																																																																																		   ImGui::GetColorU32(ImGuiCol_Text)); } : std::function<void(ImVec2, float)>{});
	UpdateWidgetTypeSize(GetWidgetTypeName());
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		EditorWindow::GetSingleton()->SetFocusedWidget(*this);
	return result;
}

void Widget::ForceWeatherReinit(RE::TESWeather* weather)
{
	auto* sky = globals::game::sky;
	if (weather && sky && sky->currentWeather == weather) {
		Util::ForceWeather(sky, weather, true);
		// An engaged lock owns the override slot; releasing it flickers the sky until the lock reasserts.
		if (EditorWindow::GetSingleton()->IsWeatherLocked())
			EditorWindow::MaintainWeatherLock();
		else
			sky->ReleaseWeatherOverride();
	}
}

void Widget::ForceCurrentWeatherReinit()
{
	if (auto* sky = globals::game::sky)
		ForceWeatherReinit(sky->currentWeather);
}

namespace
{
	struct TitleBarAction
	{
		enum class Kind
		{
			Icon,      ///< Texture icon (theme PNG)
			Glyph,     ///< Icon-font glyph
			Text,      ///< Framed text button
			LockBadge  ///< Pale green/red lock/unlock chip
		};

		std::string id;
		Kind kind = Kind::Text;
		ImTextureRef texture;
		Icons::GlyphRef glyph;  ///< Glyph kind
		std::string label;
		const char* tooltip = "";
		std::optional<ImVec4> textColor;  ///< Text/Glyph colour override
		std::optional<ImVec4> fillColor;  ///< Text kind: persistent fill colour override
		bool destructive = false;         ///< Hover highlight uses the error colour
		bool startsGroup = false;         ///< Leave a wider gap before this action
		bool locked = false;              ///< LockBadge kind
		std::function<void()> onClick;
		float width = 0.0f;
	};
}

bool Widget::DrawTitleBarActions(bool showApply, bool showSaveLoadRevert, bool showForceWeather, RE::TESWeather* weather, bool customHeaderRow, const char* searchId)
{
	auto* menu = globals::menu;
	auto* editorWindow = EditorWindow::GetSingleton();
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	// Docked windows share a tab bar, so keep actions inline there unless a native title bar hosts them.
	if (!menu || !editorWindow || !window || window->DockIsActive)
		return false;

	const bool nativeTitleBar = !customHeaderRow && !(window->Flags & ImGuiWindowFlags_NoTitleBar) && window->TitleBarHeight > 0.0f;
	if (!customHeaderRow && !nativeTitleBar)
		return false;

	const auto& style = ImGui::GetStyle();
	const auto& statusPalette = menu->GetTheme().StatusPalette;
	const bool useIcons = !editorWindow->settings.useTextButtons && menu->GetSettings().Theme.ShowActionIcons;
	const float scale = Util::GetUIScale();
	using Kind = TitleBarAction::Kind;

	// Outlives `actions`, whose tooltips point into it.
	const auto packBadge = showSaveLoadRevert ? GetPackBadge() : std::nullopt;

	std::vector<TitleBarAction> actions;

	auto addIcon = [&](const char* id, ImTextureRef texture, const char* tooltip, std::function<void()> onClick) -> TitleBarAction& {
		TitleBarAction action;
		action.id = id;
		action.kind = Kind::Icon;
		action.texture = texture;
		action.tooltip = tooltip;
		action.onClick = std::move(onClick);
		return actions.emplace_back(std::move(action));
	};
	auto addGlyph = [&](const char* id, Icons::GlyphRef glyph, const char* tooltip, std::function<void()> onClick) -> TitleBarAction& {
		TitleBarAction action;
		action.id = id;
		action.kind = Kind::Glyph;
		action.glyph = glyph;
		action.tooltip = tooltip;
		action.onClick = std::move(onClick);
		return actions.emplace_back(std::move(action));
	};
	auto addText = [&](const char* id, const char* label, const char* tooltip, std::function<void()> onClick) -> TitleBarAction& {
		TitleBarAction action;
		action.id = id;
		action.kind = Kind::Text;
		action.label = label;
		action.tooltip = tooltip;
		action.onClick = std::move(onClick);
		return actions.emplace_back(std::move(action));
	};

	// Force Weather / Unlock: lock badge matching the floating action bar
	if (showForceWeather && weather) {
		const bool isLocked = editorWindow->IsWeatherLockedByUser() && editorWindow->GetLockedWeather() == weather;
		const char* tooltip = !EditorWindow::AreWeatherLockHooksInstalled() ?
		                          T(TKEY("weather_lock_hooks_unavailable"), "Weather-lock hooks failed to install; the lock still works but weather may briefly flash before correcting") :
		                          (isLocked ? T(TKEY("unlock_weather"), "Unlock Weather") : T(TKEY("force_this_weather"), "Force This Weather"));
		TitleBarAction action;
		action.id = "##TitleForceWeather";
		action.kind = Kind::LockBadge;
		action.locked = isLocked;
		action.tooltip = tooltip;
		action.onClick = [editorWindow, weather, isLocked]() {
			if (isLocked)
				editorWindow->ReleaseUserWeatherLock();
			else
				editorWindow->LockWeatherForUser(weather);
		};
		actions.emplace_back(std::move(action));
	}

	// Apply
	if (showApply && (!editorWindow->settings.autoApplyChanges || RequiresManualApply())) {
		const char* tooltip = T(TKEY("apply_changes"), "Apply changes to the game");
		auto onClick = [this]() { ApplyChanges(); };
		if (useIcons && Util::IconLoader::GetIcons().applyToGame.texture) {
			addIcon("##TitleApply", Util::IconLoader::GetIcons().applyToGame.texture, tooltip, onClick);
		} else {
			auto& action = addText("##TitleApply", T(TKEY("apply"), "Apply"), tooltip, onClick);
			auto fill = statusPalette.SuccessColor;
			fill.w = 0.6f;
			action.fillColor = fill;
		}
	}

	// Save / Load / Revert / Delete
	if (showSaveLoadRevert) {
		const size_t groupStart = actions.size();

		const bool unsaved = HasUnsavedChanges();
		const char* saveTooltip = SaveManager::GetEditorSaveTooltip(unsaved);
		auto saveClick = []() { SaveManager::SaveEditor(SaveManager::Trigger::User); };
		std::optional<ImVec4> unsavedColor;
		if (unsaved)
			unsavedColor = Util::Colors::GetUnsaved();
		if (useIcons) {
			auto& action = addGlyph("##TitleSave", Icons::TI(ICON_TI_DEVICE_FLOPPY), saveTooltip, saveClick);
			action.textColor = unsavedColor;
		} else {
			auto& action = addText("##TitleSave", T(TKEY("save"), "Save"), saveTooltip, saveClick);
			action.textColor = unsavedColor;
		}

		const char* loadTooltip = T(TKEY("load_saved_file"), "Load saved file, else the active preset's values, else vanilla");
		auto loadClick = [this]() { Load(); };
		if (useIcons)
			addGlyph("##TitleLoad", Icons::TI(ICON_TI_FOLDER_OPEN), loadTooltip, loadClick);
		else
			addText("##TitleLoad", T(TKEY("load"), "Load"), loadTooltip, loadClick);

		const char* revertTooltip = T(TKEY("revert_to_original"), "Revert to original game values");
		auto revertClick = [this]() { RevertChanges(); };
		if (useIcons) {
			auto& action = addGlyph("##TitleRevert", Icons::TI(ICON_TI_RESTORE), revertTooltip, revertClick);
			action.textColor = statusPalette.Warning;
		} else {
			auto& action = addText("##TitleRevert", T(TKEY("revert"), "Revert"), revertTooltip, revertClick);
			action.textColor = statusPalette.Warning;
		}

		if (HasSavedFile()) {
			const char* deleteTooltip = T(TKEY("delete_saved_file_tooltip"), "Delete saved file");
			auto deleteClick = []() { ImGui::OpenPopup("DeleteConfirmation"); };
			TitleBarAction* deleteAction = nullptr;
			if (useIcons) {
				deleteAction = &addGlyph("##TitleDelete", Icons::TI(ICON_TI_TRASH), deleteTooltip, deleteClick);
				deleteAction->textColor = statusPalette.Error;
			} else {
				deleteAction = &addText("##TitleDelete", T(TKEY("delete"), "Delete"), deleteTooltip, deleteClick);
				deleteAction->textColor = statusPalette.Error;
			}
			deleteAction->destructive = true;
		}

		if (packBadge) {
			auto& action = addGlyph("##TitlePackSource", Icons::TI(ICON_TI_STACK_2), packBadge->tooltip.c_str(), []() {});
			action.textColor = packBadge->color;
		}

		if (groupStart > 0)
			actions[groupStart].startsGroup = true;
	}

	const float gap = style.ItemInnerSpacing.x;
	const float groupGap = gap * 3.0f;
	const float fontSize = ImGui::GetFontSize();
	const float lockBadgeSize = Util::GetLockStatusBadgeSize();
	// Match BeginWithCustomHeader's chrome height so search/actions share the title's midline.
	const float headerH = Util::GetEditorChromeHeaderHeight();
	float buttonHeight = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;

	ImRect titleRect;
	float left = 0.0f;
	float right = 0.0f;

	if (nativeTitleBar) {
		titleRect = window->TitleBarRect();
		buttonHeight = titleRect.GetHeight() - style.FramePadding.y * 2.0f;
		left = titleRect.Min.x + style.FramePadding.x;
		right = titleRect.Max.x - window->WindowBorderSize - style.FramePadding.x;
		const bool hasCollapseButton = !(window->Flags & ImGuiWindowFlags_NoCollapse) && style.WindowMenuButtonPosition != ImGuiDir_None;
		if (hasCollapseButton) {
			if (style.WindowMenuButtonPosition == ImGuiDir_Right)
				right -= fontSize + style.ItemInnerSpacing.x;
			else
				left += fontSize + style.ItemInnerSpacing.x;
		}
		if (window->HasCloseButton)
			right -= fontSize + style.ItemInnerSpacing.x;
		right -= gap;
		left += ImGui::CalcTextSize(window->Name, nullptr, true).x + groupGap;
	} else {
		// Floating custom header: SameLine after the title. Anchor the control band to the
		// full chrome row (not the short text item) so icons/search share equal top/bottom air.
		const ImRect titleItem(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		const float rowTop = titleItem.Min.y - Util::GetEditorChromeTextCursorOffsetY(headerH);
		const float contentRight = window->WorkRect.Max.x;
		const float closeSize = fontSize + style.FramePadding.y * 2.0f;
		const float leftEdge = ImGui::GetCursorScreenPos().x;
		titleRect = ImRect(ImVec2(leftEdge, rowTop), ImVec2(contentRight, rowTop + headerH));
		left = leftEdge;
		right = contentRight - closeSize - gap;
	}

	float actionsWidth = 0.0f;
	for (size_t i = 0; i < actions.size(); ++i) {
		auto& action = actions[i];
		if (action.kind == Kind::Text)
			action.width = ImGui::CalcTextSize(action.label.c_str()).x + style.FramePadding.x * 2.0f;
		else if (action.kind == Kind::LockBadge)
			action.width = lockBadgeSize;
		else
			action.width = buttonHeight;  // Icon / Glyph
		if (i > 0)
			actionsWidth += action.startsGroup ? groupGap : gap;
		actionsWidth += action.width;
	}

	const float searchWidth = searchId ? WidgetUI::kSearchBarWidth * scale : 0.0f;
	const float searchGap = searchId ? gap * 2.0f : 0.0f;
	const float needed = searchWidth + searchGap + actionsWidth;
	if (right - left < needed && !actions.empty() && !searchId)
		return false;
	if (right - left < actionsWidth && !searchId)
		return false;

	const float cursorX = left;
	ImDrawList* drawList = window->DrawList;

	const float actionsLeft = right - actionsWidth;
	bool drewSearch = false;
	if (searchId && searchWidth > 0.0f && actionsLeft - cursorX >= searchWidth + searchGap) {
		const float searchH = ImGui::GetFrameHeight();
		const float searchX = cursorX + (actionsLeft - cursorX - searchWidth) * 0.5f;
		const float searchY = titleRect.Min.y + (titleRect.GetHeight() - searchH) * 0.5f;
		ImGui::SetCursorScreenPos(ImVec2(searchX, searchY));
		ImGui::SetNextItemWidth(searchWidth);
		bool ctrlF = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		             ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false);
		if (ctrlF) {
			ClearSearchState(true);
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::InputTextWithHint(searchId, T(TKEY("search_settings_hint"), "Search settings (Ctrl+F)"), searchBuffer, sizeof(searchBuffer));
		searchInputMin = ImGui::GetItemRectMin();
		searchInputMax = ImGui::GetItemRectMax();
		if (ImGui::IsItemEdited())
			dropdownVisible = true;
		drewSearch = true;
		m_titleBarSearchDrawn = true;
	}

	if (actions.empty() && !drewSearch)
		return true;  // Nothing to show; the title bar stays clean and no inline row is needed.

	if (actionsLeft < cursorX && !actions.empty())
		return false;  // Actions don't fit; caller draws them inline.

	// Draw right-aligned actions left to right.
	// Use InvisibleButton (same pattern as the main CS menu undocked header / close button) so hits
	// win over the custom-header drag catcher's AllowOverlap. ItemAdd+ButtonBehavior alone does not.
	float x = actionsLeft;
	const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
	const ImVec4 iconTint = Util::GetIconTint();
	const float rounding = style.FrameRounding;

	std::function<void()> pendingClick;

	ImGui::PushClipRect(titleRect.Min, titleRect.Max, false);
	for (size_t i = 0; i < actions.size(); ++i) {
		const auto& action = actions[i];
		if (i > 0)
			x += action.startsGroup ? groupGap : gap;

		const float itemH = action.kind == Kind::LockBadge ? lockBadgeSize : buttonHeight;
		const float itemY = titleRect.Min.y + (titleRect.GetHeight() - itemH) * 0.5f;
		const ImRect bb(ImVec2(x, itemY), ImVec2(x + action.width, itemY + itemH));
		x += action.width;

		ImGui::SetCursorScreenPos(bb.Min);
		ImGui::InvisibleButton(action.id.c_str(), bb.GetSize());
		const bool hovered = ImGui::IsItemHovered();
		const bool held = ImGui::IsItemActive();
		if (ImGui::IsItemClicked())
			pendingClick = action.onClick;

		switch (action.kind) {
		case Kind::LockBadge:
			Util::DrawLockStatusBadge(bb.Min, action.locked, drawList, hovered, held);
			break;
		case Kind::Icon:
			{
				if (hovered || held) {
					if (action.destructive) {
						auto color = statusPalette.Error;
						color.w = held ? 0.6f : 0.4f;
						drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(color), rounding);
					} else {
						Util::DrawRoundedButtonHighlight(bb, hovered, held, drawList);
					}
				}
				const float iconInset = std::max(1.0f, buttonHeight * 0.1f);
				drawList->AddImage(action.texture, ImVec2(bb.Min.x + iconInset, bb.Min.y + iconInset), ImVec2(bb.Max.x - iconInset, bb.Max.y - iconInset),
					ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(iconTint));
				break;
			}
		case Kind::Glyph:
			{
				if (hovered || held) {
					if (action.destructive) {
						auto color = statusPalette.Error;
						color.w = held ? 0.6f : 0.4f;
						drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(color), rounding);
					} else {
						Util::DrawRoundedButtonHighlight(bb, hovered, held, drawList);
					}
				}
				if (action.glyph) {
					const ImU32 col = action.textColor ? ImGui::GetColorU32(*action.textColor) : textCol;
					Icons::DrawCenteredGlyph(drawList, bb.Min, bb.GetSize(), action.glyph, col);
				}
				break;
			}
		case Kind::Text:
			{
				ImU32 fill = ImGui::GetColorU32(ImGuiCol_Button);
				if (held)
					fill = ImGui::GetColorU32(ImGuiCol_ButtonActive);
				else if (hovered)
					fill = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
				if (action.fillColor) {
					auto color = *action.fillColor;
					if (hovered || held)
						color.w = std::min(1.0f, color.w + 0.2f);
					fill = ImGui::GetColorU32(color);
				}
				drawList->AddRectFilled(bb.Min, bb.Max, fill, rounding);
				const ImVec2 textSize = ImGui::CalcTextSize(action.label.c_str());
				drawList->AddText(
					ImVec2(bb.Min.x + (action.width - textSize.x) * 0.5f, bb.Min.y + (itemH - textSize.y) * 0.5f),
					action.textColor ? ImGui::GetColorU32(*action.textColor) : textCol,
					action.label.c_str());
				break;
			}
		}

		Util::AddTooltip(action.tooltip);
	}
	ImGui::PopClipRect();

	if (pendingClick)
		pendingClick();

	return true;
}

void Widget::DrawWidgetHeader(const char* searchId, bool showApply, bool showSaveLoadRevert, bool showForceWeather, RE::TESWeather* weather)
{
	auto editorWindow = EditorWindow::GetSingleton();
	auto menu = globals::menu;
	bool useIcons = !editorWindow->settings.useTextButtons && menu && menu->GetSettings().Theme.ShowActionIcons;
	const float scale = Util::GetUIScale();
	if (navigatedFromSearch) {
		ClearSearchState(true);
		navigatedFromSearch = false;
	}

	// Prefer the title/custom header for the action buttons; fall back to the inline row when they didn't fit.
	const bool actionsInTitleBar = m_customHeaderActionsDrawn ||
	                               DrawTitleBarActions(showApply, showSaveLoadRevert, showForceWeather, weather, false, searchId);
	const bool inlineActions = !actionsInTitleBar;
	const bool searchInTitleBar = m_titleBarSearchDrawn;

	auto drawSearchBar = [&]() {
		if (searchInTitleBar)
			return;
		ImGui::SetNextItemWidth(WidgetUI::kSearchBarWidth * scale);
		bool ctrlF = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		             ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false);
		if (ctrlF) {
			ClearSearchState(true);
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::InputTextWithHint(searchId, T(TKEY("search_settings_hint"), "Search settings (Ctrl+F)"), searchBuffer, sizeof(searchBuffer));
		searchInputMin = ImGui::GetItemRectMin();
		searchInputMax = ImGui::GetItemRectMax();
		if (ImGui::IsItemEdited())
			dropdownVisible = true;
	};

	auto drawForceWeatherButton = [&]() {
		if (!showForceWeather || !weather)
			return;
		ImGui::SameLine();
		bool isLocked = editorWindow->IsWeatherLockedByUser() && editorWindow->GetLockedWeather() == weather;
		const char* tooltip = !EditorWindow::AreWeatherLockHooksInstalled() ?
		                          T(TKEY("weather_lock_hooks_unavailable"), "Weather-lock hooks failed to install; the lock still works but weather may briefly flash before correcting") :
		                          (isLocked ? T(TKEY("unlock_weather"), "Unlock Weather") : T(TKEY("force_this_weather"), "Force This Weather"));
		if (Util::LockStatusBadgeButton("##InlineForceWeather", isLocked, tooltip)) {
			if (isLocked)
				editorWindow->ReleaseUserWeatherLock();
			else
				editorWindow->LockWeatherForUser(weather);
		}
	};

	const bool unsaved = HasUnsavedChanges();
	const char* saveTooltip = SaveManager::GetEditorSaveTooltip(unsaved);
	const auto saveAll = []() { SaveManager::SaveEditor(SaveManager::Trigger::User); };

	if (useIcons) {
		const float iconSize = ImGui::GetFrameHeight() * WidgetUI::kIconButtonSizeRatio;
		const ImVec2 buttonSize(iconSize, iconSize);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(WidgetUI::kIconButtonSpacing * scale, ImGui::GetStyle().ItemSpacing.y));

		drawSearchBar();
		if (inlineActions)
			drawForceWeatherButton();

		// Transparent icon button style
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, WidgetUI::kIconButtonTransparent);
		ImVec4 iconButtonHover = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		iconButtonHover.w = WidgetUI::kIconButtonHoverAlpha;
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, iconButtonHover);

		auto iconButton = [&](const char* suffix, void* texture, const char* tooltip, auto callback) {
			if (!texture)
				return;
			ImGui::SameLine();
			if (ImGui::ImageButton((std::string(searchId) + suffix).c_str(), texture, buttonSize))
				callback();
			Util::AddTooltip(tooltip);
		};

		// Apply button
		if (inlineActions && showApply && (!editorWindow->settings.autoApplyChanges || RequiresManualApply())) {
			if (Util::IconLoader::GetIcons().applyToGame.texture) {
				iconButton("_Apply", Util::IconLoader::GetIcons().applyToGame.texture, T(TKEY("apply_changes"), "Apply changes to the game"), [&]() { ApplyChanges(); });
			} else {
				ImGui::SameLine();
				if (ImGui::Button(T(TKEY("apply"), "Apply")))
					ApplyChanges();
				Util::AddTooltip(T(TKEY("apply_changes"), "Apply changes to the game"));
			}
		}

		// Save/Load/Revert/Delete group
		if (inlineActions && showSaveLoadRevert) {
			Util::ToolbarDivider(false);
			ImGui::SameLine();
			if (unsaved)
				ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetUnsaved());
			if (Icons::Button(std::format("{}##_Save", searchId ? searchId : "").c_str(), Icons::TI(ICON_TI_DEVICE_FLOPPY)))
				saveAll();
			if (unsaved)
				ImGui::PopStyleColor();
			Util::AddTooltip(saveTooltip);
			iconButton("_Load", Util::IconLoader::GetIcons().loadSettings.texture, T(TKEY("load_saved_file"), "Load saved file, else the active preset's values, else vanilla"), [&]() { Load(); });
			iconButton("_Revert", Util::IconLoader::GetIcons().featureSettingRevert.texture, T(TKEY("revert_to_original"), "Revert to original game values"), [&]() { RevertChanges(); });

			if (HasSavedFile()) {
				Util::ToolbarDivider(false);
				ImGui::SameLine();
				{
					Icons::FontGuard font(Icons::Family::Tabler);
					if (Util::ErrorTextButton(std::format("{}{}_Delete", ICON_TI_TRASH, searchId).c_str()))
						ImGui::OpenPopup("DeleteConfirmation");
				}
				Util::AddTooltip(T(TKEY("delete_saved_file_tooltip"), "Delete saved file"));
			}

			DrawPackSourceBadge();
		}

		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	} else {
		drawSearchBar();
		if (inlineActions)
			drawForceWeatherButton();

		if (menu) {
			auto textButton = [&](const char* label, const char* tooltip, auto callback) {
				ImGui::SameLine();
				if (Util::ButtonWithFlash(label))
					callback();
				Util::AddTooltip(tooltip);
			};

			// Apply button
			if (inlineActions && showApply && (!editorWindow->settings.autoApplyChanges || RequiresManualApply())) {
				ImGui::SameLine();
				if (Util::SuccessButton(T(TKEY("apply"), "Apply")))
					ApplyChanges();
				Util::AddTooltip(T(TKEY("apply_changes"), "Apply changes to the game"));
			}

			// Save/Load/Revert/Delete group
			if (inlineActions && showSaveLoadRevert) {
				Util::ToolbarDivider(false);
				ImGui::SameLine();
				if (unsaved)
					ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetUnsaved());
				if (Util::ButtonWithFlash(T(TKEY("save"), "Save")))
					saveAll();
				if (unsaved)
					ImGui::PopStyleColor();
				Util::AddTooltip(saveTooltip);
				textButton(T(TKEY("load"), "Load"), T(TKEY("load_saved_file"), "Load saved file, else the active preset's values, else vanilla"), [&]() { Load(); });
				ImGui::SameLine();
				if (Util::WarningButton(T(TKEY("revert"), "Revert")))
					RevertChanges();
				Util::AddTooltip(T(TKEY("revert_to_original"), "Revert to original game values"));

				if (HasSavedFile()) {
					Util::ToolbarDivider(false);
					ImGui::SameLine();
					if (Util::ErrorTextButton(T(TKEY("delete"), "Delete")))
						ImGui::OpenPopup("DeleteConfirmation");
					Util::AddTooltip(T(TKEY("delete_saved_file_tooltip"), "Delete saved file"));
				}

				DrawPackSourceBadge();
			}
		}
	}

	DrawDeleteConfirmationModal();

	if (showApply && RequiresManualApply() && editorWindow->settings.autoApplyChanges && menu) {
		ImGui::SameLine();
		ImGui::TextColored(menu->GetTheme().StatusPalette.Warning, "%s", T(TKEY("changes_require_manual_apply"), "(Changes require manual apply)"));
		Util::AddTooltip(T(TKEY("manual_apply_required_tooltip"), "This form type is only re-read by the engine on weather reinit.\nAuto-apply is disabled - use the Apply button."));
	}

	if (!m_titleBarSearchDrawn || inlineActions ||
		(showApply && RequiresManualApply() && editorWindow->settings.autoApplyChanges))
		ImGui::Separator();

	// Remember where the dropdown should appear so DrawSearchDropdown()
	// (called after this function) can anchor itself below the search bar.
	searchDropdownAnchor = m_titleBarSearchDrawn ? ImVec2(searchInputMin.x, searchInputMax.y) : ImGui::GetCursorScreenPos();

	// Rebuild match list only when the query changed; this gates per-frame
	// CollectSearchableSettings() walks plus three case-insensitive searches per entry.
	if (searchBuffer[0] != '\0') {
		if (searchResultsForQuery != searchBuffer) {
			searchResults = CollectSearchableSettings();
			std::erase_if(searchResults, [&](const SearchResult& r) {
				return !ContainsStringIgnoreCase(r.displayName, searchBuffer) &&
				       !ContainsStringIgnoreCase(r.tabName, searchBuffer) &&
				       !ContainsStringIgnoreCase(r.settingId, searchBuffer);
			});
			searchResultsForQuery = searchBuffer;
		}
	} else {
		ClearSearchState(false);
	}
}

void Widget::DrawSearchDropdown()
{
	if (!dropdownVisible || searchResults.empty())
		return;

	const float scale = Util::GetUIScale();
	ImGui::SetNextWindowPos(searchDropdownAnchor, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(WidgetUI::kSearchDropdownWidth * scale, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
	const std::string dropdownWindowId = std::format("##SearchDropdown_{}", static_cast<const void*>(this));
	if (ImGui::Begin(dropdownWindowId.c_str(), nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoFocusOnAppearing)) {
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		// Treat clicks on the search input itself as "inside" so typing/cursor
		// positioning in the input doesn't dismiss the dropdown.
		const ImRect searchInputRect(searchInputMin, searchInputMax);
		const bool clickedOutside = ImGui::GetIO().MouseClicked[0] &&
		                            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
		                            !searchInputRect.Contains(ImGui::GetIO().MousePos);
		if (clickedOutside || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			dropdownVisible = false;
		} else {
			const size_t visibleRows = std::min(WidgetUI::kSearchDropdownMaxResults, searchResults.size());
			const float childHeight = ImGui::GetFrameHeightWithSpacing() * static_cast<float>(visibleRows);
			if (ImGui::BeginChild("##SearchDropdownResults", ImVec2(0.0f, childHeight), ImGuiChildFlags_Borders)) {
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(searchResults.size()), ImGui::GetTextLineHeightWithSpacing());
				while (clipper.Step()) {
					for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
						const auto& result = searchResults[static_cast<size_t>(i)];
						std::string label = result.tabName.empty() ? result.displayName : std::format("{} ({})", result.displayName, result.tabName);

						ImGui::PushID(i);
						if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups)) {
							NavigateToSearchResult(result);
							navigatedFromSearch = true;
						}
						ImGui::PopID();
					}
				}
			}
			ImGui::EndChild();
		}
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void Widget::ClearSearchState(bool clearBuffer)
{
	if (clearBuffer)
		searchBuffer[0] = '\0';
	searchResults.clear();
	searchResultsForQuery.clear();
	dropdownVisible = false;
}

void Widget::NavigateToSearchResult(const SearchResult& result)
{
	activeTabOverride = result.tabName;
	highlightedSetting = result.settingId;
	highlightedDisplaySetting = result.displayName;
	highlightStartTime = static_cast<float>(ImGui::GetTime());
	scrollToHighlighted = true;
}

int Widget::GetTabFlagsForOverride(const std::string& tabName)
{
	if (activeTabOverride.empty() || activeTabOverride != tabName)
		return 0;
	activeTabOverride.clear();
	return ImGuiTabItemFlags_SetSelected;
}

bool Widget::MatchesSearch(const std::string& settingId) const
{
	if (searchBuffer[0] == '\0')
		return true;
	return std::any_of(searchResults.begin(), searchResults.end(),
		[&](const SearchResult& r) { return r.settingId == settingId; });
}

void Widget::AutoApply(bool changed)
{
	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges)
		ApplyChanges();
}

bool Widget::MatchesAnySearch(std::initializer_list<const char*> settingIds) const
{
	return std::any_of(settingIds.begin(), settingIds.end(), [&](const char* settingId) {
		return MatchesSearch(settingId);
	});
}

bool Widget::IsHighlighted(const std::string& settingId) const
{
	if (highlightedSetting != settingId && highlightedDisplaySetting != settingId)
		return false;
	const float elapsed = static_cast<float>(ImGui::GetTime()) - highlightStartTime;
	return elapsed < WidgetUI::kHighlightDurationSeconds;
}

void Widget::PushHighlightStyle(const std::string& settingId)
{
	if (!IsHighlighted(settingId))
		return;
	const float elapsed = static_cast<float>(ImGui::GetTime()) - highlightStartTime;
	const float normalized = std::clamp(elapsed / WidgetUI::kHighlightDurationSeconds, 0.0f, 1.0f);
	const float triangularFade = 1.0f - std::abs(normalized * 2.0f - 1.0f);
	const float alpha = std::clamp(WidgetUI::kHighlightMaxAlpha * triangularFade, 0.0f, WidgetUI::kHighlightMaxAlpha);
	ImVec4 frameBg = WidgetUI::kHighlightFrameBg;
	ImVec4 frameBgHovered = WidgetUI::kHighlightFrameBgHovered;
	frameBg.w = alpha;
	frameBgHovered.w = alpha;
	ImGui::PushStyleColor(ImGuiCol_FrameBg, frameBg);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frameBgHovered);
}

void Widget::PopHighlightStyle(const std::string& settingId)
{
	if (!IsHighlighted(settingId))
		return;
	ImGui::PopStyleColor(2);
	if (scrollToHighlighted) {
		ImGui::SetScrollHereY(0.5f);
		scrollToHighlighted = false;
	}
}

#undef I18N_KEY_PREFIX
