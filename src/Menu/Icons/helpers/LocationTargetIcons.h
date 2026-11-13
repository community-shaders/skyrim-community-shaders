#pragma once

/**
 * @file LocationTargetIcons.h
 * @brief Resolve a Scene Manager location target to a single icon glyph.
 *
 * Prefers a LocType keyword icon when the target is (or resolves to) a location
 * type; otherwise falls back to the category glyph from LocationCategoryIcons.
 */

#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/LocationCategoryIcons.h"
#include "Menu/Icons/helpers/LocationTypeIcons.h"

#include "Utils/Form.h"

#include "RE/B/BGSLocation.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESForm.h"

#include <string>
#include <string_view>
#include <vector>

namespace LocationTargetIcons
{
	using Category = LocationCategoryIcons::Kind;

	inline constexpr std::string_view kLocationTypePrefix = "LocType";

	/** @brief Whether a keyword editor ID names a LocType* keyword. */
	[[nodiscard]] inline bool IsLocationTypeEditorId(std::string_view editorId) noexcept
	{
		return editorId.starts_with(kLocationTypePrefix);
	}

	/** @brief Category glyph (worldspace / LocType / region / location / cell). */
	[[nodiscard]] inline Icons::GlyphRef ResolveCategory(Category category) noexcept
	{
		return LocationCategoryIcons::Resolve(category);
	}

	/** @brief Primary LocType glyph for a BGSLocation's keywords, or empty. */
	[[nodiscard]] inline Icons::GlyphRef ResolveFromLocation(RE::BGSLocation* location) noexcept
	{
		if (!location)
			return {};

		std::vector<std::string> locTypeIds;
		for (auto* keyword : location->GetKeywords()) {
			if (!keyword)
				continue;
			auto editorId = Util::GetFormEditorID(keyword);
			if (!IsLocationTypeEditorId(editorId))
				continue;
			locTypeIds.push_back(std::move(editorId));
		}
		return LocationTypeIcons::ResolvePrimaryLocationTypeIcon(locTypeIds);
	}

	/**
	 * @brief Identity needed to pick a glyph without pulling SceneSettingsManager.
	 * Category values match LocationTargetType order.
	 */
	struct TargetView
	{
		Category category = Category::Location;
		std::string_view editorId;
		RE::FormID formId = 0;
		std::string_view formKey;
	};

	/** @brief LocType glyph when the target is a LocType keyword or a concrete location. */
	[[nodiscard]] inline Icons::GlyphRef ResolveLocType(const TargetView& target) noexcept
	{
		if (target.category == Category::LocationType && !target.editorId.empty())
			return LocationTypeIcons::LookupLocationTypeIcon(target.editorId);

		if (target.category != Category::Location)
			return {};

		RE::BGSLocation* location = nullptr;
		if (target.formId != 0) {
			location = RE::TESForm::LookupByID<RE::BGSLocation>(target.formId);
		} else if (!target.formKey.empty()) {
			// Quiet resolve: SpidToFormId logs on miss, and authored lists redraw every frame.
			const auto components = Util::ParseSpid(std::string(target.formKey));
			if (auto* handler = RE::TESDataHandler::GetSingleton();
				handler && components.localFormId != 0 && !components.pluginName.empty())
				location = handler->LookupForm<RE::BGSLocation>(components.localFormId, components.pluginName);
		}
		return ResolveFromLocation(location);
	}

	/** @brief LocType glyph when available, otherwise the category glyph. */
	[[nodiscard]] inline Icons::GlyphRef Resolve(const TargetView& target) noexcept
	{
		if (const Icons::GlyphRef locTypeIcon = ResolveLocType(target))
			return locTypeIcon;
		return ResolveCategory(target.category);
	}
}
