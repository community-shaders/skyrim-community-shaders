#include "DebugSection.h"

#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"

#include <utility>

namespace Util
{
	namespace
	{
		bool debugSectionsHidden = false;
	}

	HideDebugSectionsGuard::HideDebugSectionsGuard(bool hide) :
		previous(std::exchange(debugSectionsHidden, hide))
	{}

	HideDebugSectionsGuard::~HideDebugSectionsGuard()
	{
		debugSectionsHidden = previous;
	}

	bool ShowDebugSections()
	{
		return !debugSectionsHidden;
	}

	DebugSection::DebugSection() :
		visible(false)
	{
		if (!ShowDebugSections())
			return;

		using C = ThemeManager::Constants;
		ImGui::Spacing();

		// Styled like a feature-category header (DrawCategoryHeader), but centred and with no icon or count.
		const ImGuiID id = ImGui::GetID("##DebugSectionHeader");
		ImGuiStorage* storage = ImGui::GetStateStorage();
		bool expanded = storage->GetBool(id, false);

		const char* label = T("menu.features.debug", "Debug");
		const float scale = Util::GetUIScale();
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 textSize = ImGui::CalcTextSize(label);
		const float pad = C::CATEGORY_HEADER_PAD * scale;
		ImVec2 pos = ImGui::GetCursorScreenPos();

		ImGui::PushID(id);
		const bool clicked = ImGui::InvisibleButton("##DebugHeader", ImVec2(availableWidth, textSize.y + pad));
		ImGui::PopID();
		const bool hovered = ImGui::IsItemHovered();
		pos.y += pad * 0.5f;

		ImVec4 color = Util::Colors::GetSecondary();
		if (!expanded)
			color.w *= C::CATEGORY_HEADER_COLLAPSED_ALPHA;
		if (hovered)
			color = ImGui::GetStyleColorVec4(ImGuiCol_Text);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float spacing = ImGui::GetFontSize() * C::SECTION_ICON_SPACING_SCALE;
		const float textX = pos.x + (availableWidth - textSize.x) * 0.5f;
		drawList->AddText(ImVec2(textX, pos.y), ImGui::GetColorU32(color), label);

		ImVec4 line = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
		line.w *= C::CATEGORY_HEADER_LINE_ALPHA;
		const ImU32 lineColor = ImGui::GetColorU32(line);
		const float padX = ImGui::GetStyle().FramePadding.x;
		const float lineY = pos.y + textSize.y * 0.5f;
		if (textX - spacing > pos.x + padX)
			drawList->AddLine(ImVec2(pos.x + padX, lineY), ImVec2(textX - spacing, lineY), lineColor, 1.0f);
		if (textX + textSize.x + spacing < pos.x + availableWidth - padX)
			drawList->AddLine(ImVec2(textX + textSize.x + spacing, lineY), ImVec2(pos.x + availableWidth - padX, lineY), lineColor, 1.0f);

		if (clicked) {
			expanded = !expanded;
			storage->SetBool(id, expanded);
		}

		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + pad * 0.5f));
		ImGui::Dummy(ImVec2(availableWidth, 0.0f));
		visible = expanded;
	}

	void DebugSection::Hint(const char* text) const
	{
		if (!visible)
			return;

		const auto& disabled = globals::menu->GetSettings().Theme.StatusPalette.Disable;
		ImGui::PushStyleColor(ImGuiCol_Text, disabled);
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}
}
