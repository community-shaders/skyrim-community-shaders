#include "FormListPage.h"

#include "../../I18n/I18n.h"
#include "../EditorWindow.h"
#include "../Weather/CellLightingWidget.h"
#include "../Weather/WeatherWidget.h"
#include "../WeatherUtils.h"
#include "BrowserWidgets.h"
#include "Features/CSEditor.h"
#include "Globals.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/Icons/helpers/WeatherTypeIcons.h"
#include "Utils/UI.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	using Browser::FilterScope;
	using Browser::FormListState;
	using Browser::FormRow;
	using Browser::SortKey;
	using FormListPage::Context;
	using FormListPage::WidgetVec;

	constexpr std::string_view kWeather = "Weather";
	constexpr std::string_view kImageSpace = "ImageSpace";
	constexpr std::string_view kLightingTemplate = "Lighting Template";
	constexpr std::string_view kCellLighting = "Cell Lighting";
	constexpr std::string_view kVolumetricLighting = "Volumetric Lighting";
	constexpr std::string_view kShaderParticle = "Shader Particle Geometry";
	constexpr std::string_view kLensFlare = "Lens Flare";
	constexpr std::string_view kVisualEffect = "Visual Effect";

	/// Rows are rebuilt at least this often so unsaved edits and files saved elsewhere show up.
	constexpr double kViewRefreshSeconds = 0.5;
	/// Below this content width (at the 1080p baseline) the inspector moves under the list.
	constexpr float kInspectorBesideMinWidth = 620.0f;
	constexpr float kInspectorWidth = 340.0f;
	/// Recent chips are capped so one long editor ID cannot push the rest off the row.
	constexpr float kRecentChipMaxWidth = 220.0f;
	constexpr float kMinSearchWidth = 160.0f;
	constexpr float kPluginComboWidth = 170.0f;
	/// The plugin dropdown opens wider than its combo so long names and the search field fit.
	constexpr float kPluginPopupMinWidth = 260.0f;
	constexpr float kPluginPopupMaxRows = 14.0f;
	constexpr float kStatusColumnWidth = 110.0f;
	/// Icons DrawStateIcons lays out: open, unsaved, saved, preset.
	constexpr int kStateSlotCount = 4;
	constexpr float kPeriodColumnWidth = 70.0f;
	constexpr int kUsageListLimit = 12;
	/// Active records shown at most; ImageSpace and Volumetric Lighting have one per time of day.
	constexpr size_t kMaxActiveRecords = 4;
	/// The favourite star mixes the theme's hotkey yellow toward its warning orange, a softer gold.
	constexpr float kFavoriteTowardWarning = 0.3f;
	constexpr float kInspectorBgAlpha = 0.5f;

	enum ColumnId : ImGuiID
	{
		ColFav,
		ColName,
		ColFile,
		ColFormId,
		ColStatus,
		ColState
	};

	struct ActiveRecord
	{
		std::string label;
		std::string suffix;
		RE::FormID formId = 0;
		/// The weather fading out during a transition.
		bool outgoing = false;
		/// The list the record belongs to; the fallback weather can show on any category.
		std::string category;
		std::function<void()> open;
	};

	const char* GetScopeName(FilterScope scope)
	{
		switch (scope) {
		case FilterScope::EditorID:
			return T(TKEY("filter_editor_id"), "Editor ID");
		case FilterScope::FormID:
			return T(TKEY("filter_form_id"), "Form ID");
		case FilterScope::File:
			return T(TKEY("filter_file"), "File");
		case FilterScope::Status:
			return T(TKEY("filter_status"), "Status");
		default:
			return T(TKEY("filter_all"), "All");
		}
	}

	ImVec4 FavoriteColor()
	{
		const auto& palette = Menu::GetSingleton()->GetTheme().StatusPalette;
		const ImVec4& hot = palette.CurrentHotkey;
		const ImVec4& warn = palette.Warning;
		const float t = kFavoriteTowardWarning;
		return ImVec4(hot.x + (warn.x - hot.x) * t, hot.y + (warn.y - hot.y) * t, hot.z + (warn.z - hot.z) * t, 1.0f);
	}

	bool IsEnterPressed()
	{
		return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
	}

	bool IsActivateKeyPressed()
	{
		return IsEnterPressed() || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false);
	}

	/// Text cut to maxWidth with a trailing "...", keeping UTF-8 sequences whole.
	std::string FitText(const std::string& text, float maxWidth)
	{
		if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
			return text;
		const float dotsWidth = ImGui::CalcTextSize("...").x;
		std::string out = text;
		while (!out.empty() && ImGui::CalcTextSize(out.c_str()).x + dotsWidth > maxWidth) {
			while (!out.empty()) {
				const auto c = static_cast<unsigned char>(out.back());
				out.pop_back();
				if ((c & 0xC0) != 0x80)
					break;
			}
		}
		return out + "...";
	}

	/// Forms without an editor ID are listed by their local form ID; the plugin has its own column.
	std::string FallbackDisplay(const Widget& widget)
	{
		const std::string key = widget.GetSaveKey();
		const auto tilde = key.find('~');
		return tilde == std::string::npos ? key : key.substr(0, tilde);
	}

	Widget* FindWidget(WidgetVec& widgets, RE::FormID formId)
	{
		if (formId == 0)
			return nullptr;
		for (const auto& widget : widgets)
			if (widget->form && widget->form->GetFormID() == formId)
				return widget.get();
		return nullptr;
	}

	RE::FormID GetSelectedId(const FormListState& state, const std::string& category)
	{
		const auto it = state.selectedByCategory.find(category);
		return it != state.selectedByCategory.end() ? it->second : 0;
	}

	void OpenWidget(EditorWindow& editor, const std::string& category, Widget& widget)
	{
		widget.SetOpen(true);
		widget.RequestFocus();
		editor.AddToRecent(widget.GetEditorID(), category);
	}

	/// Selects a record in its category's list, switching category; the list scrolls to it.
	void JumpTo(FormListState& state, EditorWindow& editor, const std::string& category, RE::FormID formId)
	{
		state.selectedByCategory[category] = formId;
		state.scrollToSelection = true;
		editor.SelectCategory(category);
	}

	/// A plain click selects the record in its own list; Ctrl opens its editor instead.
	void FollowLink(FormListState& state, EditorWindow& editor, const std::string& category, Widget* widget, RE::FormID formId)
	{
		if (!ImGui::GetIO().KeyCtrl)
			JumpTo(state, editor, category, formId);
		else if (widget)
			OpenWidget(editor, category, *widget);
	}

	void RevealInExplorer(const std::string& path)
	{
		const std::wstring args = L"/select,\"" + std::filesystem::path(path).wstring() + L"\"";
		ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
	}

	bool MatchesFilter(const FormRow& row, const FormListState& state)
	{
		const std::string_view text = state.filter;
		if (text.empty())
			return true;
		switch (state.scope) {
		case FilterScope::EditorID:
			return ContainsStringIgnoreCase(row.editorId, text);
		case FilterScope::FormID:
			return ContainsStringIgnoreCase(row.formIdHex, text);
		case FilterScope::File:
			return ContainsStringIgnoreCase(row.file, text);
		case FilterScope::Status:
			return !row.marker.empty() && ContainsStringIgnoreCase(row.marker, text);
		default:
			return ContainsStringIgnoreCase(row.editorId, text) || ContainsStringIgnoreCase(row.formIdHex, text) ||
			       ContainsStringIgnoreCase(row.file, text) || (!row.marker.empty() && ContainsStringIgnoreCase(row.marker, text));
		}
	}

	std::string MakeViewKey(const FormListState& state, const Context& context)
	{
		return std::format("{}\x1f{}\x1f{}{}{}{}{}{}\x1f{}\x1f{}{}\x1f{}", context.category, std::string_view(state.filter),
			static_cast<int>(state.scope), state.onlyFavorites, state.onlyFlagged, state.onlySaved, state.onlyUnsaved, state.onlyPreset,
			state.plugin, static_cast<int>(state.sortKey), state.sortAscending, state.dataGeneration);
	}

	void SortRows(FormListState& state)
	{
		if (state.sortKey == SortKey::None)
			return;
		const SortKey key = state.sortKey;
		const bool ascending = state.sortAscending;
		std::ranges::stable_sort(state.rows, [key, ascending](const FormRow& a, const FormRow& b) {
			int comparison = 0;
			switch (key) {
			case SortKey::EditorID:
				comparison = _stricmp(a.editorId.c_str(), b.editorId.c_str());
				break;
			case SortKey::FormID:
				comparison = _stricmp(a.formIdHex.c_str(), b.formIdHex.c_str());
				break;
			case SortKey::File:
				comparison = _stricmp(a.file.c_str(), b.file.c_str());
				break;
			case SortKey::Status:
				comparison = _stricmp(a.marker.c_str(), b.marker.c_str());
				break;
			case SortKey::Saved:
				comparison = static_cast<int>(a.hasSavedFile) - static_cast<int>(b.hasSavedFile);
				if (comparison == 0)
					comparison = static_cast<int>(a.fromPack) - static_cast<int>(b.fromPack);
				if (comparison == 0)
					comparison = static_cast<int>(a.unsaved) - static_cast<int>(b.unsaved);
				break;
			default:
				break;
			}
			// The form ID breaks ties, so equal keys keep a fixed order.
			if (comparison == 0)
				comparison = a.formId < b.formId ? -1 : (a.formId > b.formId ? 1 : 0);
			return ascending ? comparison < 0 : comparison > 0;
		});
	}

	void RebuildView(FormListState& state, const Context& context)
	{
		EditorWindow& editor = *context.editor;
		const auto& settings = editor.settings;
		const WidgetVec& widgets = *context.widgets;

		std::vector<Widget*> all;
		all.reserve(widgets.size());
		for (const auto& widget : widgets)
			all.push_back(widget.get());
		context.refreshSavedFiles(all);

		state.rows.clear();
		state.total = static_cast<int>(all.size());
		state.favoriteCount = state.flaggedCount = state.savedCount = state.unsavedCount = state.presetCount = 0;
		std::map<std::string, int, decltype([](const std::string& a, const std::string& b) {
			return _stricmp(a.c_str(), b.c_str()) < 0;
		})>
			plugins;

		for (Widget* widget : all) {
			FormRow row;
			row.widget = widget;
			row.formId = widget->form ? widget->form->GetFormID() : 0;
			row.editorId = widget->GetEditorID();
			row.display = widget->HasFallbackEditorID() ? FallbackDisplay(*widget) : row.editorId;
			row.formIdHex = widget->GetFormID();
			row.file = widget->GetFilename();
			if (const auto marker = settings.markedRecords.find(row.editorId); marker != settings.markedRecords.end())
				row.marker = marker->second;
			row.favorite = editor.IsFavorite(row.editorId);
			row.hasSavedFile = context.hasSavedFile(widget);
			row.unsaved = widget->HasUnsavedChanges();
			if (auto packBadge = widget->GetPackBadge()) {
				row.fromPack = true;
				row.packBadgeColor = packBadge->color;
				row.packBadgeTooltip = std::move(packBadge->tooltip);
			}

			++plugins[row.file];
			state.favoriteCount += row.favorite;
			state.flaggedCount += !row.marker.empty();
			state.savedCount += row.hasSavedFile;
			state.unsavedCount += row.unsaved;
			state.presetCount += row.fromPack;

			if (!MatchesFilter(row, state) ||
				(state.onlyFavorites && !row.favorite) ||
				(state.onlyFlagged && row.marker.empty()) ||
				(state.onlySaved && !row.hasSavedFile) ||
				(state.onlyUnsaved && !row.unsaved) ||
				(state.onlyPreset && !row.fromPack) ||
				(!state.plugin.empty() && row.file != state.plugin))
				continue;
			state.rows.push_back(std::move(row));
		}

		state.plugins.assign(plugins.begin(), plugins.end());
		SortRows(state);
		state.viewKey = MakeViewKey(state, context);
		state.builtAt = ImGui::GetTime();
	}

	void RebuildViewIfNeeded(FormListState& state, const Context& context)
	{
		if (MakeViewKey(state, context) != state.viewKey || ImGui::GetTime() - state.builtAt > kViewRefreshSeconds)
			RebuildView(state, context);
	}

	std::vector<ActiveRecord> CollectActiveRecords(const Context& context)
	{
		EditorWindow& editor = *context.editor;
		std::vector<ActiveRecord> records;
		auto* sky = globals::game::sky;
		auto* weather = sky ? sky->currentWeather : nullptr;

		auto add = [&](RE::TESForm* form, WidgetVec& widgets, std::string suffix, bool outgoing, std::string_view category) {
			if (!form)
				return;
			const auto id = form->GetFormID();
			if (std::ranges::any_of(records, [id](const ActiveRecord& record) { return record.formId == id; }))
				return;
			WidgetVec* collection = &widgets;
			records.push_back({ context.resolveEditorId(form, widgets), std::move(suffix), id, outgoing, std::string(category),
				[id, collection]() {
					for (const auto& widget : *collection) {
						if (widget->form && widget->form->GetFormID() == id) {
							widget->SetOpen(true);
							widget->RequestFocus();
							break;
						}
					}
				} });
		};
		auto addTimesOfDay = [&](auto*(&fields)[RE::TESWeather::ColorTimes::kTotal], WidgetVec& widgets, std::string_view category) {
			for (int tod = 0; tod < RE::TESWeather::ColorTimes::kTotal; ++tod)
				add(fields[tod], widgets, TOD::GetPeriodName(tod), false, category);
		};

		const std::string& category = context.category;
		if (category == kWeather) {
			add(weather, editor.weatherWidgets, "", false, kWeather);
			if (sky && sky->lastWeather != weather)
				add(sky->lastWeather, editor.weatherWidgets, T(TKEY("transitioning"), "transitioning"), true, kWeather);
		} else if (category == kImageSpace) {
			if (weather)
				addTimesOfDay(weather->imageSpaces, editor.imageSpaceWidgets, kImageSpace);
		} else if (category == kLightingTemplate) {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && player->parentCell)
				add(player->parentCell->GetRuntimeData().lightingTemplate, editor.lightingTemplateWidgets, "", false, kLightingTemplate);
		} else if (category == kCellLighting) {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && player->parentCell && player->parentCell->IsInteriorCell()) {
				auto* cell = player->parentCell;
				const char* cellName = cell->GetName();
				records.push_back({ cellName && cellName[0] ? cellName : T(TKEY("unnamed_cell"), "[Unnamed Cell]"), "",
					cell->GetFormID(), false, std::string(kCellLighting), [&editor, cell]() { editor.OpenCellLighting(cell, false); } });
			}
		} else if (category == kVolumetricLighting) {
			if (weather)
				addTimesOfDay(weather->volumetricLighting, editor.volumetricLightingWidgets, kVolumetricLighting);
		} else if (category == kShaderParticle) {
			if (weather)
				add(weather->precipitationData, editor.precipitationWidgets, "", false, kShaderParticle);
		} else if (category == kLensFlare) {
			if (weather)
				add(weather->sunGlareLensFlare, editor.lensFlareWidgets, "", false, kLensFlare);
		} else if (category == kVisualEffect) {
			if (weather)
				add(weather->referenceEffect, editor.referenceEffectWidgets, "", false, kVisualEffect);
		}

		// Categories with nothing active point at the current weather instead.
		if (records.empty())
			add(weather, editor.weatherWidgets, "", false, kWeather);
		if (records.size() > kMaxActiveRecords)
			records.resize(kMaxActiveRecords);
		return records;
	}

	const char* GetActiveBadgeLabel(const ActiveRecord& record)
	{
		return record.outgoing ? T(TKEY("badge_outgoing"), "OUTGOING") : T(TKEY("badge_live"), "LIVE");
	}

	ImVec4 GetActiveBadgeColor(const ActiveRecord& record)
	{
		return record.outgoing ? Util::Colors::GetSecondary() : Util::Colors::GetSuccess();
	}

	const ActiveRecord* FindActive(const std::vector<ActiveRecord>& records, RE::FormID formId)
	{
		const auto it = std::ranges::find_if(records, [formId](const ActiveRecord& record) { return record.formId == formId; });
		return it != records.end() ? &*it : nullptr;
	}

	void DrawActiveStrip(FormListState& state, const Context& context, const std::vector<ActiveRecord>& records)
	{
		EditorWindow& editor = *context.editor;
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const ImVec4 live = Util::Colors::GetSuccess();

		ImGui::AlignTextToFramePadding();
		Util::Text::Secondary("%s", T(TKEY("active"), "Active:"));

		for (size_t i = 0; i < records.size(); ++i) {
			const ActiveRecord& record = records[i];
			ImGui::PushID(static_cast<int>(i));

			const std::string label = record.suffix.empty() ? record.label : std::format("{} \xC2\xB7 {}", record.label, record.suffix);
			Icons::GlyphRef icon = context.icon;
			ImVec4 iconColor = Util::Colors::GetSecondary();
			if (record.category == kWeather) {
				auto* weather = RE::TESForm::LookupByID<RE::TESWeather>(record.formId);
				icon = WeatherTypeIcons::ResolveGlyph(weather, icon);
				iconColor = CSEditor::GetWeatherTypeColor(weather);
			}

			const float width = BrowserUI::MeasureChip(label.c_str(), icon.IsValid()) + spacing + BrowserUI::IconButtonSize();
			ImGui::SameLine();
			if (ImGui::GetContentRegionAvail().x < width)
				ImGui::NewLine();

			if (BrowserUI::Chip("##chip", label.c_str(), icon, &iconColor, record.outgoing ? nullptr : &live)) {
				if (record.category == kCellLighting)
					record.open();
				else
					JumpTo(state, editor, record.category, record.formId);
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				record.open();
			Util::AddTooltip(T(TKEY("active_chip_tooltip"), "Click to find it in its list. Double-click to open its editor."));

			ImGui::SameLine(0.0f, spacing);
			if (BrowserUI::IconButton("##open", Icons::TI(ICON_TI_EXTERNAL_LINK), T(TKEY("open_editor_tooltip"), "Open its editor window.")))
				record.open();
			ImGui::PopID();
		}
	}

	void DrawToolbar(FormListState& state)
	{
		const auto& style = ImGui::GetStyle();
		const float scale = Util::GetUIScale();
		const float spacing = style.ItemSpacing.x;

		float scopeWidth = 0.0f;
		for (int scope = 0; scope < static_cast<int>(FilterScope::Count); ++scope)
			scopeWidth = std::max(scopeWidth, ImGui::CalcTextSize(GetScopeName(static_cast<FilterScope>(scope))).x);
		scopeWidth += style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
		const float pluginWidth = kPluginComboWidth * scale;
		const float chipsWidth = BrowserUI::MeasureToggleChip(state.favoriteCount) + BrowserUI::MeasureToggleChip(state.flaggedCount) +
		                         BrowserUI::MeasureToggleChip(state.savedCount) + BrowserUI::MeasureToggleChip(state.unsavedCount) +
		                         BrowserUI::MeasureToggleChip(state.presetCount) + spacing * 4.0f;

		const float avail = ImGui::GetContentRegionAvail().x;
		const float fixedWidth = scopeWidth + pluginWidth + spacing * 2.0f;
		// When the row is too narrow the chips wrap under it rather than squeezing the search field.
		const bool chipsInline = avail - fixedWidth - chipsWidth - spacing >= kMinSearchWidth * scale;
		const float searchWidth = std::max(kMinSearchWidth * scale * 0.5f, avail - fixedWidth - (chipsInline ? chipsWidth + spacing : 0.0f));

		BrowserUI::SearchField("##ObjectFilter", state.filter, sizeof(state.filter), T(TKEY("filter_hint"), "Filter... (Ctrl+F)"), searchWidth, true);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(scopeWidth);
		int scope = static_cast<int>(state.scope);
		if (ImGui::Combo("##FilterBy", &scope, [](void*, int index) -> const char* { return GetScopeName(static_cast<FilterScope>(index)); }, nullptr, static_cast<int>(FilterScope::Count)))
			state.scope = static_cast<FilterScope>(scope);
		Util::AddTooltip(T(TKEY("filter_scope_tooltip"), "The field the filter text is matched against."));

		ImGui::SameLine();
		ImGui::SetNextItemWidth(pluginWidth);
		static constexpr const char* kPluginSearchId = "BrowserPluginCombo";
		const char* allPlugins = T(TKEY("all_plugins"), "All plugins");
		// The list may be wider than the combo: plugin names run long, and the search field sits on top.
		// Setting constraints replaces the combo's own height cap, so restore one with room for the search row.
		const float popupMaxHeight = ImGui::GetTextLineHeightWithSpacing() * kPluginPopupMaxRows + style.WindowPadding.y * 2.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(kPluginPopupMinWidth * scale, 0.0f), ImVec2(FLT_MAX, popupMaxHeight));
		if (ImGui::BeginCombo("##Plugin", state.plugin.empty() ? allPlugins : state.plugin.c_str())) {
			const auto searchText = Util::DrawComboSearchInput(kPluginSearchId);
			// Always listed: it is the way back out of a plugin filter, whatever was typed.
			if (ImGui::Selectable(allPlugins, state.plugin.empty())) {
				state.plugin.clear();
				Util::ClearComboSearch(kPluginSearchId);
			}
			bool anyMatch = false;
			for (const auto& [name, count] : state.plugins) {
				if (!searchText.empty() && !Util::StringMatchesSearch(name, searchText))
					continue;
				anyMatch = true;
				const bool selected = state.plugin == name;
				if (ImGui::Selectable(std::format("{}  ({})###{}", name, count, name).c_str(), selected)) {
					state.plugin = name;
					Util::ClearComboSearch(kPluginSearchId);
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			if (!anyMatch && !searchText.empty())
				ImGui::TextDisabled("%s", T(TKEY("plugin_search_empty"), "No plugins match."));
			ImGui::EndCombo();
		} else {
			Util::ClearComboSearch(kPluginSearchId);
		}

		if (chipsInline)
			ImGui::SameLine();
		if (BrowserUI::ToggleChip("##OnlyFavorites", BrowserUI::StarIcon(true), state.favoriteCount, state.onlyFavorites, FavoriteColor(),
				T(TKEY("favorites"), "Favorites")))
			state.onlyFavorites = !state.onlyFavorites;
		ImGui::SameLine();
		if (BrowserUI::ToggleChip("##OnlyFlagged", BrowserUI::FlagIcon(true), state.flaggedCount, state.onlyFlagged, Util::Colors::GetWarning(),
				T(TKEY("flagged"), "Flagged")))
			state.onlyFlagged = !state.onlyFlagged;
		ImGui::SameLine();
		if (BrowserUI::ToggleChip("##OnlySaved", BrowserUI::GlyphIcon(Icons::TI(ICON_TI_DEVICE_FLOPPY)), state.savedCount, state.onlySaved,
				Util::Colors::GetInfo(), T(TKEY("filter_saved_tooltip"), "Saved: has a JSON override on disk")))
			state.onlySaved = !state.onlySaved;
		ImGui::SameLine();
		if (BrowserUI::ToggleChip("##OnlyUnsaved", BrowserUI::DotIcon(), state.unsavedCount, state.onlyUnsaved, Util::Colors::GetWarning(),
				T(TKEY("filter_unsaved_tooltip"), "Unsaved: edited since it was last saved")))
			state.onlyUnsaved = !state.onlyUnsaved;
		ImGui::SameLine();
		if (BrowserUI::ToggleChip("##OnlyPreset", BrowserUI::GlyphIcon(Icons::TI(ICON_TI_STACK_2)), state.presetCount, state.onlyPreset,
				Util::Colors::GetInfo(), T(TKEY("filter_preset_tooltip"), "Preset: the active preset ships a file for it")))
			state.onlyPreset = !state.onlyPreset;
	}

	void OpenRecent(FormListState& state, const Context& context, const std::string& editorId)
	{
		for (const auto& widget : *context.widgets) {
			if (widget->GetEditorID() != editorId)
				continue;
			if (widget->form) {
				state.selectedByCategory[context.category] = widget->form->GetFormID();
				state.scrollToSelection = true;
			}
			OpenWidget(*context.editor, context.category, *widget);
			return;
		}
	}

	void DrawRecentRow(FormListState& state, const Context& context)
	{
		const auto& settings = context.editor->settings;
		const auto it = settings.recentWidgets.find(context.category);
		if (it == settings.recentWidgets.end() || it->second.empty())
			return;
		// A copy: opening one moves it to the front of the stored list.
		const std::vector<std::string> recent = it->second;

		const float scale = Util::GetUIScale();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

		ImGui::AlignTextToFramePadding();
		Util::Text::Secondary("%s", T(TKEY("recent"), "Recent:"));

		size_t shown = 0;
		for (; shown < recent.size(); ++shown) {
			const std::string label = FitText(recent[shown], kRecentChipMaxWidth * scale);
			const size_t remaining = recent.size() - shown - 1;
			const float overflowWidth = remaining ? BrowserUI::MeasureChip(std::format("+{}", remaining).c_str(), false) + spacing : 0.0f;
			ImGui::SameLine();
			if (ImGui::GetCursorScreenPos().x + BrowserUI::MeasureChip(label.c_str(), false) + overflowWidth > right)
				break;
			ImGui::PushID(static_cast<int>(shown));
			if (BrowserUI::Chip("##recent", label.c_str()))
				OpenRecent(state, context, recent[shown]);
			if (label != recent[shown])
				Util::AddTooltip(recent[shown].c_str());
			ImGui::PopID();
		}

		if (shown < recent.size()) {
			if (shown > 0)
				ImGui::SameLine();
			const std::string more = std::format("+{}", recent.size() - shown);
			if (BrowserUI::Chip("##recentMore", more.c_str()))
				ImGui::OpenPopup("##RecentMore");
			if (ImGui::BeginPopup("##RecentMore")) {
				for (size_t i = shown; i < recent.size(); ++i)
					if (ImGui::Selectable(std::format("{}##more{}", recent[i], i).c_str()))
						OpenRecent(state, context, recent[i]);
				ImGui::EndPopup();
			}
		}
	}

	void DrawWeatherTooltip(const Context& context, Widget* widget)
	{
		auto* weatherWidget = dynamic_cast<WeatherWidget*>(widget);
		if (!weatherWidget || !weatherWidget->weather)
			return;
		EditorWindow& editor = *context.editor;
		const auto& palette = Menu::GetSingleton()->GetTheme().StatusPalette;

		const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
		const ImVec2 pad = ImGui::GetStyle().WindowPadding;
		constexpr int kSectionHeaders = 2;
		constexpr int kTodValuesPerSection = 4;
		const float estimatedHeight = (kSectionHeaders + kTodValuesPerSection * 2) * lineHeight + ImGui::GetStyle().ItemSpacing.y + pad.y * 2.0f;
		Util::SetTooltipPositionNearMouse(estimatedHeight);
		if (!ImGui::BeginTooltip())
			return;
		ImGui::TextColored(palette.InfoColor, "%s", T(TKEY("imagespace_label"), "ImageSpace:"));
		for (int tod = 0; tod < RE::TESWeather::ColorTimes::kTotal; ++tod) {
			const auto name = context.resolveEditorId(weatherWidget->weather->imageSpaces[tod], editor.imageSpaceWidgets);
			ImGui::Text("  %s: %s", TOD::GetPeriodName(tod), name.empty() ? T(TKEY("none_filter"), "None") : name.c_str());
		}
		ImGui::Spacing();
		ImGui::TextColored(palette.InfoColor, "%s", T(TKEY("volumetric_lighting_label"), "Volumetric Lighting:"));
		for (int tod = 0; tod < RE::TESWeather::ColorTimes::kTotal; ++tod) {
			const auto name = context.resolveEditorId(weatherWidget->weather->volumetricLighting[tod], editor.volumetricLightingWidgets);
			ImGui::Text("  %s: %s", TOD::GetPeriodName(tod), name.empty() ? T(TKEY("none_filter"), "None") : name.c_str());
		}
		ImGui::EndTooltip();
	}

	void SetMarker(FormListState& state, EditorWindow& editor, const std::string& editorId, const std::string* marker)
	{
		if (marker)
			editor.settings.markedRecords[editorId] = *marker;
		else
			editor.settings.markedRecords.erase(editorId);
		editor.Save();
		state.Invalidate();
	}

	void DrawRowContextMenu(FormListState& state, const Context& context, const FormRow& row)
	{
		if (!ImGui::BeginPopupContextItem("##RowMenu"))
			return;
		EditorWindow& editor = *context.editor;
		state.selectedByCategory[context.category] = row.formId;

		if (ImGui::MenuItem(T(TKEY("open_editor"), "Open editor")))
			OpenWidget(editor, context.category, *row.widget);
		if (ImGui::MenuItem(T(TKEY("toggle_favorite"), "Favourite"), nullptr, row.favorite))
			editor.ToggleFavorite(row.editorId);

		ImGui::SeparatorText(T(TKEY("status"), "Status"));
		for (const auto& [name, color] : editor.settings.recordMarkers)
			if (ImGui::MenuItem(name.c_str(), nullptr, row.marker == name))
				SetMarker(state, editor, row.editorId, &name);
		if (ImGui::MenuItem(T(TKEY("remove"), "Remove"), nullptr, false, !row.marker.empty()))
			SetMarker(state, editor, row.editorId, nullptr);

		ImGui::Separator();
		if (ImGui::MenuItem(T(TKEY("copy_editor_id"), "Copy editor ID")))
			ImGui::SetClipboardText(row.editorId.c_str());
		if (ImGui::MenuItem(T(TKEY("copy_form_id"), "Copy form ID")))
			ImGui::SetClipboardText(std::format("0x{}", row.formIdHex).c_str());
		if (ImGui::MenuItem(T(TKEY("copy_save_key"), "Copy save key")))
			ImGui::SetClipboardText(row.widget->GetSaveKey().c_str());

		ImGui::Separator();
		if (ImGui::MenuItem(T(TKEY("show_in_folder"), "Show in folder"), nullptr, false, row.hasSavedFile))
			RevealInExplorer(row.widget->GetSaveFilePath());
		if (ImGui::MenuItem(T(TKEY("delete_json_file"), "Delete JSON file"), nullptr, false, row.hasSavedFile))
			context.requestDeleteSavedFile(row.widget);
		ImGui::EndPopup();
	}

	/// Preset / saved / unsaved / open markers, each in a fixed slot so they line up down the column.
	void DrawStateIcons(const FormRow& row)
	{
		const float box = ImGui::GetFontSize();
		const float gap = ImGui::GetStyle().ItemSpacing.x * 0.5f;
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		const float y = cursor.y + (ImGui::GetFrameHeight() - box) * 0.5f;
		float x = cursor.x + ImGui::GetContentRegionAvail().x - box;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const bool windowHovered = ImGui::IsWindowHovered();

		auto slot = [&](bool on, const BrowserUI::IconPainter& paint, ImU32 color, const char* tooltip) {
			if (on) {
				const ImVec2 min(x, y);
				const ImVec2 max(x + box, y + box);
				paint(drawList, ImVec2(x + box * 0.5f, y + box * 0.5f), box, color);
				if (windowHovered && ImGui::IsMouseHoveringRect(min, max))
					ImGui::SetTooltip("%s", tooltip);
			}
			x -= box + gap;
		};

		const bool open = row.widget->IsOpen();
		slot(open, BrowserUI::GlyphIcon(Icons::TI(ICON_TI_EXTERNAL_LINK)), ImGui::GetColorU32(Util::Colors::GetAccent()),
			T(TKEY("state_open_tooltip"), "Its editor window is open"));
		// Checked live rather than from the cached row, so the dot follows edits, saves and undo at once.
		slot(row.widget->HasUnsavedChanges(), BrowserUI::DotIcon(), ImGui::GetColorU32(Util::Colors::GetUnsaved()),
			T(TKEY("state_unsaved_tooltip"), "Edited since it was last saved"));
		slot(row.hasSavedFile, BrowserUI::GlyphIcon(Icons::TI(ICON_TI_DEVICE_FLOPPY)), ImGui::GetColorU32(Util::Colors::GetSecondary()),
			T(TKEY("state_saved_tooltip"), "Has a JSON override on disk"));
		slot(row.fromPack, BrowserUI::GlyphIcon(Icons::TI(ICON_TI_STACK_2)), ImGui::GetColorU32(row.packBadgeColor),
			row.packBadgeTooltip.c_str());
	}

	void DrawRow(FormListState& state, const Context& context, const FormRow& row, RE::FormID selectedId,
		const std::vector<ActiveRecord>& active)
	{
		EditorWindow& editor = *context.editor;
		const float rowHeight = BrowserUI::RowHeight();
		const bool selected = selectedId != 0 && row.formId == selectedId;

		ImGui::PushID(static_cast<int>(row.formId));
		ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

		// Status stripe and favourite star
		ImGui::TableSetColumnIndex(0);
		if (!row.marker.empty()) {
			if (const auto color = editor.settings.recordMarkers.find(row.marker); color != editor.settings.recordMarkers.end())
				BrowserUI::RowStripe(ImGui::GetColorU32(color->second), rowHeight);
		}
		// The star stays out of keyboard navigation so the arrow keys walk the rows.
		ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
		const ImU32 starColor = row.favorite ? ImGui::GetColorU32(FavoriteColor()) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().CellPadding.x);
		if (BrowserUI::PaintedIconButton("##fav", BrowserUI::StarIcon(row.favorite), starColor, T(TKEY("toggle_favorite"), "Favourite")))
			editor.ToggleFavorite(row.editorId);
		ImGui::PopItemFlag();

		// The row selectable spans every column; the name cell's content is drawn over it.
		ImGui::TableSetColumnIndex(1);
		const ImVec2 cellStart = ImGui::GetCursorScreenPos();
		const bool pressed = Util::TableRowSelectable("##row", selected,
			ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap |
				ImGuiSelectableFlags_SelectOnNav);
		const bool rowHovered = ImGui::IsItemHovered();
		if (pressed) {
			state.selectedByCategory[context.category] = row.formId;
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || IsActivateKeyPressed())
				OpenWidget(editor, context.category, *row.widget);
		}
		DrawRowContextMenu(state, context, row);
		if (rowHovered && context.category == kWeather && !editor.settings.browserShowInspector)
			DrawWeatherTooltip(context, row.widget);

		float nameX = cellStart.x;
		if (context.category == kWeather) {
			if (auto* weatherWidget = dynamic_cast<WeatherWidget*>(row.widget); weatherWidget && weatherWidget->weather) {
				const float box = ImGui::GetFontSize();
				WeatherTypeIcons::Draw(WeatherTypeIcons::Resolve(weatherWidget->weather), ImGui::GetWindowDrawList(),
					ImVec2(cellStart.x, cellStart.y + (ImGui::GetFrameHeight() - box) * 0.5f), box,
					ImGui::GetColorU32(CSEditor::GetWeatherTypeColor(weatherWidget->weather)));
				nameX += box + ImGui::GetStyle().ItemSpacing.x;
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(nameX, cellStart.y));
		ImGui::AlignTextToFramePadding();
		const ActiveRecord* activeRecord = FindActive(active, row.formId);
		const char* badge = activeRecord ? GetActiveBadgeLabel(*activeRecord) : nullptr;
		const float badgeWidth = badge ? Util::MeasureBadgeWidth(badge) + ImGui::GetStyle().ItemSpacing.x : 0.0f;
		BrowserUI::EllipsizedText(row.display.c_str(), std::max(1.0f, ImGui::GetContentRegionAvail().x - badgeWidth), ImGui::GetStyleColorVec4(ImGuiCol_Text));
		if (badge)
			BrowserUI::InlineBadge(badge, GetActiveBadgeColor(*activeRecord));

		if (ImGui::TableSetColumnIndex(2)) {
			ImGui::AlignTextToFramePadding();
			BrowserUI::MetaText(row.file.c_str());
		}
		if (ImGui::TableSetColumnIndex(3)) {
			ImGui::AlignTextToFramePadding();
			BrowserUI::MetaText(row.formIdHex.c_str());
		}
		if (ImGui::TableSetColumnIndex(4) && !row.marker.empty()) {
			ImGui::AlignTextToFramePadding();
			const auto color = editor.settings.recordMarkers.find(row.marker);
			if (color != editor.settings.recordMarkers.end()) {
				Util::DrawInlineIndicatorDot(ImGui::GetColorU32(color->second), true);
				ImGui::SameLine();
			}
			BrowserUI::MetaText(row.marker.c_str());
		}
		if (ImGui::TableSetColumnIndex(5))
			DrawStateIcons(row);

		ImGui::PopID();
	}

	void DrawList(FormListState& state, const Context& context, const ImVec2& size, const std::vector<ActiveRecord>& active)
	{
		const auto& style = ImGui::GetStyle();
		const float scale = Util::GetUIScale();
		const float rowHeight = BrowserUI::RowHeight();
		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
		                                   ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate |
		                                   ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

		BrowserUI::RowShadeScope shade;
		if (!ImGui::BeginTable("##FormList", 6, kFlags, size))
			return;

		const char* stateLabel = T(TKEY("state_column"), "State");
		const float stateWidth = std::max(ImGui::GetFontSize() * kStateSlotCount + style.ItemSpacing.x * 0.5f * (kStateSlotCount - 1),
			ImGui::CalcTextSize(stateLabel).x + ImGui::GetFontSize());
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoResize,
			ImGui::GetFrameHeight() + style.CellPadding.x, ColFav);
		ImGui::TableSetupColumn(T(TKEY("editor_id"), "Editor ID"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 3.0f, ColName);
		ImGui::TableSetupColumn(T(TKEY("file"), "File"), ImGuiTableColumnFlags_WidthStretch, 1.4f, ColFile);
		ImGui::TableSetupColumn(T(TKEY("form_id"), "Form ID"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide,
			ImGui::CalcTextSize("00000000").x + style.CellPadding.x, ColFormId);
		ImGui::TableSetupColumn(T(TKEY("status"), "Status"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide,
			kStatusColumnWidth * scale, ColStatus);
		ImGui::TableSetupColumn(stateLabel, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, stateWidth, ColState);
		ImGui::TableHeadersRow();

		if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
			SortKey key = SortKey::None;
			bool ascending = true;
			if (specs->SpecsCount > 0) {
				switch (specs->Specs[0].ColumnUserID) {
				case ColName:
					key = SortKey::EditorID;
					break;
				case ColFile:
					key = SortKey::File;
					break;
				case ColFormId:
					key = SortKey::FormID;
					break;
				case ColStatus:
					key = SortKey::Status;
					break;
				case ColState:
					key = SortKey::Saved;
					break;
				default:
					break;
				}
				ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
			}
			state.sortKey = key;
			state.sortAscending = ascending;
			specs->SpecsDirty = false;
			RebuildViewIfNeeded(state, context);
		}

		const RE::FormID selectedId = GetSelectedId(state, context.category);
		int scrollIndex = -1;
		if (state.scrollToSelection) {
			for (int i = 0; i < static_cast<int>(state.rows.size()); ++i) {
				if (state.rows[i].formId == selectedId) {
					scrollIndex = i;
					break;
				}
			}
			state.scrollToSelection = false;
		}

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(state.rows.size()), rowHeight);
		if (scrollIndex >= 0)
			clipper.IncludeItemByIndex(scrollIndex);
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				DrawRow(state, context, state.rows[i], selectedId, active);
				if (i == scrollIndex)
					ImGui::SetScrollHereY(0.5f);
			}
		}

		if (state.rows.empty()) {
			ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
			ImGui::TableSetColumnIndex(1);
			ImGui::AlignTextToFramePadding();
			Util::Text::Disabled("%s", T(TKEY("list_empty"), "No records match the filters."));
			ImGui::SameLine();
			if (BrowserUI::Link("##ClearFilters", T(TKEY("clear_filters"), "Clear filters")))
				state.ResetFilters();
		}

		ImGui::EndTable();
	}

	/// A record reference that selects it in its own list, or opens its editor with Ctrl held.
	void FormLink(FormListState& state, const Context& context, RE::TESForm* form, std::string_view category, WidgetVec& widgets, const char* id)
	{
		if (!form) {
			Util::Text::Disabled("%s", T(TKEY("none_filter"), "None"));
			return;
		}
		const std::string label = context.resolveEditorId(form, widgets);
		if (BrowserUI::Link(id, label.c_str())) {
			FollowLink(state, *context.editor, std::string(category), FindWidget(widgets, form->GetFormID()), form->GetFormID());
		}
		Util::AddTooltip(T(TKEY("form_link_tooltip"), "Click to find it in its list. Ctrl+click to open its editor."));
	}

	void DrawIdentity(FormListState& state, const Context& context, Widget& widget, const std::vector<ActiveRecord>& active)
	{
		EditorWindow& editor = *context.editor;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float button = BrowserUI::IconButtonSize();
		const std::string editorId = widget.GetEditorID();
		const bool favorite = editor.IsFavorite(editorId);

		// Name, with the favourite toggle at the right
		ImGui::AlignTextToFramePadding();
		Icons::GlyphRef icon = context.icon;
		ImVec4 iconColor = Util::Colors::GetAccent();
		if (auto* weatherWidget = dynamic_cast<WeatherWidget*>(&widget); weatherWidget && weatherWidget->weather) {
			icon = WeatherTypeIcons::ResolveGlyph(weatherWidget->weather, icon);
			iconColor = CSEditor::GetWeatherTypeColor(weatherWidget->weather);
		}
		if (icon) {
			Icons::FontGuard font(icon);
			ImGui::TextColored(iconColor, "%s", icon.utf8);
			ImGui::SameLine();
		}
		BrowserUI::EllipsizedText(editorId.c_str(), std::max(1.0f, ImGui::GetContentRegionAvail().x - button - spacing), ImGui::GetStyleColorVec4(ImGuiCol_Text));
		BrowserUI::RightAlign(button);
		if (BrowserUI::PaintedIconButton("##InspectorFavorite", BrowserUI::StarIcon(favorite),
				favorite ? ImGui::GetColorU32(FavoriteColor()) : ImGui::GetColorU32(ImGuiCol_TextDisabled), T(TKEY("toggle_favorite"), "Favourite")))
			editor.ToggleFavorite(editorId);

		// Form ID and plugin, with a copy button
		const std::string formId = widget.GetFormID();
		ImGui::AlignTextToFramePadding();
		BrowserUI::MetaText(std::format("{} \xC2\xB7 {}", formId, widget.GetFilename()).c_str(), std::max(1.0f, ImGui::GetContentRegionAvail().x - button - spacing));
		BrowserUI::RightAlign(button);
		if (BrowserUI::IconButton("##CopyFormId", Icons::TI(ICON_TI_COPY), T(TKEY("copy_form_id"), "Copy form ID")))
			ImGui::SetClipboardText(std::format("0x{}", formId).c_str());

		// State badges
		bool any = false;
		auto badge = [&any](const char* label, const ImVec4& tint) {
			if (any)
				ImGui::SameLine();
			Util::Badge(label, tint);
			any = true;
		};
		if (const ActiveRecord* record = widget.form ? FindActive(active, widget.form->GetFormID()) : nullptr)
			badge(GetActiveBadgeLabel(*record), GetActiveBadgeColor(*record));
		if (widget.HasUnsavedChanges())
			badge(T(TKEY("badge_unsaved"), "Unsaved"), Util::Colors::GetWarning());
		if (context.hasSavedFile(&widget))
			badge(T(TKEY("badge_saved"), "Saved"), Util::Colors::GetInfo());
		if (const auto packBadge = widget.GetPackBadge()) {
			badge(T(TKEY("badge_preset"), "Preset"), packBadge->color);
			Util::AddTooltip(packBadge->tooltip.c_str());
		}
		if (widget.HasFallbackEditorID())
			badge(T(TKEY("badge_no_editor_id"), "No editor ID"), Util::Colors::GetSecondary());
		if (widget.IsOpen())
			badge(T(TKEY("badge_open"), "Open"), Util::Colors::GetAccent());

		// Status marker
		ImGui::Spacing();
		auto& markers = editor.settings.markedRecords;
		const auto current = markers.find(editorId);
		const std::string marker = current != markers.end() ? current->second : std::string();
		ImGui::AlignTextToFramePadding();
		Util::Text::Secondary("%s", T(TKEY("status"), "Status"));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##InspectorStatus", marker.empty() ? T(TKEY("none"), "None") : marker.c_str())) {
			if (ImGui::Selectable(T(TKEY("none"), "None"), marker.empty()) && !marker.empty())
				SetMarker(state, editor, editorId, nullptr);
			for (const auto& [name, color] : editor.settings.recordMarkers) {
				ImGui::PushID(name.c_str());
				Util::DrawInlineIndicatorDot(ImGui::GetColorU32(color), true);
				ImGui::SameLine();
				if (ImGui::Selectable(name.c_str(), marker == name))
					SetMarker(state, editor, editorId, &name);
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}

	void DrawWeatherLinks(FormListState& state, const Context& context, WeatherWidget& widget)
	{
		auto* weather = widget.weather;
		if (!weather)
			return;
		EditorWindow& editor = *context.editor;
		const float scale = Util::GetUIScale();

		ImGui::Spacing();
		BrowserUI::SectionLabel(T(TKEY("time_of_day"), "Time of day"));
		if (ImGui::BeginTable("##TimeOfDay", 3, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableSetupColumn("##period", ImGuiTableColumnFlags_WidthFixed, kPeriodColumnWidth * scale);
			ImGui::TableSetupColumn("##imagespace", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("##volumetric", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(1);
			Util::Text::Disabled("%s", T(TKEY("category_imagespace"), "ImageSpace"));
			ImGui::TableSetColumnIndex(2);
			Util::Text::Disabled("%s", T(TKEY("category_volumetric_lighting"), "Volumetric Lighting"));
			for (int tod = 0; tod < RE::TESWeather::ColorTimes::kTotal; ++tod) {
				ImGui::PushID(tod);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				Util::Text::Secondary("%s", TOD::GetPeriodName(tod));
				ImGui::TableSetColumnIndex(1);
				FormLink(state, context, weather->imageSpaces[tod], kImageSpace, editor.imageSpaceWidgets, "##imagespace");
				ImGui::TableSetColumnIndex(2);
				FormLink(state, context, weather->volumetricLighting[tod], kVolumetricLighting, editor.volumetricLightingWidgets, "##volumetric");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::Spacing();
		BrowserUI::SectionLabel(T(TKEY("linked_forms"), "Linked forms"));
		if (ImGui::BeginTable("##LinkedForms", 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("##link", ImGuiTableColumnFlags_WidthStretch);
			auto row = [&](const char* label, RE::TESForm* form, std::string_view category, WidgetVec& widgets, const char* id) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				Util::Text::Secondary("%s", label);
				ImGui::TableSetColumnIndex(1);
				FormLink(state, context, form, category, widgets, id);
			};
			row(T(TKEY("category_lens_flare"), "Lens Flare"), weather->sunGlareLensFlare, kLensFlare, editor.lensFlareWidgets, "##lensflare");
			row(T(TKEY("category_precipitation"), "Precipitation"), weather->precipitationData, kShaderParticle, editor.precipitationWidgets, "##precipitation");
			row(T(TKEY("category_visual_effect"), "Visual Effect"), weather->referenceEffect, kVisualEffect, editor.referenceEffectWidgets, "##visualeffect");
			ImGui::EndTable();
		}
	}

	void RefreshUsage(FormListState& state, const Context& context, RE::TESForm* form)
	{
		state.usage.clear();
		const std::string& category = context.category;
		for (const auto& entry : context.editor->weatherWidgets) {
			auto* weather = static_cast<WeatherWidget*>(entry.get())->weather;
			if (!weather)
				continue;
			std::string slots;
			auto addSlot = [&slots](const char* name) {
				if (!slots.empty())
					slots += ", ";
				slots += name;
			};
			for (int tod = 0; tod < RE::TESWeather::ColorTimes::kTotal; ++tod) {
				if ((category == kImageSpace && weather->imageSpaces[tod] == form) ||
					(category == kVolumetricLighting && weather->volumetricLighting[tod] == form))
					addSlot(TOD::GetPeriodName(tod));
			}
			if ((category == kShaderParticle && weather->precipitationData == form) ||
				(category == kLensFlare && weather->sunGlareLensFlare == form) ||
				(category == kVisualEffect && weather->referenceEffect == form))
				addSlot(context.title);
			if (!slots.empty())
				state.usage.push_back({ entry.get(), std::move(slots) });
		}
		state.usageFor = form->GetFormID();
		state.usageCategory = category;
		state.usageBuiltAt = state.builtAt;
	}

	void DrawWeatherUsage(FormListState& state, const Context& context, Widget& widget)
	{
		RE::TESForm* form = widget.form;
		if (!form)
			return;
		// Recomputed with the list, so a weather re-pointed in its editor shows up within a refresh.
		if (state.usageFor != form->GetFormID() || state.usageCategory != context.category || state.usageBuiltAt != state.builtAt)
			RefreshUsage(state, context, form);

		ImGui::Spacing();
		BrowserUI::SectionLabel(I18n::GetSingleton()->Format(TKEY("used_by_weathers"), { { "count", std::to_string(state.usage.size()) } },
														"Used by {count} weathers")
				.c_str());
		if (state.usage.empty()) {
			Util::Text::WrappedDisabled("%s", T(TKEY("used_by_none"), "No weather uses this record."));
			return;
		}

		EditorWindow& editor = *context.editor;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		for (size_t i = 0; i < state.usage.size(); ++i) {
			if (i == kUsageListLimit) {
				Util::Text::Disabled("%s", I18n::GetSingleton()->Format(TKEY("used_by_more"), { { "count", std::to_string(state.usage.size() - i) } },
																   "and {count} more")
											   .c_str());
				break;
			}
			const auto& use = state.usage[i];
			ImGui::PushID(static_cast<int>(i));
			const std::string name = use.weather->GetEditorID();
			const float avail = ImGui::GetContentRegionAvail().x;
			const float slotsWidth = std::min(ImGui::CalcTextSize(use.slots.c_str()).x + spacing, avail * 0.45f);
			if (BrowserUI::Link("##weather", name.c_str(), avail - slotsWidth) && use.weather->form)
				FollowLink(state, editor, std::string(kWeather), use.weather, use.weather->form->GetFormID());
			Util::AddTooltip(T(TKEY("form_link_tooltip"), "Click to find it in its list. Ctrl+click to open its editor."));
			ImGui::SameLine();
			BrowserUI::MetaText(use.slots.c_str());
			ImGui::PopID();
		}
	}

	void DrawLightingTemplateUsage(Widget& widget)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->parentCell : nullptr;
		if (!cell || !widget.form || cell->GetRuntimeData().lightingTemplate != widget.form)
			return;
		ImGui::Spacing();
		Util::Badge(T(TKEY("used_by_current_cell"), "Used by the current cell"), Util::Colors::GetSuccess());
	}

	void DrawSavedOverride(const Context& context, Widget& widget)
	{
		ImGui::Spacing();
		BrowserUI::SectionLabel(T(TKEY("saved_override"), "Saved override"));
		if (!context.hasSavedFile(&widget)) {
			Util::Text::WrappedDisabled("%s", T(TKEY("no_saved_override"), "Not saved yet. Saving from its editor writes a JSON override."));
			return;
		}
		const std::string path = widget.GetSaveFilePath();
		const std::string fileName = std::filesystem::path(path).filename().string();
		ImGui::AlignTextToFramePadding();
		{
			Icons::FontGuard font(Icons::TI(ICON_TI_DEVICE_FLOPPY));
			Util::Text::Secondary("%s", ICON_TI_DEVICE_FLOPPY);
		}
		ImGui::SameLine();
		BrowserUI::MetaText(fileName.c_str());
		Util::AddTooltip(path.c_str());

		if (Icons::LabeledButton("##RevealSaved", Icons::TI(ICON_TI_FOLDER_OPEN), T(TKEY("show_in_folder"), "Show in folder")))
			RevealInExplorer(path);
		ImGui::SameLine();
		{
			auto _style = Util::StatusTextButtonStyle(Util::Colors::GetError());
			if (Icons::LabeledButton("##DeleteSaved", Icons::TI(ICON_TI_TRASH), T(TKEY("delete"), "Delete")))
				context.requestDeleteSavedFile(&widget);
		}
		Util::AddTooltip(T(TKEY("delete_json_file"), "Delete JSON file"));
	}

	void DrawInspectorActions(const Context& context, Widget& widget)
	{
		EditorWindow& editor = *context.editor;
		auto* weatherWidget = context.category == kWeather ? dynamic_cast<WeatherWidget*>(&widget) : nullptr;
		const bool canLock = weatherWidget && weatherWidget->weather;
		const float lockWidth = canLock ? Util::GetLockStatusBadgeSize() + ImGui::GetStyle().ItemSpacing.x : 0.0f;

		{
			auto _style = Util::StatusButtonStyle(Util::Colors::GetAccent());
			if (Icons::LabeledButton("##OpenEditor", Icons::TI(ICON_TI_EXTERNAL_LINK), T(TKEY("open_editor"), "Open editor"),
					ImVec2(ImGui::GetContentRegionAvail().x - lockWidth, 0.0f)))
				OpenWidget(editor, context.category, widget);
		}
		if (!canLock)
			return;
		ImGui::SameLine();
		const bool locked = editor.IsWeatherLockedByUser() && editor.GetLockedWeather() == weatherWidget->weather;
		const char* tooltip = !EditorWindow::AreWeatherLockHooksInstalled() ?
		                          T(TKEY("weather_lock_hooks_unavailable"), "Weather-lock hooks failed to install; the lock still works but weather may briefly flash before correcting") :
		                          (locked ? T(TKEY("unlock_weather"), "Unlock Weather") : T(TKEY("force_this_weather"), "Force This Weather"));
		if (Util::LockStatusBadgeButton("##InspectorLock", locked, tooltip)) {
			if (locked)
				editor.ReleaseUserWeatherLock();
			else
				editor.LockWeatherForUser(weatherWidget->weather);
		}
	}

	/// A rounded, softly tinted child window; the caller ends it whatever this returns.
	bool BeginCard(const char* id, const ImVec2& size, ImGuiChildFlags flags = ImGuiChildFlags_None)
	{
		const auto& style = ImGui::GetStyle();
		ImVec4 background = style.Colors[ImGuiCol_FrameBg];
		background.w *= kInspectorBgAlpha;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.FrameRounding);
		const bool visible = ImGui::BeginChild(id, size, flags | ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		return visible;
	}

	void DrawInspector(FormListState& state, const Context& context, const ImVec2& size, const std::vector<ActiveRecord>& active)
	{
		const auto& style = ImGui::GetStyle();
		if (BeginCard("##Inspector", size)) {
			Widget* widget = FindWidget(*context.widgets, GetSelectedId(state, context.category));
			if (!widget) {
				BrowserUI::EmptyState(T(TKEY("inspector_empty"), "Nothing selected"),
					T(TKEY("inspector_empty_hint"), "Select a record to see its details. Double-click a row or press Enter to open its editor."));
			} else {
				// The actions stay pinned under a scrolling body.
				const float footerHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f + style.SeparatorTextBorderSize;
				if (ImGui::BeginChild("##InspectorBody", ImVec2(0.0f, -footerHeight))) {
					DrawIdentity(state, context, *widget, active);
					if (context.category == kWeather) {
						if (auto* weatherWidget = dynamic_cast<WeatherWidget*>(widget))
							DrawWeatherLinks(state, context, *weatherWidget);
					} else if (context.category == kLightingTemplate) {
						DrawLightingTemplateUsage(*widget);
					} else {
						DrawWeatherUsage(state, context, *widget);
					}
					DrawSavedOverride(context, *widget);
				}
				ImGui::EndChild();
				ImGui::Separator();
				DrawInspectorActions(context, *widget);
			}
		}
		ImGui::EndChild();
	}

	void DrawCellLightingCard(FormListState& state, const Context& context)
	{
		EditorWindow& editor = *context.editor;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->parentCell : nullptr;
		if (!cell) {
			BrowserUI::EmptyState(T(TKEY("player_cell_unavailable"), "Player cell not available."));
			return;
		}
		if (!cell->IsInteriorCell()) {
			BrowserUI::EmptyState(T(TKEY("cell_lighting_interior_only"), "Cell Lighting is only available for interior cells."),
				T(TKEY("currently_exterior_cell"), "You are currently in an exterior cell."));
			return;
		}

		ImGui::Spacing();
		if (BeginCard("##CellCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY)) {
			const char* cellName = cell->GetName();
			{
				Icons::FontGuard font(context.icon);
				ImGui::TextColored(Util::Colors::GetAccent(), "%s", context.icon.utf8);
			}
			ImGui::SameLine();
			BrowserUI::EllipsizedText(cellName && cellName[0] ? cellName : T(TKEY("unnamed_cell"), "[Unnamed Cell]"), 0.0f,
				ImGui::GetStyleColorVec4(ImGuiCol_Text));

			auto* file = cell->GetFile(0);
			BrowserUI::MetaText(std::format("0x{:08X} \xC2\xB7 {}", cell->GetFormID(), file ? file->fileName : "").c_str());

			Util::Badge(T(TKEY("interior_cell"), "Interior Cell"), Util::Colors::GetInfo());
			const bool open = editor.currentCellLightingWidget && editor.currentCellLightingWidget->cell == cell &&
			                  editor.currentCellLightingWidget->IsOpen();
			if (open) {
				ImGui::SameLine();
				Util::Badge(T(TKEY("badge_open"), "Open"), Util::Colors::GetAccent());
			}

			if (auto* lightingTemplate = cell->GetRuntimeData().lightingTemplate) {
				ImGui::Spacing();
				Util::Text::Secondary("%s", T(TKEY("category_lighting_template"), "Lighting Template"));
				ImGui::SameLine();
				FormLink(state, context, lightingTemplate, kLightingTemplate, editor.lightingTemplateWidgets, "##CellTemplate");
			}

			ImGui::Spacing();
			auto _style = Util::StatusButtonStyle(Util::Colors::GetAccent());
			if (Icons::LabeledButton("##OpenCellLighting", Icons::TI(ICON_TI_EXTERNAL_LINK), T(TKEY("open_editor"), "Open editor")))
				editor.OpenCellLighting(cell, true);
		}
		ImGui::EndChild();

		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
			IsEnterPressed())
			editor.OpenCellLighting(cell, true);
	}
}

FormListPage::WidgetVec* FormListPage::GetCollection(EditorWindow& editor, const std::string& category)
{
	if (category == kWeather)
		return &editor.weatherWidgets;
	if (category == kImageSpace)
		return &editor.imageSpaceWidgets;
	if (category == kLightingTemplate)
		return &editor.lightingTemplateWidgets;
	if (category == kVolumetricLighting)
		return &editor.volumetricLightingWidgets;
	if (category == kShaderParticle)
		return &editor.precipitationWidgets;
	if (category == kLensFlare)
		return &editor.lensFlareWidgets;
	if (category == kVisualEffect)
		return &editor.referenceEffectWidgets;
	return nullptr;
}

void FormListPage::Draw(Browser::FormListState& state, const Context& context)
{
	EditorWindow& editor = *context.editor;
	const bool hasList = context.widgets != nullptr;
	if (hasList)
		RebuildViewIfNeeded(state, context);

	// Page header: title, record count, help and the inspector toggle
	std::string subtitle;
	if (!hasList)
		subtitle = T(TKEY("current_interior_cell"), "Current interior cell");
	else if (static_cast<int>(state.rows.size()) == state.total)
		subtitle = I18n::GetSingleton()->Format(TKEY("records_count"), { { "count", std::to_string(state.total) } }, "{count} records");
	else
		subtitle = I18n::GetSingleton()->Format(TKEY("records_filtered"),
			{ { "shown", std::to_string(state.rows.size()) }, { "total", std::to_string(state.total) } }, "{shown} of {total} records");
	BrowserUI::PageHeader(context.icon, context.title, subtitle.c_str());
	if (hasList) {
		BrowserUI::RightAlign(ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x + BrowserUI::IconButtonSize());
		Util::HelpMarker(T(TKEY("browser_help"),
			"Click a row to inspect it; double-click it or press Enter to open its editor.\n"
			"The arrow keys move through the list. Ctrl+F focuses the filter.\n"
			"The filter matches the field picked beside it; All searches Editor ID, Form ID, File and Status.\n"
			"Right-click a row to mark it, copy its IDs or manage its saved JSON.\n"
			"Right-click a column header to show Form ID or Status."));
		ImGui::SameLine();
		if (BrowserUI::IconButton("##InspectorToggle", Icons::TI(ICON_TI_LAYOUT_COLUMNS), T(TKEY("inspector_toggle_tooltip"), "Show or hide the inspector"),
				editor.settings.browserShowInspector)) {
			editor.settings.browserShowInspector = !editor.settings.browserShowInspector;
			editor.Save();
		}
	}

	const auto active = CollectActiveRecords(context);
	if (!active.empty())
		DrawActiveStrip(state, context, active);

	if (!hasList) {
		DrawCellLightingCard(state, context);
		return;
	}

	DrawToolbar(state);
	DrawRecentRow(state, context);
	ImGui::Spacing();

	if (!editor.settings.browserShowInspector) {
		DrawList(state, context, ImGui::GetContentRegionAvail(), active);
		return;
	}
	BrowserUI::SplitPane({ "##FormListSplit", kInspectorBesideMinWidth, kInspectorWidth, Browser::FormListState::kDefaultStackedRatio }, state.stackedRatio, [&](const ImVec2& size) { DrawList(state, context, size, active); }, [&](const ImVec2& size) { DrawInspector(state, context, size, active); });
}

#undef I18N_KEY_PREFIX
