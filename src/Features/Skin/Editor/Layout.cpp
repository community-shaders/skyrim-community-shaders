#include "Features/Skin.h"

#include "CSEditor/Browser/BrowserWidgets.h"
#include "Globals.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "State.h"
#include "Utils/UI.h"

#include <set>

// Page frame: page actions, the character bar, ignored-file banner and the skin texture column.

using namespace SkinMaterials;
using namespace SkinEditor;

namespace
{
	/// Starting width of the texture column at the baseline font size; the user can drag the divider.
	constexpr float kListWidth = 250.0f;
	constexpr ImGuiTableFlags kLayoutFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;

	std::string Stem(const std::string& a_key)
	{
		auto name = a_key.substr(a_key.rfind('/') + 1);
		if (name.ends_with(".dds"))
			name.resize(name.size() - 4);
		return name;
	}

	struct RowFace
	{
		ImVec4 dot;
		bool filled = false;
		const char* status = "";
	};
	RowFace Face(const SkinLibrary::Entry& a_entry)
	{
		if (a_entry.definition.status == Status::Invalid)
			return { Util::Colors::GetError(), true, T("feature.skin.material_file_invalid", "Invalid material file - ignored") };
		switch (a_entry.origin) {
		case SkinLibrary::Origin::Loose:
		case SkinLibrary::Origin::Archive:
			return { Util::Colors::GetSuccess(), true, T("feature.skin.row_has_material", "Has a material file") };
		case SkinLibrary::Origin::Session:
			return { Util::Colors::GetInfo(), true, T("feature.skin.material_origin_session", "Material file saved in this session") };
		default:
			return { Util::Colors::GetDisabled(), false, T("feature.skin.material_origin_none", "No material file - common settings") };
		}
	}

	/// Two-line selectable: a status dot and the texture name, with where it is shown beneath.
	bool TextureRow(const std::string& a_id, const std::string& a_name, const std::string& a_meta, const RowFace& a_face, bool a_selected,
		bool a_adjusted, bool a_unsaved)
	{
		const auto& style = ImGui::GetStyle();
		const float line = ImGui::GetTextLineHeight();
		const float height = line * 2.0f + style.FramePadding.y * 2.0f;
		ImGui::PushID(a_id.c_str());
		const bool clicked = ImGui::Selectable("##row", a_selected, 0, ImVec2(0.0f, height));
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		auto* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(min, max, true);
		const float radius = line * 0.22f;
		const float top = min.y + style.FramePadding.y;
		const ImVec2 dot{ min.x + style.FramePadding.x + radius, top + line * 0.5f };
		Util::DrawIconCircle(dot, radius, ImGui::GetColorU32(a_face.dot), a_face.filled);
		const float textX = dot.x + radius + style.ItemInnerSpacing.x;
		// Markers on the right: unsaved edits, then this character's own adjustments.
		float right = max.x - style.FramePadding.x;
		const auto marker = [&](const ImVec4& color) {
			right -= radius * 2.0f;
			drawList->AddCircleFilled({ right + radius, top + line * 0.5f }, radius * 0.8f, ImGui::GetColorU32(color));
			right -= style.ItemInnerSpacing.x;
		};
		if (a_unsaved)
			marker(Util::Colors::GetWarning());
		if (a_adjusted)
			marker(Util::Colors::GetAccent());
		drawList->PopClipRect();
		drawList->PushClipRect({ textX, min.y }, { right, max.y }, true);
		drawList->AddText({ textX, top }, ImGui::GetColorU32(ImGuiCol_Text), a_name.c_str());
		drawList->AddText({ textX, top + line }, ImGui::GetColorU32(Util::Colors::GetSecondary()), a_meta.c_str());
		drawList->PopClipRect();
		ImGui::PopID();
		return clicked;
	}
}

std::string Skin::GetEditorSubtitle()
{
	std::lock_guard lock(mutex);
	if (!editor.actor.get())
		return {};
	std::set<std::string> keys;
	for (const auto& surface : editor.surfaces)
		if (!surface.key.empty())
			keys.insert(surface.key);
	return I18n::GetSingleton()->Format("feature.skin.subtitle", { { "name", editor.actorName.empty() ? T("feature.skin.unnamed_character", "Unnamed character") : editor.actorName }, { "count", std::to_string(keys.size()) } },
		"{name} - {count} skin textures");
}

void Skin::DrawEditorPageActions()
{
	std::lock_guard lock(mutex);
	const float button = Util::IconButtonSize();
	BrowserUI::RightAlign(button * 2.0f + ImGui::GetStyle().ItemSpacing.x);
	const auto tools = std::format("{}\n{}", T("feature.skin.workspace_textures", "Texture tools"),
		T("feature.skin.texture_tools_tooltip", "Pack grayscale maps into a Skin controls texture, or a detail normal with its region mask."));
	if (Util::IconButton("##skin_tools", Icons::TI(ICON_TI_TOOLS), tools.c_str(), editor.toolsWindow))
		editor.toolsWindow = !editor.toolsWindow;
	ImGui::SameLine();
	const auto reload = std::format("{}\n{}", T("feature.skin.reload_skin_resources", "Reload skin resources"),
		T("feature.skin.reload_skin_resources_help_v2", "Re-reads material files, character files and DDS inputs changed outside the game."));
	if (Util::IconButton("##skin_reload", Icons::TI(ICON_TI_REFRESH), reload.c_str()))
		ReloadResources();
}

void Skin::DrawTargetBar()
{
	const ImVec4 accent = Util::Colors::GetAccent();
	const ImVec4 iconColor = Util::Colors::GetSecondary();
	using Source = Editor::Source;
	if (BrowserUI::Chip("##skin_player", T("feature.skin.player", "Player"), Icons::TI(ICON_TI_USER), &iconColor, editor.source == Source::Player ? &accent : nullptr))
		SelectActor(RE::PlayerCharacter::GetSingleton(), Source::Player);
	Util::AddTooltip(T("feature.skin.player_tooltip", "Edit the skin textures your character shows."));
	ImGui::SameLine();
	if (BrowserUI::Chip("##skin_console", T("feature.skin.console_selection", "Console selection"), Icons::TI(ICON_TI_CROSSHAIR), &iconColor, editor.source == Source::Console ? &accent : nullptr)) {
		const auto reference = RE::Console::GetSelectedRef();
		auto* actor = reference ? reference->As<RE::Actor>() : nullptr;
		if (!actor)
			throw std::runtime_error(T("feature.skin.select_a_reference_in_the_console_or_choose_player", "Select a reference in the console, or choose Player."));
		SelectActor(actor, Source::Console);
	}
	Util::AddTooltip(T("feature.skin.console_tooltip", "Open the console, click an NPC, then choose this to edit what it shows."));
	const auto actor = editor.actor.get();
	if (!actor)
		return;
	// Equipment and RaceMenu changes rebuild the model; keep the list current without user action.
	if (globals::state->frameCount - editor.lastRefresh > 30)
		RefreshSurfaces();
	Util::ToolbarDivider();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(editor.actorName.empty() ? T("feature.skin.unnamed_character", "Unnamed character") : editor.actorName.c_str());
	ImGui::SameLine();
	BrowserUI::MetaText(editor.draftTarget ? editor.draftTarget.text.c_str() : T("feature.skin.no_record_short", "no stable record"));
	if (!editor.draftTarget)
		Util::AddTooltip(T("feature.skin.character_no_identity", "This character was generated at runtime and has no record to save adjustments for."));
}

void Skin::DrawProblems()
{
	const auto state = SkinCharacters::Get();
	if (state->problems.empty())
		return;
	const auto message = I18n::GetSingleton()->Format("feature.skin.character_problems", { { "count", std::to_string(state->problems.size()) } },
		"{count} character files are ignored. Click for details.");
	if (Util::StatusBanner(ICON_TI_ALERT_TRIANGLE, message.c_str(), Util::Colors::GetWarning()))
		editor.showProblems = !editor.showProblems;
	if (!editor.showProblems)
		return;
	ImGui::Indent();
	for (const auto& problem : state->problems) {
		BrowserUI::MetaText(UTF8(problem.file.filename()).c_str());
		Util::Text::WrappedSecondary("%s", problem.diagnostic.c_str());
	}
	ImGui::Unindent();
}

void Skin::DrawTextureList()
{
	if (!editor.actor.get()) {
		BrowserUI::EmptyState(T("feature.skin.empty_no_character", "No character"),
			T("feature.skin.choose_target_first_v2", "Choose Player, or select an NPC in the console, to see which skin textures it shows."));
		return;
	}
	Util::SearchField("##skin_filter", editor.filter, T("feature.skin.filter_textures", "Filter textures..."), ImGui::GetContentRegionAvail().x, true);
	const std::string query = editor.filter;

	// One row per texture under the part it is mainly shown on; the row lists every place it appears.
	struct Row
	{
		SkinSurfaces::Part part = SkinSurfaces::Part::Other;
		std::vector<std::string> shown;
	};
	std::vector<std::string> order;
	std::map<std::string, Row> rows;
	size_t unidentified = 0;
	for (const auto& surface : editor.surfaces) {
		if (surface.key.empty()) {
			++unidentified;
			continue;
		}
		auto [row, inserted] = rows.try_emplace(surface.key);
		if (inserted) {
			order.push_back(surface.key);
			row->second.part = surface.description.part;
		}
		const auto label = SkinSurfaces::Label(surface.description);
		if (std::find(row->second.shown.begin(), row->second.shown.end(), label) == row->second.shown.end())
			row->second.shown.push_back(label);
	}
	const auto matches = [&](const std::string& key) { return query.empty() || Util::StringMatchesSearch(key, query); };
	const auto draw = [&](const std::string& key, const std::string& meta) {
		const auto entry = FindMaterial(key);
		const auto face = Face(*entry);
		const bool adjusted = editor.draftTarget && editor.character.materials.contains(key);
		const bool unsaved = key == editor.draftKey && MaterialDirty();
		if (TextureRow(key, Stem(key), meta, face, key == editor.key, adjusted, unsaved))
			SelectKey(key);
		std::string tooltip = key + "\n" + face.status;
		if (adjusted)
			tooltip += std::string("\n") + T("feature.skin.row_adjusted", "Adjusted for this character");
		if (unsaved)
			tooltip += std::string("\n") + T("feature.skin.material_draft_unsaved", "Texture material: unsaved");
		Util::AddTooltip(tooltip.c_str());
	};
	const auto names = SkinSurfaces::PartNames();
	for (size_t part = 0; part < names.size(); ++part) {
		bool labelled = false;
		for (const auto& key : order) {
			const auto& row = rows.at(key);
			if (static_cast<size_t>(row.part) != part || !matches(key))
				continue;
			if (!labelled) {
				ImGui::Spacing();
				BrowserUI::SectionLabel(names[part]);
				labelled = true;
			}
			std::string meta;
			for (const auto& shown : row.shown)
				meta += (meta.empty() ? "" : " · ") + shown;
			draw(key, meta);
		}
	}
	// Adjustments are kept for textures the character no longer shows, for example after a race change.
	bool labelled = false;
	for (const auto& [key, changes] : editor.character.materials) {
		if (rows.contains(key) || !matches(key))
			continue;
		if (!labelled) {
			ImGui::Spacing();
			BrowserUI::SectionLabel(T("feature.skin.inactive_short", "Adjusted, not shown now"));
			Util::AddTooltip(T("feature.skin.inactive_character_materials_help", "These apply again when the character shows the texture, for example after switching back to an earlier race. They never apply to a different texture."));
			labelled = true;
		}
		draw(key, T("feature.skin.not_shown_now", "Not shown on this character now"));
	}
	if (rows.empty() && !labelled)
		BrowserUI::EmptyState(T("feature.skin.no_compatible_skin_surfaces_were_found", "No compatible skin surfaces were found."));
	if (unidentified) {
		ImGui::Spacing();
		Util::Text::WrappedDisabled("%s", I18n::GetSingleton()->Format("feature.skin.unidentified_count", { { "count", std::to_string(unidentified) } },
																  "{count} skin surfaces have no identifiable texture and keep the common settings.")
											  .c_str());
	}
}

void Skin::DrawMaterialEditor()
{
	std::lock_guard lock(mutex);
	try {
		// Ctrl+S is the editor's Save All; it also saves the layer being edited here. Undo stays on the toolbar,
		// since the editor already claims Ctrl+Z for its own history.
		const auto& io = ImGui::GetIO();
		if (!io.WantTextInput && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false) && LayerAvailable(editor.layer) && LayerDirty(editor.layer))
			SaveLayer();
		DrawTargetBar();
		DrawProblems();
		ImGui::Spacing();
		if (ImGui::BeginTable("##skin_layout", 2, kLayoutFlags)) {
			const SKSE::stl::scope_exit endTable([]() noexcept { ImGui::EndTable(); });
			ImGui::TableSetupColumn("##textures", ImGuiTableColumnFlags_WidthFixed, kListWidth * Util::GetUIScale());
			ImGui::TableSetupColumn("##body", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();
			// Each column scrolls on its own, so a long texture list never drags the material with it.
			ImGui::TableSetColumnIndex(0);
			{
				const bool visible = ImGui::BeginChild("##skin_texture_list");
				const SKSE::stl::scope_exit endChild([]() noexcept { ImGui::EndChild(); });
				if (visible)
					DrawTextureList();
			}
			ImGui::TableSetColumnIndex(1);
			{
				const bool visible = ImGui::BeginChild("##skin_texture_panel");
				const SKSE::stl::scope_exit endChild([]() noexcept { ImGui::EndChild(); });
				if (visible)
					DrawTexturePanel();
			}
		}
	} catch (const std::exception& error) {
		Notify(error.what(), true);
	}
	try {
		DrawEditorDialogs();
		DrawTextureTools();
	} catch (const std::exception& error) {
		Notify(error.what(), true);
	}
}
