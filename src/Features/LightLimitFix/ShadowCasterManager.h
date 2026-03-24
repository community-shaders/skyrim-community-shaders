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
		/// lightindex, lightintensity, lightdistance, lightradius, lightx/y/z,
		/// lightr/g/b, lightambientr/g/b, lightchosenlastframe, lightneverfades,
		/// lightportalstrict, lightns, lightconverted, camerax/y/z, isinterior, timeofday
		std::string ScoreFormula = "lightradius * lightintensity / (1 + ((1 - lightneverfades) * lightdistance) / 1000) * (1 + lightchosenlastframe * 0.3)";

		/// Redraw interval formula (per light).  Higher = less frequent redraws.
		std::string RedrawIntervalFormula = "min(10, (max(0, lightdistance - lightradius * 0.5) / 500) / max(0.5, lightintensity)) * (lightconverted * 5 + 1)";

		/// Redraw budget formula (per frame, in ms).  Used in Formula mode.
		/// Key variables: frametime (smoothed ms), frametarget (90th-pct ms), stableframes.
		std::string RedrawBudgetFormula = "max(0, frametarget - frametime - 0.5) * min(stableframes, 45) / 45";
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

	/// Read-only view of the per-slot metadata for the current frame.
	const std::vector<ShadowSlotInfo>& GetSlotInfos();

	/// Returns the display name for a shadow type index (0=Spot, 1=Hemi, 2=Omni).
	const char* GetShadowTypeName(uint32_t type);

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

	/// Draw the ImGui settings panel for the shadow caster scheduler.
	/// Call from LightLimitFix::DrawSettings().
	void DrawSettings(Settings& settings);

}
