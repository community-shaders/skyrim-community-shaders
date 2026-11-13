#pragma once

/**
 * @file WeatherTypeIcons.h
 * @brief Weather-class glyphs for TESWeather records.
 *
 * Prefer editor ID / display-name tokens over DATA flags: vanilla flags are often
 * wrong or used as catch-alls (many event / DLC weathers are tagged Cloudy).
 *
 * Precipitation flags (rain / snow) are still trusted when labels don't match.
 * Aurora flags are used the same way. Weathers with no class flag at all (the
 * "None" filter: world map, window and other engine-support weathers) use a
 * cloud with a gear. Classified weathers whose labels match nothing use the
 * custom CloudSun draw-list icon (Menu/Icons/helpers/Custom/imgui_cloud_sun_icon.h).
 *
 * Glyphs are family-tagged GlyphRefs from open-source icon fonts under Icons/glyphs.
 */

#include "IconsTabler.h"
#include "Menu/Icons/helpers/Custom/imgui_cloud_sun_icon.h"
#include "Menu/Icons/helpers/IconFonts.h"

#include "RE/T/TESWeather.h"
#include "Utils/Format.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

namespace WeatherTypeIcons
{
	inline constexpr Icons::GlyphRef kRain = Icons::TI(ICON_TI_CLOUD_RAIN);
	inline constexpr Icons::GlyphRef kStorm = Icons::TI(ICON_TI_CLOUD_STORM);
	inline constexpr Icons::GlyphRef kSnow = Icons::TI(ICON_TI_SNOWFLAKE);
	inline constexpr Icons::GlyphRef kCloudy = Icons::TI(ICON_TI_CLOUD);
	inline constexpr Icons::GlyphRef kFog = Icons::TI(ICON_TI_CLOUD_FOG);
	inline constexpr Icons::GlyphRef kAsh = Icons::TI(ICON_TI_VOLCANO);
	inline constexpr Icons::GlyphRef kAurora = Icons::TI(ICON_TI_SPARKLES);
	inline constexpr Icons::GlyphRef kEclipse = Icons::TI(ICON_TI_MOON_2);
	inline constexpr Icons::GlyphRef kPleasant = Icons::TI(ICON_TI_SUN);
	/// Unclassified engine-support weathers (world map, windows, scripted utility weathers).
	inline constexpr Icons::GlyphRef kSystem = Icons::TI(ICON_TI_CLOUD_COG);

	/** @brief True when a weather carries none of the class flags, the editor's "None" category. */
	[[nodiscard]] inline bool IsUnclassified(const RE::TESWeather* weather) noexcept
	{
		using Flag = RE::TESWeather::WeatherDataFlag;
		return !weather->data.flags.any(Flag::kPleasant, Flag::kCloudy, Flag::kRainy, Flag::kSnow, Flag::kPermAurora,
			Flag::kAuroraFollowsSun);
	}

	/**
	 * @brief Draw a resolved weather icon into an axis-aligned size×size box.
	 * Unknown uses CloudSun; font glyphs use Icons::DrawCenteredGlyph.
	 */
	inline void Draw(std::optional<Icons::GlyphRef> icon, ImDrawList* dl, ImVec2 pos, float size, ImU32 col)
	{
		if (!dl || size <= 0.0f || !icon.has_value())
			return;
		if (!icon->IsValid()) {
			Icons::CloudSun(dl, pos, size, col);
			return;
		}
		Icons::DrawCenteredGlyph(dl, pos, ImVec2(size, size), *icon, col);
	}

	/** @brief Infer a weather-class glyph from a descriptive editor ID / name. */
	[[nodiscard]] inline std::optional<Icons::GlyphRef> ResolveFromLabel(std::string_view label) noexcept
	{
		if (label.empty())
			return std::nullopt;

		const std::string lower = Util::ToLower(std::string(label));

		auto has = [&](std::string_view token) {
			return lower.find(token) != std::string::npos;
		};

		// Most specific tokens first, e.g. SkyrimOvercastRain is rain, not cloudy.
		if (has("storm") || has("thunder") || has("lightning"))
			return kStorm;
		if (has("blizzard") || has("snow") || has("ice"))
			return kSnow;
		if (has("rain"))
			return kRain;
		if (has("ash") || has("volcan") || has("erupt"))
			return kAsh;
		if (has("eclipse") || has("bloodmoon") || has("blood moon"))
			return kEclipse;
		if (has("aurora") || has("northernlight") || has("northern light"))
			return kAurora;
		if (has("fog") || has("mist") || has("haze"))
			return kFog;
		if (has("cloudy") || has("overcast") || has("clouded"))
			return kCloudy;
		if (has("clear") || has("sunny") || has("pleasant") || has("fair"))
			return kPleasant;
		return std::nullopt;
	}

	/**
	 * @brief Primary weather-class icon for a weather record.
	 * @return nullopt when weather is null; kSystem for unclassified weathers; empty GlyphRef for
	 *         unknown classified ones (CloudSun); otherwise a family-tagged glyph.
	 */
	[[nodiscard]] inline std::optional<Icons::GlyphRef> Resolve(RE::TESWeather* weather) noexcept
	{
		if (!weather)
			return std::nullopt;

		// Checked first so the icon always agrees with the "None" filter, whatever the label says.
		if (IsUnclassified(weather))
			return kSystem;

		if (const char* editorId = weather->GetFormEditorID()) {
			if (auto icon = ResolveFromLabel(editorId))
				return icon;
		}
		if (const char* name = weather->GetName()) {
			if (auto icon = ResolveFromLabel(name))
				return icon;
		}

		using Flag = RE::TESWeather::WeatherDataFlag;
		// Only trust precipitation / aurora flags: Cloudy and Pleasant are widely
		// mis-set on event, scripted, and DLC weathers, which made the cloud glyph
		// show up everywhere. Those fall through to CloudSun instead.
		if (weather->data.flags.any(Flag::kRainy))
			return kRain;
		if (weather->data.flags.any(Flag::kSnow))
			return kSnow;
		if (weather->data.flags.any(Flag::kPermAurora) || weather->data.flags.any(Flag::kAuroraFollowsSun))
			return kAurora;

		return Icons::GlyphRef{};  // unknown → CloudSun
	}

	/** @brief Resolve() for font-only callers: CloudSun is painted, so it and a null weather give `fallback`. */
	[[nodiscard]] inline Icons::GlyphRef ResolveGlyph(RE::TESWeather* weather, Icons::GlyphRef fallback = kCloudy) noexcept
	{
		const auto icon = Resolve(weather);
		return icon && icon->IsValid() ? *icon : fallback;
	}
}
