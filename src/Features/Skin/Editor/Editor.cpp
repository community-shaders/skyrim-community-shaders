#include "Features/Skin.h"

#include "CSEditor/Browser/BrowserWidgets.h"
#include "Utils/UI.h"

#include <imgui_stdlib.h>

// Floating texture tools window and the editor's confirmation dialogs.

using namespace SkinMaterials;
using namespace SkinEditor;

namespace
{
	struct ScopeID
	{
		explicit ScopeID(int a_id) { ImGui::PushID(a_id); }
		~ScopeID() { ImGui::PopID(); }
	};
	void SourcePicker(const char* a_label, std::string& a_path)
	{
		ImGui::PushID(a_label);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(a_label);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 7.0f));
		ImGui::InputTextWithHint("##path", T("feature.skin.constant_not_selected", "Constant / not selected"), &a_path);
		ImGui::SameLine();
		// Source images are working files, not game resources, so they come from anywhere on disk.
		if (ImGui::Button(T("feature.skin.choose_image", "Choose image")))
			if (const auto path = ChooseFile(false, ImageFilter, nullptr))
				a_path = UTF8(*path);
		ImGui::PopID();
	}
}

void Skin::DrawTextureTools()
{
	if (!editor.toolsWindow)
		return;
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34, ImGui::GetFontSize() * 30), ImGuiCond_FirstUseEver);
	const bool visible = Util::BeginWithRoundedClose(std::format("{}###SkinTextureTools", T("feature.skin.workspace_textures", "Texture tools")).c_str(), &editor.toolsWindow);
	if (!visible) {
		ImGui::End();
		return;
	}
	try {
		Util::Explainer(T("feature.skin.texture_tools_explainer_label", "How it works"),
			T("feature.skin.texture_tool_flow_v2", "Source images can be any images on disk. The result is written as a game resource into the output mod folder, with linear channels and mipmaps, and can be assigned to the texture being edited."));
		BrowserUI::SectionLabel(T("feature.skin.tools_type", "Type"));
		int type = editor.tools.detail ? 1 : 0;
		const char* types[] = { T("feature.skin.skin_controls", "Skin controls"), T("feature.skin.detail_normal_mask", "Detail normal + mask") };
		if (Util::SegmentedControl("##tools_type", types, 2, type)) {
			editor.tools.detail = type == 1;
			editor.toolsOutput.clear();
		}

		ImGui::Spacing();
		BrowserUI::SectionLabel(T("feature.skin.tools_sources", "Sources"));
		if (editor.tools.detail) {
			SourcePicker(T("feature.skin.rgb_tangent_space_normal", "RGB tangent-space normal"), editor.tools.normal);
			SourcePicker(T("feature.skin.main_uv_region_mask_white_by_default", "Main-UV region mask (white by default)"), editor.tools.mask);
			Util::Text::WrappedSecondary("%s", T("feature.skin.rgb_repeats_with_detail_tiling_the_mask_uses_the_model", "RGB repeats with detail tiling. The mask uses the model's main UV. Empty mask writes white; old detail AO is discarded."));
		} else {
			const char* labels[] = { T("feature.skin.r_roughness", "R: roughness"), T("feature.skin.g_fuzz_multiplier", "G: fuzz multiplier"), T("feature.skin.b_ambient_occlusion", "B: ambient occlusion"), T("feature.skin.a_specular", "A: specular") };
			for (size_t i = 0; i < 4; ++i) {
				ScopeID scopedID(static_cast<int>(i));
				SourcePicker(labels[i], editor.tools.channels[i]);
				if (editor.tools.channels[i].empty()) {
					ImGui::Indent();
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
					ImGui::SliderFloat(T("feature.skin.constant", "Constant"), &editor.tools.defaults[i], 0, 1);
					ImGui::Unindent();
				}
			}
			Util::Text::WrappedSecondary("%s", T("feature.skin.use_grayscale_images_of_equal_dimensions_the_red_channel_of", "Use grayscale images of equal dimensions. The red channel of each input is copied into the selected output channel."));
		}

		ImGui::Spacing();
		BrowserUI::SectionLabel(T("feature.skin.texture_output_section", "Output"));
		const auto& anchor = editor.key;
		if (editor.toolsOutput.empty() && anchor.size() > 4)
			editor.toolsOutput = anchor.substr(0, anchor.size() - 4) + (editor.tools.detail ? "_detail.dds" : "_rfaos.dds");
		ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - Util::IconButtonSize() - ImGui::GetStyle().ItemSpacing.x));
		ImGui::InputTextWithHint("##tools_output", "textures/myskin/body_rfaos.dds", &editor.toolsOutput);
		Util::AddTooltip(T("feature.skin.texture_output_help", "Source images can come from anywhere. The result is a game resource: choose its path inside textures/. It is written below the same mod folder as material files."));
		ImGui::SameLine();
		if (const auto chosen = ResourcePicker(editor.browser, "...", editor.toolsOutput.empty() ? anchor : editor.toolsOutput, ".dds"))
			editor.toolsOutput = *chosen;
		Util::AddTooltip(T("feature.skin.choose_existing_dds", "Replace an existing DDS..."));
		std::string resource;
		try {
			resource = NormalizeTexturePath(editor.toolsOutput);
		} catch (const std::exception& e) {
			Util::Text::WrappedError("%s", e.what());
		}
		const auto root = OutputRoot();
		if (!resource.empty())
			BrowserUI::MetaText(UTF8(root / std::filesystem::u8path(resource)).c_str());
		ImGui::Checkbox(T("feature.skin.compress_as_bc7_slower", "Compress as BC7 (slower)"), &editor.tools.compress);

		ImGui::Spacing();
		ImGui::BeginDisabled(resource.empty());
		if (ImGui::Button(T("feature.skin.build_and_save_dds", "Build and save DDS"), ImVec2(-FLT_MIN, 0))) {
			PublishResource(root, resource, editor.tools.detail ? PackDetail(editor.tools) : PackControls(editor.tools));
			editor.generatedDDS = resource;
			editor.generatedDetail = editor.tools.detail;
			Invalidate(true);
			Notify(T("feature.skin.dds_saved_as_resource", "DDS saved with linear channels, alpha and mipmaps."));
		}
		ImGui::EndDisabled();

		if (!editor.generatedDDS.empty()) {
			ImGui::Spacing();
			BrowserUI::SectionLabel(T("feature.skin.texture_output", "Created texture"));
			BrowserUI::MetaText(editor.generatedDDS.c_str());
			const bool usable = editor.layer != Layer::CharacterAll && LayerAvailable(editor.layer);
			ImGui::BeginDisabled(!usable);
			const auto assign = I18n::GetSingleton()->Format("feature.skin.assign_to_layer", { { "layer", LayerLabel(editor.layer) } }, "Use in {layer}");
			if (ImGui::Button(assign.c_str())) {
				PushUndo({ editor.material, editor.character });
				LayerChanges(editor.layer)->textures[editor.generatedDetail ? 2 : 0] = { TextureMode::Resource, editor.generatedDDS };
				editor.section = 0;
				Invalidate(true);
				Notify(T("feature.skin.texture_assigned_to_draft", "Texture assigned to the draft. Preview and save the material to finish."));
			}
			ImGui::EndDisabled();
			if (!usable)
				Util::AddTooltip(T("feature.skin.assign_unavailable", "Select a skin texture and a layer that holds textures."), Util::kTooltipWhenDisabled);
		}
	} catch (const std::exception& error) {
		Notify(error.what(), true);
	}
	ImGui::End();
}

void Skin::DrawEditorDialogs()
{
	static Util::ConfirmationPopup discard;
	static Util::ConfirmationPopup clear;
	if (editor.pendingAction && !discard.IsOpen()) {
		discard.title = T("feature.skin.unsaved_title", "Unsaved changes");
		discard.message = T("feature.skin.unsaved_draft", "This editor has unsaved changes. Save or apply them first, or discard them to continue.");
		discard.confirmLabel = T("feature.skin.discard_and_continue", "Discard and continue");
		discard.cancelLabel = T("feature.skin.keep_editing", "Keep editing");
		discard.Request();
	}
	if (discard.Draw()) {
		auto action = std::move(editor.pendingAction);
		editor.pendingAction = {};
		editor.discardConfirmed = true;
		struct Reset
		{
			bool& flag;
			~Reset() { flag = false; }
		} reset{ editor.discardConfirmed };
		if (action)
			action();
	} else if (!discard.IsOpen()) {
		editor.pendingAction = {};
	}

	if (editor.clearRequested && !clear.IsOpen()) {
		clear.title = T("feature.skin.clear_layer", "Clear layer");
		clear.message = I18n::GetSingleton()->Format("feature.skin.clear_layer_message", { { "layer", LayerLabel(editor.layer) } },
			"Remove every value {layer} sets for this texture? Nothing is written until you save.");
		clear.confirmLabel = T("feature.skin.clear_layer", "Clear layer");
		clear.cancelLabel = T("feature.skin.cancel", "Cancel");
		clear.Request();
		editor.clearRequested = false;
	}
	if (clear.Draw())
		ClearLayer();
}
