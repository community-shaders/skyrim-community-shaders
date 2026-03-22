// ShadowCasterManager.h
// Shadow caster scheduling for LightLimitFix.
//
// Based on Intellightent by meh321
//   https://www.nexusmods.com/skyrimspecialedition/mods/172423
//
// Ported and adapted for Community Shaders by the Community Shaders team with permission.
//
// The original plugin managed shadow caster selection, temporal shadow
// update scheduling, and depth buffer extension entirely outside Community
// Shaders.  This file houses the CPU-side shadow scheduling subsystem so it
// can live alongside (and share settings with) LightLimitFix's GPU-side
// clustered light culling without coupling the two concerns inside a single
// translation unit.

#pragma once

#include <d3d11.h>

namespace ShadowCasterManager
{
	// -------------------------------------------------------------------------
	// Formula parameter indices
	// -------------------------------------------------------------------------
	enum FormulaParams
	{
		kFormulaParam_LightIndex,
		kFormulaParam_LightIntensity,
		kFormulaParam_LightDistance,
		kFormulaParam_LightRadius,
		kFormulaParam_LightX,
		kFormulaParam_LightY,
		kFormulaParam_LightZ,
		kFormulaParam_LightR,
		kFormulaParam_LightG,
		kFormulaParam_LightB,
		kFormulaParam_LightAmbientR,
		kFormulaParam_LightAmbientG,
		kFormulaParam_LightAmbientB,
		kFormulaParam_LightChosenLastFrame,
		kFormulaParam_LightNeverFades,
		kFormulaParam_LightPortalStrict,
		kFormulaParam_LightNS,
		kFormulaParam_LightConverted,

		kFormulaParam_CameraX,
		kFormulaParam_CameraY,
		kFormulaParam_CameraZ,
		kFormulaParam_IsInterior,
		kFormulaParam_TimeOfDay,

		kFormulaParam_Max
	};

	// -------------------------------------------------------------------------
	// Expression-based formula evaluator (wraps exprtk)
	// -------------------------------------------------------------------------
	struct FormulaHelper
	{
		FormulaHelper();
		~FormulaHelper();

		bool Parse(const std::string& input);
		double Calculate();
		static void SetParam(int32_t index, double value);
		static double GetParam(int32_t index);

	private:
		void* _ptr;
	};
	// -------------------------------------------------------------------------
	// Settings
	// All shadow-scheduling knobs.  Held inside LightLimitFix::Settings and
	// serialised as part of that JSON blob.  Pass a const-ref to Init().
	// -------------------------------------------------------------------------
	struct Settings
	{
		// --- Phase 1: extended shadow slots ---

		/// Number of simultaneous shadow-casting point/spot lights.
		/// 0 = disabled (vanilla 4-light limit applies).
		/// 4 = default vanilla behaviour with selection intelligence.
		/// 5-32 = extended mode; depth buffer array is expanded beyond game's 8-slot limit
		///   when this exceeds 8.
		int32_t ShadowLightCount = 4;

		/// Number of additional converted-light slots (lights treated as normal lights
		/// for geometry but tracked alongside shadow casters).  Used in Phase 4.
		int32_t ConvertedShadowSlots = 0;

		// --- Phase 2: shadow caster selection ---

		/// Force portalStrict on all shadow lights so they obey portal culling.
		bool ForcePortalStrict = true;

		/// Allow a newly-chosen light to draw even if it was not chosen last frame.
		bool AllowDrawNewLight = true;

		// --- Phase 3: temporal shadow updates & budget ---

		/// Hard cap on how many lights may re-render their shadow maps in one frame.
		int32_t MaxRedrawPerFrame = 4;

		/// Per-frame time budget for shadow re-renders (milliseconds).
		/// Lights whose estimated GPU cost would exceed this are deferred to a later frame.
		/// Default varies by scene type; the formula "1 + isinterior" gives 1 ms exterior,
		/// 2 ms interior.  A framerate-proportional formula ("frametime * 0.15") is recommended
		/// for adaptive quality.
		float RedrawBudgetMs = 2.0f;

		// --- Phase 4: light conversion ---

		/// Convert shadow lights that fail portal/culling tests to normal (non-shadow) lights
		/// so they still contribute diffuse lighting without a shadow-map cost.
		bool ConvertDistantToNormal = true;

		/// Promote normal (non-shadow) lights to shadow casters when there is budget.
		/// Disabled by default; experimental.
		bool PromoteNormalToShadow = false;

		/// Maximum shadow→normal conversions per frame.
		int32_t MaxConvertCount = 32;

		/// Maximum normal→shadow promotions per frame.
		int32_t MaxConvertCountShadow = 4;

		// --- Formula strings (exprtk expressions) ---

		/// Light priority scoring formula.  Available variables:
		/// lightindex, lightintensity, lightdistance, lightradius, lightx/y/z,
		/// lightr/g/b, lightambientr/g/b, lightchosenlastframe, lightneverfades,
		/// lightportalstrict, lightns, lightconverted, camerax/y/z, isinterior, timeofday
		std::string ScoreFormula = "lightradius * lightintensity / (1 + ((1 - lightneverfades) * lightdistance) / 1000) * (1 + lightchosenlastframe * 0.3)";

		/// If non-empty, evaluated per-light to decide if shadow→normal conversion is allowed.
		/// Return >= 0.5 to allow.
		std::string AllowConvertFormula;

		/// If non-empty, evaluated per-light for promoted (isNS) lights to decide
		/// if normal→shadow conversion is allowed. Return >= 0.5 to allow.
		std::string AllowConvertShadowFormula;

		/// Redraw interval formula (per light).  Higher = less frequent redraws.
		std::string RedrawIntervalFormula = "min(10, (max(0, lightdistance - lightradius * 0.5) / 500) / max(0.5, lightintensity)) * (lightconverted * 5 + 1)";

		/// Redraw budget formula (per frame, in ms).
		std::string RedrawBudgetFormula = "1 + isinterior";
	};

	// -------------------------------------------------------------------------
	// Per-light schedule entry
	// -------------------------------------------------------------------------
	struct LightEntry
	{
		RE::BSShadowLight* Light{ nullptr };

		/// Sort key: LastDrawnFrame + computed interval.  Lower = higher priority.
		double RedrawScore{ 0.0 };

		/// Frame number this light last rendered its shadow map.
		int32_t LastDrawnFrame{ -1 };

		/// Set each frame by the scheduler; consumed by the render hook.
		bool RedrawFrame{ false };

		/// Slot index in the LightContainer array.
		int32_t Index{ -1 };

		void Clear()
		{
			Light = nullptr;
			LastDrawnFrame = -1;
			RedrawFrame = false;
		}
	};

	// -------------------------------------------------------------------------
	// Container for the active light pool
	// -------------------------------------------------------------------------
	struct LightContainer
	{
		LightEntry* Lights{ nullptr };

		/// true when index 0 is the directional sun (always active, never rescheduled).
		bool Sun{ false };

		/// Total allocated slots (ShadowLightCount + ConvertedShadowSlots).
		int32_t Size{ 0 };

		/// Returns the first free shadow-caster slot index, or -1 if full.
		int32_t FindFreeIndex(bool shadowSlot, int32_t shadowCount, int32_t convertCount) const;

		/// Returns the index of a light pointer in the shadow-caster range, or -1.
		int32_t FindLight(RE::BSShadowLight* light, int32_t shadowCount) const;
	};

	// -------------------------------------------------------------------------
	// Per-light GPU timing tracker (sliding-window average over 8 frames)
	// -------------------------------------------------------------------------
	static constexpr int kBudgetWindowSize = 8;

	struct BudgetEntry
	{
		BudgetEntry() { memset(Tracked, 0, sizeof(Tracked)); }

		uint64_t Key{ 0 };
		uint16_t Tracked[kBudgetWindowSize]{};  ///< Ring buffer of per-frame µs costs.
		int32_t TrackedCount{ 0 };
		int32_t LastTrackedHelper{ -1 };
		uint16_t Progress{ 0 };  ///< Accumulated step-0 cost awaiting step-1.
		int32_t Current{ 0 };    ///< Rolling sum of Tracked[].

		void BeginStep(int32_t step);
		void EndStep(int32_t step, int32_t helperCounter);

		/// Returns true when the entry hasn't been updated in ~600 scheduler ticks.
		bool IsExpired(int32_t helperCounter) const;

	private:
		int64_t _startTime{ 0 };
	};

	struct BudgetTracker
	{
		void Begin(int32_t step);
		void BeginLight(RE::BSShadowLight* light, int32_t step);
		void EndLight(RE::BSShadowLight* light, int32_t step);

		/// Returns estimated render cost (µs) for a light.
		/// Falls back to the mean of all tracked lights for unseen lights.
		int32_t GetCost(RE::BSShadowLight* light) const;

	private:
		int32_t _counter{ 0 };
		std::unordered_map<uint64_t, BudgetEntry*> _map;

		void CleanupExpired();
	};

	// -------------------------------------------------------------------------
	// Public API
	// -------------------------------------------------------------------------

	/// Call once from LightLimitFix::PostPostLoad() before Install().
	/// Allocates the light container and initialises state from settings.
	void Init(const Settings& settings);

	/// Install all game hooks.  Call from LightLimitFix::PostPostLoad().
	void Install(const Settings& settings);

	/// Per-frame update: runs the scheduler.  Call from LightLimitFix::Prepass().
	void Update(const Settings& settings, RE::ShadowSceneNode* shadowSceneNode,
		RE::NiCamera* worldCamera);

	/// Returns a read-only view of the active light pool for UI/visualization.
	const LightContainer& GetLights();

	/// Draw the ImGui settings panel for the shadow caster scheduler.
	/// Call from LightLimitFix::DrawSettings().
	void DrawSettings(Settings& settings);

	/// Extended depth buffer arrays (indexed by shadow slot).
	/// The game's built-in DepthStencilData only has 8 slots; these arrays are
	/// used for indices 0..ShadowLightCount-1 when ShadowLightCount > 8.
	extern void** g_normalDepthBuffer;
	extern void** g_readOnlyDepthBuffer;
}
