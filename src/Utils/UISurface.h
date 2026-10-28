#pragma once

#include <array>
#include <cstdint>
#include <imgui.h>
#include <nlohmann/json_fwd.hpp>

/**
 * @brief The menu's flat surface style: a fill a subtle step above the window colour, a hairline border and, for
 * floating things only, a single soft shadow. Everything derives from style colours so every theme works, and
 * every surface role has its own look in the active theme (Menu::ThemeSettings::Surfaces).
 */
namespace Util::Surface
{
	/** @brief The places the menu draws a surface; each has its own theme settings. */
	enum class Role : std::uint8_t
	{
		HomeCard,    /**< @brief Cards on the Home page. */
		Toast,       /**< @brief Notification toast cards. */
		OverlayCard, /**< @brief The shader compilation status card. */
		Palette,     /**< @brief The Ctrl+K go-to palette. */
		NavSelected, /**< @brief The selected row of the left navigation. */
		OptionCard,  /**< @brief Clickable cards that pick one option and open it below (Util::OptionCard). */
		SectionCard, /**< @brief Cards grouping related settings on a page (Util::SectionCard). */
		Count
	};

	/** @brief Untranslated key of a role, used as its JSON key. */
	const char* RoleKey(Role role);

	/** @brief Look of one surface role. Spreads and widths are 1080p baseline values, scaled by the UI scale when drawn. */
	struct RoleSettings
	{
		bool enabled = true;                          /**< @brief Draw the surface at all. */
		float fillLift = 0.04f;                       /**< @brief Window colour lifted toward white by this share. */
		float fillAlpha = 1.0f;                       /**< @brief Fill opacity multiplier (NavSelected: accent fill opacity). */
		float rounding = -1.0f;                       /**< @brief Corner radius; negative uses the style's rounding. */
		bool border = true;                           /**< @brief Draw the hairline border. */
		float borderAlpha = 0.08f;                    /**< @brief Hairline border opacity. */
		bool useBorderColor = false;                  /**< @brief Use borderColor instead of the white hairline. */
		ImVec4 borderColor{ 1.0f, 1.0f, 1.0f, 0.2f }; /**< @brief Border colour when useBorderColor is set. */
		bool shadow = false;                          /**< @brief Draw the soft outer shadow. */
		float shadowSpread = 6.0f;                    /**< @brief How far the shadow grows past the surface. */
		float shadowAlpha = 0.22f;                    /**< @brief Shadow opacity (black). */
		float shadowOffsetX = 0.0f;                   /**< @brief Shadow sideways shift; positive moves it right. */
		float shadowOffsetY = 2.0f;                   /**< @brief Shadow drop. */
		float padding = 12.0f;                        /**< @brief Inner padding on every side (HomeCard only). */
		bool accentBar = true;                        /**< @brief Accent bar on the left edge (NavSelected only). */
		float accentBarWidth = 3.0f;                  /**< @brief Accent bar width (NavSelected only). */
	};

	/** @brief Surface settings of every role; part of a theme. Missing JSON fields keep these defaults. */
	struct Settings
	{
		std::array<RoleSettings, static_cast<size_t>(Role::Count)> roles;

		/** @brief Fills every role with its built-in default look. */
		Settings();

		/** @brief The built-in default look of a role. */
		static RoleSettings Defaults(Role role);

		/** @brief Clamps every value to its valid range; non-finite values fall back to the role default. */
		void Sanitize();
	};

	void to_json(nlohmann::json& j, const Settings& settings);
	/** @brief Tolerant loader: absent, mistyped or out-of-range fields fall back to defaults or are clamped. */
	void from_json(const nlohmann::json& j, Settings& settings);

	/** @brief Settings of a role in the active theme (built-in defaults before the menu exists). */
	const RoleSettings& Get(Role role);

	/** @brief Per-call geometry and fade; the look comes from the role's settings. */
	struct Options
	{
		float rounding = -1.0f;    /**< @brief Style rounding used when the role sets none; negative uses ChildRounding. */
		float alpha = 1.0f;        /**< @brief Multiplier on every layer, for fades. */
		ImU32 borderColor = 0;     /**< @brief Border colour override (e.g. a card's status colour); 0 uses the role's. */
		float minFillAlpha = 0.0f; /**< @brief Floor for the fill's opacity before fades, so a see-through window colour cannot thin the surface. */
		ImVec4 clip{ 0, 0, 0, 0 }; /**< @brief Screen rect (min x, min y, max x, max y) the surface and shadow stay inside, such as the scrolling page
		                                holding a card; an empty rect leaves them unbounded. */
	};

	/** @brief The visible area of the window enclosing the current child window, as an Options::clip. */
	ImVec4 ParentClipRect();

	/**
	 * @brief Draws an accent frame inside [min, max]: a border of even thickness on every side plus a thicker stripe
	 * down the left edge. Drawn inside the rect and unclipped by the owning window's inner clip rect (which trims
	 * the left and right edges by half the window padding), so all four sides come out the same.
	 */
	void DrawAccentFrame(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color, float rounding, float borderThickness,
		float stripeWidth);

	/**
	 * @brief Draws a surface into a draw list, using the role's settings from the active theme. Draws nothing when
	 * the role is disabled.
	 * @param drawList Target list (e.g. ImGui::GetWindowDrawList()).
	 * @param min, max Screen rect of the surface.
	 * @param role Which surface this is.
	 * @param options Geometry fallback, fade and border colour override; see Options.
	 */
	void Draw(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, Role role, const Options& options = {});
}
