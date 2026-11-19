#include "PostProcessing.h"

#include "CSEditor/EditorWindow.h"
#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "Features/CSEditor.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "imgui_stdlib.h"

#include "Menu.h"
#include "Menu/Fonts.h"
#include "Menu/MenuIcons.h"
#include "Menu/PresetsPageRenderer.h"
#include "PostProcessingMode.h"
#include "Profiler.h"
#include "State.h"
#include "Util.h"

#include "PostProcessing/RasterPass.h"

#include "Features/HDRDisplay.h"
#include "Features/LinearLighting.h"
#include "Features/Upscaling.h"
#include "Utils/ColorSpace.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>

namespace
{
	constexpr std::array kPointerButtons{ ImGuiMouseButton_Left, ImGuiMouseButton_Right, ImGuiMouseButton_Middle };
	constexpr std::array kActivateKeys{ ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_GamepadFaceDown };

	/** @brief Continues the current line and moves the cursor so an item of @p width ends at the content's right edge. */
	void RightAlignOnLine(float width)
	{
		ImGui::SameLine();
		const float target = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
		if (target > ImGui::GetCursorPosX())
			ImGui::SetCursorPosX(target);
	}

	/** @brief Whether this frame's input could activate a control, so idle frames skip the settings diff. */
	bool InputMayEditSettings()
	{
		return ImGui::IsAnyItemActive() ||
		       std::ranges::any_of(kPointerButtons, [](auto button) { return ImGui::IsMouseClicked(button) || ImGui::IsMouseReleased(button); }) ||
		       std::ranges::any_of(kActivateKeys, [](auto key) { return ImGui::IsKeyPressed(key); });
	}

	bool skipVanillaDoF = false;
	bool skipVanillaBloom = false;
	std::optional<bool> savedVanillaLensFlare;

	struct VanillaDoF_IsActive
	{
		static bool thunk(RE::ImageSpaceEffectDepthOfField* effect)
		{
			return (!skipVanillaDoF || globals::state->permutationData.RenderToUI) && func(effect);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct VanillaBlur_Render
	{
		static void thunk(RE::ImageSpaceEffect* effect, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
		{
			if (skipVanillaBloom && !globals::state->permutationData.RenderToUI &&
				effect->effectTextures.capacity() > 0 && effect->effectTextures[0] &&
				effect->effectTextures[0]->renderTarget == RE::RENDER_TARGETS::kHDR_BLOOM)
				return;
			func(effect, shape, param);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace
{
	// Which pipeline page the settings UI shows: 0 the effect list, 1 one effect's settings, 2 Cinematic Camera.
	int pipelinePageNum = 0;
	int pipelineFeatIdx = 0;
}

PostProcessFeature* PostProcessing::GetOpenEffectWithDebug() const
{
	if (pipelinePageNum != 1 || pipelineFeatIdx < 0 || pipelineFeatIdx >= static_cast<int>(pipeline.size()))
		return nullptr;
	auto* feat = pipeline[pipelineFeatIdx].get();
	return feat && feat->IsActive() && feat->HasDebugSection() ? feat : nullptr;
}

void PostProcessing::DrawSettings()
{
	json before;
	// Unbound widgets (buttons, popup combos) bypass the interceptor, so a diff is their only edit signal.
	const bool watchEdits = !SceneWidgetInterceptor::IsArmed() && InputMayEditSettings();
	if (watchEdits)
		SaveSettings(before);

	// A scene replica of this page edits a scene layer, and inside the CS Editor (its Base Settings window draws this
	// page too) the editor is already open, so its shortcuts are left out there.
	std::array<PostProcessingMode::SelectorAction, 2> pageActions;
	if (const auto* editor = EditorWindow::GetSingleton(); !SceneWidgetInterceptor::IsArmed() && (!editor || !editor->open)) {
		auto& presets = pageActions[0];
		presets.label = T("menu.presets.open_presets", "Presets");
		presets.tooltip = T("feature.post_processing.open_presets_tooltip", "Load and switch Post Processing presets on the Presets page.");
		presets.icon = MenuIcons::kPresets;
		presets.onClick = [] { PresetsPageRenderer::Open(); };

		auto& openEditor = pageActions[1];
		openEditor.label = T("menu.presets.open_cs_editor", "CS Editor");
		openEditor.tooltip = T("menu.presets.open_cs_editor_tooltip", "Open the CS Editor for weather, lighting, and scene editing.");
		openEditor.icon = MenuIcons::kCSEditor;
		openEditor.onClick = [] { CSEditor::OpenEditorWindow(); };
	}
	PostProcessingMode::DrawSelector(pageActions);

	ImGui::Spacing();

	// The pipeline only runs in its own mode, so its controls would do nothing in the others.
	const auto mode = PostProcessingMode::Get();
	const Util::LockedSection modeLock(mode != PostProcessingMode::Mode::PostProcessing,
		mode == PostProcessingMode::Mode::Effects11 ?
			T("common.settings_managed_by_enb", "This setting is managed by Effects 11.") :
			T("feature.post_processing.off_in_vanilla", "Vanilla mode is active. Select Post Processing above to use these settings."));

	const char* settingsTooltip = T("feature.post_processing.edit_settings_for_this_feature", "Edit settings for this feature.");

	// Draws a read-only checkbox for a forced-on sub-feature and returns true. Otherwise callers bind
	// `&feat->enabled` themselves: the scene catalog generator only recognises that form as a scene toggle.
	auto isForcedOn = [this](PostProcessFeature& feature) {
		return feature.IsAutoEnabled() ||
		       (&feature == GetPipelineFeature<HistogramAutoExposure>(FeaturePipelineIndex::AutoExposure) && GetActivePhysicalCameraState());
	};
	auto drawForcedEnabled = [&isForcedOn](const char* label, PostProcessFeature& feature) {
		if (!isForcedOn(feature))
			return false;
		bool active = feature.IsActive();
		ImGui::BeginDisabled();
		ImGui::Checkbox(label, &active);
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(T("feature.post_processing.cinematic_camera.exposure_required", "Exposure processing is required by Cinematic Camera. Disable Cinematic Camera to restore the saved enable state."));
		return true;
	};

	// Sub-page header: the way back, then the page title and its description.
	auto drawSubPageHeader = [&](const char* title, const char* description) {
		if (Util::LabeledButton("##BackToPipeline", Icons::TI(ICON_TI_ARROW_LEFT),
				T("feature.post_processing.back_to_pipeline", "Back to Pipeline")))
			pipelinePageNum = 0;
		ImGui::Spacing();
		{
			MenuFonts::FontRoleGuard heading(Menu::FontRole::Heading);
			ImGui::TextUnformatted(title);
		}
		if (description && description[0])
			Util::SettingHint(description, false);
		ImGui::Separator();
		ImGui::Spacing();
	};

	if (pipelinePageNum == 0) {
		Util::SeparatorTextWithFont(T("feature.post_processing.section_camera", "Camera"), Menu::FontRole::Subheading);
		{
			auto& cam = cinematicCamera;
			ImGui::Checkbox(T("feature.post_processing.cinematic_camera.name", "Cinematic Camera"), &cinematicCamera.settings.Enabled);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T("feature.post_processing.cinematic_camera.description",
									  "Controls lens, focus, exposure and FOV. Exposure processing runs automatically while the camera is active; other effects must be enabled separately."));
			RightAlignOnLine(Util::IconButtonSize());
			if (Util::IconButton("##CinematicCameraSettings", Icons::TI(MenuIcons::kSettings), settingsTooltip))
				pipelinePageNum = 2;

			if (const auto* state = cam.GetState()) {
				const auto isoText = state->Exposure == CinematicCamera::ExposureMode::AutoISO ?
				                         std::format("{} {:.0f}-{:.0f}", T("feature.post_processing.cinematic_camera.exposure_auto_iso", "Auto ISO"), state->MinISO, state->MaxISO) :
				                         std::format("ISO {:.0f}", state->ISO);
				char summary[256];
				std::snprintf(summary, sizeof(summary),
					T("feature.post_processing.cinematic_camera.summary_exposure", "%s - %.1f mm - f/%.1f - %.0f deg - %s - FOV %.1f deg - %s"),
					cam.GetFilmbackPresetName(),
					state->FocalLengthMM,
					state->FNumber,
					cam.settings.Exposure.ShutterAngleDeg,
					isoText.c_str(),
					state->HorizontalFOVDeg,
					cam.GetFovStateText());
				Util::SettingHint(summary);
			} else if (cam.settings.Enabled) {
				const auto summary = std::format("{} - {}", cam.GetFilmbackPresetName(), cam.GetFovStateText());
				Util::SettingHint(summary.c_str());
			}
		}

		Util::SeparatorTextWithFont(T("feature.post_processing.section_effects", "Effects"), Menu::FontRole::Subheading);
		Util::SettingHint(T("feature.post_processing.pipeline_order_hint", "Effects run top to bottom. Click an effect's name to edit its settings."), false);
		ImGui::Spacing();

		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		for (int i = 0; i < pipeline.size(); ++i) {
			auto& feat = pipeline[i];
			if (!feat || !feat->IsVisible())
				continue;
			auto displayName = feat->GetDisplayName();
			auto description = feat->GetDesc();
			ImGui::PushID(feat->GetType().c_str());

			const bool automatic = isForcedOn(*feat);
			const bool experimental = feat->IsExperimental();
			if (!drawForcedEnabled("##Enabled", *feat))
				ImGui::Checkbox("##Enabled", &feat->enabled);

			// The name opens the settings too, so the whole row reads as one entry
			ImGui::SameLine();
			const char* autoLabel = T("feature.post_processing.auto_badge", "Auto");
			const char* experimentalLabel = T("feature.post_processing.experimental_badge", "Experimental");
			const float nameWidth = std::max(ImGui::GetContentRegionAvail().x - Util::IconButtonSize() - spacing, ImGui::GetFontSize());
			{
				const bool dim = !feat->IsActive();
				if (dim)
					ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetSecondary());
				if (Util::Selectable(displayName.c_str(), false, ImGuiSelectableFlags_None, ImVec2(nameWidth, 0.0f))) {
					pipelineFeatIdx = i;
					pipelinePageNum = 1;
				}
				if (dim)
					ImGui::PopStyleColor();
			}
			if (!description.empty())
				Util::AddTooltip(description.c_str());
			if (automatic || experimental) {
				const ImVec2 rowMin = ImGui::GetItemRectMin();
				const ImVec2 rowMax = ImGui::GetItemRectMax();
				float rightX = rowMax.x;
				auto drawRowBadge = [&](const char* label, const ImVec4& tint) {
					const float width = Util::MeasureBadgeWidth(label);
					Util::BadgeOptions badge;
					badge.at = ImVec2(rightX - width, rowMin.y);
					badge.rowHeight = rowMax.y - rowMin.y;
					Util::Badge(label, tint, badge);
					rightX -= width + spacing;
				};
				if (automatic)
					drawRowBadge(autoLabel, Util::Colors::GetAccent());
				if (experimental)
					drawRowBadge(experimentalLabel, Util::Colors::GetWarning());
			}

			ImGui::SameLine();
			if (Util::IconButton("##Settings", Icons::TI(MenuIcons::kSettings), settingsTooltip)) {
				pipelineFeatIdx = i;
				pipelinePageNum = 1;
			}
			ImGui::PopID();
		}
	} else if (pipelinePageNum == 1) {
		if (pipelineFeatIdx >= 0 && pipelineFeatIdx < pipeline.size() && pipeline[pipelineFeatIdx]) {
			auto& feat = pipeline[pipelineFeatIdx];
			auto displayName = feat->GetDisplayName();
			auto description = feat->GetDesc();
			ImGui::PushID(feat->GetType().c_str());

			// Recompiling is a troubleshooting action, so it sits quietly at the header's right
			const float headerTop = ImGui::GetCursorPosY();
			drawSubPageHeader(displayName.c_str(), description.c_str());
			const float headerBottom = ImGui::GetCursorPosY();
			ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - Util::IconButtonSize(), headerTop));
			if (Util::IconButton("##RecompileShaders", Icons::TI(ICON_TI_REFRESH),
					T("feature.post_processing.recompile_shaders_for_this_sub_feature_only", "Recompile shaders for this sub-feature only.")))
				feat->ClearShaderCache();
			ImGui::SetCursorPosY(headerBottom);

			if (!drawForcedEnabled(T("feature.post_processing.enabled", "Enabled"), *feat))
				ImGui::Checkbox(T("feature.post_processing.enabled", "Enabled"), &feat->enabled);
			ImGui::Spacing();
			if (feat->IsActive())
				feat->DrawSettings();
			else
				Util::SettingHint(T("feature.post_processing.enable_the_feature_to_see_its_settings", "Enable the feature to see its settings."), false);

			ImGui::PopID();
		} else {
			ImGui::TextDisabled("%s", T("feature.post_processing.invalid_feature_selected_returning_to_list", "Invalid feature selected. Returning to list."));
			pipelinePageNum = 0;
		}
	} else if (pipelinePageNum == 2) {
		drawSubPageHeader(T("feature.post_processing.cinematic_camera.name", "Cinematic Camera"),
			T("feature.post_processing.cinematic_camera.description",
				"Controls lens, focus, exposure and FOV. Exposure processing runs automatically while the camera is active; other effects must be enabled separately."));

		ImGui::Checkbox(T("feature.post_processing.cinematic_camera.name", "Cinematic Camera"), &cinematicCamera.settings.Enabled);
		ImGui::Spacing();
		if (!cinematicCamera.settings.Enabled)
			Util::SettingHint(T("feature.post_processing.enable_the_feature_to_see_its_settings", "Enable the feature to see its settings."), false);

		cinematicCamera.DrawSettings(GetPipelineFeature<PhysicalLens>(FeaturePipelineIndex::PhysicalLens));
		if (cinematicCamera.settings.Enabled) {
			if (auto* exposure = GetPipelineFeature<HistogramAutoExposure>(FeaturePipelineIndex::AutoExposure)) {
				ImGui::SeparatorText(T("feature.post_processing.cinematic_camera.exposure_meter", "Exposure Meter"));
				exposure->DrawCameraExposureReadout();
			}

			// Linked-effect status: which adapters are currently consuming overrides.
			ImGui::SeparatorText(T("feature.post_processing.cinematic_camera.linked_effects", "Linked Effects"));
			if (ImGui::BeginTable("##LinkedEffects", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
				auto drawLinked = [&](FeaturePipelineIndex idx, const char* fallbackName) {
					auto& pipe = pipeline[static_cast<size_t>(idx)];
					if (!pipe)
						return;
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(pipe->GetDisplayName().empty() ? fallbackName : pipe->GetDisplayName().c_str());
					ImGui::TableNextColumn();
					if (pipe->IsActive())
						ImGui::TextColored(Util::Colors::GetSuccess(), "%s", T("feature.post_processing.cinematic_camera.linked_active", "Active"));
					else
						ImGui::TextDisabled("%s", T("feature.post_processing.cinematic_camera.linked_disabled", "Disabled - no override"));
				};
				drawLinked(FeaturePipelineIndex::DoF, "Depth of Field");
				drawLinked(FeaturePipelineIndex::LensFlare, "Lens Flare");
				drawLinked(FeaturePipelineIndex::PhysicalLens, "Physical Lens");
				drawLinked(FeaturePipelineIndex::PhysicalGlare, "Physical Glare");
				drawLinked(FeaturePipelineIndex::Vignette, "Vignette");
				drawLinked(FeaturePipelineIndex::MotionBlur, "Motion Blur");
				drawLinked(FeaturePipelineIndex::AutoExposure, "Histogram Auto Exposure");
				drawLinked(FeaturePipelineIndex::Camera, "Camera");
				ImGui::EndTable();
			}
		}
	}

	if (watchEdits) {
		json after;
		SaveSettings(after);
		if (before != after)
			SceneSettingsManager::GetSingleton()->CaptureExternalFeatureChanges(this, true);
	}
}

void PostProcessing::LoadSettings(json& o_json)
{
	if (!pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)])
		pendingSettings = o_json;
	else
		ProcessSettings(o_json);
}

void PostProcessing::ProcessSettings(json& o_json)
{
	// Scene blends reload every frame, so no info-level log and no per-load SetupResources:
	// settings-dependent resources are reconciled in Draw.
	logger::debug("Loading post processing settings...");

	for (auto& feat : pipeline) {
		if (feat && o_json.contains(feat->GetType())) {
			if (!feat->IsAutoEnabled())
				feat->enabled = o_json.value(feat->GetType(), json::object()).value("enabled", true);
			json featSettings = o_json.value(feat->GetType(), json::object()).value("settings", json::object());
			feat->LoadSettings(featSettings);
		}
	}

	if (o_json.contains("cinematic_camera")) {
		json camJson = o_json["cinematic_camera"];
		cinematicCamera.LoadSettings(camJson);
	} else {
		cinematicCamera.RestoreDefaultSettings();
	}
}

void PostProcessing::SaveSettings(json& o_json)
{
	if (!pendingSettings.empty()) {
		o_json = pendingSettings;
		o_json.erase("ppsettings");
		return;
	}

	// The code defaults are captured before SetupResources, and Baseline files validate against their shape,
	// so a default pipeline stands in until then.
	const bool built = pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)] != nullptr;
	if (!built)
		BuildPipeline();
	const SKSE::stl::scope_exit dropDefaults([&]() noexcept {
		if (!built)
			pipeline = {};
	});

	for (auto& pipe : pipeline) {
		if (pipe) {
			json featureSetting{};
			pipe->SaveSettings(featureSetting);
			o_json[pipe->GetType()] = {
				{ "enabled", pipe->enabled },
				{ "settings", featureSetting }
			};
		}
	}

	json camJson{};
	cinematicCamera.SaveSettings(camJson);
	o_json["cinematic_camera"] = camJson;
	o_json.erase("ppsettings");
}

void PostProcessing::ApplyContextualDefaults(json& a_defaults) const
{
	for (const auto& pipe : pipeline) {
		if (!pipe)
			continue;
		if (auto section = a_defaults.find(pipe->GetType()); section != a_defaults.end() && section->contains("settings"))
			pipe->ApplyContextualDefaults((*section)["settings"]);
	}
}

void PostProcessing::BuildPipeline()
{
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LocalExposure)] = std::make_shared<LocalExposure>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)] = std::make_shared<HistogramAutoExposure>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)] = std::make_shared<ColorGrading>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LUT)] = std::make_shared<LUT>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::MotionBlur)] = std::make_shared<MotionBlur>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::DoF)] = std::make_shared<DoF>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::PhysicalGlare)] = std::make_shared<PhysicalGlare>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::CODBloom)] = std::make_shared<CODBloom>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LensFlare)] = std::make_shared<LensFlare>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::PhysicalLens)] = std::make_shared<PhysicalLens>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Composite)] = std::make_shared<Composite>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Vignette)] = std::make_shared<Vignette>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Camera)] = std::make_shared<Camera>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Border)] = std::make_shared<Border>();
	ApplyDefaultEnabledStates();
}

void PostProcessing::ApplyDefaultEnabledStates()
{
	using enum FeaturePipelineIndex;
	constexpr std::array kOffByDefault = { LUT, MotionBlur, PhysicalGlare, PhysicalLens, Camera, Border };
	for (size_t index = 0; index < pipeline.size(); ++index) {
		if (auto& pipe = pipeline[index]; pipe && !pipe->IsAutoEnabled())
			pipe->enabled = std::ranges::find(kOffByDefault, static_cast<FeaturePipelineIndex>(index)) == kOffByDefault.end();
	}
}

void PostProcessing::RestoreDefaultSettings()
{
	pendingSettings = {};
	cinematicCamera.RestoreDefaultSettings();

	// Before SetupResources there is no pipeline yet; it is built from these same defaults.
	if (!pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)])
		return;

	ApplyDefaultEnabledStates();
	for (auto& pipe : pipeline) {
		if (pipe)
			pipe->RestoreDefaultSettings();
	}
}

void PostProcessing::ClearShaderCache()
{
	for (auto& pipe : pipeline) {
		if (pipe)
			pipe->ClearShaderCache();
	}
}

void PostProcessing::SetupResources()
{
	colorTargets.clear();
	colorTargetRetry.clear();

	auto& renderTargets = globals::game::renderer->GetRuntimeData().renderTargets;
	renderTargets[RE::RENDER_TARGETS::kMAIN].texture->GetDesc(&mainColorDesc);
	renderTargets[RE::RENDER_TARGETS::kMAIN_COPY].texture->GetDesc(&sceneColorDesc);

	copyCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<CopyCB>());

	if (auto rawPtr = reinterpret_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\PostProcessing\\fullscreen.hlsli", {}, "vs_5_0", "FullscreenTriangleVS")))
		fullscreenVS.attach(rawPtr);
	if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\PostProcessing\\copy.ps.hlsl", {}, "ps_5_0")))
		copyPS.attach(rawPtr);
	if (!fullscreenVS || !copyPS)
		stl::report_and_fail("Post Processing requires its input/output shaders."sv);

	BuildPipeline();

	for (auto& pipe : pipeline) {
		if (pipe) {
			pipe->owner = this;
			pipe->SetupResources();
		}
	}

	bokehResources.Setup();

	cinematicCamera.focusResolver.RequestTDM();

	ProcessSettings(pendingSettings);
	pendingSettings = {};
}

void PostProcessing::Reset()
{
	skipVanillaDoF = false;
	skipVanillaBloom = false;

	// Cleared per frame rather than only at the end of PreProcess: when Effects11 owns the
	// tonemap (or the pipeline is bypassed) PreProcess never runs, and a stale flag would
	// make the next frame we do run read from the wrong buffer.
	isrefraction = false;
}

std::optional<PostProcessing::RenderTargetCopy> PostProcessing::PrepareRenderTargetCopy(
	ID3D11Texture2D* target, PostProcessFeature::TextureInfo source, const CopyCB& conversion)
{
	if (!target || !source.tex)
		return std::nullopt;

	const bool convert = conversion.scale != 1.f || conversion.gamma != 1.f || conversion.inputGamut != conversion.outputGamut;
	D3D11_TEXTURE2D_DESC srcDesc;
	source.tex->GetDesc(&srcDesc);

	D3D11_TEXTURE2D_DESC targetDesc;
	target->GetDesc(&targetDesc);

	if (!convert && srcDesc.Format == targetDesc.Format)
		return RenderTargetCopy{ target, source, nullptr, conversion };

	if (!copyPS || !fullscreenVS || !source.srv)
		return std::nullopt;

	auto* scratch = GetColorTarget(source.tex, targetDesc.Width, targetDesc.Height, targetDesc.Format);
	if (!scratch)
		return std::nullopt;

	return RenderTargetCopy{ target, source, scratch, conversion };
}

void PostProcessing::CopyToRenderTarget(const RenderTargetCopy& copy)
{
	auto* context = globals::d3d::context;
	auto* scratch = copy.scratch;
	if (!scratch) {
		if (copy.target != copy.source.tex) {
			globals::profiler->BeginPass("PostProcessing::CopyToTarget");
			context->CopySubresourceRegion(copy.target, 0, 0, 0, 0, copy.source.tex, 0, nullptr);
			globals::profiler->EndPass();
		}
		return;
	}

	globals::profiler->BeginPass("PostProcessing::ConvertToTarget");
	{
		PostProcessingRaster::RasterPass pass(context);

		copyCB->Update(copy.conversion);
		ID3D11Buffer* cb = copyCB->CB();
		context->PSSetConstantBuffers(1, 1, &cb);
		ID3D11ShaderResourceView* srv = copy.source.srv;
		context->PSSetShaderResources(0, 1, &srv);
		pass.SetTargets({ scratch->rtv.get() }, (float)scratch->desc.Width, (float)scratch->desc.Height);
		pass.SetShaders(fullscreenVS.get(), copyPS.get());
		pass.Draw();

		srv = nullptr;
		context->PSSetShaderResources(0, 1, &srv);
		cb = nullptr;
		context->PSSetConstantBuffers(1, 1, &cb);
	}

	context->CopySubresourceRegion(copy.target, 0, 0, 0, 0, scratch->resource.get(), 0, nullptr);
	globals::profiler->EndPass();
}

Texture2D* PostProcessing::GetColorTarget(ID3D11Texture2D* input, DXGI_FORMAT format)
{
	return GetColorTarget(input, sceneColorDesc.Width, sceneColorDesc.Height,
		format == DXGI_FORMAT_UNKNOWN ? sceneColorDesc.Format : format);
}

Texture2D* PostProcessing::GetColorTarget(ID3D11Texture2D* input, UINT width, UINT height, DXGI_FORMAT format)
{
	// Writeback can also target formats that only need an RTV.
	const bool needsShaderViews = format == sceneColorDesc.Format || format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT;
	const UINT bindFlags = D3D11_BIND_RENDER_TARGET | (needsShaderViews ? D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS : 0u);

	// Only the current input is live. Two targets per size and format suffice,
	// even for a pass such as Camera that advances the chain twice.
	for (const auto& target : colorTargets) {
		if (target->resource.get() != input && target->desc.Width == width &&
			target->desc.Height == height && target->desc.Format == format && target->desc.BindFlags == bindFlags)
			return target.get();
	}

	const auto now = std::chrono::steady_clock::now();
	const auto key = std::tuple{ width, height, format, bindFlags };
	if (const auto retry = colorTargetRetry.find(key); retry != colorTargetRetry.end()) {
		if (now < retry->second)
			return nullptr;
		colorTargetRetry.erase(retry);
	}

	try {
		const D3D11_TEXTURE2D_DESC desc{
			.Width = width,
			.Height = height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = format,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = bindFlags
		};
		auto name = std::format("PostProcessing::ColorScratch{}", colorTargets.size());
		auto target = std::make_unique<Texture2D>(desc, name.c_str());
		target->CreateRTV({ .Format = format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D, .Texture2D = { .MipSlice = 0 } });
		if (needsShaderViews) {
			target->CreateSRV({ .Format = format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, .Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
			target->CreateUAV({ .Format = format, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D, .Texture2D = { .MipSlice = 0 } });
		}
		return colorTargets.emplace_back(std::move(target)).get();
	} catch (const std::exception& e) {
		logger::error("[PostProcessing] Could not create a {}x{} color scratch target (format {}, bind flags {}): {}",
			width, height, static_cast<uint>(format), bindFlags, e.what());
		colorTargetRetry.emplace(key, now + std::chrono::seconds(5));
		return nullptr;
	}
}

bool PostProcessing::BeginLinearProcessing(PostProcessFeature::TextureInfo& texture)
{
	auto& ll = globals::features::linearLighting;
	if (ll.IsLinearLightingActive())
		return true;

	auto* texInput = GetColorTarget(texture.tex, mainColorDesc.Width, mainColorDesc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (!texInput)
		return false;

	auto* context = globals::d3d::context;
	globals::profiler->BeginPass("PostProcessing::BeginLinear");
	PostProcessingRaster::RasterPass pass(context);
	copyCB->Update(CopyCB{ .gamma = Util::ColorSpace::GAME_GAMMA });
	ID3D11Buffer* cb = copyCB->CB();
	context->OMSetRenderTargets(0, nullptr, nullptr);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	context->PSSetConstantBuffers(1, 1, &cb);
	context->PSSetShaderResources(0, 1, &texture.srv);
	pass.SetTargets({ texInput->rtv.get() }, (float)texInput->desc.Width, (float)texInput->desc.Height);
	pass.SetShaders(fullscreenVS.get(), copyPS.get());
	pass.Draw();
	ID3D11ShaderResourceView* srv = nullptr;
	context->PSSetShaderResources(0, 1, &srv);
	cb = nullptr;
	context->PSSetConstantBuffers(1, 1, &cb);
	globals::profiler->EndPass();
	texture = { texInput->resource.get(), texInput->srv.get() };
	return true;
}

void PostProcessing::DrawFeature(PostProcessFeature& feature, PostProcessFeature::TextureInfo& lastTexColor)
{
	if (feature.WritesToMainTexture()) {
		feature.Draw(lastTexColor);
	} else {
		PostProcessFeature::TextureInfo inTex = lastTexColor;
		feature.Draw(inTex);
	}
}

void PostProcessing::DrawBeforeUpscaling()
{
	if (!IsPipelineRunning())
		return;

	auto& upscaling = globals::features::upscaling;
	if (!upscaling.loaded)
		return;

	auto renderer = globals::game::renderer;
	auto state = globals::state;

	bool inMainLoadingMenu = state->IsMainOrLoadingMenuOpen();
	auto gameTexMain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	PostProcessFeature::TextureInfo lastTexColor = { gameTexMain.texture, gameTexMain.SRV };
	bool processing = false;

	state->BeginPerfEvent("[Post Processing] Pre-Upscale");

	// update auto-enabled features
	for (auto& pipe : pipeline) {
		if (pipe && pipe->IsAutoEnabled())
			pipe->UpdateAutoEnabled();
	}

	// go through each fx
	for (auto& pipe : pipeline) {
		if (pipe && pipe->IsActive() && !pipe->DrawAfterColorGrading() && !(inMainLoadingMenu && pipe->DisableInMainLoadingMenu()) && pipe->DrawBeforeUpscaling()) {
			if (!processing) {
				globals::d3d::context->OMSetRenderTargets(0, nullptr, nullptr);
				globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
				if (!BeginLinearProcessing(lastTexColor))
					break;
				processing = true;
			}
			DrawFeature(*pipe, lastTexColor);
		}
	}

	if (processing) {
		const CopyCB conversion{ .gamma = globals::features::linearLighting.IsLinearLightingActive() ? 1.f : Util::ColorSpace::GAME_GAMMA_INV };
		if (const auto copy = PrepareRenderTargetCopy(gameTexMain.texture, lastTexColor, conversion))
			CopyToRenderTarget(*copy);
	}

	state->EndPerfEvent();
}

std::optional<RE::RENDER_TARGET> PostProcessing::PreProcess(RE::RENDER_TARGET a_input)
{
	if (globals::features::linearLighting.IsLinearLightingActive() &&
		(globals::state->permutationData.RenderToUI || globals::state->IsMainOrLoadingMenuOpen()))
		return std::nullopt;
	auto& ll = globals::features::linearLighting;
	const bool linear = ll.IsLinearLightingActive();
	if (bypass && !linear)
		return std::nullopt;

	auto renderer = globals::game::renderer;

	auto& upscaling = globals::features::upscaling;

	// This runs before the HDR chain, so ISRefraction still has kMAIN_COPY bound as a render
	// target. D3D11 silently nulls any SRV of a resource that is also an output, which would
	// make the pipeline sample black instead of the scene.
	globals::d3d::context->OMSetRenderTargets(0, nullptr, nullptr);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	bool inMainLoadingMenu = globals::state->IsMainOrLoadingMenuOpen();

	auto& gameTexMainRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& gameTexMainCopyRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

	// The tonemap hook hands us the pass input directly, so no need to probe the bound RTV.
	// Refraction still routes through kMAIN_COPY without that being reflected in a_input.
	const bool useMainCopy = isrefraction || a_input == RE::RENDER_TARGETS::kMAIN_COPY;
	const auto sceneTarget = useMainCopy ? RE::RENDER_TARGETS::kMAIN_COPY : RE::RENDER_TARGETS::kMAIN;
	isrefraction = false;

	auto gameTexMain = useMainCopy ? gameTexMainCopyRT : gameTexMainRT;
	PostProcessFeature::TextureInfo lastTexColor = { gameTexMain.texture, gameTexMain.SRV };
	if (!BeginLinearProcessing(lastTexColor))
		return sceneTarget;
	auto gameTexMainAlt = useMainCopy ? gameTexMainRT : gameTexMainCopyRT;

	// update auto-enabled features
	for (auto& pipe : pipeline) {
		if (pipe && pipe->IsAutoEnabled())
			pipe->UpdateAutoEnabled();
	}

	const auto canDraw = [&](const auto& pipe) {
		return !bypass && pipe && pipe->IsActive() && !(inMainLoadingMenu && pipe->DisableInMainLoadingMenu()) &&
		       (!pipe->DrawBeforeUpscaling() || !upscaling.loaded);
	};
	const auto& grading = pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)];
	const auto& composite = pipeline[static_cast<size_t>(FeaturePipelineIndex::Composite)];
	const auto& border = pipeline[static_cast<size_t>(FeaturePipelineIndex::Border)];

	const bool sceneOutput = globals::state->GetTonemapOwner() != State::TonemapOwner::kPostProcessing;
	const auto sceneGamut = ll.IsACEScgActive() ? Gamut::ACEScg : Gamut::Rec709;
	const auto displayGamut = globals::features::hdrDisplay.loaded && globals::features::hdrDisplay.settings.enableHDR ? Gamut::Rec2020 : Gamut::Rec709;
	CopyCB conversion{
		.inputGamut = sceneGamut,
		.outputGamut = sceneOutput ? sceneGamut : displayGamut,
		.gamma = sceneOutput && !linear ? Util::ColorSpace::GAME_GAMMA_INV : 1.f
	};

	ColorGrading* fusedGrading = nullptr;
	Composite::ConversionTarget fusedConversion;
	if (linear && canDraw(composite)) {
		if (canDraw(grading)) {
			fusedGrading = static_cast<ColorGrading*>(grading.get());
		} else if (gameTexMain.texture && std::ranges::none_of(pipeline, [&](const auto& pipe) {
					   return canDraw(pipe) && pipe->DrawAfterColorGrading();
				   })) {
			// Composite takes a scratch target after the preceding effects have selected
			// its input. Matching the game target lets writeback use a direct copy.
			D3D11_TEXTURE2D_DESC targetDesc;
			gameTexMain.texture->GetDesc(&targetDesc);
			if (targetDesc.Width == sceneColorDesc.Width && targetDesc.Height == sceneColorDesc.Height &&
				targetDesc.SampleDesc.Count == 1 && targetDesc.SampleDesc.Quality == 0) {
				copyCB->Update(conversion);
				fusedConversion = { targetDesc.Format, copyCB->CB() };
			}
		}
	}

	bool colorGraded = false;
	bool borderDrawn = false;
	bool outputConverted = false;
	bool exposureResolved = !linear || globals::state->preExposure == 1.0f;
	for (auto& pipe : pipeline) {
		if (canDraw(pipe) && !pipe->DrawAfterColorGrading()) {
			auto* input = lastTexColor.tex;
			if (pipe == composite) {
				const auto output = static_cast<Composite*>(pipe.get())->Draw(lastTexColor, fusedGrading, fusedConversion);
				exposureResolved |= lastTexColor.tex != input;
				colorGraded = output == Composite::Output::Graded;
				outputConverted = output == Composite::Output::Converted;
			} else if (pipe == grading) {
				if (colorGraded)
					continue;
				// ISHDR can decode the original scene when an enabled tone mapper cannot run.
				if (!exposureResolved)
					return sceneTarget;
				DrawFeature(*pipe, lastTexColor);
				colorGraded = lastTexColor.tex != input;
				if (!colorGraded)
					return sceneTarget;
			} else {
				DrawFeature(*pipe, lastTexColor);
			}
		}
	}

	for (auto& pipe : pipeline) {
		if (exposureResolved && canDraw(pipe) && pipe->DrawAfterColorGrading()) {
			auto* input = lastTexColor.tex;
			DrawFeature(*pipe, lastTexColor);
			if (pipe == border)
				borderDrawn = lastTexColor.tex != input;
		}
	}

	if (outputConverted) {
		conversion = {};
	} else {
		conversion.inputGamut = colorGraded ? displayGamut : sceneGamut;
		conversion.scale = exposureResolved ? 1.f : 1.f / globals::state->preExposure;
	}
	// Both copies must be ready before either game target is modified. The
	// alternate is derived from the committed main target, including its format.
	const auto mainCopy = PrepareRenderTargetCopy(gameTexMain.texture, lastTexColor, conversion);
	if (!mainCopy)
		return sceneTarget;
	const auto alternateCopy = PrepareRenderTargetCopy(gameTexMainAlt.texture, { gameTexMain.texture, gameTexMain.SRV }, CopyCB{});
	if (!alternateCopy)
		return sceneTarget;

	CopyToRenderTarget(*mainCopy);
	CopyToRenderTarget(*alternateCopy);

	if (borderDrawn && upscaling.ShouldUseFrameGenerationThisFrame() && !globals::state->permutationData.RenderToUI)
		static_cast<Border*>(border.get())->ClearMotionVectorsForFrameGen();

	return std::nullopt;
}

bool PostProcessing::WantsTonemapOwnership() const
{
	if (globals::features::linearLighting.IsLinearLightingActive())
		return !globals::state->IsMainOrLoadingMenuOpen();
	return !bypass;
}

bool PostProcessing::IsPipelineRunning() const
{
	return !bypass && globals::state->GetTonemapOwner() == State::TonemapOwner::kPostProcessing;
}

bool PostProcessing::WantsAutoHDR() const
{
	if (globals::state->IsPostProcessingFallback() || globals::state->GetTonemapOwner() != State::TonemapOwner::kPostProcessing)
		return false;

	auto* colorGrading = static_cast<const ColorGrading*>(pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)].get());
	return colorGrading && colorGrading->IsActive() && colorGrading->WantsAutoHDR();
}

void PostProcessing::UpdateVanillaEffects(bool active)
{
	const bool runnable = active && loaded && !bypass && globals::shaderCache->IsEnabled();
	const bool inMainLoadingMenu = globals::state->IsMainOrLoadingMenuOpen();
	const bool linear = globals::features::linearLighting.IsLinearLightingActive();
	const auto replaces = [&](FeaturePipelineIndex idx) {
		const auto& feature = pipeline[static_cast<size_t>(idx)];
		return runnable && feature && feature->IsActive() &&
		       (!inMainLoadingMenu || (!linear && !feature->DisableInMainLoadingMenu()));
	};

	skipVanillaDoF = replaces(FeaturePipelineIndex::DoF);
	skipVanillaBloom = globals::state->enablePShaders && globals::state->ShaderEnabled(RE::BSShader::Type::ImageSpace) &&
	                   (replaces(FeaturePipelineIndex::PhysicalGlare) || replaces(FeaturePipelineIndex::CODBloom));
	if (skipVanillaBloom) {
		using enum RE::ImageSpaceManager::ImageSpaceEffectEnum;
		static const std::array<uint32_t, 2> tonemapShaders{
			RE::ImageSpaceManager::GetCurrentIndex(ISHDRTonemapBlendCinematic),
			RE::ImageSpaceManager::GetCurrentIndex(ISHDRTonemapBlendCinematicFade)
		};
		skipVanillaBloom = globals::shaderCache->blockedKeyIndex == -1 &&
		                   globals::shaderCache->HasPixelShaders(RE::BSShader::Type::ImageSpace, tonemapShaders);
	}

	static auto* lensFlare = RE::GetINISetting("bLensFlare:ImageSpace");
	if (lensFlare) {
		if (replaces(FeaturePipelineIndex::LensFlare)) {
			if (!savedVanillaLensFlare) {
				savedVanillaLensFlare = lensFlare->data.b;
				lensFlare->data.b = false;
			}
		} else if (savedVanillaLensFlare) {
			lensFlare->data.b = *savedVanillaLensFlare;
			savedVanillaLensFlare.reset();
		}
	}
}

void PostProcessing::Prepass()
{
	if (!pendingSettings.empty()) {
		logger::info("Processing pending post processing settings...");
		ProcessSettings(pendingSettings);
		pendingSettings = {};
		UpdateVanillaEffects(globals::state->GetTonemapOwner() == State::TonemapOwner::kPostProcessing);
	}

	if (auto* lut = GetPipelineFeature<::LUT>(FeaturePipelineIndex::LUT))
		lut->UpdateTexture();

	{
		auto graphicsState = globals::game::graphicsState;
		const float aspect = graphicsState && graphicsState->screenHeight > 0 ?
		                         (float)graphicsState->screenWidth / (float)graphicsState->screenHeight :
		                         16.0f / 9.0f;
		const bool runnable = IsPipelineRunning() && !globals::state->IsMainOrLoadingMenuOpen();
		cinematicCamera.Update(runnable, aspect);
	}

	// Update gameISData
	const auto ImageSpace = RE::ImageSpaceManager::GetSingleton();
	const auto& iSRuntimeData = ImageSpace->GetRuntimeData();
	imageSpaceManager->gameISData = iSRuntimeData.data;
	if (const auto& overrideBaseData = iSRuntimeData.overrideBaseData) {
		imageSpaceManager->gameISData.baseData = *overrideBaseData;
	} else {
		imageSpaceManager->gameISData.baseData = *iSRuntimeData.currentBaseData;
	}
}

void PostProcessing::PostPostLoad()
{
	logger::info("Hooking preprocess passes");
	stl::write_vfunc<0x6, VanillaDoF_IsActive>(RE::VTABLE_ImageSpaceEffectDepthOfField[0]);
	stl::write_vfunc<0x1, VanillaBlur_Render>(RE::VTABLE_ImageSpaceEffectBlur[0]);
	stl::write_vfunc<0x2, BSImagespaceShaderRefraction_SetupTechnique>(RE::VTABLE_BSImagespaceShaderRefraction[0]);
}
