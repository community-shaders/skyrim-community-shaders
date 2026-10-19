#include "PresetsPageRenderer.h"
#include "PCH.h"

#include "CSEditor/Browser/BrowserWidgets.h"
#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "Features/CSEditor.h"
#include "Features/Effects11/Editor/Effects11Editor.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Presets/PresetCompatibility.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "Util.h"
#include "Utils/UI.h"

#include <algorithm>
#include <format>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

std::optional<UnifiedPresetCatalog::PresetType> PresetsPageRenderer::typeFilter;
char PresetsPageRenderer::searchBuffer[128] = {};
std::string PresetsPageRenderer::selectedPackId;
bool PresetsPageRenderer::discovered = false;
std::string PresetsPageRenderer::lightboxPackId;
int PresetsPageRenderer::lightboxImageIndex = -1;
bool PresetsPageRenderer::lightboxSuppressClose = false;

namespace
{
	const char* BackendLabelE11(bool compact)
	{
		return compact ? "E11" : "Effects 11";
	}

	const char* BackendLabelCS(bool compact)
	{
		return compact ? "CS" : "CS Presets";
	}

	const char* BackendLabelBaseline(bool)
	{
		return T("menu.presets.filter_baseline", "Baseline");
	}

	float BadgeGap()
	{
		return ImGui::GetStyle().ItemInnerSpacing.x;
	}

	/** @brief Add a badge gap to width unless this is the first badge. */
	void AppendBadgeGap(float& width, bool& needGap)
	{
		if (needGap)
			width += BadgeGap();
		needGap = true;
	}

	/** @brief A backend label with its resting fill color, the fill's alpha at rest, and the text color drawn over it. */
	struct BackendBadge
	{
		const char* label;
		ImVec4 color;
		float restingAlpha;
		ImVec4 textColor;
	};

	/** @brief Calls visit with a BackendBadge for each backend present, in display order. */
	template <typename Visitor>
	void ForEachBackendBadge(bool hasE11, bool hasCSPresets, bool hasBaseline, bool compact, Visitor&& visit)
	{
		const auto& palette = globals::menu->GetTheme().StatusPalette;
		if (hasE11)
			visit(BackendBadge{ BackendLabelE11(compact), palette.InfoColor, 0.9f, ImVec4(0.05f, 0.05f, 0.08f, 1.0f) });
		if (hasCSPresets)
			visit(BackendBadge{ BackendLabelCS(compact), palette.Warning, 0.85f, ImVec4(0.08f, 0.06f, 0.02f, 1.0f) });
		if (hasBaseline)
			visit(BackendBadge{ BackendLabelBaseline(compact), palette.SuccessColor, 0.85f, ImVec4(0.02f, 0.08f, 0.04f, 1.0f) });
	}

	bool CanApplyPack(const UnifiedPresetCatalog::PackInfo& pack)
	{
		return pack.valid && (pack.hasEffects11 || pack.hasCSPresets || pack.hasBaseline || pack.hasFormEdits);
	}

	void ApplyPack(const UnifiedPresetCatalog::PackInfo& pack)
	{
		if (UnifiedPresetCatalog::GetSingleton().ApplyPack(pack.id, true))
			logger::info("[Presets] Applied pack '{}'", pack.id);
	}

	Util::ConfirmationPopup applyConfirmation;
	/// Pack awaiting applyConfirmation; looked up again on confirm since a rescan may replace it.
	std::string pendingApplyPackId;

	/** @brief Applies the pack, first asking for confirmation when its compatibility check found problems. */
	void RequestApplyPack(const UnifiedPresetCatalog::PackInfo& pack)
	{
		if (!pack.compat.HasIssues()) {
			ApplyPack(pack);
			return;
		}
		std::string message;
		for (const auto* line : { &pack.compat.versionMessage, &pack.compat.featuresMessage }) {
			if (!line->empty())
				message += *line + "\n";
		}
		message += T("menu.presets.compat_apply_anyway", "Apply it anyway?");

		pendingApplyPackId = pack.id;
		applyConfirmation.title = T("menu.presets.compat_apply_title", "Compatibility warning");
		applyConfirmation.message = std::move(message);
		applyConfirmation.confirmLabel = T("menu.presets.apply", "Apply Preset");
		applyConfirmation.cancelLabel = T("ui.cancel", "Cancel");
		applyConfirmation.Request();
	}

	void DrawRoundedImage(ImDrawList* dl, ImTextureID texture, const ImVec2& p0, const ImVec2& p1, float rounding)
	{
		dl->AddImageRounded(texture, p0, p1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, rounding);
	}

	/** @brief Circular translucent arrow button centered on center; returns true when clicked. */
	bool DrawCarouselArrow(const char* id, Icons::GlyphRef icon, const ImVec2& center, float diameter)
	{
		ImGui::SetCursorScreenPos(ImVec2(center.x - diameter * 0.5f, center.y - diameter * 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, diameter * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

		const bool clicked = ImGui::InvisibleButton(id, ImVec2(diameter, diameter));
		const ImVec2 p0 = ImGui::GetItemRectMin();
		const ImVec2 p1 = ImGui::GetItemRectMax();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 bg = ImGui::GetColorU32(ImGui::IsItemActive()  ? ImGuiCol_ButtonActive :
											ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered :
																	 ImGuiCol_Button);
		dl->AddCircleFilled(ImVec2((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f), diameter * 0.5f, bg);
		Icons::DrawCenteredGlyph(dl, p0, ImVec2(p1.x - p0.x, p1.y - p0.y), icon, ImGui::GetColorU32(ImGuiCol_Text));

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(3);
		return clicked;
	}

	/** @brief Baseline layer note, the features the pack sets, and its disable-at-boot list. */
	void DrawBaselineDetails(const UnifiedPresetCatalog::PackInfo& pack)
	{
		ImGui::Spacing();
		ImGui::TextDisabled("%s", T("menu.presets.baseline_layer_note",
									  "Baseline settings are feature defaults applied beneath the Scene Manager, independent of the active pack. "
									  "They win over saved settings until you change a value yourself."));
		if (!pack.baselineFeatures.empty()) {
			ImGui::SeparatorText(T("menu.presets.baseline_features", "Baseline feature settings"));
			for (const auto& featureName : pack.baselineFeatures) {
				auto* feature = Feature::FindFeatureByShortName(featureName);
				if (feature)
					ImGui::BulletText("%s", feature->GetDisplayName().c_str());
				else
					ImGui::BulletText("%s %s", featureName.c_str(), T("menu.presets.baseline_feature_unavailable", "(not available)"));
			}
		}
		if (!pack.disableAtBoot.empty()) {
			ImGui::Spacing();
			ImGui::SeparatorText(T("menu.presets.baseline_boot", "Disable at boot"));
			ImGui::TextDisabled("%s", T("menu.presets.baseline_boot_note",
										  "Applied when this Baseline pack is enabled. A game restart is still required for load/unload."));
			std::vector<std::pair<std::string, bool>> bootEntries(pack.disableAtBoot.begin(), pack.disableAtBoot.end());
			std::ranges::sort(bootEntries, {}, &std::pair<std::string, bool>::first);
			for (const auto& [featureName, disabled] : bootEntries) {
				std::string label = featureName;
				for (auto* feature : Feature::GetFeatureList()) {
					if (feature && feature->GetShortName() == featureName) {
						label = feature->GetDisplayName();
						break;
					}
				}
				ImGui::BulletText("%s: %s", label.c_str(),
					disabled ? T("menu.features.disabled", "Disabled") : T("menu.features.enabled", "Enabled"));
			}
		}
	}

	constexpr float kHoverLift = 0.1f;
	/// Floor for a list row's name width, in font sizes, so badges cannot squeeze it to nothing.
	constexpr float kMinRowNameFontSizes = 5.0f;
	/// Narrowest the detail title column may get, in font sizes, before the badges move beneath it.
	constexpr float kMinTitleColumnFontSizes = 14.0f;

	/** @brief Width of a default ImGui::Button with this label. */
	float MeasureButtonWidth(const char* label)
	{
		return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	}

	/** @brief Apply, open-folder, remove-baseline and disable buttons, then a line saying how the pack is active. */
	void DrawPackActions(UnifiedPresetCatalog& catalog, const UnifiedPresetCatalog::PackInfo& pack)
	{
		const auto& theme = globals::menu->GetTheme();
		const bool packActive = catalog.GetActivePackId() == pack.id;
		const bool baselineEnabled = catalog.IsBaselineEnabled(pack.id);

		// Each button after the first wraps to a new row when the pane is too narrow to hold it.
		ImGui::BeginDisabled(!CanApplyPack(pack));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
		const ImVec4& info = theme.StatusPalette.InfoColor;
		const ImVec4 infoHovered(std::min(1.0f, info.x + kHoverLift), std::min(1.0f, info.y + kHoverLift), std::min(1.0f, info.z + kHoverLift), 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, info);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, infoHovered);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, info);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.07f, 0.1f, 1.0f));
		if (Util::ButtonWithFlash(T("menu.presets.apply", "Apply Preset")))
			RequestApplyPack(pack);
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar();
		ImGui::EndDisabled();

		const Icons::GlyphRef folderIcon = Icons::TI(ICON_TI_FOLDER_OPEN);
		BrowserUI::SameLineIfFits(Icons::CalcGlyphSize(folderIcon).x + ImGui::GetStyle().FramePadding.x * 2.0f);
		{
			auto _style = Util::TransparentIconButtonStyle();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			if (Icons::Button("##OpenPack", folderIcon))
				catalog.OpenPackFolder(pack.id);
			ImGui::PopStyleVar();
		}
		Util::AddTooltip(T("menu.presets.open_pack", "Open Preset Folder"));

		if (baselineEnabled) {
			const char* removeLabel = T("menu.presets.remove_baseline", "Remove Baseline");
			BrowserUI::SameLineIfFits(MeasureButtonWidth(removeLabel));
			if (ImGui::Button(removeLabel))
				catalog.RemoveBaseline(pack.id);
			Util::AddTooltip(T("menu.presets.remove_baseline_tooltip",
				"Stops applying this pack's baseline settings. Its features go back to what defaults and your other Baseline packs set; your own edits are kept."));
		}

		if (packActive) {
			const char* disableLabel = T("menu.presets.disable", "Disable Preset");
			BrowserUI::SameLineIfFits(MeasureButtonWidth(disableLabel));
			if (ImGui::Button(disableLabel))
				catalog.DisableActivePack();
			Util::AddTooltip(T("menu.presets.disable_tooltip",
				"Turns this preset off. Scene settings and form edits fall back to your own, and an Effects 11 preset switches to Vanilla."));
		}

		if (packActive || baselineEnabled) {
			MenuFonts::FontRoleGuard sub(Menu::FontRole::Subtext);
			ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.SuccessColor);
			ImGui::TextWrapped("%s", packActive ? T("menu.presets.active_pack", "This pack is currently active.") :
												  T("menu.presets.baseline_enabled", "This pack's baseline settings are enabled."));
			ImGui::PopStyleColor();
		}
	}
}

float PresetsPageRenderer::MeasureBackendBadgesWidth(bool hasE11, bool hasCSPresets, bool hasBaseline, bool compact)
{
	const ImGuiStyle& style = ImGui::GetStyle();
	float width = 0.0f;
	bool needGap = false;
	ForEachBackendBadge(hasE11, hasCSPresets, hasBaseline, compact, [&](const BackendBadge& badge) {
		AppendBadgeGap(width, needGap);
		width += ImGui::CalcTextSize(badge.label).x + style.FramePadding.x * 2.0f;
	});
	return width;
}

void PresetsPageRenderer::DrawBackendBadges(bool hasE11, bool hasCSPresets, bool hasBaseline, bool compact)
{
	const float gap = BadgeGap();

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
	ImGui::BeginGroup();
	bool needGap = false;
	ForEachBackendBadge(hasE11, hasCSPresets, hasBaseline, compact, [&](const BackendBadge& badge) {
		if (needGap)
			ImGui::SameLine(0.0f, gap);
		needGap = true;
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(badge.color.x, badge.color.y, badge.color.z, badge.restingAlpha));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, badge.color);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, badge.color);
		ImGui::PushStyleColor(ImGuiCol_Text, badge.textColor);
		ImGui::SmallButton(badge.label);
		ImGui::PopStyleColor(4);
	});
	ImGui::EndGroup();
	ImGui::PopStyleVar();
}

void PresetsPageRenderer::RenderToolbar()
{
	auto& catalog = UnifiedPresetCatalog::GetSingleton();
	const float scale = Util::GetUIScale();

	BrowserUI::SearchField("##PresetSearch", searchBuffer, IM_ARRAYSIZE(searchBuffer),
		T("menu.presets.search", "Search presets..."), 220.0f * scale, true);

	ImGui::SameLine();
	using PresetType = UnifiedPresetCatalog::PresetType;
	const auto typeChip = [](const char* label, std::optional<PresetType> type) {
		ImGui::PushID(type ? static_cast<int>(*type) : -1);
		if (BrowserUI::FilterChip("##type", { label }, typeFilter == type))
			typeFilter = type;
		ImGui::PopID();
	};
	typeChip(T("menu.presets.filter_all", "All"), std::nullopt);
	ImGui::SameLine();
	typeChip(T("menu.presets.filter_e11", "E11"), PresetType::E11);
	ImGui::SameLine();
	typeChip(T("menu.presets.filter_cs", "CS"), PresetType::CS);
	ImGui::SameLine();
	typeChip(T("menu.presets.filter_baseline", "Baseline"), PresetType::Baseline);

	// A small divider keeps these maintenance actions from reading as more filter chips.
	Util::ToolbarDivider();

	{
		auto _style = Util::TransparentIconButtonStyle();
		if (Icons::Button("##PresetsRefresh", Icons::TI(ICON_TI_REFRESH))) {
			catalog.Discover();
			discovered = true;
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", T("menu.presets.refresh_tooltip", "Rescan unified packs and Effects 11 library presets."));

		ImGui::SameLine();
		if (Icons::Button("##PresetsOpenFolder", Icons::TI(ICON_TI_FOLDER_OPEN)))
			catalog.OpenPresetsFolder();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", T("menu.presets.open_folder_tooltip", "Open the unified Presets library folder in Explorer."));
	}

	DrawOpenCSEditorButton("##PresetsOpenCSEditor");
}

void PresetsPageRenderer::DrawOpenCSEditorButton(const char* id)
{
	const char* title = T("menu.presets.open_cs_editor", "CS Editor");
	const Icons::GlyphRef brush = Icons::TI(ICON_TI_BRUSH);
	BrowserUI::RightAlign(Icons::MeasureLabeledButton(brush, title));
	if (Icons::LabeledButton(id, brush, title))
		CSEditor::OpenEditorWindow();
	Util::AddTooltip(T("menu.presets.open_cs_editor_tooltip", "Open the CS Editor for weather, lighting, and scene editing."));
}

void PresetsPageRenderer::RenderBaselineOrder()
{
	auto& catalog = UnifiedPresetCatalog::GetSingleton();
	const auto& ids = catalog.GetBaselinePackIds();
	if (ids.size() < 2)
		return;

	const bool open = ImGui::CollapsingHeader(T("menu.presets.baseline_order", "Baseline Load Order"), ImGuiTreeNodeFlags_DefaultOpen);
	Util::AddTooltip(T("menu.presets.baseline_order_tooltip",
		"Enabled Baseline packs, highest priority first. A pack wins any setting it shares with the packs below it; your own edits still win over all of them."));
	if (!open)
		return;

	// Highest priority first, like a datapack list. Moves apply after the loop so the list is not edited mid-draw.
	std::string moveId;
	bool moveRaise = false;
	for (size_t row = 0; row < ids.size(); ++row) {
		const auto& id = ids[ids.size() - 1 - row];
		ImGui::PushID(id.c_str());

		ImGui::BeginDisabled(row == 0);
		if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
			moveId = id;
			moveRaise = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::BeginDisabled(row + 1 == ids.size());
		if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
			moveId = id;
			moveRaise = false;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		const auto* pack = catalog.FindPack(id);
		const std::string label = std::format("{}. {}", row + 1, pack && !pack->name.empty() ? pack->name : id);
		if (ImGui::Selectable(label.c_str(), selectedPackId == id)) {
			selectedPackId = id;
			lightboxPackId.clear();
			lightboxImageIndex = -1;
			lightboxSuppressClose = false;
		}

		ImGui::PopID();
	}
	if (!moveId.empty())
		catalog.MoveBaseline(moveId, moveRaise);

	ImGui::Spacing();
	ImGui::Separator();
}

void PresetsPageRenderer::RenderList(float width)
{
	auto& catalog = UnifiedPresetCatalog::GetSingleton();
	const auto& theme = globals::menu->GetTheme();
	const float scale = Util::GetUIScale();

	const auto indices = catalog.Query(typeFilter, searchBuffer);

	ImGui::BeginChild("##PresetList", ImVec2(width, 0), true);
	RenderBaselineOrder();
	{
		MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
		ImGui::TextUnformatted(T("menu.presets.installed", "Installed"));
	}
	ImGui::Separator();

	if (indices.empty()) {
		ImGui::TextDisabled("%s", T("menu.presets.empty", "No presets found. Drop packs into the Presets folder, install Effects 11 presets, or export from Scene Manager."));
	}

	const float rowPad = 6.0f * scale;
	const float logoSize = 40.0f * scale;
	const float rowGap = ImGui::GetStyle().ItemSpacing.y;
	const float rowInset = 8.0f * scale;
	const float statusGap = 6.0f * scale;

	for (size_t idx : indices) {
		const auto& packId = catalog.GetPacks()[idx].id;
		auto* packPtr = catalog.FindPack(packId);
		if (!packPtr)
			continue;
		auto& pack = *packPtr;
		const bool selected = selectedPackId == pack.id;
		const bool isActive = catalog.GetActivePackId() == pack.id || catalog.IsBaselineEnabled(pack.id);
		const auto& compat = pack.compat;
		const int warnLines = (compat.versionGap != PresetCompatibility::VersionGap::None ? 1 : 0) +
		                      (!compat.featuresMessage.empty() ? 1 : 0) +
		                      (!compat.pluginsMessage.empty() ? 1 : 0);
		const float textLines = 2.0f + static_cast<float>(warnLines);
		const float textBlockH = ImGui::GetTextLineHeightWithSpacing() * textLines;
		const float rowHeight = std::max(logoSize, textBlockH) + rowPad * 2.0f;

		ImGui::PushID(pack.id.c_str());

		const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
		const float textWidth = ImGui::GetContentRegionAvail().x - logoSize - rowInset * 3.0f;
		const bool clicked = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowHeight));
		if (clicked) {
			selectedPackId = pack.id;
			lightboxPackId.clear();
			lightboxImageIndex = -1;
			lightboxSuppressClose = false;
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && CanApplyPack(pack))
				RequestApplyPack(pack);
		}

		// Draw contents inside the selectable bounds, then park the cursor past the row so the
		// next selectable cannot overlap this highlight.
		ImGui::SetCursorScreenPos(ImVec2(rowOrigin.x + rowInset, rowOrigin.y + rowPad));

		catalog.EnsureArtwork(pack);
		{
			ImGui::Dummy(ImVec2(logoSize, logoSize));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p0 = ImGui::GetItemRectMin();
			const ImVec2 p1 = ImGui::GetItemRectMax();
			const float rounding = ImGui::GetStyle().FrameRounding;
			if (pack.logoSRV)
				DrawRoundedImage(dl, reinterpret_cast<ImTextureID>(pack.logoSRV.get()), p0, p1, rounding);
			else
				dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(ImVec4(0.15f, 0.15f, 0.18f, 1.0f)), rounding);
		}

		ImGui::SameLine(0.0f, rowInset);
		ImGui::BeginGroup();
		{
			MenuFonts::FontRoleGuard body(Menu::FontRole::Body);
			const char* activeLabel = T("menu.presets.active", "Active");
			const char* invalidLabel = T("menu.presets.invalid", "Invalid");

			// The name gives way to the badges and status, ellipsised so they stay on screen.
			float trailingWidth = rowInset + MeasureBackendBadgesWidth(pack.IsE11(), pack.IsCS(), pack.IsBaseline(), true);
			if (isActive)
				trailingWidth += statusGap + ImGui::CalcTextSize(activeLabel).x;
			if (!pack.valid)
				trailingWidth += statusGap + ImGui::CalcTextSize(invalidLabel).x;
			BrowserUI::EllipsizedText(pack.name.c_str(), std::max(textWidth - trailingWidth, ImGui::GetFontSize() * kMinRowNameFontSizes),
				ImGui::GetStyleColorVec4(ImGuiCol_Text));

			ImGui::SameLine(0.0f, rowInset);
			DrawBackendBadges(pack.IsE11(), pack.IsCS(), pack.IsBaseline(), true);
			if (isActive) {
				ImGui::SameLine(0.0f, statusGap);
				ImGui::TextColored(theme.StatusPalette.SuccessColor, "%s", activeLabel);
			}
			if (!pack.valid) {
				ImGui::SameLine(0.0f, statusGap);
				ImGui::TextColored(theme.StatusPalette.Error, "%s", invalidLabel);
			}
		}
		{
			MenuFonts::FontRoleGuard sub(Menu::FontRole::Subtext);
			std::string meta;
			if (!pack.author.empty() && !pack.version.empty())
				meta = std::format("{} · v{}", pack.author, pack.version);
			else if (!pack.author.empty())
				meta = pack.author;
			else if (!pack.version.empty())
				meta = std::format("v{}", pack.version);
			else if (pack.source == UnifiedPresetCatalog::SourceKind::Effects11Orphan)
				meta = T("menu.presets.source_e11", "Effects 11 library");
			else if (pack.IsE11())
				meta = T("menu.presets.source_enb", "ENB / Effects 11");
			else if (pack.IsCS())
				meta = T("menu.presets.source_cs", "CS Presets");
			else if (pack.IsBaseline())
				meta = T("menu.presets.source_baseline", "Baseline feature settings");
			if (!meta.empty())
				BrowserUI::EllipsizedText(meta.c_str(), textWidth, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			PresetCompatibility::DrawWarnings(compat, true);
		}
		ImGui::EndGroup();

		ImGui::SetCursorScreenPos(ImVec2(rowOrigin.x, rowOrigin.y + rowHeight + rowGap));
		ImGui::Dummy(ImVec2(0, 0));

		ImGui::PopID();
	}

	ImGui::EndChild();
}

void PresetsPageRenderer::RenderDetail()
{
	auto& catalog = UnifiedPresetCatalog::GetSingleton();
	const auto& theme = globals::menu->GetTheme();
	const ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild("##PresetDetail", ImVec2(0, 0), true);

	auto* pack = catalog.FindPack(selectedPackId);
	if (!pack) {
		ImGui::TextDisabled("%s", T("menu.presets.select_prompt", "Select a preset from the list to view details."));
		ImGui::EndChild();
		return;
	}

	catalog.EnsureArtwork(*pack);

	if (lightboxPackId != pack->id) {
		lightboxPackId = pack->id;
		lightboxImageIndex = -1;
		lightboxSuppressClose = false;
	}
	if (lightboxImageIndex >= static_cast<int>(pack->screenshotSRVs.size())) {
		lightboxImageIndex = -1;
		lightboxSuppressClose = false;
	}

	const float badgesWidth = MeasureBackendBadgesWidth(pack->IsE11(), pack->IsCS(), pack->IsBaseline(), false);
	const float posterH = ImGui::GetFrameHeight() * 6.0f;
	const float posterW = posterH * (2.0f / 3.0f);  // movie-poster portrait

	// A narrow pane would leave the title no room beside the badges, so they drop beneath it instead.
	const bool badgesBesideTitle = ImGui::GetContentRegionAvail().x - posterW - badgesWidth - style.ItemSpacing.x * 2.0f >=
	                               ImGui::GetFontSize() * kMinTitleColumnFontSizes;

	// Poster | title+author | badges in one table so pills share a baseline and spacing is style-driven.
	if (ImGui::BeginTable("##PresetDetailHeader", badgesBesideTitle ? 3 : 2,
			ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
		ImGui::TableSetupColumn("poster", ImGuiTableColumnFlags_WidthFixed, posterW);
		ImGui::TableSetupColumn("title", ImGuiTableColumnFlags_WidthStretch);
		if (badgesBesideTitle)
			ImGui::TableSetupColumn("badges", ImGuiTableColumnFlags_WidthFixed, badgesWidth);

		ImGui::TableNextRow(ImGuiTableRowFlags_None, posterH);
		ImGui::TableSetColumnIndex(0);
		{
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(posterW, posterH));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p1(origin.x + posterW, origin.y + posterH);
			const float rounding = style.FrameRounding;
			// Deliberately the pack's own cover art only - never a screenshot, so this stays a
			// stable "poster" instead of jumping to whatever the filmstrip/hero viewer is showing.
			if (ID3D11ShaderResourceView* poster = pack->coverSRV.get()) {
				DrawRoundedImage(dl, reinterpret_cast<ImTextureID>(poster), origin, p1, rounding);
			} else {
				dl->AddRectFilled(origin, p1,
					ImGui::ColorConvertFloat4ToU32(ImVec4(0.12f, 0.12f, 0.14f, 1.0f)), rounding);
			}
		}

		// Top-aligned rather than centred in the poster's full height: the actions below now live
		// here too, so this column fills most of the row instead of floating in its middle.
		const float titleTopPad = 2.0f * Util::GetUIScale();

		ImGui::TableSetColumnIndex(1);
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + titleTopPad);
			ImGui::BeginGroup();
			{
				const auto stage = PresetCompatibility::ReleaseStageFromVersion(pack->version);
				const std::string stageTag = Feature::GetReleaseStageTag(stage);

				MenuFonts::FontRoleGuard title(Menu::FontRole::Heading);
				ImGui::SetWindowFontScale(1.35f);
				ImGui::TextWrapped("%s", pack->name.c_str());
				if (!stageTag.empty()) {
					MenuFonts::FontRoleGuard body(Menu::FontRole::Body);
					BrowserUI::SameLineIfFits(ImGui::CalcTextSize(stageTag.c_str()).x);
					ImGui::TextColored(Feature::GetReleaseStageColor(stage), "%s", stageTag.c_str());
				}
				ImGui::SetWindowFontScale(1.0f);
			}
			{
				MenuFonts::FontRoleGuard sub(Menu::FontRole::Subtext);
				std::string metaLine;
				if (!pack->author.empty())
					metaLine += pack->author;
				if (!pack->version.empty()) {
					if (!metaLine.empty())
						metaLine += "  ·  ";
					metaLine += "v" + pack->version;
				}
				if (!metaLine.empty())
					Util::Text::Secondary("%s", metaLine.c_str());
				if (!pack->nexusUrl.empty()) {
					ImGui::SameLine(0.0f, style.ItemSpacing.x);
					if (Icons::Button("##NexusLink", Icons::TI(ICON_TI_EXTERNAL_LINK)))
						Util::FileHelpers::OpenWithShell(pack->nexusUrl);
					Util::AddTooltip(T("menu.presets.nexus_link_tooltip", "Open this preset's Nexus Mods page."));
				}
				PresetCompatibility::DrawWarnings(pack->compat, false);
			}

			if (!badgesBesideTitle && badgesWidth > 0.0f) {
				ImGui::Spacing();
				DrawBackendBadges(pack->IsE11(), pack->IsCS(), pack->IsBaseline(), false);
			}

			ImGui::Spacing();

			DrawPackActions(catalog, *pack);

			ImGui::EndGroup();
		}

		if (badgesBesideTitle && badgesWidth > 0.0f) {
			ImGui::TableSetColumnIndex(2);
			// Aligned to the title line rather than centred in the whole poster height, now that
			// the title column reads top-down (title, author, actions) instead of floating in the middle.
			const float titleLineH = ImGui::GetTextLineHeight() * 1.35f;
			const float badgeH = ImGui::GetFrameHeight();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + titleTopPad + std::max(0.0f, (titleLineH - badgeH) * 0.5f));
			DrawBackendBadges(pack->IsE11(), pack->IsCS(), pack->IsBaseline(), false);
		}

		ImGui::EndTable();
	}

	ImGui::Spacing();

	if (!pack->description.empty()) {
		MenuFonts::FontRoleGuard body(Menu::FontRole::Body);
		ImGui::TextWrapped("%s", pack->description.c_str());
	} else {
		ImGui::TextDisabled("%s", T("menu.presets.no_description", "No description provided."));
	}

	if (pack->hasCSPresets) {
		ImGui::Spacing();
		ImGui::TextDisabled("%s", T("menu.presets.cs_layer_note",
									  "Applying makes this pack's CS Presets the active scene layer, replacing the previous pack's."));
	}

	if (pack->hasBaseline)
		DrawBaselineDetails(*pack);

	if (!pack->valid && !pack->invalidReason.empty()) {
		ImGui::Spacing();
		ImGui::TextColored(theme.StatusPalette.Error, "%s", pack->invalidReason.c_str());
	}

	if (!pack->screenshotSRVs.empty()) {
		ImGui::Spacing();
		ImGui::SeparatorText(T("menu.presets.screenshots", "Screenshots"));
		const float thumb = ImGui::GetFrameHeight() * 4.0f;
		const float thumbH = thumb * (9.0f / 16.0f);
		for (size_t i = 0; i < pack->screenshotSRVs.size(); ++i) {
			if (i > 0)
				ImGui::SameLine(0.0f, style.ItemSpacing.x);
			ImGui::PushID(static_cast<int>(i));
			const bool isOpen = static_cast<int>(i) == lightboxImageIndex;
			const bool clicked = ImGui::InvisibleButton("##shot", ImVec2(thumb, thumbH));
			const ImVec2 p0 = ImGui::GetItemRectMin();
			const ImVec2 p1 = ImGui::GetItemRectMax();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			if (pack->screenshotSRVs[i])
				DrawRoundedImage(dl, reinterpret_cast<ImTextureID>(pack->screenshotSRVs[i].get()), p0, p1, style.FrameRounding);
			if (isOpen || ImGui::IsItemHovered()) {
				const ImU32 border = ImGui::ColorConvertFloat4ToU32(isOpen ?
																		theme.StatusPalette.InfoColor :
																		ImGui::GetStyleColorVec4(ImGuiCol_Border));
				dl->AddRect(p0, p1, border, style.FrameRounding, 0, style.FrameBorderSize + (isOpen ? 1.0f : 0.0f));
			}
			if (clicked) {
				lightboxImageIndex = static_cast<int>(i);
				lightboxSuppressClose = true;
			}
			ImGui::PopID();
		}
	}

	ImGui::EndChild();
}

void PresetsPageRenderer::Open()
{
	// Both editors hide the main menu while open, so the page would be selected but never seen.
	if (auto* editorWindow = EditorWindow::GetSingleton(); editorWindow && editorWindow->open)
		editorWindow->open = false;
	Effects11Editor::GetSingleton().Close(false);

	auto* menu = globals::menu;
	if (!menu)
		return;
	menu->IsEnabled = true;
	// Addressed by display name, the same translation the left panel draws.
	menu->SelectFeatureMenu(T("menu.features.presets", "Presets"));
}

float PresetsPageRenderer::MeasureOpenButton()
{
	return Icons::MeasureLabeledButton(Icons::TI(ICON_TI_STACK_2), T("menu.presets.open_presets", "Presets"));
}

void PresetsPageRenderer::DrawOpenButton(const char* id, const char* tooltip)
{
	if (Icons::LabeledButton(id, Icons::TI(ICON_TI_STACK_2), T("menu.presets.open_presets", "Presets")))
		Open();
	Util::AddTooltip(tooltip);
}

bool PresetsPageRenderer::CloseLightboxIfOpen()
{
	if (lightboxImageIndex < 0)
		return false;
	lightboxImageIndex = -1;
	lightboxSuppressClose = false;
	return true;
}

void PresetsPageRenderer::RenderScreenshotLightbox()
{
	if (lightboxImageIndex < 0)
		return;

	auto& catalog = UnifiedPresetCatalog::GetSingleton();
	auto* pack = catalog.FindPack(selectedPackId);
	if (!pack || lightboxImageIndex >= static_cast<int>(pack->screenshotSRVs.size()) ||
		!pack->screenshotSRVs[static_cast<size_t>(lightboxImageIndex)]) {
		lightboxImageIndex = -1;
		return;
	}

	catalog.EnsureArtwork(*pack);

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiStyle& style = ImGui::GetStyle();
	const int shotCount = static_cast<int>(pack->screenshotSRVs.size());

	// Full-viewport dim layer. Semi-transparent WindowBg lets BackgroundBlur show through.
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowFocus();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.04f, 0.72f));

	constexpr ImGuiWindowFlags kFlags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoCollapse;

	if (!ImGui::Begin("##PresetScreenshotLightbox", nullptr, kFlags)) {
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
		return;
	}

	const float maxW = viewport->Size.x * 0.8f;
	const float maxH = viewport->Size.y * 0.8f;
	float imgW = maxW;
	float imgH = imgW * (9.0f / 16.0f);
	if (imgH > maxH) {
		imgH = maxH;
		imgW = imgH * (16.0f / 9.0f);
	}

	const ImVec2 imgOrigin(
		viewport->Pos.x + (viewport->Size.x - imgW) * 0.5f,
		viewport->Pos.y + (viewport->Size.y - imgH) * 0.5f);
	const ImVec2 imgEnd(imgOrigin.x + imgW, imgOrigin.y + imgH);

	// Backdrop → image → arrows. AllowOverlap so later items win hover/click over earlier ones;
	// without it, arrow hits land on the image (or image hits on the backdrop) and dismiss.
	ImGui::SetNextItemAllowOverlap();
	ImGui::SetCursorScreenPos(viewport->Pos);
	const bool backdropClicked = ImGui::InvisibleButton("##LightboxBackdrop", viewport->Size);

	ImGui::SetNextItemAllowOverlap();
	ImGui::SetCursorScreenPos(imgOrigin);
	const bool imageClicked = ImGui::InvisibleButton("##LightboxImage", ImVec2(imgW, imgH));
	ImDrawList* dl = ImGui::GetWindowDrawList();
	DrawRoundedImage(dl,
		reinterpret_cast<ImTextureID>(pack->screenshotSRVs[static_cast<size_t>(lightboxImageIndex)].get()),
		imgOrigin, imgEnd, style.FrameRounding);

	bool navigated = false;
	if (shotCount > 1) {
		const float arrowBtn = ImGui::GetFrameHeight() * 1.35f;
		const float midY = imgOrigin.y + imgH * 0.5f;
		const float inset = style.ItemSpacing.x * 2.0f;
		if (DrawCarouselArrow("##LightboxPrev", Icons::TI(ICON_TI_CHEVRON_LEFT),
				ImVec2(imgOrigin.x + inset + arrowBtn * 0.5f, midY), arrowBtn)) {
			lightboxImageIndex = (lightboxImageIndex - 1 + shotCount) % shotCount;
			navigated = true;
		}
		if (DrawCarouselArrow("##LightboxNext", Icons::TI(ICON_TI_CHEVRON_RIGHT),
				ImVec2(imgEnd.x - inset - arrowBtn * 0.5f, midY), arrowBtn)) {
			lightboxImageIndex = (lightboxImageIndex + 1) % shotCount;
			navigated = true;
		}
	}

	// Escape is primarily consumed in Menu::ProcessInputEventQueue via CloseLightboxIfOpen.
	// Suppress click-to-close until the mouse releases so the opening thumb click cannot dismiss it.
	if (lightboxSuppressClose && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		lightboxSuppressClose = false;

	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		lightboxImageIndex = -1;
	else if (!lightboxSuppressClose && !navigated && (imageClicked || backdropClicked))
		lightboxImageIndex = -1;

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
}

void PresetsPageRenderer::Render()
{
	auto& catalog = UnifiedPresetCatalog::GetSingleton();
	if (!discovered) {
		catalog.Discover();
		discovered = true;
		if (selectedPackId.empty() && !catalog.GetActivePackId().empty())
			selectedPackId = catalog.GetActivePackId();
		else if (selectedPackId.empty() && !catalog.GetPacks().empty())
			selectedPackId = catalog.GetPacks().front().id;
	}

	ImGui::BeginChild("PresetsPage", ImVec2(0, 0), false);

	RenderToolbar();
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	const float scale = Util::GetUIScale();
	const float listWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.34f, 260.0f * scale, 380.0f * scale);
	RenderList(listWidth);
	ImGui::SameLine();
	RenderDetail();

	ImGui::EndChild();

	RenderScreenshotLightbox();

	if (applyConfirmation.Draw()) {
		if (const auto* pack = catalog.FindPack(pendingApplyPackId))
			ApplyPack(*pack);
	}
}
