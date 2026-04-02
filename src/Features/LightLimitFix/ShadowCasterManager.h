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

#include "RE/B/BSShadowLight.h"
#include "RE/S/ShadowSceneNode.h"

struct ImVec4;

namespace ShadowCasterManager
{
	// -------------------------------------------------------------------------
	// shadowLightsAccum iterator
	//
	// shadowLightsAccum is a flat slot array where a dual-paraboloid (DP) light
	// occupies shadowMapCount==2 consecutive physical slots (the second is null).
	// A plain range-for visits every physical slot and will see the null padding.
	// ForEachShadowLight advances by shadowMapCount so each logical light is
	// visited exactly once, matching the game's own iteration contract.
	//
	// Usage:
	//   ShadowCasterManager::ForEachShadowLight(ssn->GetRuntimeData().shadowLightsAccum,
	//       [](RE::BSShadowLight* light) { ... });
	// -------------------------------------------------------------------------
	template <typename Fn>
	inline void ForEachShadowLight(const RE::BSTArray<RE::BSShadowLight*>& accum, Fn&& fn)
	{
		int idx = 0;
		while (true) {
			RE::BSShadowLight* light = accum[idx];
			if (!light)
				break;
			fn(light);
			idx += light->shadowMapCount;
		}
	}

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
		kFormulaParam_LightDisplacement,    ///< distance moved since last shadow map render (game units)
		kFormulaParam_PlayerLightDistance,  ///< distance from the player character to the light (game units)
		kFormulaParam_LightImportance,      ///< contribution importance: lum(diffuse*fade) * max(att_cam, att_plr); set in interval loop only

		kFormulaParam_CameraX,
		kFormulaParam_CameraY,
		kFormulaParam_CameraZ,
		kFormulaParam_IsInterior,
		kFormulaParam_TimeOfDay,

		kFormulaParam_FrameTime,     ///< EMA-smoothed frame time (ms)
		kFormulaParam_FrameTarget,   ///< 90th-percentile frame time (ms) — target budget ceiling
		kFormulaParam_StableFrames,  ///< consecutive frames the EMA has been below FrameTarget

		kFormulaParam_Max
	};

	// -------------------------------------------------------------------------
	// Expression-based formula evaluator (wraps exprtk)
	// -------------------------------------------------------------------------
	struct FormulaHelper
	{
		FormulaHelper();
		~FormulaHelper();

		FormulaHelper(const FormulaHelper&) = delete;
		FormulaHelper& operator=(const FormulaHelper&) = delete;
		FormulaHelper(FormulaHelper&&) = delete;
		FormulaHelper& operator=(FormulaHelper&&) = delete;

		bool Parse(const std::string& input);
		double Calculate();

		/// Re-parse with a new expression, replacing any previously compiled formula.
		/// Returns true on success. On failure the old formula remains active.
		bool Reparse(const std::string& input);

		/// Compile `input` into a temporary expression and return true if it succeeds.
		/// On failure, `errorOut` receives the first parser error message.
		/// Does NOT affect the active formula.
		static bool Validate(const std::string& input, std::string& errorOut);

		static void SetParam(int32_t index, double value);
		static double GetParam(int32_t index);

	private:
		void* _ptr;
	};
	// -------------------------------------------------------------------------
	// -------------------------------------------------------------------------
	// Budget mode enum
	// -------------------------------------------------------------------------
	enum class BudgetModeEnum : int32_t
	{
		Auto = 0,     ///< DRS-style adaptive controller
		Manual = 1,   ///< Fixed slider value
		Formula = 2,  ///< User-editable exprtk expression
	};

	// -------------------------------------------------------------------------
	// Settings
	// All shadow-scheduling knobs.  Held inside LightLimitFix::Settings and
	// serialised as part of that JSON blob.  Pass a const-ref to Init().
	// -------------------------------------------------------------------------
	struct Settings
	{
		/// Enable the shadow caster scheduler entirely.  Requires a game restart to take effect.
		bool Enabled = true;

		/// Number of simultaneous shadow-casting point/spot lights (NOT counting the directional sun).
		/// 0 = scheduler active but selects no point lights (sun/directional unaffected).
		/// 4 = vanilla point light count with intelligent selection replacing the game's default.
		/// 5-64 = extended mode; depth buffer array is expanded beyond game's 8-slot limit
		///   when this exceeds 8.
		/// Higher values allow more lights to hold stale shadow maps between redraws at
		/// the cost of startup memory. The redraw budget and interval formula control
		/// per-frame GPU cost independently.
		int32_t ShadowLightCount = 16;

		/// Number of additional converted-light slots (lights treated as normal lights
		/// for geometry but tracked alongside shadow casters when ConvertExcessToNormal is enabled).
		int32_t ConvertedShadowSlots = 32;

		/// Allow a newly-chosen light to draw even if it was not chosen last frame.
		bool AllowDrawNewLight = true;

		/// Hard cap on how many lights may re-render their shadow maps in one frame.
		int32_t MaxRedrawPerFrame = 16;

		/// How the per-frame shadow redraw budget is determined.
		BudgetModeEnum BudgetMode = BudgetModeEnum::Auto;

		/// Target FPS for the auto-budget controller.
		/// 0 = auto-detect from monitor refresh rate.
		/// Any positive value overrides the auto-detected target.
		float AutoTargetFPS = 0.0f;

		/// Per-frame time budget for shadow re-renders (milliseconds).
		/// Used in Manual mode.  Lights whose estimated GPU cost would exceed this
		/// are deferred to a later frame.
		float RedrawBudgetMs = 2.0f;

		/// Demote shadow lights that exceed the active caster limit to normal (non-shadow) lights
		/// so they still contribute diffuse lighting without a shadow-map cost.
		bool ConvertExcessToNormal = true;

		/// Promote normal (non-shadow) lights to shadow casters when there is budget.
		/// Disabled by default; experimental.
		bool PromoteNormalToShadow = false;

		// --- Formula strings (exprtk expressions) ---

		/// Light priority scoring formula.  Available variables:
		/// lightindex, lightintensity, lightdistance, playerlightdistance, lightradius, lightx/y/z,
		/// lightr/g/b, lightambientr/g/b, lightchosenlastframe, lightneverfades,
		/// lightportalstrict, lightns, lightconverted, camerax/y/z, isinterior, timeofday
		std::string ScoreFormula = "lightradius * lightintensity / (1 + ((1 - lightneverfades) * lightdistance) / 1000) * (1 + lightchosenlastframe * 0.3)";

		/// Redraw interval formula (per light).  Higher = less frequent redraws.
		/// Uses min(lightdistance, playerlightdistance) so that a light near the player
		/// character is always treated as close even in third-person (camera is further away).
		/// `lightdisplacement` further reduces the interval for lights that have moved.
		std::string RedrawIntervalFormula = "min(10, (max(0, min(lightdistance, playerlightdistance) - lightradius * 0.5) / 500) / max(0.5, lightintensity)) * (lightconverted * 5 + 1) - min(lightdisplacement / 5, 10)";

		/// Redraw budget formula (per frame, in ms).  Used in Formula mode.
		/// Key variables: frametime (smoothed ms), frametarget (90th-pct ms), stableframes.
		std::string RedrawBudgetFormula = "max(0, frametarget - frametime - 0.5) * min(stableframes, 45) / 45";

		// --- Importance scheduling curve ---

		/// Interval multiplier applied to high-importance lights (importance >= 1).
		/// Lower values make frequently-contributing lights update shadows more aggressively.
		/// Default: 0.05 (updates 40x more frequently than unimportant lights).
		float ImportanceMinScale = 0.05f;

		/// Interval multiplier applied to unimportant lights (importance == 0).
		/// Higher values defer dim or distant lights more aggressively.
		/// Default: 2.0.
		float ImportanceMaxScale = 2.0f;
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(BudgetModeEnum,
		{ { BudgetModeEnum::Auto, 0 }, { BudgetModeEnum::Manual, 1 }, { BudgetModeEnum::Formula, 2 } })

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		Settings,
		Enabled,
		ShadowLightCount,
		ConvertedShadowSlots,
		AllowDrawNewLight,
		MaxRedrawPerFrame,
		BudgetMode,
		AutoTargetFPS,
		RedrawBudgetMs,
		ConvertExcessToNormal,
		PromoteNormalToShadow,
		ScoreFormula,
		RedrawIntervalFormula,
		RedrawBudgetFormula,
		ImportanceMinScale,
		ImportanceMaxScale)

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

		/// World position of the light at its last rendered shadow map frame.
		/// Used to prioritise redraws for lights that have moved significantly.
		RE::NiPoint3 lastRenderedPos{ 0.0f, 0.0f, 0.0f };

		/// Contribution-weighted importance score from the last scheduling frame.
		/// importance = luminance(diffuse × fade) × attenuation²(viewer, radius)
		/// where attenuation = max(1 − (dist/radius)², 0)  (Skyrim's quadratic falloff).
		/// Typically in [0, 1]; can exceed 1 for very bright lights at close range.
		/// Higher = light strongly illuminates the area around the viewer.
		float lastImportance{ 0.0f };

		void Clear()
		{
			Light = nullptr;
			LastDrawnFrame = -1;
			RedrawFrame = false;
			lastRenderedPos = { 0.0f, 0.0f, 0.0f };
			lastImportance = 0.0f;
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
		uint64_t Key{ 0 };
		uint32_t Tracked[kBudgetWindowSize]{};  ///< Ring buffer of per-frame µs costs.
		int32_t TrackedCount{ 0 };
		int32_t LastTrackedHelper{ -1 };
		uint32_t Progress{ 0 };  ///< Accumulated step-0 cost awaiting step-1.
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

		/// Returns the mean GPU cost (µs) averaged over all currently tracked lights.
		int32_t GetAverageCostUs() const;

	private:
		int32_t _counter{ 0 };
		std::unordered_map<uint64_t, std::unique_ptr<BudgetEntry>> _map;

		void CleanupExpired();
	};

	// -------------------------------------------------------------------------
	// Per-slot visualization metadata (filled by LLF::CopyPointShadowData)
	// -------------------------------------------------------------------------
	struct ShadowSlotInfo
	{
		uint32_t type = 0;       ///< Shadow type: 0=spot/frustum, 1=hemisphere, 2=omnidirectional
		float range = 0.0f;      ///< Light range (world units) -- radius for point lights, cone distance for spots
		bool valid = false;      ///< true when this slot was written this frame
		uintptr_t lightKey = 0;  ///< Light object pointer (stable key for suppression)
	};

	/// Resets slot metadata for a new frame.  Call at the start of CopyPointShadowData.
	void BeginSlotFrame(uint32_t slotCount);

	/// Records metadata for one filled shadow slot.
	void RecordSlot(uint32_t depthSlot, const ShadowSlotInfo& info);

	/// Returns true if the light with this pointer key has been suppressed by the user.
	bool IsSuppressed(uintptr_t lightKey);

	/// Returns true if any lights are currently suppressed.
	bool HasSuppressedLights();

	/// Returns the number of shadow slots consumed this frame.
	uint32_t GetSlotUsage();

	/// Returns the number of active shadow-casting lights whose importance score
	/// exceeds 0.1 (lights meaningfully illuminating the camera or player area).
	uint32_t GetHighImportanceCount();

	/// Read-only view of the per-slot metadata for the current frame.
	const std::vector<ShadowSlotInfo>& GetSlotInfos();

	/// Returns the display name for a shadow type index (0=Spot, 1=Hemi, 2=Omni).
	const char* GetShadowTypeName(uint32_t type);

	/// Returns the golden-ratio hue colour for shadow-map slot slotIdx as an ImVec4.
	/// Matches the mode-8 shader visualisation colour.
	ImVec4 ShadowSlotHueColor(uint32_t slotIdx);

	/// Draw the interactive shadow caster table (suppress/filter/sort).
	/// compact=true caps height; showColor adds a hue swatch column (viz mode 8).
	/// sceneOnly=true shows only lights currently in the scene (overlay); false shows all known lights including disabled ones (settings).
	void DrawShadowLightTable(bool compact, bool showColor, bool sceneOnly = false);

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

	/// Returns the stable container-slot index i for a shadow light (0 = sun, 1+ = point lights).
	/// Uses the internal s_lights pool — does not read the descriptor's shadowmapIndex field,
	/// which may be corrupted by ReturnShadowmaps().  Returns -1 if the light is not active.
	int32_t GetShadowSlot(RE::BSShadowLight* light);

	/// Draw shadow-specific statistics lines (slot usage, importance count).
	/// Call from LightLimitFix::DrawSettings() inside its Statistics tree node.
	/// shadowLightCount / shadowUnshadowedLightCount are owned by LightLimitFix.
	void DrawShadowStats(uint32_t shadowLightCount, uint32_t shadowUnshadowedLightCount);

	/// Draw per-mode overlay info for shadow-related visualisation modes (3-9).
	/// Call from LightLimitFix::DrawOverlay() inside the vizOn block for modes >= 3.
	/// totalLightCount is the current clustered light count owned by LightLimitFix.
	void DrawOverlayShadowModeInfo(uint32_t mode, uint32_t shadowUnshadowedLightCount, uint32_t totalLightCount);

	/// Appends tooltip text for visualisation modes 3-9 (all shadow-specific).
	/// Call from LightLimitFix::DrawSettings() inside the LightsVisualisationMode hover tooltip,
	/// immediately after the LLF-owned entries for modes 0-2.
	void DrawVisualisationTooltipShadowModes();

	/// Draw the ImGui settings panel for the shadow caster scheduler.
	/// Call from LightLimitFix::DrawSettings().
	void DrawSettings(Settings& settings);

}
