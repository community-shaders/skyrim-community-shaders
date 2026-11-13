#include "Features/Skin.h"

#include "CSEditor/Browser/BrowserWidgets.h"
#include "CSEditor/EditorWindow.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/SceneActionIcons.h"
#include "Utils/UI.h"

#include <shellapi.h>

// The selected texture: its header, the layer breadcrumb with the page toolbar, and what saving does.

using namespace SkinMaterials;
using namespace SkinEditor;

namespace
{
	constexpr float kRightMargin = 8.0f;

	std::string Stem(const std::string& a_key)
	{
		auto name = a_key.substr(a_key.rfind('/') + 1);
		if (name.ends_with(".dds"))
			name.resize(name.size() - 4);
		return name;
	}
	// Accepts the mod folder itself, its textures folder, or any folder below textures.
	std::filesystem::path ModRoot(std::filesystem::path a_folder)
	{
		for (auto current = a_folder; current.has_parent_path() && current != current.parent_path(); current = current.parent_path())
			if (_wcsicmp(current.filename().c_str(), L"textures") == 0)
				return current.parent_path();
		return a_folder;
	}
	void OpenFolder(const std::filesystem::path& a_folder)
	{
		std::error_code ignored;
		std::filesystem::create_directories(a_folder, ignored);
		ShellExecuteW(nullptr, L"open", std::filesystem::absolute(a_folder).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	std::string Action(const char* a_label, const char* a_description)
	{
		return std::format("{}\n{}", a_label, a_description);
	}
	/// Right-aligns a toolbar on the current row, moving it to its own row when it does not fit.
	void AlignToolbar(float a_width)
	{
		const float margin = kRightMargin * Util::GetUIScale();
		ImGui::SameLine();
		if (ImGui::GetContentRegionAvail().x < a_width + margin)
			ImGui::NewLine();
		if (const float avail = ImGui::GetContentRegionAvail().x; avail > a_width + margin)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - a_width - margin);
	}
}

void Skin::DrawTexturePanel()
{
	if (editor.key.empty()) {
		BrowserUI::EmptyState(T("feature.skin.empty_no_texture", "No skin texture selected"),
			T("feature.skin.empty_no_texture_hint", "Pick a texture on the left. Everything that shows it - other characters, races, and skin inside outfits - shares its material."));
		return;
	}
	DrawTextureHeader();
	ImGui::Spacing();
	DrawLayerStack();
	DrawLayerToolbar();
	DrawLayerNote();
	ImGui::Separator();
	ImGui::Spacing();
	DrawMaterialControls();
}

void Skin::DrawTextureHeader()
{
	const auto entry = FindMaterial(editor.key);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(Stem(editor.key).c_str());
	const char* origin;
	ImVec4 color;
	if (entry->definition.status == Status::Invalid) {
		origin = T("feature.skin.badge_invalid", "Invalid file");
		color = Util::Colors::GetError();
	} else if (entry->origin == SkinLibrary::Origin::Archive) {
		origin = T("feature.skin.badge_archive", "Material in archive");
		color = Util::Colors::GetSuccess();
	} else if (entry->origin == SkinLibrary::Origin::Loose) {
		origin = T("feature.skin.badge_loose", "Material file");
		color = Util::Colors::GetSuccess();
	} else if (entry->origin == SkinLibrary::Origin::Session) {
		origin = T("feature.skin.badge_session", "Saved this session");
		color = Util::Colors::GetInfo();
	} else {
		origin = T("feature.skin.badge_none", "Common settings only");
		color = Util::Colors::GetDisabled();
	}
	BrowserUI::InlineBadge(origin, color);
	Util::AddTooltip(entry->savedTo.empty() ? entry->path.c_str() : entry->savedTo.c_str());
	BrowserUI::MetaText(editor.key.c_str());

	std::string shown;
	for (const auto& surface : editor.surfaces)
		if (surface.key == editor.key) {
			const auto label = SkinSurfaces::Label(surface.description);
			if (shown.find(label) == std::string::npos)
				shown += (shown.empty() ? "" : " · ") + label;
		}
	if (shown.empty())
		Util::Text::WrappedDisabled("%s", T("feature.skin.not_shown_now", "Not shown on this character now"));
	else
		Util::Text::WrappedSecondary("%s", I18n::GetSingleton()->Format("feature.skin.shown_on", { { "parts", shown } }, "Shown on {parts}").c_str());

	if (entry->definition.status == Status::Invalid) {
		const auto message = std::format("{} {}", T("feature.skin.material_file_invalid_detail", "The existing file is ignored and the common settings apply. Saving replaces it:"), entry->definition.diagnostic);
		Util::StatusBanner(ICON_TI_ALERT_TRIANGLE, message.c_str(), Util::Colors::GetError());
	}
	if (!editor.fingerprintWarning.empty())
		Util::StatusBanner(ICON_TI_ALERT_TRIANGLE, editor.fingerprintWarning.c_str(), Util::Colors::GetWarning());
}

void Skin::DrawLayerStack()
{
	const auto& style = ImGui::GetStyle();
	const ImVec4 accent = Util::Colors::GetAccent();
	const Icons::GlyphRef separator = Icons::TI(ICON_TI_CHEVRON_RIGHT);
	const auto name = editor.actorName.empty() ? std::string(T("feature.skin.unnamed_character", "Unnamed character")) : editor.actorName;
	const auto describe = [&](std::optional<Layer> a_layer) -> std::string {
		if (!a_layer)
			return T("feature.skin.layer_common_tooltip", "The Advanced Skin feature settings. They apply to every compatible skin unless a layer to the right sets a value. Click to open them.");
		switch (*a_layer) {
		case Layer::Material:
			return T("feature.skin.layer_material_tooltip", "Stored in a file beside the skin texture. Shared by every character, race and outfit that shows the texture.");
		case Layer::CharacterAll:
			return I18n::GetSingleton()->Format("feature.skin.layer_character_all_tooltip", { { "name", name } }, "{name}'s character file: numbers applied to every skin texture this character shows.");
		default:
			return I18n::GetSingleton()->Format("feature.skin.layer_character_material_tooltip", { { "name", name } }, "{name}'s character file: values and textures used only while this character shows this texture. They win over everything else.");
		}
	};

	ImGui::PushID("##skin_layer_stack");
	ImGui::AlignTextToFramePadding();
	if (BrowserUI::Link("##common", T("feature.skin.layer_common", "Common")))
		if (auto* window = EditorWindow::GetSingleton())
			window->OpenBaseSettings(GetShortName());
	Util::AddTooltip(describe(std::nullopt).c_str());
	for (const auto layer : { Layer::Material, Layer::CharacterAll, Layer::CharacterMaterial }) {
		ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
		ImGui::AlignTextToFramePadding();
		{
			Icons::FontGuard font(separator);
			Util::Text::Secondary("%s", separator.utf8);
		}
		ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
		ImGui::PushID(static_cast<int>(layer));
		auto tooltip = describe(layer);
		if (LayerDirty(layer))
			tooltip += std::string("\n\n") + T("feature.skin.layer_unsaved", "Has unsaved changes.");
		if (layer == editor.layer) {
			BrowserUI::Chip("##current", LayerLabel(layer), {}, nullptr, &accent);
			Util::AddTooltip(tooltip.c_str());
		} else if (LayerAvailable(layer)) {
			ImGui::AlignTextToFramePadding();
			if (BrowserUI::Link("##layer", LayerLabel(layer)))
				editor.layer = layer;
			Util::AddTooltip((tooltip + "\n\n" + T("feature.skin.layer_click", "Click to edit this layer.")).c_str());
		} else {
			ImGui::AlignTextToFramePadding();
			Util::Text::Disabled("%s", LayerLabel(layer));
			Util::AddTooltip(editor.actor.get() ? T("feature.skin.character_no_identity", "This character was generated at runtime and has no record to save adjustments for.") :
												  T("feature.skin.layer_needs_character", "Choose Player or an NPC to adjust one character."),
				Util::kTooltipWhenDisabled);
		}
		if (LayerDirty(layer)) {
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x * 0.5f);
			Util::DrawInlineIndicatorDot(ImGui::GetColorU32(Util::Colors::GetWarning()), true);
		}
		ImGui::PopID();
	}
	ImGui::PopID();
}

void Skin::DrawLayerToolbar()
{
	const float button = Util::IconButtonSize();
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	// Undo and redo, save and revert, copy, clear and preview, with dividers between the groups.
	AlignToolbar(button * 7.0f + spacing * 3.0f + (spacing * 4.0f + 1.0f) * 3.0f);
	ImGui::PushID("##skin_layer_toolbar");
	const SKSE::stl::scope_exit popID([]() noexcept { ImGui::PopID(); });
	const bool dirty = LayerDirty(editor.layer);

	{
		Util::DisableGuard disabled(editor.undo.empty());
		if (Util::IconButton("##undo", Icons::TI(ICON_TI_ARROW_BACK_UP), T("feature.skin.undo", "Undo")))
			Undo();
	}
	ImGui::SameLine();
	{
		Util::DisableGuard disabled(editor.redo.empty());
		if (Util::IconButton("##redo", Icons::TI(ICON_TI_ARROW_FORWARD_UP), T("feature.skin.redo", "Redo")))
			Redo();
	}

	Util::ToolbarDivider();
	const auto saveTooltip = Action(T("feature.skin.save_layer", "Save (Ctrl+S)"), editor.layer == Layer::Material ?
																					   T("feature.skin.save_layer_material", "Writes this texture's material file.") :
																					   T("feature.skin.save_layer_character", "Writes this character's file, with both character layers."));
	{
		Util::DisableGuard disabled(!dirty);
		if (Util::IconButton("##save", SceneActionIcons::kSave, saveTooltip.c_str(), dirty, dirty ? ImGui::GetColorU32(Util::Colors::GetWarning()) : 0))
			SaveLayer();
		ImGui::SameLine();
		if (Util::IconButton("##revert", Icons::TI(ICON_TI_RESTORE), Action(T("feature.skin.revert_layer", "Revert"), T("feature.skin.revert_layer_tooltip", "Return this layer to its saved values.")).c_str()))
			RevertLayer();
	}

	Util::ToolbarDivider();
	if (Util::IconButton("##copy", SceneActionIcons::kCopy, Action(T("feature.skin.copy_layer", "Copy and share"), T("feature.skin.copy_layer_tooltip", "Copy values from another file, or share this character's file.")).c_str()))
		ImGui::OpenPopup("##skin_copy");

	Util::ToolbarDivider();
	const auto* changes = LayerChanges(editor.layer);
	const bool empty = !changes || changes->Empty();
	{
		Util::DisableGuard disabled(empty);
		if (Util::IconButton("##clear", SceneActionIcons::kDelete, Action(T("feature.skin.clear_layer", "Clear layer"), T("feature.skin.clear_layer_tooltip", "Remove every value this layer sets. Save to make it permanent.")).c_str(),
				false, Util::DestructiveIconColor()))
			editor.clearRequested = true;
	}
	ImGui::SameLine();
	if (Util::IconButton("##wetness", Icons::TI(ICON_TI_DROPLET), Action(T("feature.skin.preview_override_wetness", "Test a specific wetness level"), T("feature.skin.preview_wetness_tooltip", "Previews the drafts at a fixed wetness while the editor is open.")).c_str(),
			editor.previewWetness >= 0))
		ImGui::OpenPopup("##skin_wetness");

	if (ImGui::BeginPopup("##skin_wetness")) {
		bool fixed = editor.previewWetness >= 0;
		if (ImGui::Checkbox(T("feature.skin.preview_override_wetness", "Test a specific wetness level"), &fixed))
			editor.previewWetness = fixed ? 0.5f : -1.0f;
		{
			Util::DisableGuard disabled(!fixed);
			float wetness = std::max(editor.previewWetness, 0.0f);
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
			if (ImGui::SliderFloat(T("feature.skin.preview_wetness", "Preview wetness"), &wetness, 0, 1) && fixed)
				editor.previewWetness = wetness;
		}
		if (!fixed)
			Util::Text::Disabled("%s", T("feature.skin.preview_live_wetness", "Using the character's current wetness."));
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("##skin_copy")) {
		const SKSE::stl::scope_exit endPopup([]() noexcept { ImGui::EndPopup(); });
		if (editor.layer == Layer::Material) {
			if (const auto chosen = ResourcePicker(editor.browser, T("feature.skin.import_material_file", "Copy values from another material file..."), editor.key, SidecarExtension)) {
				const auto bytes = FindResource(*chosen);
				const auto definition = bytes ? Parse(*bytes) : Definition{};
				if (definition.status != Status::Valid)
					throw std::runtime_error(definition.diagnostic.empty() ? T("feature.skin.game_resource_is_unavailable", "Game resource is unavailable: ") + *chosen : definition.diagnostic);
				// Painted inputs belong to the other texture's UV layout, so only the numeric values are copied.
				PushUndo({ editor.material, editor.character });
				for (size_t i = 0; i < ParameterCount; ++i)
					if (definition.material.specified[i])
						editor.material.parameters[i] = definition.material.parameters.values[i];
				Invalidate();
				Notify(T("feature.skin.values_copied", "Values copied into the draft. Painted textures are not copied: they only fit their own texture."));
				ImGui::CloseCurrentPopup();
			}
		} else {
			if (ImGui::MenuItem(T("feature.skin.import_character_file", "Load from another character file..."))) {
				std::error_code ignored;
				std::filesystem::create_directories(SkinCharacters::Directory(), ignored);
				if (const auto path = ChooseFile(false, CharacterFilter, L"json", std::filesystem::absolute(SkinCharacters::Directory()) / "")) {
					auto imported = SkinCharacters::Import(*path);
					PushUndo({ editor.material, editor.character });
					imported.name = editor.character.name.empty() ? editor.actorName : editor.character.name;
					editor.character = std::move(imported);
					Invalidate(true);
					Notify(T("feature.skin.character_file_loaded_short", "Loaded into the draft. Save to keep it."));
				}
			}
			if (ImGui::MenuItem(T("feature.skin.export_character_file", "Save a copy..."))) {
				if (const auto path = ChooseFile(true, CharacterFilter, L"json", std::filesystem::path(editor.actorName.empty() ? "Skin character" : editor.actorName).concat(".json"))) {
					SkinCharacters::Export(*path, editor.draftTarget, editor.character);
					Notify(T("feature.skin.character_copy_saved", "Copy saved. Share any custom DDS files it refers to alongside it."));
				}
			}
			if (ImGui::MenuItem(T("feature.skin.open_character_folder_v2", "Open folder")))
				OpenFolder(SkinCharacters::Directory());
		}
	}
}

void Skin::DrawLayerNote()
{
	const auto& style = ImGui::GetStyle();
	if (editor.layer == Layer::Material) {
		const auto root = OutputRoot();
		ImGui::AlignTextToFramePadding();
		Util::Text::Secondary("%s", T("feature.skin.saves_to", "Saves to"));
		ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
		BrowserUI::MetaText(UTF8(root / std::filesystem::u8path(SidecarPath(editor.key))).c_str(), ImGui::GetContentRegionAvail().x * 0.6f);
		ImGui::SameLine();
		if (BrowserUI::Link("##output_folder", T("feature.skin.change_output_folder", "Change folder")))
			if (const auto folder = ChooseFolder())
				settings.MaterialOutputFolder = UTF8(ModRoot(*folder));
		Util::AddTooltip(T("feature.skin.output_folder_help", "Paths inside materials are always game resource paths, so they work from any mod and from archives. Only the mod folder that new files are written into is a location on disk."));
		if (!settings.MaterialOutputFolder.empty()) {
			ImGui::SameLine();
			if (BrowserUI::Link("##data_folder", T("feature.skin.save_material_to_data", "Use the game's Data folder")))
				settings.MaterialOutputFolder.clear();
		}
		ImGui::SameLine();
		ImGui::Checkbox(T("feature.skin.record_fingerprint_short", "Record diffuse version"), &editor.recordFingerprint);
		Util::AddTooltip(T("feature.skin.record_fingerprint", "Record which version of the diffuse texture this was made for"));
		return;
	}
	const auto* saved = SkinCharacters::Find(*SkinCharacters::Get(), editor.draftTarget.key);
	ImGui::AlignTextToFramePadding();
	Util::Text::Secondary("%s", T("feature.skin.saves_to", "Saves to"));
	ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
	BrowserUI::MetaText(saved ? UTF8(saved->file).c_str() : UTF8(SkinCharacters::Directory()).c_str(), ImGui::GetContentRegionAvail().x * 0.7f);
	ImGui::SameLine();
	if (BrowserUI::Link("##character_folder", T("feature.skin.open_character_folder_v2", "Open folder")))
		OpenFolder(SkinCharacters::Directory());
	Util::AddTooltip(T("feature.skin.character_scope_v2", "Adjustments are saved as a file for this character. They apply in every save, to every actor using this record, and the file can be edited by hand or shared. A change of race or skin texture simply switches to that texture's material."));
	if (editor.layer == Layer::CharacterMaterial && std::none_of(editor.surfaces.begin(), editor.surfaces.end(), [this](const auto& surface) { return surface.key == editor.key; }))
		Util::Text::WrappedWarning("%s", T("feature.skin.character_texture_not_shown_v2", "This character does not show this texture now. These values apply again when it does."));
}
