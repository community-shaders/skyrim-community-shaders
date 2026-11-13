#include "Features/Skin.h"

#include "CSEditor/Browser/BrowserWidgets.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Utils/UI.h"

#include <imgui_stdlib.h>

// Parameter and texture rows. Each row leads with a gutter, as in the Scene Manager: ticked when the edited
// layer sets the value, tinted green when its value is the one shown, and amber when a higher layer of the
// selected character wins over it.

using namespace SkinMaterials;
using namespace SkinEditor;

namespace
{
	int ParameterSection(Parameter parameter)
	{
		switch (parameter) {
		case Parameter::Fuzz:
		case Parameter::FuzzRoughness:
		case Parameter::FuzzF0:
		case Parameter::ExtraEdgeRoughness:
			return 2;
		case Parameter::SSSAmount:
		case Parameter::Transmission:
		case Parameter::TransmissionDepth:
		case Parameter::TransmissionEnabled:
			return 3;
		case Parameter::DetailEnabled:
		case Parameter::DetailStrength:
		case Parameter::DetailTiling:
		case Parameter::BodyTilingMultiplier:
			return 4;
		case Parameter::WetResponse:
			return 5;
		default:
			return 1;
		}
	}
	constexpr ImGuiTableFlags kRowsFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;
}

void Skin::DrawMaterialControls()
{
	const Editor::Draft before{ editor.material, editor.character };
	const auto layer = editor.layer;
	if (!LayerAvailable(layer))
		editor.layer = Layer::Material;
	const auto inherited = InheritedMaterial(editor.layer);
	auto* changes = LayerChanges(editor.layer);
	if (!changes)
		return;

	const char* sections[] = { T("feature.skin.section_textures", "Textures"), T("feature.skin.section_specular", "Dual specular lobes"), T("feature.skin.section_fuzz", "Fuzz and edges"), T("feature.skin.section_transmission", "Transmission and SSS"), T("feature.skin.section_detail", "Detail normals"), T("feature.skin.section_wetness", "Wet response") };
	Util::SegmentedControl("##skin_sections", sections, 6, editor.section);
	ImGui::Spacing();
	const auto effective = Apply(inherited, *changes);
	switch (editor.section) {
	case 1:
		Util::Text::WrappedSecondary("%s", effective.textures[0].empty() ? T("feature.skin.specular_scalar_mode", "No RFAOS: the two lobes use their own roughness values and the native specular-map control. RFAOS multipliers are retained for use when a controls texture is assigned.") : T("feature.skin.specular_texture_mode", "RFAOS assigned: its red channel is multiplied separately for each lobe. Absolute roughness values are retained for use without RFAOS."));
		break;
	case 3:
		Util::Text::WrappedSecondary("%s", T("feature.skin.transmission_sss_help", "Transmission controls backlighting through the native thickness texture. SSS strength controls the separate post-process effect."));
		break;
	case 4:
		Util::Text::WrappedSecondary("%s", T("feature.skin.detail_section_help", "Detail RGB repeats; the alpha strength mask stays in the main UV layout. Empty texture slots use the common detail texture. Unchecked parameters follow common settings."));
		break;
	case 5:
		Util::Text::WrappedSecondary("%s", T("feature.skin.wet_section_help", "Set how strongly this skin responds to wetness. Overall wetness, sweat and noise controls remain in the Advanced Skin feature settings."));
		break;
	default:
		break;
	}
	if (editor.section == 0)
		DrawTextureInputs(*changes, inherited);
	else
		DrawParameters(*changes, inherited);

	if (editor.layer == Layer::CharacterMaterial && changes->Empty())
		editor.character.materials.erase(editor.key);
	if (before != Editor::Draft{ editor.material, editor.character }) {
		// A slider drag is one undo step, not one per frame.
		if (!editor.editGesture)
			PushUndo(before);
		editor.editGesture = ImGui::IsAnyItemActive();
		Invalidate();
	} else if (!ImGui::IsAnyItemActive()) {
		editor.editGesture = false;
	}
}

namespace
{
	std::string ProvenanceTooltip(const Skin::Provenance& a_provenance, const char* a_layerName, const char* a_winnerName, const char* a_fromName)
	{
		std::string text;
		if (a_provenance.own)
			text = I18n::GetSingleton()->Format("feature.skin.value_set_here", { { "layer", a_layerName } }, "Set on {layer}. Untick to inherit it again.");
		else if (a_fromName)
			text = I18n::GetSingleton()->Format("feature.skin.value_inherited", { { "layer", a_fromName } }, "Inherited from {layer}. Change it to set it here.");
		else
			text = T("feature.skin.value_common", "Follows the common settings. Change it to set it here.");
		if (a_winnerName)
			text += "\n\n" + I18n::GetSingleton()->Format("feature.skin.value_overridden", { { "layer", a_winnerName } }, "{layer} sets this too, so its value is what the character shows.");
		return text;
	}
	struct Tint
	{
		std::optional<ImVec4> color;
		explicit Tint(std::optional<ImVec4> a_color) :
			color(a_color)
		{
			if (color)
				Util::PushTintedFrameStyle(*color);
		}
		~Tint()
		{
			if (color)
				Util::PopTintedFrameStyle();
		}
	};
	std::optional<ImVec4> TintOf(const Skin::Provenance& a_provenance)
	{
		if (a_provenance.overriddenBy && a_provenance.own)
			return Util::Colors::GetWarning();
		if (a_provenance.own)
			return Util::Colors::GetSuccess();
		return std::nullopt;
	}
}

void Skin::DrawParameters(SkinMaterials::Changes& a_changes, const SkinMaterials::Material& a_inherited)
{
	const auto fields = ParameterTable();
	float labelWidth = 0;
	for (const auto parameter : ParameterOrder)
		if (ParameterSection(parameter) == editor.section)
			labelWidth = std::max(labelWidth, ImGui::CalcTextSize(ParameterLabel(static_cast<size_t>(parameter))).x);
	BrowserUI::RowShadeScope shade;
	if (!ImGui::BeginTable("##skin_parameters", 2, kRowsFlags))
		return;
	ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth + ImGui::GetStyle().CellPadding.x * 2.0f);
	ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
	for (const auto parameter : ParameterOrder) {
		if (ParameterSection(parameter) != editor.section)
			continue;
		const auto i = static_cast<size_t>(parameter);
		const auto provenance = ParameterProvenance(i, editor.layer);
		ImGui::PushID(static_cast<int>(i));
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		if (provenance.own)
			ImGui::TextUnformatted(ParameterLabel(i));
		else
			Util::Text::Secondary("%s", ParameterLabel(i));
		const auto tooltip = ProvenanceTooltip(provenance, LayerLabel(editor.layer), provenance.overriddenBy ? LayerLabel(*provenance.overriddenBy) : nullptr,
			provenance.inheritedFrom ? LayerLabel(*provenance.inheritedFrom) : nullptr);
		Util::AddTooltip(tooltip.c_str());

		ImGui::TableNextColumn();
		const auto tint = TintOf(provenance);
		bool own = provenance.own;
		{
			Tint scope(tint);
			if (ImGui::Checkbox("##own", &own))
				a_changes.parameters[i] = own ? std::optional(a_inherited.parameters.values[i]) : std::nullopt;
		}
		Util::AddTooltip(tooltip.c_str());
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		float value = a_changes.parameters[i].value_or(a_inherited.parameters.values[i]);
		bool changed;
		{
			Tint scope(tint);
			if (fields[i].integer) {
				bool flag = value != 0;
				changed = ImGui::Checkbox("##value", &flag);
				value = flag ? 1.0f : 0.0f;
			} else {
				ImGui::SetNextItemWidth(-FLT_MIN);
				changed = ImGui::SliderFloat("##value", &value, fields[i].minimum, fields[i].maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			}
		}
		// Editing a value sets it on this layer, like any control in the Scene Manager.
		if (changed)
			a_changes.parameters[i] = value;
		if (ImGui::BeginPopupContextItem("##menu")) {
			if (ImGui::MenuItem(T("feature.skin.remove_value", "Inherit again"), nullptr, false, provenance.own))
				a_changes.parameters[i] = std::nullopt;
			const auto reset = I18n::GetSingleton()->Format("feature.skin.factory_value", { { "value", std::format("{:.3f}", fields[i].initial) } }, "Set to factory value ({value})");
			if (ImGui::MenuItem(reset.c_str()))
				a_changes.parameters[i] = fields[i].initial;
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::EndTable();
}

void Skin::DrawTextureInputs(SkinMaterials::Changes& a_changes, const SkinMaterials::Material& a_inherited)
{
	if (editor.layer == Layer::CharacterAll) {
		BrowserUI::EmptyState(T("feature.skin.character_all_no_textures_title", "No textures on this layer"),
			T("feature.skin.character_all_no_textures_v2", "Textures are painted for one texture's UV layout, so they cannot apply to every skin of a character. Set them on the layer below."));
		if (LayerAvailable(Layer::CharacterMaterial)) {
			const float width = ImGui::CalcTextSize(LayerLabel(Layer::CharacterMaterial)).x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
			if (ImGui::Button(LayerLabel(Layer::CharacterMaterial)))
				editor.layer = Layer::CharacterMaterial;
		}
		return;
	}
	const bool material = editor.layer == Layer::Material;
	const auto labels = TextureLabels();
	float labelWidth = 0;
	for (const auto* label : labels)
		labelWidth = std::max(labelWidth, ImGui::CalcTextSize(label).x);
	const char* help[] = { T("feature.skin.controls_help", "R roughness, G fuzz, B ambient occlusion, A specular. Without it, each lobe uses its own roughness value."),
		T("feature.skin.wet_help", "RGB tangent normal of the wet layer, A where wetness acts. Without it, wetness is flat and covers everything."),
		T("feature.skin.detail_help", "RGB tileable normal, A region mask in the main UV. Without it, the common detail texture is used.") };
	{
		BrowserUI::RowShadeScope shade;
		if (ImGui::BeginTable("##skin_textures", 2, kRowsFlags)) {
			ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth + ImGui::GetStyle().CellPadding.x * 2.0f);
			ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
			for (size_t i = 0; i < TextureCount; ++i) {
				ImGui::PushID(static_cast<int>(i));
				const auto provenance = TextureProvenance(i, editor.layer);
				auto& edit = a_changes.textures[i];
				const auto path = Apply(a_inherited, a_changes).textures[i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				if (provenance.own)
					ImGui::TextUnformatted(labels[i]);
				else
					Util::Text::Secondary("%s", labels[i]);
				const auto tooltip = std::string(help[i]) + "\n\n" + ProvenanceTooltip(provenance, LayerLabel(editor.layer), provenance.overriddenBy ? LayerLabel(*provenance.overriddenBy) : nullptr, provenance.inheritedFrom ? LayerLabel(*provenance.inheritedFrom) : nullptr);
				Util::AddTooltip(tooltip.c_str());

				ImGui::TableNextColumn();
				const auto tint = TintOf(provenance);
				bool own = provenance.own;
				{
					Tint scope(tint);
					if (ImGui::Checkbox("##own", &own))
						edit = own ? (path.empty() ? TextureEdit{ TextureMode::Default, {} } : TextureEdit{ TextureMode::Resource, path }) : TextureEdit{};
				}
				Util::AddTooltip(tooltip.c_str());
				ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
				// Archived files are not listed by the browser, so the path can always be typed.
				std::string input = path;
				const float buttons = Util::IconButtonSize() * 3.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
				ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - buttons));
				const char* none = i == 2 ? T("feature.skin.global_detail", "Global detail") : T("feature.skin.no_texture_short", "None");
				{
					Tint scope(tint);
					if (ImGui::InputTextWithHint("##path", none, &input, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
						const auto normalized = NormalizeTexturePath(input);
						edit = normalized.empty() ? (material ? TextureEdit{} : TextureEdit{ TextureMode::Default, {} }) : TextureEdit{ TextureMode::Resource, normalized };
						Invalidate(true);
					}
				}
				Util::AddTooltip(path.empty() ? none : path.c_str());
				ImGui::SameLine();
				if (Util::IconButton("##pick", Icons::TI(ICON_TI_FOLDER_OPEN), T("feature.skin.choose_dds_tooltip", "Choose a DDS from the game's Data folder")))
					OpenResourceBrowser(editor.browser, path.empty() ? editor.key : path);
				if (const auto chosen = ResourceBrowserPopup(editor.browser, ".dds")) {
					edit = { TextureMode::Resource, NormalizeTexturePath(*chosen) };
					Invalidate(true);
				}
				ImGui::SameLine();
				if (Util::IconButton("##none", Icons::TI(ICON_TI_X), i == 2 ? T("feature.skin.use_global_detail", "Use global detail") : T("feature.skin.use_neutral", "Use neutral")))
					edit = material ? TextureEdit{} : TextureEdit{ TextureMode::Default, {} };
				ImGui::SameLine();
				if (i != 1 && Util::IconButton("##build", Icons::TI(ICON_TI_TOOLS), T("feature.skin.open_texture_tools", "Build a packed texture..."))) {
					editor.tools.detail = i == 2;
					editor.toolsOutput.clear();
					editor.toolsWindow = true;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}
	ImGui::Spacing();
	DrawTextureChannels(Apply(a_inherited, a_changes));
}

void Skin::DrawTextureChannels(const SkinMaterials::Material& a_material)
{
	if (!ImGui::CollapsingHeader(T("feature.skin.texture_channels_and_dependencies", "Texture channels and dependencies")))
		return;
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
	ImGui::Combo(T("feature.skin.texture", "Texture"), &editor.texturePreview, TextureLabels().data(), static_cast<int>(TextureLabels().size()));
	const char* channels[] = { "RGB", "R", "G", "B", "A" };
	ImGui::SameLine();
	Util::SegmentedControl("##skin_channel", channels, 5, editor.channel);
	auto path = a_material.textures[editor.texturePreview];
	if (editor.texturePreview == 2 && path.empty())
		path = settings.DetailTexture;
	const auto key = path + std::to_string(editor.channel);
	if (editor.thumbnailKey != key) {
		editor.thumbnail = {};
		editor.thumbnailKey = key;
		if (!path.empty())
			editor.thumbnail = ChannelPreview(path, editor.channel);
	}
	if (editor.thumbnail.view) {
		const float size = std::min(256.0f * Util::GetUIScale(), ImGui::GetContentRegionAvail().x);
		ImGui::Image(reinterpret_cast<ImTextureID>(editor.thumbnail.view.get()), ImVec2(size, size));
	} else {
		Util::Text::Disabled("%s", T("feature.skin.no_per_material_dds_dependency", "No per-material DDS dependency"));
	}
	if (ImGui::Button(T("feature.skin.check_dependencies", "Check dependencies"))) {
		CheckTextures(a_material);
		Notify(T("feature.skin.all_explicit_skin_dds_inputs_are_readable_single_2d_textures", "All explicit Skin DDS inputs are readable single 2D textures. Package them using the displayed paths."));
	}
}
