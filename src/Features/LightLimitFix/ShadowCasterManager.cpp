// ShadowCasterManager.cpp
// Shadow caster scheduling for LightLimitFix.
//
// Based on Intellightent by meh321
//   https://www.nexusmods.com/skyrimspecialedition/mods/172423
//
// Ported and adapted for Community Shaders by the Community Shaders team with permission.

#include "ShadowCasterManager.h"
#include "../../Deferred.h"
#include "../../Globals.h"
#include "../../State.h"
#include "../../Utils/Game.h"
#include "../../Utils/UI.h"
#include "../Upscaling.h"
#include "../VR.h"

#include <exprtk.hpp>

namespace ShadowCasterManager
{
	// =========================================================================
	// Formula evaluator (exprtk)
	// =========================================================================

	struct FormulaWrapper
	{
		exprtk::expression<double> expression;
		exprtk::parser<double> parser;
	};

	static double s_formulaParams[kFormulaParam_Max];
	static exprtk::symbol_table<double> s_symbolTable;
	static bool s_formulaInited = false;

	struct FormulaVarInfo
	{
		const char* name;
		const char* description;
		int32_t index;
	};

	// Single authoritative list of formula variables.
	// Drives both symbol table registration and the formula editor help text.
	static constexpr FormulaVarInfo kFormulaVars[] = {
		{ "lightindex", "sequential index of this candidate light", kFormulaParam_LightIndex },
		{ "lightintensity", "NiLight fade/intensity", kFormulaParam_LightIntensity },
		{ "lightdistance", "camera-to-light distance (game units; 1 unit ~= 1.428 cm)", kFormulaParam_LightDistance },
		{ "lightradius", "light radius/range (game units; 1 unit ~= 1.428 cm)", kFormulaParam_LightRadius },
		{ "lightx", "light world X", kFormulaParam_LightX },
		{ "lighty", "light world Y", kFormulaParam_LightY },
		{ "lightz", "light world Z", kFormulaParam_LightZ },
		{ "lightr", "diffuse red", kFormulaParam_LightR },
		{ "lightg", "diffuse green", kFormulaParam_LightG },
		{ "lightb", "diffuse blue", kFormulaParam_LightB },
		{ "lightambientr", "ambient red", kFormulaParam_LightAmbientR },
		{ "lightambientg", "ambient green", kFormulaParam_LightAmbientG },
		{ "lightambientb", "ambient blue", kFormulaParam_LightAmbientB },
		{ "lightchosenlastframe", "1 if this light held a slot last frame", kFormulaParam_LightChosenLastFrame },
		{ "lightframessincerender", "frames since this light's slot was last actually rendered into the shadow atlas; 1e6 sentinel when never rendered or unassigned", kFormulaParam_LightFramesSinceRender },
		{ "lightneverfades", "1 if lodFade disabled (permanent light)", kFormulaParam_LightNeverFades },
		{ "lightportalstrict", "1 if portal-strict (always 1 for shadow casters)", kFormulaParam_LightPortalStrict },
		{ "lightns", "1 if promoted from normal light (PromoteNormalToShadow)", kFormulaParam_LightNS },
		{ "lightconverted", "1 if light is in the converted (non-shadow) slot range", kFormulaParam_LightConverted },
		{ "lightdisplacement", "distance this light moved since its last shadow map render (game units; 0 when not yet tracked or in score formula)", kFormulaParam_LightDisplacement },
		{ "playerlightdistance", "distance from the player character to the light (game units; falls back to lightdistance when player unavailable)", kFormulaParam_PlayerLightDistance },
		{ "lightimportance", "contribution score: lum(diffuse*fade) * max(att_cam,att_plr) where att=(1-(dist/radius)^2)^2; 0 in score formula", kFormulaParam_LightImportance },
		{ "lightisspot", "1 if this is a spot/frustum shadow light (BSShadowFrustumLight); 0 for omni / hemi / sun", kFormulaParam_LightIsSpot },
		{ "lightspotvisible", "1 if the spot's cone plausibly reaches the camera frustum, 0 otherwise. Always 1 for non-spot lights so existing omni-only formulas are unaffected", kFormulaParam_LightSpotVisible },
		{ "camerax", "camera world X", kFormulaParam_CameraX },
		{ "cameray", "camera world Y", kFormulaParam_CameraY },
		{ "cameraz", "camera world Z", kFormulaParam_CameraZ },
		{ "isinterior", "1 in interior cells, 0 outdoors", kFormulaParam_IsInterior },
		{ "timeofday", "in-game hour (0.0-24.0)", kFormulaParam_TimeOfDay },
		{ "frametime", "EMA-smoothed frame time (ms)", kFormulaParam_FrameTime },
		{ "frametarget", "90th-percentile recent frame time (ms) -- headroom ceiling", kFormulaParam_FrameTarget },
		{ "stableframes", "consecutive frames EMA has been below frametarget", kFormulaParam_StableFrames },
	};

	static void InitFormulaSystem()
	{
		if (s_formulaInited)
			return;
		s_formulaInited = true;

		memset(s_formulaParams, 0, sizeof(double) * kFormulaParam_Max);

		for (const auto& v : kFormulaVars)
			s_symbolTable.add_variable(v.name, s_formulaParams[v.index]);
	}

	FormulaHelper::FormulaHelper() :
		_ptr(nullptr) { InitFormulaSystem(); }

	FormulaHelper::~FormulaHelper()
	{
		if (_ptr)
			delete static_cast<FormulaWrapper*>(_ptr);
	}

	bool FormulaHelper::Parse(const std::string& input)
	{
		if (_ptr)
			return false;
		auto* w = new FormulaWrapper();
		_ptr = w;
		w->expression.register_symbol_table(s_symbolTable);
		return w->parser.compile(input, w->expression);
	}

	double FormulaHelper::Calculate()
	{
		auto* w = static_cast<FormulaWrapper*>(_ptr);
		return w ? w->expression.value() : 0.0;
	}

	bool FormulaHelper::Reparse(const std::string& input)
	{
		std::string err;
		if (!Validate(input, err))
			return false;
		if (_ptr)
			delete static_cast<FormulaWrapper*>(_ptr);
		_ptr = nullptr;
		return Parse(input);
	}

	bool FormulaHelper::Validate(const std::string& input, std::string& errorOut)
	{
		InitFormulaSystem();
		FormulaWrapper tmp;
		tmp.expression.register_symbol_table(s_symbolTable);
		if (tmp.parser.compile(input, tmp.expression))
			return true;
		if (tmp.parser.error_count() > 0)
			errorOut = tmp.parser.get_error(0).diagnostic;
		else
			errorOut = "Unknown parse error";
		return false;
	}

	void FormulaHelper::SetParam(int32_t index, double value) { s_formulaParams[index] = value; }
	double FormulaHelper::GetParam(int32_t index) { return s_formulaParams[index]; }

	// =========================================================================
	// Module-level state
	// =========================================================================

	/// Total LightEntry slots: sun (1) + shadow casters (≥4) + converted pool.
	static int32_t LightContainerSize(const Settings& s)
	{
		return std::max(4, s.ShadowLightCount) + 1 + s.ConvertedShadowSlots;
	}

	static Settings s_settings;
	static LightContainer s_lights;
	static BudgetTracker s_budget;

	// External conflict detection -- set during Install(), checked by Update() and DrawSettings().
	static bool s_externalConflict = false;
	static std::string s_conflictMessage;

	// Rolling redraw history (128-frame window) for DrawSettings statistics.
	static constexpr int kRedrawHistorySize = 128;
	static int32_t s_redrawHistory[kRedrawHistorySize] = {};
	static int32_t s_redrawHistoryPos = 0;
	static int32_t s_redrawSum = 0;

	// Rolling budget-consumed history (same window) for DrawSettings statistics.
	static int32_t s_budgetHistory[kRedrawHistorySize] = {};
	static int32_t s_budgetHistoryPos = 0;
	static int64_t s_budgetSum = 0;

	// Frame-time tracking — used by Formula's frametime/frametarget/stableframes
	// formula params, the shared frame-state diagnostic block, and stats UI.
	// Persists in both Manual and Formula modes; the cost is one float per frame.
	static constexpr int kFrameWindow = 120;  // ~2 s at 60 fps
	static float s_ftRing[kFrameWindow]{};
	static int s_ftHead = 0;
	static int s_ftCount = 0;
	static float s_ftEMA = 0.0f;
	static int s_stableFrames = 0;
	static float s_autoBudgetMs = 0.0f;  // last computed budget; used by UI, scheduling, and stats

	// "Steady" state thresholds for the shared frame-state diagnostic.
	// Mirror the old Auto-mode hysteresis values so the indicator behaves the
	// same way users grew used to, just informational rather than driving control.
	static constexpr float kFrameHeadroomDeadZoneMs = 0.3f;  // |headroom| below this = "steady"
	static constexpr float kFrameHeadroomSafetyMs = 0.5f;    // headroom must clear this before "growing"

	// Budget tracking for UI display
	static int32_t s_redrawnLightsThisFrame = 0;
	static int32_t s_totalShadowLightsThisFrame = 0;
	static uint32_t s_highImportanceLightCount = 0;
	static float s_redrawnLightsSmoothed = 0.0f;  // EMA-smoothed for stable UI display

	// Tracy diagnostic counters reset at the start of each scheduler frame.
	// Each candidate-handling path increments its bucket; values are emitted
	// as TracyPlot at frame end so a capture can be queried to identify
	// which paths fire under which budget/setting combinations. Cross-
	// reference per-action ZoneText emissions (light pointer, reason) to
	// identify *which* lights are hitting each path.
	struct SchedDiagCounters
	{
		int candidates_total = 0;
		int candidates_chosen = 0;
		int candidates_excess = 0;
		int candidates_invalid_camera = 0;
		int candidates_invalid_portal = 0;
		int candidates_invalid_frustum = 0;  // sub-reason: outside camera frustum
		int candidates_invalid_lod = 0;      // sub-reason: lodDimmer zeroed (engine LOD fade)
		int candidates_invalid_other = 0;    // invalidCamera but neither frustum nor LOD flag
		int converted_invalid = 0;           // ConvertLight from c.invalidCamera path
		int converted_excess = 0;            // ConvertLight from c.excess path
		int disabled_invalid = 0;            // DisableLight from c.invalid path (portal/spot/no-convert)
		int disabled_excess = 0;             // DisableLight from c.excess path (spot/no-convert)
		int reconciliation_clears = 0;       // slot freed because light gone from activeShadowLights
		int slots_in_use = 0;                // sampled at frame end
		int first_render_skips = 0;          // chosen lights deferred from shadow set: no valid slice yet
	};
	static SchedDiagCounters s_schedDiag;

	static float ComputeFrameTimePercentile90()
	{
		if (s_ftCount == 0)
			return 16.67f;  // fallback: 60 fps target
		const int n = std::min(s_ftCount, kFrameWindow);
		float tmp[kFrameWindow];
		std::copy(s_ftRing, s_ftRing + n, tmp);
		const int idx = static_cast<int>(n * 0.9f);
		std::nth_element(tmp, tmp + idx, tmp + n);
		return tmp[idx];
	}

	// Maximum ShadowLightCount the installed infrastructure supports.
	// Set once by Install() to the *requested* count; later refined by
	// RefreshInstalledSlotCount() to reflect what the GPU actually allocated.
	// Update() clamps the user-facing setting to this.
	static int32_t s_installedShadowLightCount;

	// What SCM asked the engine for. Equals settings.ShadowLightCount --
	// the sun lives in a separate texture (kSHADOWMAPS_ESRAM), so there's
	// no +1 sun cascade slice in kSHADOWMAPS. Captured at Install so the
	// post-allocation verification can detect VRAM-exhaustion fallbacks
	// where the actual texture ends up smaller than requested.
	static uint32_t s_requestedSlotCount = 0;

	// Total kSHADOWMAPS texture-array capacity *as actually allocated*.
	// 0 until kSHADOWMAPS exists and we've read its real ArraySize back.
	// Owned here (not in Deferred) because SCM is the only thing that
	// modifies the engine's allocation request, and verification of that
	// request is the same code path. Consumers (LLF cluster pipeline,
	// SCM scheduler clamp, SCM UI) read via GetInstalledSlotCount().
	static uint32_t s_installedSlotCount = 0;

	// True once we've logged a verification result. Prevents spam if the
	// SRV stays null forever (vanilla-disabled session) or oscillates.
	static bool s_slotCountLogged = false;

	// Formula instances (allocated at Init if formula strings are non-empty)
	static std::unique_ptr<FormulaHelper> s_formulaScore;
	static std::unique_ptr<FormulaHelper> s_formulaRedrawInterval;
	static std::unique_ptr<FormulaHelper> s_formulaRedrawBudget;

	// Lights converted to normal (non-shadow) lights for diffuse-only rendering
	struct ConvertedLight
	{
		RE::BSShadowLight* light;
		bool isNS;
	};
	static std::vector<ConvertedLight> s_normalConvert;
	static std::set<RE::NiLight*> s_shadowConvert;

	// User suppression set (lightKey = BSShadowLight pointer cast to uintptr_t).
	// Persisted across light lifetimes so suppressing a torch survives the player
	// leaving and returning to a cell.
	static std::unordered_set<uintptr_t> s_suppressedLights;

	// Debugging overrides — see header docs for ClearAllOverrides / SetPinnedShadow / etc.
	// Declared up here (rather than next to s_shadowSlotInfos) because the scheduler
	// reads s_pinShadow / s_pinConvert to bias candidate scoring, and that's compiled
	// long before the table-rendering code.
	static std::unordered_set<uintptr_t> s_pinShadow;   ///< force chosen (top of score sort)
	static std::unordered_set<uintptr_t> s_pinConvert;  ///< force excess + ConvertLight
	static uintptr_t s_soloLight = 0;                   ///< 0 = no solo
	static uintptr_t s_hoverLightKey = 0;               ///< transient (per table draw)

	// =========================================================================
	// Helpers for depth-target index globals
	// SE: 14304EEE8 / AE: n/a (adjacent) / VR: 143180df0
	// =========================================================================
	static int32_t GetDepthTargetType()
	{
		static REL::RelocationID uid(524780, 388826);
		return *reinterpret_cast<int32_t*>(uid.address());
	}

	static int32_t GetDepthTargetSubIndex()
	{
		static REL::RelocationID uid(524780, 388826);
		return *reinterpret_cast<int32_t*>(uid.address() + 4);
	}

	// =========================================================================
	// Hook implementations
	// =========================================================================

	// -------------------------------------------------------------------------
	// Expanded accumulated-lights array
	// The game allocates a local array sized for 8 lights (with +1 sentinel).
	// When using more than 8 shadow casters we extend RDI (SE) / RBX (AE/VR)
	// which is the loop-end counter, and RDX (SE) which is the copy-end counter.
	// -------------------------------------------------------------------------
	static void Hook_AccumulatedLightsArray(CONTEXT& ctx)
	{
		int needed = (s_settings.ShadowLightCount + s_settings.ConvertedShadowSlots + 1) * 2;
		int have = 10;  // game default: (4+1)*2
		int extra = needed - have;
		if (extra > 0) {
			ctx.Rdi += extra;
			// SE only: RDX is a second counter in the same loop.
			if (!REL::Module::IsVR() && REL::Module::GetRuntime() != REL::Module::Runtime::AE)
				ctx.Rdx += extra;
		}
	}

	// -------------------------------------------------------------------------
	// Redirect depth-stencil-view creation to our extended arrays
	// The game loops 0..7 creating depth stencil views and stores each pointer
	// in a game-managed struct at R9.  We redirect R9 to our own arrays so
	// views >= 8 land in globals::features::llf::normalDepthBuffer / globals::features::llf::readOnlyDepthBuffer.
	// -------------------------------------------------------------------------
	static void Hook_CreateNormalDepthBuffer(CONTEXT& ctx)
	{
		// R12 (SE/AE) or R13 (VR) holds a_target * 0x13; value 4*19=76 identifies
		// the shadow-map depth target.  RDI (SE) / RBX (AE/VR) is the loop index.
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;
		int idx = (int)REL::Relocate(ctx.Rdi, ctx.Rbx, ctx.Rbx);
		ctx.R9 = reinterpret_cast<DWORD64>(&globals::features::llf::normalDepthBuffer[idx]);
	}

	static void Hook_CreateReadOnlyDepthBuffer(CONTEXT& ctx)
	{
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;
		int idx = (int)REL::Relocate(ctx.Rdi, ctx.Rbx, ctx.Rbx);
		ctx.R9 = reinterpret_cast<DWORD64>(&globals::features::llf::readOnlyDepthBuffer[idx]);
	}

	// -------------------------------------------------------------------------
	// Copy first 8 views into the game's own DepthStencilData array
	// Called after the creation loop finishes; syncs the game struct so existing
	// code reading depthStencils[4].views[0..7] still works correctly.
	// -------------------------------------------------------------------------
	static void Hook_SetupGameArray(CONTEXT& ctx)
	{
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;
		auto* renderer = reinterpret_cast<RE::BSGraphics::Renderer*>(ctx.R15);
		for (int i = 0; i < 8; i++) {
			renderer->GetDepthStencilData().depthStencils[4].views[i] = reinterpret_cast<ID3D11DepthStencilView*>(globals::features::llf::normalDepthBuffer[i]);
			renderer->GetDepthStencilData().depthStencils[4].readOnlyViews[i] = reinterpret_cast<ID3D11DepthStencilView*>(globals::features::llf::readOnlyDepthBuffer[i]);
		}
	}

	// -------------------------------------------------------------------------
	// Redirect depth-buffer selection at draw time
	// When the active depth target is type 4 (shadow maps), route sub-index
	// lookups through our extended arrays instead of the game struct.
	// Hook #1: renderer in R8, result -> RBX.
	// -------------------------------------------------------------------------
	static void Hook_SelectDepthBuffer1(CONTEXT& ctx)
	{
		auto* data = reinterpret_cast<RE::BSGraphics::RendererData*>(ctx.R8);
		int type = GetDepthTargetType();
		int sub = GetDepthTargetSubIndex();

		if (type == 4) {
			ctx.Rbx = data->readOnlyDepth ? reinterpret_cast<DWORD64>(globals::features::llf::readOnlyDepthBuffer[sub]) : reinterpret_cast<DWORD64>(globals::features::llf::normalDepthBuffer[sub]);
		} else {
			ctx.Rbx = data->readOnlyDepth ? reinterpret_cast<DWORD64>(RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[type].readOnlyViews[sub]) : reinterpret_cast<DWORD64>(RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[type].views[sub]);
		}
	}

	// Hook #2: VR: renderer in R14, result -> RBP; SE/AE: renderer in RBP, result -> R14.
	static void Hook_SelectDepthBuffer2(CONTEXT& ctx)
	{
		bool isVR = REL::Module::IsVR();
		bool readOnly = isVR ? reinterpret_cast<RE::BSGraphics::Renderer*>(ctx.R14)->GetRuntimeData().readOnlyDepth : reinterpret_cast<RE::BSGraphics::Renderer*>(ctx.Rbp)->GetRuntimeData().readOnlyDepth;

		int type = GetDepthTargetType();
		int sub = GetDepthTargetSubIndex();

		DWORD64 result;
		if (type == 4) {
			result = readOnly ? reinterpret_cast<DWORD64>(globals::features::llf::readOnlyDepthBuffer[sub]) : reinterpret_cast<DWORD64>(globals::features::llf::normalDepthBuffer[sub]);
		} else {
			result = readOnly ? reinterpret_cast<DWORD64>(RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[type].readOnlyViews[sub]) : reinterpret_cast<DWORD64>(RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[type].views[sub]);
		}

		if (isVR)
			ctx.Rbp = result;
		else
			ctx.R14 = result;
	}

	// -------------------------------------------------------------------------
	// Release extended depth buffers at renderer shutdown
	// -------------------------------------------------------------------------
	static void ReleaseExtendedDepthBuffers(int shadowCount)
	{
		for (int i = 8; i < shadowCount; i++) {
			if (globals::features::llf::normalDepthBuffer[i]) {
				reinterpret_cast<ID3D11DepthStencilView*>(globals::features::llf::normalDepthBuffer[i])->Release();
				globals::features::llf::normalDepthBuffer[i] = nullptr;
			}
			if (globals::features::llf::readOnlyDepthBuffer[i]) {
				reinterpret_cast<ID3D11DepthStencilView*>(globals::features::llf::readOnlyDepthBuffer[i])->Release();
				globals::features::llf::readOnlyDepthBuffer[i] = nullptr;
			}
		}
	}

	static void Hook_DeleteDepthBuffers_SE(CONTEXT& ctx)
	{
		// Only fire when RBX points at depthStencils[4], not at other delete calls.
		auto* data = reinterpret_cast<RE::BSGraphics::DepthStencilData*>(ctx.Rbx);
		if (data == &RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[4])
			ReleaseExtendedDepthBuffers(s_settings.ShadowLightCount);
	}

	static void Hook_DeleteDepthBuffers_AE(CONTEXT& /*ctx*/)
	{
		ReleaseExtendedDepthBuffers(s_settings.ShadowLightCount);
	}

	// -------------------------------------------------------------------------
	// Force each light to use its assigned shadow map slot
	// RenderCascade would otherwise recalculate a slot index from a global
	// counter, causing lights that weren't re-rendered this frame to corrupt
	// each other's shadow maps.
	// SE: light pointer in R15, slot index out in RSI.
	// VR: light pointer in R14, slot index out in RDX.
	// -------------------------------------------------------------------------
	static void Hook_OverwriteShadowMapIndex(CONTEXT& ctx)
	{
		// Enabled is a boot-time gate (see Init early-return) -- this
		// hook is only installed when SCM is enabled at boot, so it
		// runs unconditionally per-frame from there. Toggling Enabled
		// off at runtime no longer affects the hook; restart is the
		// only safe way to revert. See Hook_CalculateActiveShadowCasters
		// comment for the crash rationale.

		auto* light = reinterpret_cast<RE::BSShadowLight*>(REL::Relocate(ctx.R15, ctx.R15, ctx.R14));
		int32_t idx = s_lights.FindLight(light, s_settings.ShadowLightCount);
		if (idx < 0)
			idx = 0;  // should not happen; fail-safe to slot 0
		// Sun (pool[0] when Sun=true) writes 0 here too — harmless since sun's
		// descriptor.shadowmapIndex is unused (sun renders to kSHADOWMAPS_ESRAM).

		if (REL::Module::IsVR())
			ctx.Rdx = static_cast<DWORD64>(idx);
		else
			ctx.Rsi = static_cast<DWORD64>(idx);
	}

	// -------------------------------------------------------------------------
	// Color-mask pass skip and overflow fix
	// -------------------------------------------------------------------------

	// Replaces the DrawColorMask call in 107140.
	// Must use install_context_hook (not write_thunk_call) so RtlRestoreContext preserves
	// all volatile registers (rdx, r8, etc.) for the call at 107140+0x83 that
	// immediately follows and passes them directly into 107141.
	static void Hook_DisableColorMask(CONTEXT& /*ctx*/)
	{
		// ReturnShadowmaps (ClearShadowMapData) on all current shadow casters
		// so the game doesn't try to draw a color mask into our extended slots.
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn)
			return;
		ForEachShadowLight(ssn->GetRuntimeData().shadowLightsAccum,
			[](RE::BSShadowLight* l) { l->ReturnShadowmaps(); });
	};

	// =========================================================================
	// LightContainer methods
	// =========================================================================

	int32_t LightContainer::FindFreeIndex(bool shadowSlot, int32_t shadowCount, int32_t convertCount) const
	{
		// Pool layout when Sun=true:  [0]=sun, [1..shadowCount]=point lights, [shadowCount+1..]=converted
		//                  Sun=false: [0..shadowCount-1]=point lights,        [shadowCount..]=converted
		const int32_t sunOff = Sun ? 1 : 0;
		if (shadowSlot) {
			for (int i = sunOff; i < sunOff + shadowCount; i++)
				if (!Lights[i].Light)
					return i;
		} else {
			const int32_t base = sunOff + shadowCount;
			for (int i = base; i < base + convertCount; i++)
				if (!Lights[i].Light)
					return i;
		}
		return -1;
	}

	int32_t LightContainer::FindLight(RE::BSShadowLight* light, int32_t shadowCount) const
	{
		// Search both the sun slot (when Sun=true) and the point-light range.
		// Hook_OverwriteShadowMapIndex is called for the sun too, so it must
		// be findable here. Range matches FindFreeIndex's allocation range to
		// fix a long-standing off-by-one (FindFreeIndex assigned to slots
		// 1..shadowCount but FindLight searched 0..shadowCount-1, so a light
		// at slot shadowCount was unfindable and its shadowmapIndex fell back
		// to 0, corrupting the sun's slot).
		const int32_t sunOff = Sun ? 1 : 0;
		const int32_t maxIdx = sunOff + shadowCount;
		for (int i = 0; i < maxIdx; i++)
			if (Lights[i].Light == light)
				return i;
		return -1;
	}

	std::uint32_t MaxShadowAccumIterationBound()
	{
		// Each entry advances idx by its shadowMapCount. The widest type is
		// BSShadowDirectionalLight (4 cascades). With ShadowLightCount user-
		// capped at 127 and one sun bookkeeping slot, the realistic walked
		// index never exceeds (1 + 127) * 4 = 512. Add a small margin so a
		// transient mismatch between live settings and the engine's already-
		// populated array doesn't tripwire iteration. If settings haven't
		// initialised yet (s_settings is the default-constructed value with
		// ShadowLightCount=16), the bound is still generous.
		constexpr std::uint32_t kCascadesPerLight = 4;
		constexpr std::uint32_t kMargin = 16;
		const std::uint32_t lights = static_cast<std::uint32_t>(std::max(1, s_settings.ShadowLightCount));
		return (lights + 1) * kCascadesPerLight + kMargin;
	}

	// Verdict for a candidate shadow-array footprint vs the DXGI budget.
	// "tight" = free VRAM below 512 MB or shadow array > 25% of budget.
	// "over"  = free VRAM below 128 MB or shadow array > 50% of budget.
	// Driven by free headroom rather than shadow share because a small
	// array next to a tight budget is just as risky as a huge one in a
	// roomy budget.
	struct VRAMVerdict
	{
		bool tight = false;
		bool over = false;
		ImVec4 colour{ 0.55f, 0.85f, 0.55f, 1 };  // green by default
	};
	static VRAMVerdict EvaluateVRAMVerdict(std::uint64_t shadowBytes, std::uint64_t freeBytes, std::uint64_t budgetBytes)
	{
		constexpr std::uint64_t kTightFree = 512ull * 1024 * 1024;
		constexpr std::uint64_t kOverFree = 128ull * 1024 * 1024;
		VRAMVerdict v;
		v.tight = freeBytes < kTightFree || shadowBytes * 4 > budgetBytes;
		v.over = freeBytes < kOverFree || shadowBytes * 2 > budgetBytes;
		v.colour = v.over  ? ImVec4(0.95f, 0.35f, 0.35f, 1) :
		           v.tight ? ImVec4(0.95f, 0.85f, 0.25f, 1) :
		                     ImVec4(0.55f, 0.85f, 0.55f, 1);
		return v;
	}

	// Reads kSHADOWMAPS's underlying Texture2D desc, bypassing the SRV's
	// ViewDimension. Skyrim creates the SRV with a non-array view dimension
	// even though the resource itself is a Texture2DArray, so reading
	// `desc.Texture2DArray.ArraySize` from the SRV desc returns 0; only the
	// texture's own ArraySize is reliable. Returns false on any failure
	// stage; out param is left untouched.
	static bool TryReadShadowTextureDesc(D3D11_TEXTURE2D_DESC& out)
	{
		auto* renderer = globals::game::renderer;
		if (!renderer)
			return false;
		auto* srv = renderer->GetDepthStencilData()
		                .depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kSHADOWMAPS]
		                .depthSRV;
		if (!srv)
			return false;
		winrt::com_ptr<ID3D11Resource> resource;
		srv->GetResource(resource.put());
		if (!resource)
			return false;
		winrt::com_ptr<ID3D11Texture2D> tex;
		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(tex.put()))))
			return false;
		D3D11_TEXTURE2D_DESC desc{};
		tex->GetDesc(&desc);
		if (desc.ArraySize == 0)
			return false;
		out = desc;
		return true;
	}

	// Lazily verifies that the engine's actual kSHADOWMAPS slice count
	// matches what SCM patched in. Self-healing: bails until the texture
	// is readable, then early-returns. Cross-checks against the requested
	// count and clamps the scheduler on mismatch so out-of-bounds slice
	// indexing can't occur after a VRAM-exhaustion fallback.
	void RefreshInstalledSlotCount()
	{
		if (s_installedSlotCount > 0)
			return;

		D3D11_TEXTURE2D_DESC desc{};
		if (!TryReadShadowTextureDesc(desc))
			return;

		uint32_t actual = desc.ArraySize;
		s_installedSlotCount = actual;
		if (s_slotCountLogged)
			return;
		s_slotCountLogged = true;
		if (s_requestedSlotCount && actual != s_requestedSlotCount) {
			logger::warn(
				"[SCM] Requested {} kSHADOWMAPS slots, GPU allocated {} -- "
				"clamping scheduler to the actual count.",
				s_requestedSlotCount, actual);
			s_installedShadowLightCount = std::min(s_installedShadowLightCount, static_cast<int32_t>(actual));
		} else {
			logger::info("[SCM] kSHADOWMAPS array verified: {} slots allocated", actual);
		}
	}

	uint32_t GetInstalledSlotCount()
	{
		// Lazy-refresh; cheap once verified. Fall back to the requested
		// count when verification can't complete -- a non-zero slot count
		// is needed for the cluster pipeline to engage shadow handling.
		// Out-of-bounds slice indexes are hardware-clamped in D3D11, so a
		// transient over-estimate yields stale shadow data rather than a
		// crash.
		RefreshInstalledSlotCount();
		return s_installedSlotCount > 0 ? s_installedSlotCount : s_requestedSlotCount;
	}

	// Resolution actually used to allocate kSHADOWMAPS this session. Captured
	// lazily from the real D3D11 texture geometry the first time it becomes
	// readable -- NOT from the RE::Setting at Install() time. The engine's
	// SkyrimPrefs.ini load happens after our PostPostLoad hook, so a snapshot
	// at Install() catches the hardcoded default (e.g. 2048) before the
	// user's INI value (e.g. 4096) is applied. Reading from the texture is
	// the source of truth either way -- it reflects what was actually
	// allocated, regardless of where the setting ended up.
	static std::int32_t s_initialShadowMapResolution = 0;

	// kSHADOWMAPS footprint = w*h*bytesPerPixel*ArraySize. Per-slice cost
	// is 64 MB at 4K D32_FLOAT; arrays grow linearly with ShadowLightCount.
	// Returned info.valid is false only when both the DXGI budget query
	// and the texture/INI fallback fail (rare).
	VRAMInfo GetVRAMInfo()
	{
		VRAMInfo info{};

		// DXGI budget. Prefer Menu's cached adapter; fall back to the
		// device-derived path before Menu::Init() has run.
		winrt::com_ptr<IDXGIAdapter3> adapter3;
		if (auto* menu = Menu::GetSingleton())
			adapter3 = menu->GetDXGIAdapter3();
		if (!adapter3 && globals::d3d::device) {
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			if (SUCCEEDED(globals::d3d::device->QueryInterface(dxgiDevice.put()))) {
				winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
				if (SUCCEEDED(dxgiDevice->GetAdapter(dxgiAdapter.put())))
					dxgiAdapter->QueryInterface(adapter3.put());
			}
		}
		if (adapter3) {
			DXGI_QUERY_VIDEO_MEMORY_INFO vmem{};
			HRESULT hr = adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmem);
			if (SUCCEEDED(hr) && vmem.Budget > 0) {
				info.currentUsageBytes = vmem.CurrentUsage;
				info.budgetBytes = vmem.Budget;
			}
		}

		// kSHADOWMAPS geometry from the underlying texture (when readable).
		D3D11_TEXTURE2D_DESC desc{};
		if (TryReadShadowTextureDesc(desc)) {
			info.shadowWidth = desc.Width;
			info.shadowHeight = desc.Height;
			info.shadowSlices = desc.ArraySize;
			// Latch the texture's allocated resolution as the canonical
			// "what this session is using" value -- this is what the UI's
			// restart-required indicator compares against. Once latched it
			// doesn't move for the session (kSHADOWMAPS is allocated once).
			if (s_initialShadowMapResolution == 0)
				s_initialShadowMapResolution = static_cast<std::int32_t>(desc.Width);
			// Default to 4 B/pixel (R32_TYPELESS / D32_FLOAT — the format
			// Skyrim ships with) and override for stencil-packed variants.
			std::uint32_t bytesPerPixel = 4;
			switch (desc.Format) {
			case DXGI_FORMAT_R32G8X24_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
			case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
				bytesPerPixel = 8;
				break;
			case DXGI_FORMAT_R16_TYPELESS:
			case DXGI_FORMAT_D16_UNORM:
			case DXGI_FORMAT_R16_UNORM:
				bytesPerPixel = 2;
				break;
			default:
				break;  // 4 B fallback covers R24G8 and R32 families
			}
			info.bytesPerSlice = info.shadowWidth * info.shadowHeight * bytesPerPixel;
			info.shadowArrayBytes = static_cast<std::uint64_t>(info.bytesPerSlice) * info.shadowSlices;
		}

		// INI-based fallback when the texture isn't readable yet (e.g.
		// main menu, before BSShaderRenderTargets_Create). Resolution
		// from SkyrimPrefs.ini, slot count from settings; assume the
		// stock D32_FLOAT format (4 B/pixel).
		if (info.bytesPerSlice == 0) {
			std::uint32_t res = 4096;  // SkyrimPrefs.ini default
			if (auto* prefColl = RE::INIPrefSettingCollection::GetSingleton()) {
				if (auto* setting = prefColl->GetSetting("iShadowMapResolution:Display")) {
					int v = setting->GetInteger();
					if (v > 0)
						res = static_cast<std::uint32_t>(v);
				}
			}
			info.shadowWidth = res;
			info.shadowHeight = res;
			info.bytesPerSlice = info.shadowWidth * info.shadowHeight * 4;
			info.shadowSlices = static_cast<std::uint32_t>(s_settings.ShadowLightCount);
			info.shadowArrayBytes = static_cast<std::uint64_t>(info.bytesPerSlice) * info.shadowSlices;
		}

		// Budget and per-slice are independent so a partial answer still
		// renders (budget alone shows VRAM headroom, per-slice alone shows
		// projection from the INI fallback).
		info.valid = info.budgetBytes > 0 || info.bytesPerSlice > 0;

		// One-shot log on first valid observation. Any caller trips it.
		static bool s_loggedFirstValid = false;
		if (info.valid && !s_loggedFirstValid) {
			s_loggedFirstValid = true;
			const std::uint64_t freeBytes = info.budgetBytes > info.currentUsageBytes ? info.budgetBytes - info.currentUsageBytes : 0;
			const float arrayMB = static_cast<float>(info.shadowArrayBytes) / (1024.f * 1024.f);
			const float perSliceMB = static_cast<float>(info.bytesPerSlice) / (1024.f * 1024.f);
			const float budgetMB = static_cast<float>(info.budgetBytes) / (1024.f * 1024.f);
			const float usageMB = static_cast<float>(info.currentUsageBytes) / (1024.f * 1024.f);
			logger::info(
				"[SCM] kSHADOWMAPS {}x{} x {} slices, {:.2f} MB/slice -> {:.1f} MB "
				"(VRAM {:.1f}/{:.1f} MB used, ShadowLightCount={})",
				info.shadowWidth, info.shadowHeight, info.shadowSlices,
				perSliceMB, arrayMB, usageMB, budgetMB, s_settings.ShadowLightCount);
			if (info.shadowArrayBytes > freeBytes) {
				logger::warn(
					"[SCM] Shadow texture array ({:.1f} MB) exceeds remaining VRAM budget "
					"({:.1f} MB). Lower Shadow Light Count or iShadowMapResolution if you "
					"see stutter or driver hitches.",
					arrayMB, static_cast<float>(freeBytes) / (1024.f * 1024.f));
			}
		}

		return info;
	}

	std::uint64_t ProjectShadowArrayBytes(std::uint32_t sliceCount)
	{
		auto info = GetVRAMInfo();
		if (!info.valid)
			return 0;
		return static_cast<std::uint64_t>(info.bytesPerSlice) * sliceCount;
	}

	// =========================================================================
	// BudgetEntry / BudgetTracker methods
	// =========================================================================

	static int64_t GetPerfCounter()
	{
		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);

		int64_t t = (int64_t)counter.QuadPart;

		static int64_t freq = 0;
		if (freq == 0) {
			LARGE_INTEGER f;
			QueryPerformanceFrequency(&f);
			freq = f.QuadPart / 1000000;
		}

		return t / freq;
	}

	void BudgetEntry::BeginStep(int32_t /*step*/)
	{
		_startTime = GetPerfCounter();
	}

	void BudgetEntry::EndStep(int32_t step, int32_t helperCounter)
	{
		int64_t diff = GetPerfCounter() - _startTime;

		if (step == 0) {
			Progress = static_cast<uint32_t>(std::min(diff, (int64_t)0xFFFFFFFF));
		} else if (step == 1) {
			diff += Progress;
			int32_t ix = TrackedCount % kBudgetWindowSize;
			Current -= Tracked[ix];
			Tracked[ix] = static_cast<uint32_t>(std::min(diff, (int64_t)0xFFFFFFFF));
			Current += Tracked[ix];
			TrackedCount++;
			LastTrackedHelper = helperCounter;
		}
	}

	bool BudgetEntry::IsExpired(int32_t helperCounter) const
	{
		return LastTrackedHelper < 0 || (helperCounter - LastTrackedHelper) >= 600;
	}

	void BudgetTracker::Begin(int32_t step)
	{
		if (step == 0) {
			_counter++;
			// Amortise the GC: a periodic full-map walk that freed every
			// expired BudgetEntry in one frame caused ~10s-cadence stutters
			// (300 frames at 30 fps) because the heap freed dozens of
			// unique_ptr<BudgetEntry> back to back, taking a heap lock for
			// each. Run incrementally every 30 frames (~0.5s at 60fps) and
			// cap erasures per call so the cost spreads across many frames
			// instead of spiking once.
			if ((_counter % 30) == 0)
				CleanupExpired();
		}
	}

	void BudgetTracker::BeginLight(RE::BSShadowLight* light, int32_t step)
	{
		uint64_t key = reinterpret_cast<uint64_t>(light);
		auto& e = _map[key];
		if (!e) {
			e = std::make_unique<BudgetEntry>();
			e->Key = key;
		}
		e->BeginStep(step);
	}

	void BudgetTracker::EndLight(RE::BSShadowLight* light, int32_t step)
	{
		uint64_t key = reinterpret_cast<uint64_t>(light);
		auto it = _map.find(key);
		if (it == _map.end())
			return;
		it->second->EndStep(step, _counter);
	}

	int32_t BudgetTracker::GetCost(RE::BSShadowLight* light) const
	{
		uint64_t key = reinterpret_cast<uint64_t>(light);
		auto it = _map.find(key);
		if (it == _map.end() || it->second->TrackedCount == 0)
			return GetAverageCostUs();  // unknown light: fall back to fleet average
		int32_t n = std::min(kBudgetWindowSize, it->second->TrackedCount);
		return it->second->Current / std::max(1, n);
	}

	void BudgetTracker::CleanupExpired()
	{
		ZoneScopedN("SCM::BudgetTracker::CleanupExpired");
		// Hard cap on erasures per call so a wave of expirations (e.g. the
		// player crossed a cell boundary 600 frames ago and dozens of
		// shadow lights all expire on the same tick) spreads its heap-free
		// cost across many frames instead of stalling one frame. With
		// kMaxErasePerCall=4 and Begin() calling this every 30 frames, the
		// tracker can drain ~8 expired entries per second steady-state and
		// up to 4 per call worst-case -- enough to keep the map bounded
		// in practice without the periodic stutter.
		constexpr size_t kMaxErasePerCall = 4;
		size_t erased = 0;
		for (auto it = _map.begin(); it != _map.end() && erased < kMaxErasePerCall;) {
			if (it->second->IsExpired(_counter)) {
				it = _map.erase(it);
				++erased;
			} else {
				++it;
			}
		}
	}

	int32_t BudgetTracker::GetAverageCostUs() const
	{
		int64_t sum = 0;
		int32_t count = 0;
		for (auto& [k, entry] : _map) {
			int32_t n = std::min(kBudgetWindowSize, entry->TrackedCount);
			if (n == 0)
				continue;
			sum += entry->Current / std::max(1, n);
			count++;
		}
		return count > 0 ? static_cast<int32_t>(sum / count) : 0;
	}

	// =========================================================================
	// Game accessor helpers
	//
	// Thin wrappers around game globals and engine functions.
	// All REL::RelocationID pairs are (SE_id, AE_id).
	// VR addresses verified against the VR address library CSV.
	// =========================================================================

	// ---------- globals ----------

	static RE::ShadowSceneNode* GetShadowSceneNode()
	{
		static REL::RelocationID uid(513211, 390951);
		return *reinterpret_cast<RE::ShadowSceneNode**>(uid.address());
	}

	static RE::NiCamera* GetWorldCamera()
	{
		// world scene graph -> camera
		static REL::RelocationID uid(528087, 415032);
		auto* sg = *reinterpret_cast<RE::BSSceneGraph**>(uid.address());
		return sg ? sg->GetRuntimeData().camera.get() : nullptr;
	}

	static bool GetSunBool1()
	{
		static REL::RelocationID uid(513201, 390932);
		return *reinterpret_cast<bool*>(uid.address());
	}
	static int GetSunInt1()
	{
		static REL::RelocationID uid(527703, 414625);
		return *reinterpret_cast<int*>(uid.address());
	}
	static bool GetSunBool2()
	{
		static REL::RelocationID uid(528095, 415040);
		return *reinterpret_cast<bool*>(uid.address());
	}

	static bool* GetFocusShadowSelected()
	{
		static REL::RelocationID uid(528096, 415041);
		return reinterpret_cast<bool*>(uid.address());
	}
	static uint64_t* GetSunPtr()
	{
		static REL::RelocationID uid(528315, 415267);
		return reinterpret_cast<uint64_t*>(uid.address());
	}

	// Current accumulated shadow slot (used as Accumulate() first arg).
	static uint32_t* GetAccumLightSlot()
	{
		static REL::RelocationID uid(528091, 415036);
		return reinterpret_cast<uint32_t*>(uid.address());
	}
	// Running mask index counter (incremented each time a light is slotted).
	static uint32_t* GetMaskIndex()
	{
		static REL::RelocationID uid(528091, 415036);
		return reinterpret_cast<uint32_t*>(uid.address() + 4);
	}
	// Active shadow caster bitmask (ORed per slot).
	static uint32_t* GetShadowMask()
	{
		static REL::RelocationID uid(528093, 415038);
		return reinterpret_cast<uint32_t*>(uid.address());
	}
	// Written back to the game at the end of scheduling.
	static uint32_t* GetFrameLightCount()
	{
		static REL::RelocationID uid(528090, 415035);
		return reinterpret_cast<uint32_t*>(uid.address());
	}

	// VR-only globals
	static bool GetVRDrawShadows()
	{
		static REL::Offset uid{ 0x1ed3cb0 };
		return *reinterpret_cast<bool*>(uid.address());
	}
	static bool GetVRAccumFirst()
	{
		static REL::Offset uid{ 0x1ed4118 };
		return *reinterpret_cast<bool*>(uid.address());
	}
	static float GetVRDRSWidthRatio()
	{
		static REL::Offset bDis{ 0x3186d28 }, r{ 0x3186d14 };
		return *reinterpret_cast<int*>(bDis.address()) ? 1.0f : *reinterpret_cast<float*>(r.address());
	}
	static float GetVRDRSHeightRatio()
	{
		static REL::Offset bDis{ 0x3186d28 }, r{ 0x3186d18 };
		return *reinterpret_cast<int*>(bDis.address()) ? 1.0f : *reinterpret_cast<float*>(r.address());
	}

	// ---------- engine function wrappers ----------

	static void GameAccumulate(RE::BSShadowLight* light)
	{
		// BSShadowDirectionalLight::AccumulateFullFrustumCascades / unk_Accumulate
		using F = void (*)(RE::BSShadowLight*);
		static REL::Relocation<F> func{ REL::RelocationID(100819, 107603) };
		func(light);
	}

	static void GameSetupDirectionalLight(RE::BSShadowLight* light, RE::NiCamera* cam)
	{
		using F = void (*)(RE::BSShadowLight*, RE::NiCamera*);
		static REL::Relocation<F> func{ REL::RelocationID(100817, 107601) };
		func(light, cam);
	}

	static void GameEnableLight(RE::ShadowSceneNode* ssn, RE::BSLight* light)
	{
		using F = void (*)(RE::ShadowSceneNode*, RE::BSLight*);
		static REL::Relocation<F> func{ REL::RelocationID(99708, 106342) };
		func(ssn, light);
	}

	static void GameSetShadowCasterSlot(RE::ShadowSceneNode* ssn, RE::BSLight* light, uint32_t index, uint32_t unk)
	{
		using F = void (*)(RE::ShadowSceneNode*, RE::BSLight*, uint32_t, uint32_t);
		static REL::Relocation<F> func{ REL::RelocationID(99728, 106365) };
		func(ssn, light, index, unk);
	}

	static void GameClearPortalVisibility(RE::BSPortalGraphEntry* entry)
	{
		using F = void (*)(RE::BSPortalGraphEntry*);
		static REL::Relocation<F> func{ REL::RelocationID(74395, 76119) };
		func(entry);
	}

	static bool GamePortalHasSharedVisibility(RE::BSPortalGraphEntry* a, RE::BSPortalGraphEntry* b)
	{
		using F = bool (*)(RE::BSPortalGraphEntry*, RE::BSPortalGraphEntry*);
		static REL::Relocation<F> func{ REL::RelocationID(74397, 76121) };
		return func(a, b);
	}

	static void GameClearGeometryList(RE::BSLight* light)
	{
		using F = void (*)(RE::BSLight*);
		static REL::Relocation<F> func{ REL::RelocationID(101298, 108285) };
		func(light);
	}

	static bool GameIsLightAffectingSurface(RE::BSLightingShaderProperty* p, RE::BSLight* light)
	{
		using F = bool (*)(RE::BSLightingShaderProperty*, RE::BSLight*);
		static REL::Relocation<F> func{ REL::RelocationID(98902, 105550) };
		return func(p, light);
	}

	static void GameApplyLensFlare(RE::BSLight* light)
	{
		// SE/AE only -- no VR equivalent (ID 100440)
		if (REL::Module::IsVR())
			return;
		using F = void (*)(RE::BSLight*);
		static REL::Relocation<F> func{ REL::RelocationID(100440, 107157) };
		func(light);
	}

	// VR-only
	static void GameVRPrepareShadowMaps(RE::BSLight* light)
	{
		using F = void (*)(RE::BSLight*);
		static REL::Relocation<F> func{ REL::Offset(0x1356e50) };
		func(light);
	}

	static void GameVRAccumulateShadowMaps(RE::BSLight* light)
	{
		using F = void (*)(RE::BSLight*);
		static REL::Relocation<F> func{ REL::Offset(0x1357450) };
		func(light);
	}

	static void GameFrustumOverlap(RE::NiCamera* cam, float* coord, float* r1, float* r2, float eps)
	{
		// Non-VR: (cam, coord, r1, r2, eps)
		// VR:     (cam, coord, r1, r2, eyeIndex, eps)  -- pass 0xffffffff for combined frustum
		static REL::Relocation<uintptr_t> addr{ REL::RelocationID(69265, 70632) };
		auto ptr = addr.address();
		if (REL::Module::IsVR()) {
			using VR = void (*)(RE::NiCamera*, float*, float*, float*, uint32_t, float);
			reinterpret_cast<VR>(ptr)(cam, coord, r1, r2, 0xffffffffu, eps);
		} else {
			using SE = void (*)(RE::NiCamera*, float*, float*, float*, float);
			reinterpret_cast<SE>(ptr)(cam, coord, r1, r2, eps);
		}
	}

	// Convenience: runtime-aware shadow-light field accessor (SE vs VR RuntimeData differ).
	// Usage: ShadowField(light, maskIndex) = 3;
#define ShadowField(light, member) \
	(REL::Module::IsVR() ? (light)->GetVRRuntimeData().member : (light)->GetRuntimeData().member)

	// Returns the culling process for the first shadow descriptor of a light.
	static RE::BSCullingProcess* GetLightCullingProcess(RE::BSShadowLight* light)
	{
		return REL::Module::IsVR() ? light->GetVRRuntimeData().shadowmapDescriptors.front().cullingProcess : light->GetRuntimeData().shadowmapDescriptors.front().cullingProcess;
	}

	// =========================================================================
	// Formula helpers
	//
	// SetupSceneFormula: called once per frame, sets camera/scene params.
	// SetupLightFormula: called per candidate light, sets all light params.
	// CalculateLightScore: evaluates s_formulaScore if available.
	// =========================================================================

	static void SetupSceneFormula(const RE::NiCamera* camera)
	{
		if (camera) {
			FormulaHelper::SetParam(kFormulaParam_CameraX, camera->world.translate.x);
			FormulaHelper::SetParam(kFormulaParam_CameraY, camera->world.translate.y);
			FormulaHelper::SetParam(kFormulaParam_CameraZ, camera->world.translate.z);
		} else {
			FormulaHelper::SetParam(kFormulaParam_CameraX, 0.0);
			FormulaHelper::SetParam(kFormulaParam_CameraY, 0.0);
			FormulaHelper::SetParam(kFormulaParam_CameraZ, 0.0);
		}

		FormulaHelper::SetParam(kFormulaParam_IsInterior, 0);
		auto* plr = RE::PlayerCharacter::GetSingleton();
		if (plr) {
			auto* cell = plr->parentCell;
			if (cell && cell->IsInteriorCell())
				FormulaHelper::SetParam(kFormulaParam_IsInterior, 1);
		}

		// Time of day from GameHour global
		auto* cal = RE::Calendar::GetSingleton();
		if (cal)
			FormulaHelper::SetParam(kFormulaParam_TimeOfDay, cal->GetHour());
	}

	static void SetupLightFormula(const RE::BSShadowLight* light, const RE::NiCamera* camera, int32_t index)
	{
		FormulaHelper::SetParam(kFormulaParam_LightConverted, 0.0);
		FormulaHelper::SetParam(kFormulaParam_LightIndex, index);
		FormulaHelper::SetParam(kFormulaParam_LightDisplacement, 0.0);    // overridden per-entry in redraw interval loop
		FormulaHelper::SetParam(kFormulaParam_PlayerLightDistance, 0.0);  // overridden below after light position is known
		FormulaHelper::SetParam(kFormulaParam_LightImportance, 0.0);      // overridden per-entry in redraw interval loop; 0 in score formula

		// Temporal stickiness signals. Both derived from the slot pool in one
		// pass: chosenLastFrame is the boolean kept for backward-compat with
		// user formulas; framesSinceRender is a continuous age that decays to
		// zero stickiness once the slot has been stale long enough to no
		// longer represent a true rank-drift case. Sentinel 1e6 covers the
		// "no slot" and "never rendered" branches so the default formula's
		// max(0, 1 - age/window) decay term cleanly collapses to 0.
		double chosenLastFrame = 0.0;
		double framesSinceRender = 1e6;
		{
			const int32_t now = *globals::game::frameCounter;
			for (int i = s_lights.PointLightFirst(); i < s_lights.PointLightEnd(s_settings.ShadowLightCount); i++) {
				const auto& e = s_lights.Lights[i];
				if (e.Light != light)
					continue;
				chosenLastFrame = 1.0;
				if (e.LastDrawnFrame >= 0)
					framesSinceRender = static_cast<double>(now - e.LastDrawnFrame);
				break;
			}
		}
		FormulaHelper::SetParam(kFormulaParam_LightChosenLastFrame, chosenLastFrame);
		FormulaHelper::SetParam(kFormulaParam_LightFramesSinceRender, framesSinceRender);

		FormulaHelper::SetParam(kFormulaParam_LightNeverFades, light->lodFade ? 0.0 : 1.0);
		FormulaHelper::SetParam(kFormulaParam_LightPortalStrict, light->portalStrict ? 1.0 : 0.0);
		FormulaHelper::SetParam(kFormulaParam_LightNS, 0.0);

		// Spot detection + cone-aware visibility prior (option 1 from spot
		// preservation analysis). Non-spots get spotvisible=1 so existing
		// omni-tuned formulas are unaffected. For spots, we read last
		// frame's UpdateCamera verdict (frustumCull / lodDimmer) -- the
		// score runs BEFORE this frame's validation pass updates those,
		// but cameras move continuously so last-frame's cone-vs-frustum
		// is a strong predictor of this-frame's. Trading a one-frame lag
		// for not double-calling UpdateCamera is a worthwhile cost since
		// the score is a preference, not a gate.
		const bool isSpot = (skyrim_cast<const RE::BSShadowFrustumLight*>(light) != nullptr);
		double spotVisible = 1.0;  // default for non-spots: always "visible"
		if (isSpot) {
			// frustumCull == 0 means "in frustum"; engine sets 0xff when
			// cone-vs-frustum rejects. lodDimmer > 0 means the LOD fader
			// hasn't zeroed the light. Both must hold for a spot to count
			// as plausibly visible.
			// Note: the engine field is misspelled "frustrumCull" in the SDK
			// (matches Bethesda's original symbol). 0 = visible, 0xff = culled.
			const bool inFrustum = (light->frustrumCull == 0);
			const bool lodLit = (light->lodDimmer > 0.0f);
			spotVisible = (inFrustum && lodLit) ? 1.0 : 0.0;
		}
		FormulaHelper::SetParam(kFormulaParam_LightIsSpot, isSpot ? 1.0 : 0.0);
		FormulaHelper::SetParam(kFormulaParam_LightSpotVisible, spotVisible);

		float x, y, z;

		auto* nilight = light->light.get();
		if (nilight) {
			FormulaHelper::SetParam(kFormulaParam_LightIntensity, nilight->GetLightRuntimeData().fade);
			FormulaHelper::SetParam(kFormulaParam_LightRadius, nilight->GetLightRuntimeData().radius.x);
			FormulaHelper::SetParam(kFormulaParam_LightR, nilight->GetLightRuntimeData().diffuse.red);
			FormulaHelper::SetParam(kFormulaParam_LightG, nilight->GetLightRuntimeData().diffuse.green);
			FormulaHelper::SetParam(kFormulaParam_LightB, nilight->GetLightRuntimeData().diffuse.blue);
			FormulaHelper::SetParam(kFormulaParam_LightAmbientR, nilight->GetLightRuntimeData().ambient.red);
			FormulaHelper::SetParam(kFormulaParam_LightAmbientG, nilight->GetLightRuntimeData().ambient.green);
			FormulaHelper::SetParam(kFormulaParam_LightAmbientB, nilight->GetLightRuntimeData().ambient.blue);
			x = nilight->world.translate.x;
			y = nilight->world.translate.y;
			z = nilight->world.translate.z;

			if (s_settings.PromoteNormalToShadow)
				FormulaHelper::SetParam(kFormulaParam_LightNS, s_shadowConvert.find(nilight) != s_shadowConvert.end() ? 1.0 : 0.0);
		} else {
			FormulaHelper::SetParam(kFormulaParam_LightIntensity, 0.0);
			FormulaHelper::SetParam(kFormulaParam_LightRadius, 0.0);
			FormulaHelper::SetParam(kFormulaParam_LightR, 1.0);
			FormulaHelper::SetParam(kFormulaParam_LightG, 1.0);
			FormulaHelper::SetParam(kFormulaParam_LightB, 1.0);
			FormulaHelper::SetParam(kFormulaParam_LightAmbientR, 1.0);
			FormulaHelper::SetParam(kFormulaParam_LightAmbientG, 1.0);
			FormulaHelper::SetParam(kFormulaParam_LightAmbientB, 1.0);
			x = light->worldTranslate.x;
			y = light->worldTranslate.y;
			z = light->worldTranslate.z;
		}

		FormulaHelper::SetParam(kFormulaParam_LightX, x);
		FormulaHelper::SetParam(kFormulaParam_LightY, y);
		FormulaHelper::SetParam(kFormulaParam_LightZ, z);

		float camx = camera ? camera->world.translate.x : (float)FormulaHelper::GetParam(kFormulaParam_CameraX);
		float camy = camera ? camera->world.translate.y : (float)FormulaHelper::GetParam(kFormulaParam_CameraY);
		float camz = camera ? camera->world.translate.z : (float)FormulaHelper::GetParam(kFormulaParam_CameraZ);

		float dx = x - camx, dy = y - camy, dz = z - camz;
		FormulaHelper::SetParam(kFormulaParam_LightDistance, sqrtf(dx * dx + dy * dy + dz * dz));

		// Player-to-light distance: ensures third-person shadow maps redraw when the
		// player character is inside a light's radius even if the camera is outside.
		double playerLightDist = FormulaHelper::GetParam(kFormulaParam_LightDistance);
		auto* plr = RE::PlayerCharacter::GetSingleton();
		if (plr) {
			auto pp = plr->GetPosition();
			float pdx = x - pp.x, pdy = y - pp.y, pdz = z - pp.z;
			playerLightDist = static_cast<double>(sqrtf(pdx * pdx + pdy * pdy + pdz * pdz));
		}
		FormulaHelper::SetParam(kFormulaParam_PlayerLightDistance, playerLightDist);
	}

	static double CalculateLightScore(const RE::BSShadowLight* light, const RE::NiCamera* camera, int32_t index)
	{
		SetupLightFormula(light, camera, index);

		if (s_formulaScore)
			return s_formulaScore->Calculate();

		return 0.0;
	}

	// =========================================================================
	// Shadow map content hash for cached-shadow-map detection
	// =========================================================================

	/// Mixes a 32-bit value into a running 64-bit hash. boost::hash_combine
	/// constants -- the magic number 0x9e3779b9 is the golden-ratio reciprocal,
	/// chosen for good bit distribution. Fast (a few ALU ops) and we don't
	/// need cryptographic strength -- only that distinct inputs map to
	/// distinct outputs with very high probability.
	static inline std::uint64_t HashCombine(std::uint64_t h, std::uint32_t v) noexcept
	{
		return h ^ (static_cast<std::uint64_t>(v) + 0x9e3779b9ull + (h << 6) + (h >> 2));
	}
	static inline std::uint64_t HashCombineFloat(std::uint64_t h, float f) noexcept
	{
		return HashCombine(h, std::bit_cast<std::uint32_t>(f));
	}

	/// Quantize a float to a step size before hashing. Skyrim's kFlicker /
	/// kPulse light flags oscillate animated torches by sub-unit position /
	/// radius amounts every frame. Bit-exact hashing on those oscillations
	/// produces a fresh hash every frame, defeating cache validity. Quantizing
	/// at sub-pixel-precision thresholds folds imperceptible animations into
	/// a stable hash bucket so the cached-shadow priority demotion fires
	/// correctly for visually-unchanging lights.
	static inline float QuantizeFloat(float f, float step) noexcept
	{
		return std::round(f / step) * step;
	}

	/// Hash the inputs that determine a shadow map's content. Two inputs:
	///   (1) the light's own pose + radius -- moves/rotates/resizes the
	///       shadow frustum, all change the rendered depths
	///   (2) each caster's worldBound (center + radius) and identity --
	///       worldBound is engine-maintained: it tracks rigid motion AND
	///       BSDynamicTriShape vertex updates, so we don't need to dig
	///       deeper into mesh data
	///
	/// Identical hashes across frames means the shadow map currently in the
	/// slot is byte-for-byte what a fresh re-render would produce -- the
	/// caller can skip the redraw and keep the cached slot intact.
	///
	/// Returns 0 only when `light` or its inner NiLight is null (caller
	/// treats 0 as "never rendered"). Otherwise the hash itself almost
	/// never lands on 0 because of how HashCombine mixes constants in.

	static std::uint64_t ComputeShadowGeomHash(RE::BSShadowLight* light)
	{
		if (!light)
			return 0;
		auto* ni = light->light.get();
		if (!ni)
			return 0;
		std::uint64_t h = 0x9e3779b97f4a7c15ull;  // arbitrary nonzero seed

		// Quantization thresholds: tuned to be one to two orders of
		// magnitude below perceptible difference in the rendered shadow.
		//   kPosStep   = 1.0 game unit (~1.4 cm world space; sub-texel
		//                at typical 2048 shadow res * 500 unit light radius)
		//   kRotStep   = 0.01 in matrix entries (~0.5 degrees)
		//   kRadiusStep = 1.0 unit (well under any visible frustum
		//                resize from torch pulse animations)
		constexpr float kPosStep = 1.0f;
		constexpr float kRotStep = 0.01f;
		constexpr float kRadiusStep = 1.0f;

		// Light pose
		const auto& t = ni->world.translate;
		h = HashCombineFloat(h, QuantizeFloat(t.x, kPosStep));
		h = HashCombineFloat(h, QuantizeFloat(t.y, kPosStep));
		h = HashCombineFloat(h, QuantizeFloat(t.z, kPosStep));
		const auto& r = ni->world.rotate;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				h = HashCombineFloat(h, QuantizeFloat(r.entry[i][j], kRotStep));
		// Light radius (NiPointLight uses .x; spotlights use direction in
		// rotation matrix already hashed above).
		const auto& rtd = ni->GetLightRuntimeData();
		h = HashCombineFloat(h, QuantizeFloat(rtd.radius.x, kRadiusStep));
		// Caster set + each caster's worldBound (engine-updated).
		for (auto& nip : light->geomList) {
			auto* ts = nip.get();
			if (!ts)
				continue;
			const auto raw = reinterpret_cast<std::uintptr_t>(ts);
			h = HashCombine(h, static_cast<std::uint32_t>(raw));
			h = HashCombine(h, static_cast<std::uint32_t>(raw >> 32));
			const auto& wb = ts->worldBound;
			h = HashCombineFloat(h, QuantizeFloat(wb.center.x, kPosStep));
			h = HashCombineFloat(h, QuantizeFloat(wb.center.y, kPosStep));
			h = HashCombineFloat(h, QuantizeFloat(wb.center.z, kPosStep));
			h = HashCombineFloat(h, QuantizeFloat(wb.radius, kRadiusStep));
		}
		return h;
	}

	// =========================================================================
	// Light enable / disable helpers
	// =========================================================================

	/// Removes `light` from s_normalConvert and clears its geometry list.
	/// No-op if the light is not in the list.
	static void EraseFromConvertList(RE::BSShadowLight* light)
	{
		for (auto it = s_normalConvert.begin(); it != s_normalConvert.end(); ++it) {
			if (it->light == light) {
				GameClearGeometryList(light);
				s_normalConvert.erase(it);
				return;
			}
		}
	}

	static void DisableLight(RE::BSShadowLight* light)
	{
		EraseFromConvertList(light);
		auto* cull = light->cullingProcess;
		if (cull && cull->portalGraphEntry)
			GameClearPortalVisibility(reinterpret_cast<RE::BSPortalGraphEntry*>(cull->portalGraphEntry));
		light->ReturnShadowmaps();
	}

	// Activates a light as a normal (non-shadow) light by inserting it into
	// the scene's active-light list without allocating a shadow slot.
	//
	// Two paths: "already-converted re-enable" (just GameEnableLight) and
	// "first conversion this session" (ReturnShadowmaps + portal-clear +
	// track in s_normalConvert + GameEnableLight). Tracy sub-zones split
	// the cost so the next capture distinguishes the steady-state cost
	// (re-enable only) from the cost of a fresh conversion.
	static void ConvertLight(RE::BSShadowLight* light, RE::ShadowSceneNode* ssn, bool isNS)
	{
		// Already converted: just re-enable so geometry picks it up this frame.
		for (auto& c : s_normalConvert) {
			if (c.light == light) {
				ZoneNamedN(zReEnable, "SCM::Engine::ConvertLight::ReEnable", true);
				GameEnableLight(ssn, light);
				return;
			}
		}

		// First conversion this session: release shadow resources, register.
		ZoneNamedN(zFirstConv, "SCM::Engine::ConvertLight::FirstConvert", true);
		auto* cull = GetLightCullingProcess(light);
		if (cull && cull->portalGraphEntry)
			GameClearPortalVisibility(reinterpret_cast<RE::BSPortalGraphEntry*>(cull->portalGraphEntry));
		light->ReturnShadowmaps();

		s_normalConvert.push_back({ light, isNS });
		GameEnableLight(ssn, light);
	}

	// Activates a non-sun shadow light into slot `slotIndex`.
	static void EnableLight(RE::BSShadowLight* light, RE::NiCamera* camera,
		RE::ShadowSceneNode* ssn, int slotIndex)
	{
		// Remove from conversion list if it was previously converted to normal.
		EraseFromConvertList(light);

		// Focus shadow handling (only when not in extended mode).
		if (s_settings.ShadowLightCount <= 4) {
			bool drawFocus = ShadowField(light, drawFocusShadows);
			if (drawFocus || (!*GetFocusShadowSelected() && light->GetIsFrustumOrDirectionalLight())) {
				GameSetupDirectionalLight(light, camera);
				GameAccumulate(light);
				if (REL::Module::IsVR()) {
					for (auto& desc : light->GetVRRuntimeData().focusShadowmapDescriptors) {
						desc.vrRenderTarget[0] = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
						desc.vrRenderTarget[1] = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
					}
				}
				ShadowField(light, drawFocusShadows) = true;
				*GetFocusShadowSelected() = true;
				*GetSunPtr() = reinterpret_cast<uint64_t>(light);
			}
		}

		GameEnableLight(ssn, light);
		GameSetShadowCasterSlot(ssn, light, *GetAccumLightSlot(), 1);

		{
			uint32_t mi = *GetMaskIndex();
			ShadowField(light, maskIndex) = mi;
			*GetMaskIndex() = mi + 1;
		}

		// Projected bounding box for shadow map region.
		auto* nilight = light->light.get();
		if (nilight) {
			auto lpos = nilight->world.translate;
			auto cpos = camera->world.translate;
			auto delta = lpos - cpos;
			float dx = delta.x, dy = delta.y, dz = delta.z;
			float dist = lpos.GetDistance(cpos);
			float radius = nilight->GetLightRuntimeData().radius.x;

			float left, right, top, bottom;

			if (dist >= radius + camera->GetNearPlane()) {
				float inv = 1.0f / dist;
				float coord[4] = {
					lpos.x - dx * radius * inv,
					lpos.y - dy * radius * inv,
					lpos.z - dz * radius * inv,
					radius
				};
				float r1[2], r2[2];
				GameFrustumOverlap(camera, coord, r1, r2, 0.00001f);

				float vw = (float)*globals::game::viewWidth;
				float vh = (float)*globals::game::viewHeight;
				if (REL::Module::IsVR()) {
					vw *= GetVRDRSWidthRatio();
					vh *= GetVRDRSHeightRatio();
				}

				left = (r1[0] + 1.0f) * 0.5f * vw;
				right = (r2[0] + 1.0f) * 0.5f * vw;
				top = (1.0f - (r1[1] + 1.0f) * 0.5f) * vh;
				bottom = (1.0f - (r2[1] + 1.0f) * 0.5f) * vh;
			} else {
				// Light contains the camera: use full screen.
				*GetShadowMask() |= 1u << *GetAccumLightSlot();
				left = right = top = bottom = -1.0f;
			}

			ShadowField(light, projectedBoundingBox) =
				RE::NiRect<uint32_t>((uint32_t)left, (uint32_t)right, (uint32_t)top, (uint32_t)bottom);
		}

		// Accumulate into shadow slot.
		{
			uint32_t idx = static_cast<uint32_t>(slotIndex);
			light->Accumulate(idx, idx, nullptr);
			*GetAccumLightSlot() += light->shadowMapCount;
		}

		// Extended mode: pre-set kNONE renderTarget so RenderCascade re-runs
		// its slot-allocation block (where Hook_OverwriteShadowMapIndex
		// overrides the global counter with our slot index). Without this,
		// RenderCascade keeps the slot from a prior frame and lights not
		// redrawn this frame would corrupt another light's shadow map.
		// Pool index maps 1:1 to texture slot; slice 0 stays unused.
		if (s_settings.ShadowLightCount > 4) {
			int32_t idx = s_lights.FindLight(light, s_settings.ShadowLightCount);
			if (idx < 0)
				idx = 0;
			if (REL::Module::IsVR()) {
				for (auto& desc : light->GetVRRuntimeData().shadowmapDescriptors) {
					desc.renderTarget = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
					desc.shadowmapIndex = static_cast<uint32_t>(idx);
				}
			} else {
				for (auto& desc : light->GetRuntimeData().shadowmapDescriptors) {
					desc.renderTarget = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
					desc.shadowmapIndex = static_cast<uint32_t>(idx);
				}
			}
		}

		// Only apply lens flare when lensFlareData is non-null; calling it on parabolic lights
		// (null lensFlareData) registers them into the lens flare system, causing a crash
		// in the lens flare pass when it tries to dereference the null sprite data.
		if (light->lensFlareData)
			GameApplyLensFlare(light);
	}

	// =========================================================================
	// Main shadow caster manager
	//
	// Replaces the game's CalculateActiveShadowCasterLights entirely.
	// Runs via stl::detour_thunk; obtains all inputs from game globals.
	// =========================================================================

	// Lightweight per-frame candidate entry used during scheduling.
	//
	// After the validation pass, exactly one of {chosen, excess, invalid}
	// is true (or none if it's the sun, which is processed separately).
	struct CandidateLight
	{
		RE::BSShadowLight* light{ nullptr };
		double score{ 0.0 };
		bool sun{ false };
		bool chosen{ false };         // valid + within ShadowLightCount budget
		bool excess{ false };         // valid but over budget (convert or disable)
		bool invalid{ false };        // shorthand: invalidCamera || invalidPortal
		bool invalidCamera{ false };  // UpdateCamera returned false (any sub-reason).
									  // Retained as a shorthand for downstream branches
									  // that don't care WHY camera-validation failed.
									  // For action selection, prefer the explicit
									  // sub-reasons below.
		bool invalidPortal{ false };  // portal cull says light's cell is not visible
									  // from the camera's cell. Must DisableLight
									  // entirely; converting routes through cluster
									  // lighting which has no portal awareness and
									  // would bleed light through walls.

		// UpdateCamera() conflates three physically distinct fail conditions
		// into one boolean. The engine sets two side-band flags as it walks
		// per-light culling that let us recover the reason:
		//
		//   frustrumCull = 0xff -> light's bounding shape is outside the camera
		//                          frustum (off-screen). Light contributes nothing
		//                          visible this frame regardless of shadow state,
		//                          so ConvertLight is wasted work -- drop instead.
		//
		//   lodDimmer == 0.0f  -> light is past the engine's per-light shadow LOD
		//                          fade end distance. The light is still visible
		//                          and *should* still illuminate via cluster
		//                          lighting, so ConvertLight is the correct action
		//                          (keep + reset lodDimmer so cluster picks it up).
		//
		// A light can hit both simultaneously (off-screen AND distant); the
		// frustum check wins because off-screen contribution is zero.
		//
		// Sub-reasons are not mutually exclusive at the bit level but action
		// selection treats frustum-out as terminal.
		bool invalidFrustum{ false };  // engine's frustum cull failed (BSMultiBoundSphere::WithinFrustum / cone-frustum)
		bool invalidLod{ false };      // engine's LOD-fade zeroed lodDimmer
	};

	static void ScheduleShadowCasters()
	{
		ZoneScopedN("SCM::ScheduleShadowCasters");
		// Reset per-frame diagnostic counters. Each release/clear path below
		// increments its bucket; values are emitted as TracyPlot at function
		// exit so capture-time analysis can pinpoint which path fires under
		// which conditions.
		s_schedDiag = SchedDiagCounters{};
		// VR renders both eyes per frame, so the game calls CalculateAndDrawShadowCasterLights
		// twice. Block the second call: s_lights is not thread-safe and a re-entrant call
		// would null out entries the first call is still iterating over.
		static std::atomic<bool> s_inSchedule{ false };
		if (s_inSchedule.exchange(true, std::memory_order_acquire))
			return;
		struct Guard
		{
			~Guard() { s_inSchedule.store(false, std::memory_order_release); }
		} guard;

		// VR display guard: skip scheduling when the HMD display is not active.
		if (REL::Module::IsVR() && !GetVRDrawShadows())
			return;

		auto* ssn = GetShadowSceneNode();
		auto* camera = GetWorldCamera();
		if (!ssn || !camera)
			return;

		// Do NOT clear shadowLightsAccum or reset the slot counter here. The
		// outer CalculateAndDrawShadowCasterLights calls ResetCalculatedShadow-
		// CasterLights before our hook fires, and that function clears the
		// array, resets the counter, AND installs the sun at slot 0. Re-
		// clearing here wipes the sun (sun->Accumulate is the focus vfunc,
		// not a slot allocator) and the engine then skips the directional
		// cascade pass entirely.

		s_budget.Begin(0);

		int doneLightCount = 0;
		RE::BSShadowLight* sunLight = nullptr;

		// ---- Sun / directional light ----
		if (!GetSunBool2()) {
			auto* sun = ssn->GetRuntimeData().sunShadowDirLight;
			if (sun) {
				static REL::Relocation<bool*> vrUpdateFlag{ REL::Offset(0x1ed62f8) };
				uint8_t vrFlag = REL::Module::IsVR() ? static_cast<uint8_t>(*vrUpdateFlag) + 1 : 0;
				sun->Accumulate(*GetAccumLightSlot(), 0, nullptr, vrFlag);

				if (sun->lensFlareData && !REL::Module::IsVR())
					GameApplyLensFlare(sun);

				if (REL::Module::IsVR() && !GetVRAccumFirst()) {
					GameVRPrepareShadowMaps(sun);
					GameVRAccumulateShadowMaps(sun);
				}

				sunLight = sun;
			}
		}

		*GetSunPtr() = 0;

		// ---- Score all candidate lights ----
		// Reuse a static vector so we don't allocate per frame -- the
		// scheduler runs every frame and the candidate list is the same
		// shape size each call (a few hundred lights at most).
		static std::vector<CandidateLight> candidates;

		{
			ZoneScopedN("SCM::ScoreCandidates");
			SetupSceneFormula(camera);

			candidates.clear();
			candidates.reserve(ssn->GetRuntimeData().activeShadowLights.size());

			int32_t tmpIndex = 0;
			for (auto& sp : ssn->GetRuntimeData().activeShadowLights) {
				auto* l = sp.get();
				if (!l || l == sunLight)
					continue;
				auto& c = candidates.emplace_back();
				c.light = l;
				c.sun = false;
				c.score = CalculateLightScore(l, camera, tmpIndex++);
			}
#ifdef TRACY_ENABLE
			char buf[32];
			const int n = snprintf(buf, sizeof(buf), "candidates=%zu", candidates.size());
			if (n > 0)
				ZoneText(buf, static_cast<size_t>(n));
#endif
		}

		// Validation, redraw-interval scoring, and RedrawFrame marking all
		// happen before the atomic loop. Tracy capture analysis showed this
		// block dominates SCM::ScheduleShadowCasters (98%+ of the function's
		// runtime), so a dedicated zone scopes that cost separately from
		// ScoreCandidates and ScheduleLoop. Named variant because the
		// enclosing function already declares a ZoneScopedN.
		ZoneNamedN(zoneValBudget, "SCM::ValidateAndScheduleBudget", true);

		// Apply debug pins: bias scoring so pinned-shadow lights sort to the
		// top (forced into the chosen pool up to ShadowLightCount) and
		// pinned-convert lights sort to the bottom (forced into the excess pool
		// where ConvertLight runs unconditionally — see c.excess branch below).
		// Pin sets are mutually exclusive (SetPinned* enforces that), but if a
		// stale entry slips through, pin-shadow wins because the bias is checked
		// first.
		for (auto& c : candidates) {
			auto key = reinterpret_cast<uintptr_t>(c.light);
			if (s_pinShadow.count(key))
				c.score += 1e15;
			else if (s_pinConvert.count(key))
				c.score -= 1e15;
		}

		// Sort descending by score (highest priority first); sun always first.
		std::sort(candidates.begin(), candidates.end(),
			[](const CandidateLight& a, const CandidateLight& b) {
				if (a.sun != b.sun)
					return a.sun;
				return a.score > b.score;
			});

		// ---- Validation pass (no game mutations) ----
		//
		// Mirrors Intellightent's per-iteration validation gates. Splitting
		// validation from mutation lets us defer all game-state changes
		// (DisableLight / ConvertLight / EnableLight) to a single atomic loop
		// later, eliminating the dangling-pointer crash window where mutations
		// in an earlier phase invalidated raw pointers held in s_lights[].
		//
		// Slot 0 is reserved for the sun; point lights fill slots 1..ShadowLightCount.
		// Do not count the sun against ShadowLightCount -- it uses focus cascade DSV slots,
		// not parabolic point-light slots.
		auto* globalCull = *reinterpret_cast<RE::BSCullingProcess**>(
			*reinterpret_cast<uintptr_t**>(
				REL::RelocationID(528077, 415022).address()));

		int wantCount = 0;

		// Per-candidate UpdateCamera vfunc + portal-graph visibility walk
		// + chosen/excess tagging. Captured separately so memoization or
		// caching of UpdateCamera/portal verdicts can be measured.
		{
			ZoneNamedN(zoneCandVal, "SCM::CandidateValidation", true);
			for (auto& c : candidates) {
				auto* l = c.light;
				// UpdateCamera (vfunc 16, +0x80) is the engine's type-aware visibility
				// test. Verified via Ghidra (BSShadowParabolicLight_UpdateCamera at
				// 0x14151b620 in 1.6.1170, 0x14132ddf0 in 1.6.640, 0x141370c80 in VR):
				//
				//   - BSShadowParabolicLight: TWO cull conditions, both setting
				//     frustrumCull=0xff:
				//       (1) BSMultiBoundSphere::WithinFrustum (BSMultiBoundShape
				//           vfunc 0x29) -- sphere(niLight.pos, niLight.Radius.x)
				//           vs camera frustum. Geometrically correct;
				//           failure means no visible pixel can be lit because the
				//           light's bounding sphere doesn't touch the camera frustum.
				//           The radius source matches what the cluster builder reads
				//           (LightLimitFix.cpp's `runtimeData.radius.x`).
				//       (2) Shadow-distance LOD -- if (lodFade flag set on
				//           BSShadowLight) AND
				//           ((camDist^2 - radius^2) * camera.LodAdjust) >
				//               ShadowDistanceSquared_Current => cull.
				//           ShadowDistanceSquared_Current = fShadowDistance^2
				//           (8000^2 outdoors, 3000^2 indoors by default).
				//           This is NOT a visibility test -- it's "skip per-light
				//           shadow rendering at this distance". A light past
				//           shadow distance can still be IN the camera frustum and
				//           illuminating visible pixels via cluster lighting.
				//
				//   - BSShadowFrustumLight: cone-vs-frustum test (cone-aware so an
				//     off-screen spot pointing INTO the frustum is correctly kept).
				//
				//   - BSShadowDirectionalLight: cascades, separate code path.
				//
				// Implication for SCM: a `frustrumCull != 0` verdict does NOT mean
				// "geometrically off-screen". The convertOrDisable path below treats
				// all c.invalid cases uniformly (omnis convert, spots disable, portal
				// disable) so distant lights past shadow distance still reach the
				// cluster pipeline. The cluster builder's own
				// `(color * fade) > 1e-4 && radius > 1e-4` filter discards lights
				// that genuinely don't contribute.
				if (!l->UpdateCamera(camera)) {
					c.invalidCamera = true;
					c.invalid = true;
					// Recover the sub-reason from the engine's side-band flags.
					// Both can be true (a light off-screen AND LOD-faded);
					// recorded as independent bits for analysis. Action loop
					// below treats frustum-out as terminal (drop) and
					// LOD-faded-in-frustum as convert.
					c.invalidFrustum = (l->frustrumCull != 0);
					c.invalidLod = (l->lodDimmer == 0.0f);
					continue;
				}
				// Portal culling only applies in interior cells where a portal graph exists.
				// Lights with no culling process (e.g. WSU spotlights outside cell bounds)
				// or no portal are unconditionally visible; skip the check for them.
				auto* cull = GetLightCullingProcess(l);
				if (cull) {
					auto* portal = reinterpret_cast<RE::BSPortalGraphEntry*>(cull->portalGraphEntry);
					if (portal) {
						auto* gPortal = globalCull ? reinterpret_cast<RE::BSPortalGraphEntry*>(globalCull->portalGraphEntry) : nullptr;
						if (gPortal && !GamePortalHasSharedVisibility(gPortal, portal)) {
							c.invalidPortal = true;
							c.invalid = true;
							continue;
						}
					}
				}

				if (wantCount < s_settings.ShadowLightCount) {
					c.chosen = true;
					wantCount++;
				} else {
					c.excess = true;
				}
			}

			// Tracy candidate breakdown: emits per-frame so a capture can be
			// queried alongside the per-action counters to verify the math
			// (chosen + excess + invalid_camera + invalid_portal == total).
			for (auto& c : candidates) {
				s_schedDiag.candidates_total++;
				if (c.chosen)
					s_schedDiag.candidates_chosen++;
				if (c.excess)
					s_schedDiag.candidates_excess++;
				if (c.invalidCamera)
					s_schedDiag.candidates_invalid_camera++;
				if (c.invalidPortal)
					s_schedDiag.candidates_invalid_portal++;
				// Sub-reason breakdown of invalidCamera. A single light may
				// be both frustum-out AND LOD-faded -- both bits are counted
				// so the sum can exceed candidates_invalid_camera. The
				// "other" bucket catches UpdateCamera failures where the
				// engine cleared frustrumCull and left lodDimmer > 0 (rare
				// edge cases like internal state changes).
				if (c.invalidCamera) {
					if (c.invalidFrustum)
						s_schedDiag.candidates_invalid_frustum++;
					if (c.invalidLod)
						s_schedDiag.candidates_invalid_lod++;
					if (!c.invalidFrustum && !c.invalidLod)
						s_schedDiag.candidates_invalid_other++;
				}
			}
		}  // end SCM::CandidateValidation

		// Pool membership update: drop expired pointers, drop unchosen,
		// add newly chosen, sync sun slot.
		{
			ZoneNamedN(zonePoolMem, "SCM::UpdatePoolMembership", true);
			// ---- Sync s_lights (our active pool) ----
			//
			// First drop entries whose pointers are no longer in the scene's
			// activeShadowLights (game-side may have freed them since last frame).
			// This protects subsequent slot-stability lookups from dereferencing
			// dangling pointers.
			std::unordered_set<RE::BSShadowLight*> aliveSet;
			{
				auto& alive = ssn->GetRuntimeData().activeShadowLights;
				aliveSet.reserve(alive.size() + 1);
				if (sunLight)
					aliveSet.insert(sunLight);
				for (auto& sp : alive)
					if (auto* l = sp.get())
						aliveSet.insert(l);
			}
			for (int i = 0; i < s_lights.Size; i++) {
				if (!s_lights.Lights[i].Light)
					continue;
				if (aliveSet.find(s_lights.Lights[i].Light) == aliveSet.end()) {
					s_schedDiag.reconciliation_clears++;
					s_lights.Lights[i].Clear();
				}
			}

			// ---- Sync s_normalConvert (converted-to-non-shadow set) ----
			//
			// Two-tier filter:
			//
			// Tier 1: drop entries the engine has removed from BOTH active
			// lists. Hook_ConvertLights_Remove fires on individual RemoveLight
			// calls but the engine's bulk cell-teardown path bypasses it, so
			// this is our safety net for dangling pointers.
			//
			// Tier 2: drop entries that are functionally dead -- still in
			// activeShadowLights / activeLights (because GameEnableLight from
			// ConvertLight activates an entry that the engine never
			// auto-deactivates), but with fade=0 / lodDimmer=0 / null NiLight
			// so addLight in LightLimitFix would skip them anyway.
			//
			// Without tier 2 the set grows unbounded across a session: every
			// converted light stays pinned in s_normalConvert until the engine
			// triggers a removal we can hook. Heavy modlists hit 400+ entries,
			// keeping freed-then-recycled BSLight memory referenced by
			// downstream pass captures longer than necessary. The criteria
			// mirror addLight's discard filter -- entries failing it
			// contribute nothing to the cluster or engine lighting paths and
			// have no business staying in our set.
			if (!s_normalConvert.empty()) {
				std::unordered_set<RE::BSLight*> normalAlive;
				normalAlive.reserve(aliveSet.size() + ssn->GetRuntimeData().activeLights.size());
				for (auto* p : aliveSet)
					normalAlive.insert(static_cast<RE::BSLight*>(p));
				for (auto& sp : ssn->GetRuntimeData().activeLights)
					if (auto* l = sp.get())
						normalAlive.insert(l);

				const std::size_t before = s_normalConvert.size();
				std::erase_if(s_normalConvert, [&](const ConvertedLight& c) {
					// Tier 1: dangling / engine-removed.
					if (!c.light || normalAlive.find(static_cast<RE::BSLight*>(c.light)) == normalAlive.end())
						return true;
					// Tier 2: functionally dead. Cheap derefs only -- no
					// virtual calls or extra hash lookups.
					auto* niLight = c.light->light.get();
					if (!niLight)
						return true;
					const auto& rt = niLight->GetLightRuntimeData();
					const float colorSum = rt.diffuse.red + rt.diffuse.green + rt.diffuse.blue;
					if (colorSum * rt.fade <= 1e-4f)
						return true;
					if (rt.radius.x <= 1e-4f)
						return true;
					return false;
				});
				const std::size_t after = s_normalConvert.size();
				if (before != after) {
					static int loggedShrink = 0;
					if (loggedShrink++ < 20 || (before - after) > 32) {
						logger::debug("[SCM] s_normalConvert reconcile: {} -> {} ({} dropped)",
							before, after, before - after);
					}
				}
			}

			// Drop entries no longer chosen. Rank-drift suppression now lives
			// in CalculateLightScore via the lightframessincerender decay term
			// in the default ScoreFormula; the slot pool itself is a dumb
			// container that follows the chosen set without policy of its own.
			// The atomic loop's c.excess / c.invalid branches handle the
			// engine-side ConvertLight / DisableLight call for the dropped
			// occupants on the same frame.
			for (int i = 0; i < s_lights.Size; i++) {
				if (!s_lights.Lights[i].Light)
					continue;
				bool stillChosen = (i == 0 && s_lights.Sun);  // sun slot
				if (!stillChosen) {
					for (auto& c : candidates) {
						if (c.light == s_lights.Lights[i].Light && c.chosen) {
							stillChosen = true;
							break;
						}
					}
				}
				if (!stillChosen)
					s_lights.Lights[i].Clear();
			}

			// Add newly chosen lights (assigned to first free slot; keeps existing chosen lights in place).
			for (auto& c : candidates) {
				if (!c.chosen)
					continue;
				bool alreadyIn = false;
				for (int i = 0; i < s_lights.Size && !alreadyIn; i++)
					if (s_lights.Lights[i].Light == c.light)
						alreadyIn = true;
				if (alreadyIn)
					continue;

				int idx = s_lights.FindFreeIndex(true, s_settings.ShadowLightCount, s_settings.ConvertedShadowSlots);
				if (idx < 0)
					continue;
				// Reset slot metadata before installing the new occupant.
				// FindFreeIndex returns slots whose Light pointer is nullptr,
				// but eviction paths leave the rest of the LightEntry intact
				// (LastDrawnFrame, lastGeomHash, etc.). Without this clear the
				// new occupant inherits the previous owner's metadata, the
				// first_render_skips gate fails to fire (LastDrawnFrame >= 0),
				// and the cluster pipeline samples kSHADOWMAPS[idx] -- which
				// still holds the previous owner's depth content -- producing
				// a wrong-shadow visual artifact. Reset at acquire only --
				// eviction paths just null the Light pointer, letting metadata
				// persist as a cache key until the slot is genuinely reused.
				s_lights.Lights[idx].Clear();
				s_lights.Lights[idx].Light = c.light;
			}

			// Update sun slot (slot 0).
			if (sunLight) {
				if (s_lights.Lights[0].Light != sunLight) {
					s_lights.Lights[0].Clear();
					s_lights.Lights[0].Light = sunLight;
				}
				s_lights.Sun = true;
			} else {
				// Sun is gone. If slot 0 was tracking the sun, clear the stale
				// pointer. If Sun was already false coming in, slot 0 holds a
				// regular point light (sun-aware FindFreeIndex allocates point
				// lights to slot 0 when Sun=false) -- do NOT wipe it. This
				// matches Intellightent's reference behaviour (no unconditional
				// slot-0 clear in the no-sun branch).
				if (s_lights.Sun)
					s_lights.Lights[0].Clear();
				s_lights.Sun = false;
			}
		}  // end SCM::UpdatePoolMembership

		// ---- Temporal budget: decide which lights redraw this frame ----
		double budget = s_settings.RedrawBudgetMs;
		{
			// Frame-time EMA + budget formula evaluation. Scoped separately
			// from ScheduleLoop so the once-per-frame budget cost is visible
			// distinct from the per-light scheduling cost.
			{
				ZoneNamedN(zoneCompBud, "SCM::ComputeBudget", true);
				// Update frame-time EMA and ring buffer (always, for formula params and UI).
				const float dtMs = *globals::game::deltaTime * 1000.0f;
				s_ftRing[s_ftHead] = dtMs;
				s_ftHead = (s_ftHead + 1) % kFrameWindow;
				if (s_ftCount < kFrameWindow)
					++s_ftCount;
				s_ftEMA = (s_ftCount == 1) ? dtMs : 0.1f * dtMs + 0.9f * s_ftEMA;

				const float target_ms = ComputeFrameTimePercentile90();
				if (s_ftEMA < target_ms)
					s_stableFrames = std::min(s_stableFrames + 1, 45);
				else
					s_stableFrames = 0;

				FormulaHelper::SetParam(kFormulaParam_FrameTime, static_cast<double>(s_ftEMA));
				FormulaHelper::SetParam(kFormulaParam_FrameTarget, static_cast<double>(target_ms));
				FormulaHelper::SetParam(kFormulaParam_StableFrames, static_cast<double>(s_stableFrames));

				// Evaluate the budget for the whole frame.
				//   Manual:  fixed slider value (RedrawBudgetMs).
				//   Formula: user-editable exprtk expression.
				if (s_settings.BudgetMode == BudgetModeEnum::Formula && s_formulaRedrawBudget) {
					budget = s_formulaRedrawBudget->Calculate();
				}
				s_autoBudgetMs = static_cast<float>(budget);
			}  // end SCM::ComputeBudget

			// Reset redraw counter (will be updated at end of scheduling)
			s_redrawnLightsThisFrame = 0;
			s_totalShadowLightsThisFrame = s_settings.ShadowLightCount;

			ZoneScopedN("SCM::ScheduleLoop");
			int maxRedraw = std::min(s_settings.MaxRedrawPerFrame, s_lights.Size);
			int32_t budgetRemain = static_cast<int32_t>(budget * 1000.0);
			bool isFirst = true;
			int32_t now = *globals::game::frameCounter;

			// Clear RedrawFrame on slots OUTSIDE the point-light range (converted /
			// otherwise-allocated). Note PointLightEnd accounts for the sun
			// bookkeeping slot when Sun=true, so a converted-slot light at
			// pool[ShadowLightCount + 1] correctly gets cleared.
			for (int i = s_lights.PointLightEnd(s_settings.ShadowLightCount); i < s_lights.Size; i++)
				s_lights.Lights[i].RedrawFrame = false;

			// First pass: sun only. Point-light slots fall through to the
			// importance-scored pending loop below so new lights compete
			// fairly with existing redraws (sorted by importance, not pool
			// order). AllowDrawNewLight is honoured by the pending loop's
			// filter.
			for (int i = 0; i < s_lights.Size; i++) {
				auto& e = s_lights.Lights[i];
				if (!e.Light) {
					e.RedrawFrame = false;
					continue;
				}
				e.RedrawFrame = (i == 0 && s_lights.Sun);
				if (e.RedrawFrame) {
					e.LastDrawnFrame = now;
					isFirst = false;
					maxRedraw--;
					// Sun's budget cost is bookkept at 0 (different texture
					// pipeline -- it has its own cascade buffer), so no
					// budgetRemain decrement.
				}
			}

			if (maxRedraw > 0 && budgetRemain > 0) {
				std::vector<LightEntry*> pending;
				for (int i = 0; i < s_lights.Size; i++) {
					auto& e = s_lights.Lights[i];
					if (!e.Light || e.RedrawFrame)
						continue;
					// Honour AllowDrawNewLight: when disabled, brand-new
					// entries (LastDrawnFrame < 0) wait until the next frame
					// rather than competing for this frame's budget. Existing
					// lights re-entering view still schedule normally.
					if (!s_settings.AllowDrawNewLight && e.LastDrawnFrame < 0)
						continue;
					pending.push_back(&e);
				}

				for (auto* e : pending) {
					double interval = 0.0;
					if (s_formulaRedrawInterval) {
						SetupLightFormula(e->Light, camera, 0);
						// e->Index is the pool index. Beyond PointLightEnd are converted slots.
						if (e->Index >= s_lights.PointLightEnd(s_settings.ShadowLightCount))
							FormulaHelper::SetParam(kFormulaParam_LightConverted, 1.0);

						// Compute how far the light has moved since its last shadow map render.
						// Exposed as `lightdisplacement` so the formula can prioritise fast-moving
						// lights (e.g. player torches) without relying on distance-to-camera alone.
						if (auto* nilight = e->Light->light.get()) {
							auto& curr = nilight->world.translate;
							float dx = curr.x - e->lastRenderedPos.x;
							float dy = curr.y - e->lastRenderedPos.y;
							float dz = curr.z - e->lastRenderedPos.z;
							FormulaHelper::SetParam(kFormulaParam_LightDisplacement,
								static_cast<double>(sqrtf(dx * dx + dy * dy + dz * dz)));
						}

						interval = s_formulaRedrawInterval->Calculate();
					}
					interval += 1.0;

					// Contribution-weighted importance scheduling
					// ══════════════════════════════════════════════
					// importance = luminance(diffuse × fade) × max(att_cam, att_plr)
					//
					// att(pos) = max(1 − (dist/radius)², 0)²   [Skyrim's quadratic falloff]
					//
					// Physically: this is proportional to how much illumination this
					// light delivers at the camera or player position.  Dim or distant
					// lights score near zero; bright lights with the viewer inside the
					// radius score near 1 (or above for very intense lights).
					//
					// Interval multiplier:  2.0 × (0.025/2.0)^importance
					//   importance = 0 → ×2.0  (deprioritise background lights)
					//   importance = 0.5 → ×0.32 (~3× faster)
					//   importance = 1.0 → ×0.05 (~40× faster, near-forced redraw)
					//
					// A reference for this technique: Wimmer & Scherzer 2006,
					// "Instant Shadow Maps", Sec. 3 (importance-based priority);
					// Valient 2014, "Practical Shadow Maps" (luminance × coverage).

					float importance = 0.0f;

					if (auto* ni = e->Light->light.get()) {
						auto& rtd = ni->GetLightRuntimeData();
						float lightRadius = rtd.radius.x;
						auto lp = ni->world.translate;

						// Perceptual luminance (Rec.709) × engine fade factor.
						float lum = 0.2126f * rtd.diffuse.red +
						            0.7152f * rtd.diffuse.green +
						            0.0722f * rtd.diffuse.blue;
						float effectiveLum = lum * rtd.fade;

						// Primary: screen-space projected solid angle.
						// "How much of the view does this light's influence
						// sphere occupy?" Industry standard for many-light
						// shadow prioritisation -- see Olsson & Assarsson 2012,
						// "Clustered Deferred and Forward Shading"; Wronski
						// 2014, "Sample Distribution Shadow Maps"; CryEngine
						// shadow LOD docs. Approximates angular radius from
						// camera as radius/viewZ; solid angle ~ angularRadius^2.
						// Constants (screenH / 2*tan(fovY/2))^2 drop out -- they're
						// the same across all lights and don't affect ranking.
						//
						// Edge cases:
						//   viewZ < -radius : light fully behind camera, coverage=0
						//   |viewZ| < radius : light intersects camera plane;
						//                      clamp effectiveZ to avoid blow-up.
						float coverage = 0.0f;
						if (camera) {
							auto cp = camera->world.translate;
							RE::NiPoint3 fwd = camera->world.rotate.GetVectorY();
							float rx = lp.x - cp.x, ry = lp.y - cp.y, rz = lp.z - cp.z;
							float viewZ = fwd.x * rx + fwd.y * ry + fwd.z * rz;
							if (viewZ > -lightRadius) {
								float effectiveZ = std::max(viewZ, lightRadius * 0.5f);
								float angularRadius = lightRadius / effectiveZ;
								coverage = angularRadius * angularRadius;
							}
						}

						// Fallback: Skyrim-style quadratic distance falloff
						// from camera/player. Covers two cases where coverage
						// alone returns 0 but the user still sees shadows:
						//   1. Light just outside the frustum (around a corner)
						//      illuminating a visible wall.
						//   2. Player-held torch behind the camera lighting
						//      geometry ahead.
						// Weighted at 0.3 -- coverage dominates when the light
						// is in view, but out-of-view lights still get a floor
						// proportional to their illumination at the viewer.
						auto computeAtt = [&](const RE::NiPoint3& pos) -> float {
							float dx = pos.x - lp.x, dy = pos.y - lp.y, dz = pos.z - lp.z;
							float dist2 = dx * dx + dy * dy + dz * dz;
							float r2 = lightRadius * lightRadius;
							if (dist2 >= r2)
								return 0.0f;
							float t = dist2 / r2;
							float a = 1.0f - t;
							return a * a;  // matches Skyrim (1-(d/r)^2)^2 falloff
						};
						auto* plr = RE::PlayerCharacter::GetSingleton();
						float attCam = camera ? computeAtt(camera->world.translate) : 0.0f;
						float attPlr = plr ? computeAtt(plr->GetPosition()) : attCam;
						float distanceFallback = std::max(attCam, attPlr) * 0.3f;

						importance = effectiveLum * std::max(coverage, distanceFallback);
					}

					// Exponential interval scaling: maxScale*(minScale/maxScale)^clamp(importance,0,1)
					float kMaxMult = s_settings.ImportanceMaxScale;
					float kMinMult = std::min(s_settings.ImportanceMinScale, kMaxMult);
					float clampedImp = std::min(importance, 1.0f);
					interval *= static_cast<double>(kMaxMult * powf(kMinMult / kMaxMult, clampedImp));

					FormulaHelper::SetParam(kFormulaParam_LightImportance, static_cast<double>(importance));
					e->RedrawScore = e->LastDrawnFrame + interval;
					e->lastImportance = importance;

					// Cached shadow maps: if the geometry hash matches what we
					// rendered last time, the shadow map currently in the slot
					// is byte-identical to what a fresh re-render would produce.
					// No need to redraw -- push the score sky-high so this entry
					// loses every budget contest unless literally nothing else
					// needs redrawing (defensive: still allow eventual refresh
					// against any hashing bugs).
					//
					// Industry-standard pattern: UE5 "Cached Shadow Maps",
					// Frostbite movable-light caching. The hash captures
					// (1) light's own pose + radius and (2) each caster's
					// worldBound + identity -- both rigid motion and engine-
					// updated bounds (BSDynamicTriShape vertex changes update
					// worldBound).
					e->pendingGeomHash = ComputeShadowGeomHash(e->Light);
					if (e->LastDrawnFrame >= 0 && e->lastGeomHash != 0 &&
						e->pendingGeomHash == e->lastGeomHash) {
						e->RedrawScore += 1e15;
					}
				}

				// Count lights meaningfully illuminating the viewer area.
				s_highImportanceLightCount = static_cast<uint32_t>(
					std::count_if(pending.begin(), pending.end(),
						[](const LightEntry* e) { return e->lastImportance > 0.1f; }));

				std::sort(pending.begin(), pending.end(),
					[](const LightEntry* a, const LightEntry* b) { return a->RedrawScore < b->RedrawScore; });

				for (auto* e : pending) {
					if (maxRedraw <= 0)
						break;
					if (budgetRemain <= 0)
						break;
					int32_t budgetEstimate = s_budget.GetCost(e->Light);
					if (isFirst) {
						if (!s_lights.Sun || e->Index > 0)
							budgetRemain -= budgetEstimate;
						maxRedraw--;
						e->RedrawFrame = true;
						e->LastDrawnFrame = now;
						e->lastGeomHash = e->pendingGeomHash;
						isFirst = false;
						continue;
					}
					if (budgetEstimate <= budgetRemain) {
						budgetRemain -= budgetEstimate;
						maxRedraw--;
						e->RedrawFrame = true;
						e->LastDrawnFrame = now;
						e->lastGeomHash = e->pendingGeomHash;
						continue;
					}
				}
			}
		}

		// Count how many shadow lights are scheduled to redraw this frame.
		// Iterate the point-light range (sun-aware: skips pool[0] when Sun=true).
		s_redrawnLightsThisFrame = 0;
		for (int j = s_lights.PointLightFirst(); j < s_lights.PointLightEnd(s_settings.ShadowLightCount); j++) {
			if (s_lights.Lights[j].RedrawFrame)
				++s_redrawnLightsThisFrame;
		}

		// Smooth the count for stable UI display (avoid flickering)
		s_redrawnLightsSmoothed = 0.8f * s_redrawnLightsSmoothed + 0.2f * s_redrawnLightsThisFrame;

		// ---- ATOMIC PER-CANDIDATE LOOP ----
		//
		// Replaces the previous split selection / activation phases. For each
		// candidate (in score order), perform its mutation immediately:
		//
		//   - chosen + RedrawFrame + slot < ShadowLightCount + !sun-slot:
		//       Begin + EnableLight + End budget pair, render shadow map.
		//   - chosen, otherwise: DisableLight (Phase 7 below re-adds it via
		//       GameSetShadowCasterSlot at the cached-shadow tail).
		//   - excess + ConvertExcessToNormal: ConvertLight (preserves diffuse
		//       contribution as a normal non-shadow light).
		//   - excess + !ConvertExcessToNormal: DisableLight (vanilla SLF behavior).
		//   - invalid (failed UpdateCamera / culling / portal): DisableLight.
		//
		// Why this ordering matters:
		//   Score-sorted candidates have all chosen entries (rank < ShadowLightCount)
		//   processed BEFORE any excess (rank >= ShadowLightCount). When ConvertLight
		//   on an excess light triggers ReturnShadowmaps (which mutates the scene's
		//   activeShadowLights and may free other BSShadowLight objects), the chosen
		//   lights have already completed their EnableLight + budget pairing
		//   synchronously within their own iteration. There is no later phase
		//   walking those pointers, so the ConvertLight side-effect cannot induce
		//   the dangling-pointer crash that previously plagued the split design.
		//
		// Defense for chosen-light cross-invalidation: EnableLight on light A could
		// in principle invalidate light B's pointer via game-side scene mutations.
		// We rebuild a per-iteration alive check via aliveNow (cheap O(N) scan,
		// N ~16) before each EnableLight / ConvertLight call so a dangling pointer
		// is skipped rather than dereferenced.

		auto* shadowSceneNodeRT = &ssn->GetRuntimeData();

		// Two-stage validity check used before any virtual dispatch on a
		// BSShadowLight from s_lights[] or candidates[]:
		//   (1) Is the pointer still in the scene's activeShadowLights?
		//       (catches "removed since last frame")
		//   (2) Is the vtable non-zero?
		//       (catches "freed and zeroed by tbbmalloc / EngineFixes via a path
		//        that bypassed BSSmartPointer ref-counting" — the pointer is
		//        still in activeShadowLights but the object is dead)
		// Either failure → caller must skip the light.
		auto isAliveNow = [shadowSceneNodeRT, sunLight](RE::BSShadowLight* l) -> bool {
			if (!l)
				return false;
			if (l == sunLight)
				return true;
			for (auto& sp : shadowSceneNodeRT->activeShadowLights)
				if (sp.get() == l)
					return true;
			return false;
		};
		auto isVtableValid = [](RE::BSShadowLight* l) -> bool {
			return l && *reinterpret_cast<const uintptr_t*>(l) != 0;
		};
		auto isUsableLight = [&](RE::BSShadowLight* l) -> bool {
			return isAliveNow(l) && isVtableValid(l);
		};

		auto findSlotForLight = [](RE::BSShadowLight* l) -> int {
			for (int i = 0; i < s_lights.Size; i++)
				if (s_lights.Lights[i].Light == l)
					return i;
			return -1;
		};

		// Single decision point for "this light won't shadow this frame --
		// Convert (keeps diffuse via cluster pipeline) or Disable (light
		// vanishes)?". Used by both the c.invalid and c.excess branches.
		//
		// Spots always Disable: the engine has no NiSpotLight equivalent, so
		// ConvertLight on a BSShadowFrustumLight would make the cone-shaped
		// illumination spherical and bleed through walls behind the cone.
		// Omnis/hemis Convert when ConvertExcessToNormal is on or a debug
		// pin-convert is set on this light. The pin override applies even
		// when the user disabled ConvertExcessToNormal globally.
		//
		// allowConvert is a callsite veto -- the c.invalid path passes it
		// false for invalidPortal (cluster has no portal-graph awareness,
		// converting would leak light across cells) so portal-occluded
		// lights always Disable.
		//
		// Returns true on Convert, false on Disable, so callers can apply
		// path-specific follow-ups (e.g. lodDimmer=1 reset on the invalidLod
		// path so the converted light still contributes to clusters).
		auto convertOrDisable = [&](RE::BSShadowLight* light, bool allowConvert) -> bool {
			const bool isSpot = light->GetIsFrustumLight();
			const bool forceConvert = s_pinConvert.count(reinterpret_cast<uintptr_t>(light)) > 0;
			if (allowConvert && (s_settings.ConvertExcessToNormal || forceConvert) && !isSpot) {
				ConvertLight(light, ssn, false);
				return true;
			}
			DisableLight(light);
			return false;
		};

		// Sun slot (slot 0) is processed inline below — sun setup happened at the
		// top of the function; we only need to mark its mask index here.
		if (s_lights.Sun && s_lights.Lights[0].Light && s_lights.Lights[0].RedrawFrame) {
			ShadowField(s_lights.Lights[0].Light, maskIndex) = 0;
			doneLightCount++;
		}

		// Per-candidate Begin/EnableLight/End mutation loop. EnableLight may
		// trigger synchronous shadow render dispatches in the engine, so this
		// zone captures both our scheduler work and any engine-side rendering
		// it pulls in for chosen lights.
		{
			ZoneNamedN(zoneAtomic, "SCM::AtomicMutationLoop", true);
			for (auto& c : candidates) {
				if (c.invalid) {
					// isUsableLight (membership + vtable) is the same gate the
					// excess branch uses. Both ConvertLight and DisableLight
					// fan into virtually-dispatched callees (ReturnShadowmaps),
					// so a freed-but-canonical pointer must be skipped for
					// either path.
					if (!isUsableLight(c.light))
						continue;

					// All c.invalid cases route through convertOrDisable. Per the
					// Ghidra-verified UpdateCamera analysis above, frustrumCull
					// is set both by the genuine sphere-vs-frustum cull AND by
					// the shadow-distance LOD cull; treating them uniformly lets
					// distant lights past shadow distance still reach the
					// cluster pipeline. allowConvert=c.invalidCamera so portal-
					// occluded omnis fall to Disable (cluster lighting has no
					// portal-graph awareness and would leak across cells).
					ZoneNamedN(zCvt, "SCM::Engine::convertOrDisable(invalid)", true);
					if (convertOrDisable(c.light, /*allowConvert=*/c.invalidCamera)) {
						s_schedDiag.converted_invalid++;
						// UpdateCamera zeros lodDimmer alongside frustrumCull
						// when its shadow-distance LOD cull fires. The
						// cluster lighting builder multiplies light.fade by
						// lodDimmer and drops the light if the product falls
						// below 1e-4. Restore only when fully zeroed -- any
						// smooth fade value the engine set is preserved so
						// the cluster contribution fades gradually rather
						// than snapping to full intensity. Matches the
						// per-frame restore in LightLimitFix::UpdateLights
						// for already-converted lights.
						if (c.light->lodDimmer == 0.0f)
							c.light->lodDimmer = 1.0f;
					} else {
						s_schedDiag.disabled_invalid++;
					}
					continue;
				}

				if (c.chosen) {
					int slot = findSlotForLight(c.light);
					if (slot < 0)
						continue;  // matches old behaviour: chosen-but-no-slot is a no-op
					if (slot == 0 && s_lights.Sun)
						continue;  // sun handled above

					auto& e = s_lights.Lights[slot];

					// Render-this-frame path is reserved for chosen point-light slots
					// (excludes converted slots which start at PointLightEnd). Use
					// the sun-aware bound so pool[ShadowLightCount] (the highest
					// point-light slot when Sun=true) is included.
					if (e.RedrawFrame && slot < s_lights.PointLightEnd(s_settings.ShadowLightCount)) {
						// Render-this-frame path. A previous iteration's EnableLight
						// may have transitively freed this light via game-side scene
						// mutations (membership change OR tbbmalloc-zeroed memory),
						// so re-validate before any virtual dispatch.
						if (!isUsableLight(e.Light)) {
							e.Light = nullptr;
							continue;
						}

						auto* lightSnapshot = e.Light;  // value snapshot for budget pairing

						e.Light->UpdateCamera(camera);
						s_budget.BeginLight(lightSnapshot, 0);
						{
							ZoneNamedN(zEnable, "SCM::Engine::EnableLight", true);
							EnableLight(e.Light, camera, ssn, slot);
						}

						// EnableLight callbacks can null e.Light (re-entrant scheduling
						// / scene mutation), AND the engine can free the BSShadowLight
						// during the call without nulling our pointer -- a third-party
						// VR crash report (CommunityShaders.dll v1.5.1, file path
						// D:\a\skyrim-community-shaders\... at EnableLight's
						// `*GetAccumLightSlot() += light->shadowMapCount`) showed the
						// engine reading shadowMapCount from a freed BSShadowLight,
						// corrupting the global accumLightSlot counter, then a
						// downstream `[base + corrupted*8]` AV. Bare null check passes
						// for the freed-but-non-null case; isUsableLight rejects it
						// via the activeShadowLights-membership and vtable checks.
						if (!e.Light || !isUsableLight(e.Light))
							continue;
						s_budget.EndLight(lightSnapshot, 0);

						if (auto* nilight = e.Light->light.get())
							e.lastRenderedPos = nilight->world.translate;

						ShadowField(e.Light, maskIndex) = static_cast<uint32_t>(slot);
						doneLightCount++;
					}
					// Cached-shadow path (chosen + !RedrawFrame, or i >= ShadowLightCount):
					// do nothing here. The non-redrawn light keeps its stale shadow map and
					// is re-inserted by the GameSetShadowCasterSlot loop below at endIdx.
					// Calling DisableLight here would invoke ReturnShadowmaps, releasing the
					// cached shadow data for one frame and producing visible flicker that
					// worsens as the budget gets more constrained.
					continue;
				}

				if (c.excess) {
					if (!isUsableLight(c.light))
						continue;

					// Atomic ordering: by the time we reach excess (rank
					// >= ShadowLightCount), all chosen lights have completed
					// their Begin/EnableLight/End sequence. ConvertLight's
					// ReturnShadowmaps side effect can only invalidate
					// pointers we are no longer walking. LightLimitFix::
					// UpdateLights then iterates activeShadowLights to pick
					// up converted lights for the cluster pipeline.
					//
					// Rank-drift suppression (a torch's importance score
					// bobbing across the chosen/excess boundary frame-to-
					// frame) lives in the score formula via the
					// lightframessincerender decay term, not here.
					ZoneNamedN(zCvt, "SCM::Engine::convertOrDisable(excess)", true);
					if (convertOrDisable(c.light, /*allowConvert=*/true))
						s_schedDiag.converted_excess++;
					else
						s_schedDiag.disabled_excess++;
					continue;
				}
			}
		}  // end SCM::AtomicMutationLoop

		// Non-redrawn chosen lights: insert at end of shadow caster array without rendering.
		// GetAccumLightSlot() already advanced past all EnableLight()-rendered slots.
		//
		// Re-rebuild the alive set: the atomic loop above may have invalidated
		// pointers (e.g. ConvertLight on excess removes from activeShadowLights).
		// Skip s_lights entries whose pointer is no longer in the scene to avoid
		// dereferencing freed BSShadowLight memory below.
		{
			ZoneNamedN(zonePostAtomic, "SCM::PostAtomicRevalidate", true);
			std::unordered_set<RE::BSShadowLight*> aliveAfterAtomic;
			{
				auto& alive = ssn->GetRuntimeData().activeShadowLights;
				aliveAfterAtomic.reserve(alive.size() + 1);
				if (sunLight)
					aliveAfterAtomic.insert(sunLight);
				for (auto& sp : alive)
					if (auto* l = sp.get())
						aliveAfterAtomic.insert(l);
			}

			int endIdx = (int)*GetAccumLightSlot();

			for (int i = 0; i < s_lights.Size; i++) {
				auto& e = s_lights.Lights[i];
				// Re-insert (without rendering) every chosen+!RedrawFrame light
				// AND every converted-slot light (i >= PointLightEnd). The
				// PointLightEnd bound is sun-aware so converted slots correctly
				// start one slot later when Sun=true.
				if (e.Light && (!e.RedrawFrame || i >= s_lights.PointLightEnd(s_settings.ShadowLightCount))) {
					// Membership check uses the snapshot built above (a
					// game-mutation in the atomic loop may have invalidated
					// pointers; aliveAfterAtomic captures the current scene
					// state in O(N) for O(1) membership queries here).
					if (aliveAfterAtomic.find(e.Light) == aliveAfterAtomic.end()) {
						s_schedDiag.reconciliation_clears++;
						e.Clear();
						continue;
					}
					// First-render gate: a chosen light whose slot has never
					// been rendered for IT (LastDrawnFrame < 0) has no valid
					// shadow content in its kSHADOWMAPS slice -- the depth
					// content is either cleared or carries the evicted
					// previous occupant's shadow. Inserting the light as a
					// shadow caster would make the cluster shader sample stale
					// depth and project a wrong shadow shape through the new
					// light. Skip insertion this frame; the light still
					// illuminates via the cluster pipeline as a non-shadow
					// light, with no false shadow. Once it wins a redraw turn
					// LastDrawnFrame goes >= 0 and it joins the shadow set
					// normally.
					//
					// Converted-slot range (i >= PointLightEnd) is unaffected:
					// converted lights don't sample kSHADOWMAPS via this slot
					// path; they participate via the s_normalConvert non-shadow
					// pipeline.
					if (i < s_lights.PointLightEnd(s_settings.ShadowLightCount) &&
						e.LastDrawnFrame < 0 &&
						!(s_lights.Sun && i == 0)) {
						s_schedDiag.first_render_skips++;
						continue;
					}

					// Cached-shadow reuse (the UE5 / CryEngine / Frostbite
					// pattern). We unconditionally sample the cached
					// kSHADOWMAPS slice even when the geometry hash mismatches
					// (light or caster moved since the cached render). For
					// small motion the staleness is sub-pixel and invisible;
					// for large motion the shadow visibly lags the light by
					// 1-2 frames, which is much less objectionable than the
					// full-frame on/off flicker that hash-gated suppression
					// produces on every animated torch. The hash-mismatch
					// priority hint above keeps stale entries at the front of
					// the redraw queue, so the lag self-corrects within budget
					// cycles.
					//
					// The first_render_skips gate above is the only safety
					// gate that DOES suppress insertion: a slot with no
					// rendered content for its current owner (LastDrawnFrame
					// < 0) has no valid cached shadow to fall back on; the
					// GPU slice is either cleared or contains an evicted
					// previous occupant. Hash mismatch on an existing slice
					// is at worst a small visual lag.
					// GameSetShadowCasterSlot calls Accumulate virtually; reuse
					// isUsableLight's vtable guard to catch tbbmalloc-zeroed
					// objects that are still in activeShadowLights but freed.
					if (!isVtableValid(e.Light)) {
						e.Light = nullptr;
						continue;
					}
					GameSetShadowCasterSlot(ssn, e.Light, endIdx, 1);
					// Same hazard as the post-EnableLight site: the engine can
					// free the light during this call. Use isUsableLight, not
					// just null check.
					if (!e.Light || !isUsableLight(e.Light))
						continue;
					endIdx += e.Light->shadowMapCount;
					ShadowField(e.Light, maskIndex) = static_cast<uint32_t>(i);

					// GameSetShadowCasterSlot (via Accumulate) overwrites shadowmapIndex
					// with the sequential endIdx counter, diverging from the stable
					// container-slot index that CopyShadowLightData and Prepass expect.
					// All shadow-slot light types are affected:
					//   Spot (!IsParabolicLight): 1 descriptor, 1 atlas slice.
					//   Hemi (IsParabolicLight && !IsOmniLight): 1 descriptor, 1 atlas slice.
					//   Omni (IsParabolicLight && IsOmniLight): both paraboloids packed into
					//     a single atlas slice via UV splitting in GetOmnidirectionalShadow,
					//     so all descriptors should also point to i.
					// Restore shadowmapIndex = i for every non-redrawn shadow-slot light.
					// Only restore shadowmapIndex for point-light slots (skip converted).
					// PointLightEnd accounts for sun bookkeeping so the highest point-light
					// slot (Sun=true: pool[ShadowLightCount]) is included.
					if (s_settings.ShadowLightCount > 4 && i < s_lights.PointLightEnd(s_settings.ShadowLightCount)) {
						// Restore descriptor.shadowmapIndex for cached (non-redrawn)
						// chosen lights so RenderCascade samples their preserved
						// depth slice. Sun (pool[0] when Sun=true) is skipped —
						// it renders via the directional cascade path, not
						// kSHADOWMAPS, so its descriptor.shadowmapIndex is unused.
						if (s_lights.Sun && i == 0)
							continue;
						if (REL::Module::IsVR()) {
							for (auto& desc : e.Light->GetVRRuntimeData().shadowmapDescriptors)
								desc.shadowmapIndex = static_cast<uint32_t>(i);
						} else {
							for (auto& desc : e.Light->GetRuntimeData().shadowmapDescriptors)
								desc.shadowmapIndex = static_cast<uint32_t>(i);
						}
					}
				}
			}
		}
		// Update rolling redraw and budget statistics.
		{
			int redrawing = 0;
			int32_t consumed = 0;
			for (int i = 0; i < s_lights.Size; i++) {
				auto& e = s_lights.Lights[i];
				if (e.Light && e.RedrawFrame) {
					if (i != 0 || !s_lights.Sun)
						consumed += s_budget.GetCost(e.Light);
					redrawing++;
				}
			}
			s_redrawSum -= s_redrawHistory[s_redrawHistoryPos];
			s_redrawHistory[s_redrawHistoryPos] = redrawing;
			s_redrawSum += redrawing;
			s_redrawHistoryPos = (s_redrawHistoryPos + 1) % kRedrawHistorySize;

			s_budgetSum -= s_budgetHistory[s_budgetHistoryPos];
			s_budgetHistory[s_budgetHistoryPos] = consumed;
			s_budgetSum += consumed;
			s_budgetHistoryPos = (s_budgetHistoryPos + 1) % kRedrawHistorySize;
		}

		ssn->GetRuntimeData().firstPersonShadowMask = *GetShadowMask();
		*GetFrameLightCount() = static_cast<uint32_t>(doneLightCount);

		// =====================================================================
		// Tracy per-frame plots: scheduler diagnostic counters + live config.
		// Emitting both in the same frame lets a capture be queried for A/B
		// behaviour without re-running the game: the cfg_* plots are the
		// independent variables, the scm.* plots are the dependent outcomes.
		// =====================================================================
		{
			// Sample slot occupancy at frame end (post-reconciliation).
			for (int i = 0; i < s_lights.Size; i++)
				if (s_lights.Lights[i].Light)
					s_schedDiag.slots_in_use++;

			TracyPlot("scm.candidates.total", (int64_t)s_schedDiag.candidates_total);
			TracyPlot("scm.candidates.chosen", (int64_t)s_schedDiag.candidates_chosen);
			TracyPlot("scm.candidates.excess", (int64_t)s_schedDiag.candidates_excess);
			TracyPlot("scm.candidates.invalid_camera", (int64_t)s_schedDiag.candidates_invalid_camera);
			TracyPlot("scm.candidates.invalid_portal", (int64_t)s_schedDiag.candidates_invalid_portal);
			TracyPlot("scm.candidates.invalid_frustum", (int64_t)s_schedDiag.candidates_invalid_frustum);
			TracyPlot("scm.candidates.invalid_lod", (int64_t)s_schedDiag.candidates_invalid_lod);
			TracyPlot("scm.candidates.invalid_other", (int64_t)s_schedDiag.candidates_invalid_other);
			TracyPlot("scm.converted.invalid", (int64_t)s_schedDiag.converted_invalid);
			TracyPlot("scm.converted.excess", (int64_t)s_schedDiag.converted_excess);
			TracyPlot("scm.disabled.invalid", (int64_t)s_schedDiag.disabled_invalid);
			TracyPlot("scm.disabled.excess", (int64_t)s_schedDiag.disabled_excess);
			TracyPlot("scm.reconciliation.clears", (int64_t)s_schedDiag.reconciliation_clears);
			TracyPlot("scm.slots.in_use", (int64_t)s_schedDiag.slots_in_use);
			TracyPlot("scm.first_render_skips", (int64_t)s_schedDiag.first_render_skips);

			// Live config plots — record the *current* settings on each frame so
			// a single capture spanning a settings change captures both sides.
			TracyPlot("cfg.ShadowLightCount", (int64_t)s_settings.ShadowLightCount);
			TracyPlot("cfg.MaxRedrawPerFrame", (int64_t)s_settings.MaxRedrawPerFrame);
			TracyPlot("cfg.ConvertExcessToNormal", (int64_t)(s_settings.ConvertExcessToNormal ? 1 : 0));
			TracyPlot("cfg.Enabled", (int64_t)(s_settings.Enabled ? 1 : 0));
			TracyPlot("cfg.RedrawBudgetMs", (double)s_settings.RedrawBudgetMs);
		}
	}

	// =========================================================================
	// Render hook: replaces RenderActiveShadowCasterLights
	// Iterates s_lights and calls Render() on lights flagged RedrawFrame.
	// Uses install_context_hook at a specific call site in the render loop (see Install()).
	// =========================================================================

	static void RenderScheduledShadowLights()
	{
		// VR: RenderActiveShadowCasterLights normally saves+clears g_drawStereo before
		// iterating shadow casters, then restores it. Without this, each hemisphere
		// render is doubled for both eyes -> 4-quadrant shadow map texture.
		bool savedStereo = false;
		if (REL::Module::IsVR()) {
			savedStereo = *globals::game::drawStereo;
			*globals::game::drawStereo = false;
		}

		ZoneScopedN("SCM::RenderScheduledShadowLights");
		auto* state = globals::state;
		state->BeginPerfEvent("SCM::RenderScheduledShadowLights");
#ifdef TRACY_ENABLE
		TracyD3D11Zone(state->tracyCtx, "SCM::RenderScheduledShadowLights");
#endif

		s_budget.Begin(1);

		uint32_t tmp = 0;
		// Sun first: BSShadowDirectionalLight::Render emits the "Directional
		// Light Shadowmaps" marker and writes the cascade depth maps to
		// kSHADOWMAPS_ESRAM. The engine's vanilla RenderActiveShadowCasterLights
		// dispatches this via the same vtable walk it uses for point lights;
		// we replaced that walk with this loop, so we need to call sun.Render
		// explicitly. Without this, the directional cascade pass is skipped
		// and exterior scenes render with no sun shadow.
		if (s_lights.Sun && s_lights.Lights[0].Light) {
			ZoneNamedN(zSun, "SCM::Render::Sun", true);
#ifdef TRACY_ENABLE
			TracyD3D11Zone(state->tracyCtx, "SCM::Render::Sun");
#endif
			s_budget.BeginLight(s_lights.Lights[0].Light, 1);
			s_lights.Lights[0].Light->Render(tmp);
			s_budget.EndLight(s_lights.Lights[0].Light, 1);
		}

		// Point lights from PointLightFirst onwards. PointLightFirst skips
		// slot 0 (handled above when Sun=true). PointLightEnd includes the
		// highest point-light slot when Sun=true.
		{
			ZoneNamedN(zPoint, "SCM::Render::PointLights", true);
#ifdef TRACY_ENABLE
			TracyD3D11Zone(state->tracyCtx, "SCM::Render::PointLights");
#endif
			for (int i = s_lights.PointLightFirst(); i < s_lights.PointLightEnd(s_settings.ShadowLightCount); i++) {
				auto& e = s_lights.Lights[i];
				if (!e.Light || !e.RedrawFrame)
					continue;
				s_budget.BeginLight(e.Light, 1);
				e.Light->Render(tmp);
				s_budget.EndLight(e.Light, 1);
			}
		}

		state->EndPerfEvent();

		if (REL::Module::IsVR())
			*globals::game::drawStereo = savedStereo;
	}

	// Replaces the call to RenderActiveShadowCasterLights.
	// install_context_hook (RtlRestoreContext) is required so all volatile registers (r8, etc.)
	// are restored before the game continues past the patched call site.
	//
	// Non-VR (SE/AE): set ctx.Rax = 0 so the conditional between 107133+0x192 and
	// +0x1AE skips "call [r8+0x50]" -- r8 is loaded from rax there; if rax != 0,
	// r8 gets a stale pointer whose [+0x50] slot is null -> crash at execute 0x0.
	static void Hook_RenderShadowLights(CONTEXT& ctx)
	{
		if (!REL::Module::IsVR())
			ctx.Rax = 0;
		RenderScheduledShadowLights();
	};

	// Hook struct for stl::detour_thunk.
	//
	// `s_settings.Enabled` is now a BOOT-TIME flag only -- toggling at
	// runtime has no effect on this thunk, the same way ShadowLightCount
	// and atlas texture sizes are restart-gated. See Init() at the
	// settings.Enabled early-return for the boot-time gate.
	//
	// Rationale (Ghidra-verified by crash 2026-05-17 20:31:12): the AV
	// at BSBatchRenderer::sub_SE100843_AE107633 +0x54
	// (`mov rax, [r14+0x48]`, r14=1 = vfunc bool returned as pointer)
	// is reached via:
	//   NiCamera::CalculateAndDrawShadowCasterLights
	//   -> CalculateActiveShadowCasterLights  (the engine's vanilla
	//                                          scheduler -- what we'd
	//                                          route to on disable)
	//     -> BSShadowDirectionalLight::sub_SE100818_AE107602 (sun
	//                                                        shadow)
	//       -> FUN_1414bf320 (BSCullingProcess inner)
	//         -> BSCullingProcess::sub
	//           -> FUN_1414f50d0
	//             -> BSBatchRenderer::sub_SE100843_AE107633  (AV)
	//
	// The crash is in the vanilla scheduler itself. SCM's boot-time
	// modifications (kSHADOWMAPS texture sized to ShadowLightCount,
	// depth-buffer creation loop redirected via Hook_CreateNormalDepthBuffer
	// and Hook_CreateReadOnlyDepthBuffer, color-mask pass replaced by
	// Hook_DisableColorMask) make the engine state incompatible with
	// the vanilla traversal even when our runtime tracking is left
	// untouched (soft-disable still crashed). The deep engine hooking
	// is not safely reversible at runtime; restart is the only safe
	// way to revert to vanilla.
	struct Hook_CalculateActiveShadowCasters
	{
		static void thunk()
		{
			ScheduleShadowCasters();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// =========================================================================
	// Surface lights hook
	// Replaces CalculateActiveNonShadowCasterLights (ID 100997/107784).
	// Uses install_context_hook because the function has 10 args (11 in VR)
	// with VR-specific stack layout -- CONTEXT is the simplest cross-runtime approach.
	// =========================================================================

	static void Hook_CalculateActiveLightsForSurface(CONTEXT& ctx)
	{
		// Args from registers/stack (x64 fastcall, shadow space at RSP+0x00..0x20):
		auto* lightData = reinterpret_cast<RE::BSShaderPropertyLightData*>(ctx.Rcx);           // a1
		auto** lights = reinterpret_cast<RE::BSLight**>(ctx.Rdx);                              // a2
		int maxCount = static_cast<int>(ctx.R8);                                               // a3
		int* shadowCount = reinterpret_cast<int*>(ctx.R9);                                     // a4
		auto* ssn = *reinterpret_cast<RE::ShadowSceneNode**>(ctx.Rsp + 0x28);                  // a5
		auto* shaderProp = *reinterpret_cast<RE::BSLightingShaderProperty**>(ctx.Rsp + 0x30);  // a6
		bool addShadow = *reinterpret_cast<bool*>(ctx.Rsp + 0x38);                             // a7
		bool* useShadowSun = *reinterpret_cast<bool**>(ctx.Rsp + 0x40);                        // a8
		bool firstPerson = *reinterpret_cast<bool*>(ctx.Rsp + 0x48);                           // a9
		uint32_t fpMask = *reinterpret_cast<uint32_t*>(ctx.Rsp + 0x50);                        // a10

		// VR passes an 11th arg: if non-zero, skip accumulation (vanilla early-out).
		if (REL::Module::IsVR() && *reinterpret_cast<char*>(ctx.Rsp + 0x58) != 0) {
			ctx.Rax = 1;  // addedLightCount = sun only
			return;
		}

		// Determine the sun light for this surface.
		RE::BSLight* sunLight;
		if (*useShadowSun)
			sunLight = ssn->GetRuntimeData().sunShadowDirLight;
		else
			sunLight = ssn->GetRuntimeData().sunLight;
		if (shaderProp->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kCloudLOD))
			sunLight = ssn->GetRuntimeData().cloudLight;

		lights[0] = sunLight;
		*shadowCount = 0;
		int added = 1;

		if (addShadow) {
			auto& casters = ssn->GetRuntimeData().shadowLightsAccum;

			// Step 1: vanilla shadow lights gated by activeLightMask / first-person mask.
			for (uint32_t slot = 0; slot < casters.size() && added < maxCount; slot++) {
				uint32_t bit = 1u << slot;
				if (!((firstPerson && (fpMask & bit)) || (lightData->activeLightMask & bit)))
					continue;
				auto* sl = reinterpret_cast<RE::BSLight*>(casters[slot]);
				if (!sl || sl == sunLight)
					continue;
				if (GameIsLightAffectingSurface(shaderProp, sl)) {
					lights[added++] = sl;
					(*shadowCount)++;
				}
			}

			// Step 2: extended pool lights not covered by the vanilla mask.
			// Only inject lights that are present in this scene's caster array
			// (prevents world lights leaking into menu / special scenes).
			// Iterate the point-light range (sun-aware via PointLightFirst /
			// PointLightEnd; pre-helper loops missed pool[ShadowLightCount]
			// when Sun=true, dropping one shadow caster from per-surface lists).
			for (int i = s_lights.PointLightFirst(); i < s_lights.PointLightEnd(s_settings.ShadowLightCount) && added < maxCount; i++) {
				auto& e = s_lights.Lights[i];
				if (!e.Light || reinterpret_cast<RE::BSLight*>(e.Light) == sunLight)
					continue;

				bool inScene = false;
				for (uint32_t s = 0; s < casters.size() && !inScene; s++)
					if (reinterpret_cast<RE::BSLight*>(casters[s]) == reinterpret_cast<RE::BSLight*>(e.Light))
						inScene = true;
				if (!inScene)
					continue;

				bool alreadyAdded = false;
				for (int j = 1; j < added && !alreadyAdded; j++)
					if (lights[j] == reinterpret_cast<RE::BSLight*>(e.Light))
						alreadyAdded = true;
				if (alreadyAdded)
					continue;

				if (GameIsLightAffectingSurface(shaderProp, reinterpret_cast<RE::BSLight*>(e.Light))) {
					lights[added++] = reinterpret_cast<RE::BSLight*>(e.Light);
					(*shadowCount)++;
				}
			}
		}

		// Step 3: non-shadow lights from the per-surface accumulation list.
		// Skip parabolic shadow-casters (frustrumCull == 0xFF) and hidden NiLights.
		for (uint32_t i = 0; i < lightData->lights.size() && added < maxCount; i++) {
			auto* l = lightData->lights[i];
			if (!l || l == sunLight)
				continue;
			auto* ni = l->light.get();
			if (ni && (l->frustrumCull == 0xFFu || ni->GetFlags().any(RE::NiAVObject::Flag::kHidden)))
				continue;
			lights[added++] = l;
		}

		// Step 4: Inject converted shadow lights (s_normalConvert, issue #2121 #3)
		// into the per-surface lights array. These lights have frustrumCull == 0xFF
		// (parabolic shadow-caster marker) and are skipped by Step 3, while Steps
		// 1/2 don't include them either (ReturnShadowmaps cleared shadowLightsAccum).
		//
		// The cluster pipeline picks them up separately via LightLimitFix::UpdateLights'
		// activeShadowLights iteration; this Step 4 ensures the engine's vanilla
		// strict-light loop (which consumes lights[] passed to this function) also
		// sees them so non-LLF code paths and shaders without LIGHT_LIMIT_FIX still
		// receive the diffuse contribution.
		for (auto& c : s_normalConvert) {
			if (added >= maxCount)
				break;
			auto* l = reinterpret_cast<RE::BSLight*>(c.light);
			if (!l || l == sunLight)
				continue;
			auto* ni = l->light.get();
			if (!ni || ni->GetFlags().any(RE::NiAVObject::Flag::kHidden))
				continue;

			// Skip if already added in any prior step.
			bool alreadyAdded = false;
			for (int j = 1; j < added && !alreadyAdded; j++)
				if (lights[j] == l)
					alreadyAdded = true;
			if (alreadyAdded)
				continue;

			if (GameIsLightAffectingSurface(shaderProp, l))
				lights[added++] = l;
			// Note: do NOT increment *shadowCount; this is a non-shadow contribution.
		}

		ctx.Rax = static_cast<uint64_t>(added);
	}

	// =========================================================================
	// Light conversion hooks
	//
	// BSShadowLight::IsShadowLight (VFT slot 3): returns false for lights in
	// s_normalConvert so the engine treats them as normal (non-shadow) lights
	// during the geometry-shader/stencil shadow-masking pass.
	//
	// RemoveLight / AddLight / SetLight hooks maintain s_normalConvert and
	// s_shadowConvert so the lists stay consistent with scene changes.
	// =========================================================================

	static bool Hook_IsShadowLight(RE::BSShadowLight* light)
	{
		for (auto& c : s_normalConvert)
			if (c.light == light)
				return false;
		return true;
	}

	// Fires at start of ShadowSceneNode::RemoveLight (ID 99697/106331).
	static void Hook_ConvertLights_Remove(CONTEXT& ctx)
	{
		auto* ssn = reinterpret_cast<RE::ShadowSceneNode*>(ctx.Rcx);
		auto* light = reinterpret_cast<RE::NiLight*>(ctx.Rdx);
		if (ssn != GetShadowSceneNode())
			return;
		for (auto it = s_normalConvert.begin(); it != s_normalConvert.end(); ++it) {
			auto* nl = it->light->light.get();
			if (nl && nl == light) {
				GameClearGeometryList(it->light);
				s_normalConvert.erase(it);
				break;
			}
		}
		if (light)
			s_shadowConvert.erase(light);
	}

	// Fires at start of ShadowSceneNode::AddLight (ID 99692/106326).
	// Optionally promotes normal light to shadow light; always forces portal-strict.
	static void Hook_ConvertLights_Add(CONTEXT& ctx)
	{
		auto* ssn = reinterpret_cast<RE::ShadowSceneNode*>(ctx.Rcx);
		auto* light = reinterpret_cast<RE::NiLight*>(ctx.Rdx);
		auto* p = reinterpret_cast<RE::ShadowSceneNode::LIGHT_CREATE_PARAMS*>(ctx.R8);
		if (ssn != GetShadowSceneNode() || !light || !p)
			return;

		if (s_settings.PromoteNormalToShadow && !p->shadowLight) {
			p->shadowLight = true;
			p->fov = 6.2831855f;
			p->dynamic = true;
			p->restrictedNode = nullptr;
			p->falloff = 1.0f;
			p->depthBias = 1.0f;
			p->nearDistance = (light->GetLightRuntimeData().radius.x / 512.0f) * 219.6356f;
			s_shadowConvert.insert(light);
		}
		// Portal-strict policy by shadow type. The engine picks the concrete
		// shadow class (BSShadowParabolicLight / BSShadowHemisphereLight /
		// BSShadowFrustumLight) based on the FOV in LIGHT_CREATE_PARAMS:
		//   fov >= ~2pi  -> dual-paraboloid omni
		//   fov >= ~pi   -> hemisphere
		//   fov <  ~pi   -> perspective spot/frustum
		// Tightening portal-strict on omnis/hemis usefully exercises the
		// portal-graph visibility test; doing it on spots drops culled-but-
		// visible spots entirely (the cone test rejects spots whose origin
		// sits behind a portal even when the cone sweeps into a visible
		// room). Honour the per-type toggle so users can A/B easily.
		constexpr float kFovHemiThreshold = 3.0f;  // ~pi
		constexpr float kFovOmniThreshold = 6.0f;  // ~2pi
		bool enforce = false;
		if (p->fov >= kFovOmniThreshold)
			enforce = s_settings.ForceEnablePortalStrictOmni;
		else if (p->fov >= kFovHemiThreshold)
			enforce = s_settings.ForceEnablePortalStrictHemi;
		else
			enforce = s_settings.ForceEnablePortalStrictSpot;
		if (enforce)
			p->portalStrict = true;
	}

	// Fires at start of BSLight::SetLight (ID 101302/108289).
	// Tracks NiLight pointer reassignments in s_shadowConvert.
	static void Hook_ConvertLights_SetLight(CONTEXT& ctx)
	{
		auto* bslight = reinterpret_cast<RE::BSLight*>(ctx.Rcx);
		auto* nilight = reinterpret_cast<RE::NiLight*>(ctx.Rdx);
		if (!bslight)
			return;
		auto* oldlight = bslight->light.get();
		if (oldlight && oldlight != nilight) {
			bool did = s_shadowConvert.erase(oldlight) != 0;
			if (nilight && did)
				s_shadowConvert.insert(nilight);
		}
	}

	// =========================================================================
	// Stealth detection fix
	//
	// GetLightLevel (AIProcess::CalculateLightValue, ID 38900/39946) uses the
	// engine shadow-light iteration internally. When we replace shadow caster
	// selection, the vanilla per-light affect-player loop no longer sees our
	// chosen lights correctly. We replace it with our own pass that iterates
	// activeShadowLights and calls IsLightAffectingActor() directly.
	// =========================================================================

	// Temporary set of lights that affect the player -- populated each frame
	// in Hook_UpdateLightLevelPlayer, consumed in Hook_CheckLightLevelPlayer.
	static std::set<uint64_t> s_stealthDetectionTmp;

	static void* GetUnkDetectionGlobal()
	{
		// SE: 142F6DB98 -- a ~80-byte detection struct; GetSingleton equivalent
		static REL::RelocationID uid(518074, 404596);
		return *reinterpret_cast<void**>(uid.address());
	}

	static bool IsLightAffectingActor(RE::BSShadowLight* light, RE::Actor* actor, RE::NiPoint3* pos)
	{
		// SE: 14071A380 (ID 41661)
		using F = bool (*)(void*, RE::BSShadowLight*, RE::Actor*, RE::NiPoint3*);
		static REL::Relocation<F> func{ REL::RelocationID(41661, 42744) };
		return func(GetUnkDetectionGlobal(), light, actor, pos);
	}

	// Replaces the vanilla shadow-light-affect-player loop.
	// RBP-33 holds the player's position (NiPoint3*).
	static void Hook_UpdateLightLevelPlayer(CONTEXT& ctx)
	{
		auto* pos = reinterpret_cast<RE::NiPoint3*>(ctx.Rbp - 33);
		auto* player = RE::PlayerCharacter::GetSingleton();

		s_stealthDetectionTmp.clear();
		auto* ssn = GetShadowSceneNode();
		if (!ssn)
			return;

		for (auto& sp : ssn->GetRuntimeData().activeShadowLights) {
			auto* l = sp.get();
			if (!l)
				continue;
			auto* ni = l->light.get();
			if (!ni || ni->GetFlags().any(RE::NiAVObject::Flag::kHidden))
				continue;
			if (IsLightAffectingActor(l, player, pos))
				s_stealthDetectionTmp.insert(reinterpret_cast<uint64_t>(l));
		}
	}

	// Per-light check inside the vanilla affect-player path.
	// If the light is not in our set, skip the branch (ctx.Rip += 0x16).
	// Note: Execute() sets ctx.Rip = resumeAddr BEFORE calling this, so
	// ctx.Rip += 0x16 skips 0x16 bytes past the hook site -- correct.
	static void Hook_CheckLightLevelPlayer(CONTEXT& ctx)
	{
		auto* light = reinterpret_cast<RE::BSShadowLight*>(ctx.Rcx);
		if (s_stealthDetectionTmp.find(reinterpret_cast<uint64_t>(light)) == s_stealthDetectionTmp.end())
			ctx.Rip += 0x16;
	}

	// =========================================================================
	// Public API
	// =========================================================================

	void Init(const Settings& settings)
	{
		s_settings = settings;

		// Check for external shadow management plugins that conflict with our hooks.
		if (GetModuleHandleW(L"intellightent-ng.dll")) {
			s_externalConflict = true;
			s_conflictMessage =
				"Disabled: intellightent-ng.dll detected. Both mods manage shadow caster "
				"selection and cannot run simultaneously. Remove one to use the other.";
			logger::warn("[SCM] {}", s_conflictMessage);
			return;
		}

		int total = LightContainerSize(settings);
		s_lights.Size = total;
		s_lights.Sun = false;
		s_lights.Lights = new LightEntry[total]();
		for (int i = 0; i < total; i++)
			s_lights.Lights[i].Index = i;

		// Seed auto-budget ring buffer to 60 fps so the first few frames have sane values.
		std::fill(std::begin(s_ftRing), std::end(s_ftRing), 16.67f);
		s_ftEMA = 16.67f;

		// Parse formula strings
		if (!settings.ScoreFormula.empty()) {
			s_formulaScore = std::make_unique<FormulaHelper>();
			if (!s_formulaScore->Parse(settings.ScoreFormula))
				logger::error("[SCM] Failed to parse ScoreFormula");
		}
		if (!settings.RedrawIntervalFormula.empty()) {
			s_formulaRedrawInterval = std::make_unique<FormulaHelper>();
			if (!s_formulaRedrawInterval->Parse(settings.RedrawIntervalFormula))
				logger::error("[SCM] Failed to parse RedrawIntervalFormula");
		}
		if (!settings.RedrawBudgetFormula.empty()) {
			s_formulaRedrawBudget = std::make_unique<FormulaHelper>();
			if (!s_formulaRedrawBudget->Parse(settings.RedrawBudgetFormula))
				logger::error("[SCM] Failed to parse RedrawBudgetFormula");
		}
	}

	// Set by the resolution combo when the user picks a new tier. Gates the
	// SaveINISettings write so we only touch SkyrimPrefs.ini when there's an
	// actual change to persist -- without this, every Save Settings click
	// would rewrite the user's prefs file even if shadow res wasn't edited.
	static bool s_shadowResolutionDirty = false;

	void LoadINISettings()
	{
		// No-op: the engine already loaded SkyrimPrefs.ini at startup, so the
		// live RE::Setting reflects the user's saved value. Future overrides
		// that need to land before SCM::Install hook here.
	}

	void SaveINISettings()
	{
		if (!s_shadowResolutionDirty)
			return;
		auto* prefColl = RE::INIPrefSettingCollection::GetSingleton();
		if (!prefColl)
			return;
		auto* setting = prefColl->GetSetting("iShadowMapResolution:Display");
		if (!setting)
			return;

		// The engine's INIPrefSettingCollection::WriteSetting requires
		// OpenHandle to have been called first (it writes via the cached
		// `handle` member, which is null between RefreshINI calls). Calling
		// it directly returns true but silently no-ops -- verified by the
		// fact that the live RE::Setting updates but SkyrimPrefs.ini's
		// timestamp doesn't change after Save Settings.
		//
		// Sidestep the engine path entirely with WritePrivateProfileStringA.
		// CommonLib stores the full path of SkyrimPrefs.ini in subKey at
		// startup (see InitializeSkyrimINIPrefSettingCollection caller at
		// SE 1406489e6 / AE 140648990 / VR equivalent -- it concatenates the
		// Documents path with "SkyrimPrefs.ini"). The setting name encodes
		// "<key>:<section>" -- "iShadowMapResolution:Display" means
		// [Display]\niShadowMapResolution=N.
		const char* fullName = setting->GetName();
		const char* colon = std::strchr(fullName, ':');
		if (!colon) {
			logger::warn("[SCM] Setting name '{}' has no section -- cannot write to INI", fullName);
			s_shadowResolutionDirty = false;
			return;
		}
		const std::string key(fullName, colon - fullName);
		const std::string section(colon + 1);
		const std::string value = std::to_string(setting->GetInteger());

		// subKey holds the full path to SkyrimPrefs.ini.
		const char* iniPath = prefColl->subKey;
		if (!iniPath || !iniPath[0]) {
			logger::warn("[SCM] INIPrefSettingCollection subKey is empty -- cannot write to INI");
			s_shadowResolutionDirty = false;
			return;
		}

		if (::WritePrivateProfileStringA(section.c_str(), key.c_str(), value.c_str(), iniPath)) {
			// Windows caches INI writes in-process; the file on disk doesn't
			// update until the cache is flushed. Calling WritePrivateProfile
			// with three NULL parameters forces the flush. Without this the
			// write succeeds (returns non-zero, no error) but the file's
			// timestamp and contents stay stale until the process exits.
			// See KB Q104112 / MSDN remarks for WritePrivateProfileString.
			::WritePrivateProfileStringA(nullptr, nullptr, nullptr, iniPath);
			logger::info("[SCM] Persisted [{}]{}={} to {}", section, key, value, iniPath);
		} else {
			const DWORD err = ::GetLastError();
			logger::warn("[SCM] WritePrivateProfileStringA failed (err={}) writing [{}]{}={} to {}",
				err, section, key, value, iniPath);
		}
		s_shadowResolutionDirty = false;
	}

	void Install(const Settings& settings)
	{
		s_settings = settings;
		s_installedShadowLightCount = settings.ShadowLightCount;
		// kSHADOWMAPS is point/spot only -- the sun renders to a separate
		// kSHADOWMAPS_ESRAM texture (cascade descriptors live there, not
		// here). So the engine allocates exactly ShadowLightCount slices
		// in kSHADOWMAPS; no +1 for the sun.
		s_requestedSlotCount = static_cast<uint32_t>(settings.ShadowLightCount);

		if (s_externalConflict)
			return;

		if (!settings.Enabled) {
			logger::info("[SCM] Shadow caster manager disabled -- skipping hook installation.");
			return;
		}

		bool extended = settings.ShadowLightCount > 4;
		bool needExtraBuffers = settings.ShadowLightCount > 8;

		// ---- Extended depth buffer infrastructure -------------------------

		if (needExtraBuffers) {
			globals::features::llf::normalDepthBuffer = new void*[settings.ShadowLightCount + 1]();
			globals::features::llf::readOnlyDepthBuffer = new void*[settings.ShadowLightCount + 1]();

			// Patch the creation-loop count from 8 to ShadowLightCount.
			// SE/VR: pattern "C7 44 24 68 08 00 00 00" (+4 = the imm32 0x00000008)
			// AE:    same pattern at different offset
			//
			// The instruction encodes a 32-bit immediate; we overwrite all four
			// bytes so values >255 don't silently truncate (a single-byte write
			// to the low byte would leave higher bytes stale, capping us at 255
			// while making the cap silent).
			{
				static REL::RelocationID uid(100458, 107175);
				uintptr_t addr = uid.address() + REL::Relocate(0xD326 - 0xC940, 0xBF6 - 0x210, 0xc91);
				int immOff = REL::Relocate(4, 4, 3);
				uint32_t newCount = static_cast<uint32_t>(settings.ShadowLightCount);
				REL::safe_write(addr + immOff, &newCount, sizeof(newCount));
			}

			// Redirect depth-buffer pointer storage in the creation loop.
			{
				// Normal DSV creation: SE 140D6AB52 / VR 140DBCA00
				static REL::RelocationID uid(75469, 77255);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0xB52 - 0x9E0, 0x2EB - 0x180, 0x1a0);
				int sz = REL::Relocate(7, 7, 8);
				if (!SKSE::stl::install_context_hook(base + off, sz, Hook_CreateNormalDepthBuffer, sz))
					logger::error("[SCM] Failed to install Hook_CreateNormalDepthBuffer");
			}
			{
				// ReadOnly DSV creation: SE 140D6AB71 / VR 140DBCA24
				static REL::RelocationID uid(75469, 77255);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0xB71 - 0x9E0, 0x2FC - 0x180, 0x1c4);
				int sz = REL::Relocate(8, 7, 7);
				if (!SKSE::stl::install_context_hook(base + off, sz, Hook_CreateReadOnlyDepthBuffer, sz))
					logger::error("[SCM] Failed to install Hook_CreateReadOnlyDepthBuffer");
			}

			// Sync the first 8 slots into the game's own DepthStencilData array.
			{
				// SE 140D6AC00 / VR 140DBCAB0
				static REL::RelocationID uid(75469, 77255);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0xC00 - 0x9E0, 0x384 - 0x180, 0x250);
				if (!SKSE::stl::install_context_hook(base + off, 8, Hook_SetupGameArray, 8))
					logger::error("[SCM] Failed to install Hook_SetupGameArray");
			}

			// Depth-buffer selection at draw time.
			{
				// SE 140D70444
				static REL::RelocationID uid(75580, 77386);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0x444 - 0x2F0, 0x704 - 0x5B0, 0x1c3);
				if (!SKSE::stl::install_context_hook(base + off, 21, Hook_SelectDepthBuffer1))
					logger::error("[SCM] Failed to install Hook_SelectDepthBuffer1");
			}
			{
				// SE 140D6A1A5 / VR 140DBBFFC
				static REL::RelocationID uid(75462, 77247);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0x1A5 - 0x070, 0x985 - 0x850, 0x19c);
				int sz = REL::Relocate(10, 10, 0x2e);
				if (!SKSE::stl::install_context_hook(base + off, sz, Hook_SelectDepthBuffer2))
					logger::error("[SCM] Failed to install Hook_SelectDepthBuffer2");
			}

			// Release extended buffers at renderer shutdown.
			// SE: ZeroDepthStencilData; AE/VR: Renderer::Shutdown and related dtor paths.
			if (REL::Module::GetRuntime() != REL::Module::Runtime::AE) {
				// SE + VR share the same pattern.
				static REL::RelocationID uid(75628, 0 /*AE unused*/);
				uintptr_t addr = uid.address() + (0xE27 - 0xDD0);
				if (!SKSE::stl::install_context_hook(addr, 9, Hook_DeleteDepthBuffers_SE, -9))
					logger::error("[SCM] Failed to install Hook_DeleteDepthBuffers_SE");
			} else {
				// AE has three separate shutdown paths.
				static REL::RelocationID uid1(0, 77228);
				if (!SKSE::stl::install_context_hook(uid1.address() + (0x3195 - 0x2E10), 7, Hook_DeleteDepthBuffers_AE, 7))
					logger::error("[SCM] Failed to install Hook_DeleteDepthBuffers_AE (path 1)");

				static REL::RelocationID uid2(0, 77237);
				if (!SKSE::stl::install_context_hook(uid2.address() + (0x3B8C - 0x34A0), 7, Hook_DeleteDepthBuffers_AE, 7))
					logger::error("[SCM] Failed to install Hook_DeleteDepthBuffers_AE (path 2)");

				static REL::RelocationID uid3(0, 77238);
				if (!SKSE::stl::install_context_hook(uid3.address() + (0x3E79 - 0x3BC0), 6, Hook_DeleteDepthBuffers_AE, -6))
					logger::error("[SCM] Failed to install Hook_DeleteDepthBuffers_AE (path 3)");
			}
		}

		// Expanded accumulated-lights array (needed when ShadowLightCount > 4).
		if (extended) {
			// SE: BSShadowFrustumLight accumulation setup
			static REL::RelocationID uid(99686, 106320);
			uintptr_t base = uid.address();
			uintptr_t off = REL::Relocate(0xFCA4 - 0xF950, 0xF05 - 0xBB0, 0x387);
			if (!SKSE::stl::install_context_hook(base + off, 5, Hook_AccumulatedLightsArray, 5))
				logger::error("[SCM] Failed to install Hook_AccumulatedLightsArray");
		}

		// Force per-light shadow map slot assignment.
		// Required whenever our temporal scheduler is active (ShadowLightCount >= 4):
		// RenderCascade recalculates the slot from a global counter each call; without
		// this hook, a light not redrawn this frame gets a different slot than last
		// frame and corrupts another light's shadow map.
		{
			// SE: RenderCascade+0xBE; VR: RenderCascade+0xE0
			static REL::RelocationID uid(100820, 107604);
			uintptr_t base = uid.address();
			uintptr_t off = REL::Relocate(0xA9E - 0x9E0, 0xDB0 - 0xCF0, 0xe0);
			if (!SKSE::stl::install_context_hook(base + off, 0x25, Hook_OverwriteShadowMapIndex))
				logger::error("[SCM] Failed to install Hook_OverwriteShadowMapIndex");
		}

		// ---- Focus shadow disable (slots 4-7 conflict with extended lights) --
		if (extended) {
			// Patch two "get selected focus shadows" thunks to always return 0.
			// IDs 10209/10247 and 10207/10245 (SE/AE).  VR shares the same IDs.
			// Pattern "8B 05 xx xx xx xx" (MOV EAX, [rip+N]) -> "48 31 C0 90 90 90" (XOR RAX,RAX + NOPs)
			const uint8_t xorRax[6] = { 0x48, 0x31, 0xC0, 0x90, 0x90, 0x90 };

			static REL::RelocationID uid1(10209, 10247);
			REL::safe_write(uid1.address(), xorRax, 6);

			static REL::RelocationID uid2(10207, 10245);
			REL::safe_write(uid2.address(), xorRax, 6);

			// Zero the focus-shadow enable byte: SE 141E33EB3 / AE 141E33EB3
			static REL::RelocationID uid3(513201, 390932);
			const uint8_t zero = 0;
			REL::safe_write(uid3.address(), &zero, 1);
		}

		// ---- Color mask pass: skip it and fix out-of-bounds array access -----
		// Installed unconditionally: our scheduler changes light/slot state in a way
		// that makes the vanilla color-mask pass crash even at ShadowLightCount=4.
		{
			// Replace the call to DrawColorMask with our thunk that calls
			// ReturnShadowmaps on each light instead.
			{
				static REL::RelocationID uid(100422, 107140);
				uintptr_t addr = uid.address() + REL::Relocate(0xF20 - 0xE90, 0x67E - 0x600, 0x9e);
				if (!SKSE::stl::install_context_hook(addr, 5, Hook_DisableColorMask))
					logger::error("[SCM] Failed to install Hook_DisableColorMask");
			}
		}

		// ---- Shadow caster selection -----------------------------------------

		// Replace CalculateActiveShadowCasterLights entirely (ID 100419/107137).
		// VR confirmed: 0x1413226e0
		stl::detour_thunk<Hook_CalculateActiveShadowCasters>(REL::RelocationID(100419, 107137));

		// Replace the CALL to RenderActiveShadowCasterLights inside the render loop.
		// ID 100415/107133; VR confirmed: 0x141322130
		// Offsets: SE = 0xF76-0xE30 (0x146), AE = 0xC17D-0xBFF0 (0x18D), VR = 0x1CA
		// Must use install_context_hook (not write_thunk_call) so RtlRestoreContext restores
		// volatile registers (r8, etc.) before the game continues past the call site.
		{
			static REL::RelocationID uid(100415, 107133);
			uintptr_t addr = uid.address() + REL::Relocate(0xF76 - 0xE30, 0xC17D - 0xBFF0, 0x1CA);
			if (!SKSE::stl::install_context_hook(addr, 5, Hook_RenderShadowLights))
				logger::error("[SCM] Failed to install Hook_RenderShadowLights");
		}

		// Replace CalculateActiveNonShadowCasterLights (surface light injection).
		// ID 100997/107784; VR confirmed: 0x141354d20
		// Uses install_context_hook because the function has 10 args (11 in VR) with
		// platform-specific stack layout. We write a RET at func+5 so
		// RtlRestoreContext lands on ret and the function returns cleanly.
		{
			static REL::RelocationID uid(100997, 107784);
			if (!SKSE::stl::install_context_hook(uid.address(), 5, Hook_CalculateActiveLightsForSurface))
				logger::error("[SCM] Failed to install Hook_CalculateActiveLightsForSurface");
			const uint8_t ret = 0xC3;
			REL::safe_write(uid.address() + 5, &ret, 1);
		}

		// ---- Stealth detection fix -------------------------------------------
		// GetLightLevel (ID 38900/39946) iterates shadow lights to check which
		// affect the player. We replace that iteration with our own.
		// VR: 38900 confirmed (0x1406892e0); offsets assumed same as SE for VR.
		{
			static REL::RelocationID uid(38900, 39946);

			// Hook at the start of the affect-player loop.
			// Original bytes: "41 83 CE FF 33 C0" (6 bytes) -- keep them running first.
			uintptr_t off1 = REL::Relocate(0x185 - 0x050, 0x847 - 0x710, 0x185 - 0x050);
			if (!SKSE::stl::install_context_hook(uid.address() + off1, 6, Hook_UpdateLightLevelPlayer, 6))
				logger::error("[SCM] Failed to install Hook_UpdateLightLevelPlayer");

			// Byte patch: change JA (0x73) to JMP (0xEB) to skip the vanilla iteration.
			uintptr_t off2 = REL::Relocate(0x194 - 0x050, 0x856 - 0x710, 0x194 - 0x050);
			const uint8_t jmp = 0xEB;
			REL::safe_write(uid.address() + off2, &jmp, 1);
		}
		// Per-light check (ID 99725/106362): not yet confirmed in VR address library,
		// so guard VR until addresses are found.
		if (!REL::Module::IsVR()) {
			static REL::RelocationID uid(99725, 106362);
			uintptr_t off = REL::Relocate(0x648 - 0x560, 0xB49 - 0xA60, 0x648 - 0x560);
			if (!SKSE::stl::install_context_hook(uid.address() + off, 5, Hook_CheckLightLevelPlayer))
				logger::error("[SCM] Failed to install Hook_CheckLightLevelPlayer");
		}

		// ---- Light conversion ------------------------------------------------
		// All conversion-related hooks install unconditionally when SCM is
		// enabled. Their runtime behaviour is gated by the relevant settings
		// (s_settings.ConvertExcessToNormal, s_settings.PromoteNormalToShadow)
		// and by container membership (s_normalConvert, s_shadowConvert) --
		// so when the user has both flags false at boot the hooks fire but
		// are no-ops. Installing them unconditionally means toggling the
		// settings on at runtime takes effect immediately rather than
		// silently doing nothing (the prior boot-time gating left the
		// vtable patch and SetLight/RemoveLight hooks uninstalled if the
		// flags were false at Init time, so a later toggle-on couldn't
		// observe new conversions properly).

		{
			// BSShadowLight vtable slot 3 = IsShadowLight; replace on all 4 shadow light types.
			// Reads s_normalConvert membership -- empty when ConvertExcessToNormal
			// off, so the hook returns vanilla truth for every light.
			REL::Relocation<uintptr_t> vtbl1{ RE::BSShadowLight::VTABLE[0] };
			vtbl1.write_vfunc(3, Hook_IsShadowLight);
			REL::Relocation<uintptr_t> vtbl2{ RE::BSShadowDirectionalLight::VTABLE[0] };
			vtbl2.write_vfunc(3, Hook_IsShadowLight);
			REL::Relocation<uintptr_t> vtbl3{ RE::BSShadowFrustumLight::VTABLE[0] };
			vtbl3.write_vfunc(3, Hook_IsShadowLight);
			REL::Relocation<uintptr_t> vtbl4{ RE::BSShadowParabolicLight::VTABLE[0] };
			vtbl4.write_vfunc(3, Hook_IsShadowLight);
		}

		{
			// ShadowSceneNode::RemoveLight -- fires at +0x9 (SE: 6 bytes, AE: 5 bytes).
			// Drains s_normalConvert / s_shadowConvert entries for the removed light.
			// No-op when both containers are empty.
			static REL::RelocationID uid(99697, 106331);
			int sz = REL::Relocate(6, 5, 6);
			if (!SKSE::stl::install_context_hook(uid.address() + REL::Relocate(0x9, 0x9, 0x9), sz, Hook_ConvertLights_Remove, sz))
				logger::error("[SCM] Failed to install Hook_ConvertLights_Remove");
		}

		{
			// ShadowSceneNode::AddLight -- at function start (5 bytes).
			// Applies portal-strict per type (always) and PromoteNormalToShadow
			// flag mutation (when enabled).
			static REL::RelocationID uid(99692, 106326);
			if (!SKSE::stl::install_context_hook(uid.address(), 5, Hook_ConvertLights_Add, 5))
				logger::error("[SCM] Failed to install Hook_ConvertLights_Add");
		}

		{
			// BSLight::SetLight -- at function start (5 bytes).
			// Tracks NiLight* reassignments for s_shadowConvert. No-op when
			// PromoteNormalToShadow is off (s_shadowConvert is empty).
			static REL::RelocationID uid(101302, 108289);
			if (!SKSE::stl::install_context_hook(uid.address(), 5, Hook_ConvertLights_SetLight, 5))
				logger::error("[SCM] Failed to install Hook_ConvertLights_SetLight");
		}

		logger::info("[SCM] Hooks installed (ShadowLightCount={})", settings.ShadowLightCount);

		// Wholesale reset on LoadingMenu open so transient session state
		// (s_normalConvert, s_lights pool, debug pins) drops the previous
		// cell's pointers before the engine tears them down. Mirrors the
		// pattern in DynamicCubemaps for similar reset-on-scene-transition
		// behaviour.
		RegisterSceneTransitionEvents();

		// DXGI budget snapshot at install. Per-slice geometry follows once
		// Update() sees a non-null kSHADOWMAPS SRV.
		if (auto* menu = Menu::GetSingleton()) {
			if (auto adapter3 = menu->GetDXGIAdapter3()) {
				DXGI_QUERY_VIDEO_MEMORY_INFO vmem{};
				if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmem)) && vmem.Budget > 0) {
					const float budgetMB = static_cast<float>(vmem.Budget) / (1024.f * 1024.f);
					const float usageMB = static_cast<float>(vmem.CurrentUsage) / (1024.f * 1024.f);
					logger::info("[SCM] VRAM at install: {:.1f}/{:.1f} MB used", usageMB, budgetMB);
				}
			}
		}
	}

	void Update(const Settings& settings, RE::ShadowSceneNode* /*shadowSceneNode*/,
		RE::NiCamera* /*worldCamera*/)
	{
		ZoneScopedN("SCM::Update");
		if (s_externalConflict)
			return;

		// Lazy verification of the kSHADOWMAPS allocation. Self-healing:
		// retries until kSHADOWMAPS exists, then early-returns. Cheap.
		// This must run BEFORE the clamp below so a VRAM-exhaustion
		// fallback gets reflected in the same frame the verification
		// succeeds.
		RefreshInstalledSlotCount();

		Settings capped = settings;
		if (s_installedShadowLightCount > 0)
			capped.ShadowLightCount = std::min(settings.ShadowLightCount, s_installedShadowLightCount);

		int newTotal = LightContainerSize(capped);
		if (newTotal != s_lights.Size) {
			auto* newLights = new LightEntry[newTotal]();
			int copyCount = std::min(s_lights.Size, newTotal);
			for (int i = 0; i < copyCount; i++)
				newLights[i] = s_lights.Lights[i];
			for (int i = copyCount; i < newTotal; i++)
				newLights[i].Index = i;
			delete[] s_lights.Lights;
			s_lights.Lights = newLights;
			s_lights.Size = newTotal;
		}

		// Apply settings as a pure flag flip. Conversion-related state
		// (s_normalConvert, s_shadowConvert, s_lights pool) is NOT
		// drained on toggles -- it ages out at the next LoadingMenu
		// via the natural ResetSession() in SceneTransitionEventHandler.
		// See Hook_CalculateActiveShadowCasters::thunk for the rationale:
		// wholesale clearing mid-session caused engine accumulate-shadow
		// crashes (2026-05-17 crash logs) because the engine still had
		// our converted/promoted lights in activeShadowLights with
		// shadowmapDescriptors already cleared by Hook_DisableColorMask,
		// and tearing our tracking left the engine walking half-state.
		//
		// Each setting's gating still takes effect immediately via the
		// runtime checks in the relevant hook / scheduler branches:
		//   - Enabled: per-frame thunk routes to vanilla; OverwriteShadowMapIndex no-ops.
		//   - ConvertExcessToNormal: convertOrDisable routes excess omnis to DisableLight.
		//   - PromoteNormalToShadow: Hook_ConvertLights_Add stops promoting.
		// "Off = stop converting" is the documented semantic; existing
		// converted/promoted lights persist in their current form until
		// the engine itself drops them at cell change.
		s_settings = capped;
	}

	void ResetSession()
	{
		// Wholesale drop of pointers the engine is about to free during
		// a scene transition. Called by RegisterSceneTransitionEvents
		// when the LoadingMenu opens. The per-frame reconciliation in
		// ScheduleShadowCasters keeps these caches honest during normal
		// play; this is the explicit "scene is gone" signal so the UI
		// counter, debug pins, and tracking sets read empty during the
		// loading screen rather than displaying stale entries from the
		// previous cell.
		//
		// Stale BSRenderPass.sceneLights[] captures that would otherwise AV
		// in BSEffectShader::SetupGeometry are handled by the defensive
		// guard there (clamps numLights past the first stale entry), not by
		// trying to drive engine-side cleanup from here. An earlier version
		// tried calling ShadowSceneNode::RemoveLight to undo our
		// ConvertLight -> GameEnableLight pinning, but the engine function
		// takes NiLight* (not BSLight* as the wrapper assumed); the call
		// was a silent no-op on every runtime and accomplished nothing.
		s_normalConvert.clear();
		s_shadowConvert.clear();
		s_pinShadow.clear();
		s_pinConvert.clear();
		s_soloLight = 0;
		s_suppressedLights.clear();
		// Clear pool entries but keep the array allocation; size is set by
		// Install/Update based on the configured ShadowLightCount.
		if (s_lights.Lights) {
			for (int i = 0; i < s_lights.Size; ++i)
				s_lights.Lights[i].Clear();
			s_lights.Sun = false;
		}
	}

	class SceneTransitionEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && a_event->opening)
				ResetSession();
			return RE::BSEventNotifyControl::kContinue;
		}
		static SceneTransitionEventHandler* GetSingleton()
		{
			static SceneTransitionEventHandler singleton;
			return &singleton;
		}
	};

	void RegisterSceneTransitionEvents()
	{
		auto* ui = globals::game::ui;
		if (!ui) {
			logger::error("[SCM] No UI singleton; cannot register LoadingMenu handler");
			return;
		}
		ui->AddEventSink(SceneTransitionEventHandler::GetSingleton());
		logger::info("[SCM] LoadingMenu event handler registered");
	}

	const LightContainer& GetLights()
	{
		return s_lights;
	}

	int32_t GetShadowSlot(RE::BSShadowLight* light)
	{
		// Returns the kSHADOWMAPS texture-array slot for `light`, or -1 if the
		// light has no kSHADOWMAPS slice. Pool index == texture slot for point
		// lights (1:1). Sun's pool slot returns -1 since the sun renders to
		// kSHADOWMAPS_ESRAM (a separate texture) — callers in ShadowRenderer
		// upload and LightLimitFix cluster builder must skip it.
		const int32_t poolIdx = s_lights.FindLight(light, s_settings.ShadowLightCount);
		if (poolIdx < 0)
			return -1;
		if (s_lights.Sun && poolIdx == 0)
			return -1;  // sun
		return poolIdx;
	}

	void ForEachConvertedLight(const std::function<void(RE::BSShadowLight*)>& visitor)
	{
		for (auto& c : s_normalConvert) {
			if (!c.light)
				continue;
			// Defensive vtable check: catches lights freed and zeroed by
			// tbbmalloc / EngineFixes between our per-frame reconciliation
			// in ScheduleShadowCasters and the cluster builder running. The
			// reconciliation prunes stale pointers up-front, but a bulk
			// engine teardown could still happen mid-frame.
			if (*reinterpret_cast<const uintptr_t*>(c.light) == 0)
				continue;
			visitor(c.light);
		}
	}

	// =========================================================================
	// Per-slot visualization state (owned by ShadowCasterManager)
	// =========================================================================

	static constexpr const char* kShadowTypeNames[] = { "Spot", "Hemisphere", "Omni" };

	static std::vector<ShadowSlotInfo> s_shadowSlotInfos;
	static uint32_t s_shadowSlotUsage = 0;
	// Persists last-seen ShadowSlotInfo for every light ever recorded this session,
	// so suppressed lights that leave the active slots still have metadata for the settings table.
	static std::unordered_map<uintptr_t, ShadowSlotInfo> s_knownLights;

	/// Computes the golden-ratio hue color for a shadow-map slot (matches mode-8 shader).
	ImVec4 ShadowSlotHueColor(uint32_t slotIdx)
	{
		auto chan = [](float h, float shift) {
			float v = fmodf(h + shift, 1.0f);
			if (v < 0.0f)
				v += 1.0f;
			return std::clamp(fabsf(v * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
		};
		float hue = fmodf(float(slotIdx) * 0.618033988f, 1.0f);
		return ImVec4(chan(hue, 0.0f), chan(hue, 2.0f / 3.0f), chan(hue, 1.0f / 3.0f), 1.0f);
	}

	// =========================================================================
	// Slot frame API implementations
	// =========================================================================

	void BeginSlotFrame(uint32_t slotCount)
	{
		s_shadowSlotInfos.assign(slotCount, ShadowSlotInfo{});
		s_shadowSlotUsage = 0;
	}

	void RecordSlot(uint32_t depthSlot, const ShadowSlotInfo& info)
	{
		if (depthSlot < static_cast<uint32_t>(s_shadowSlotInfos.size()))
			s_shadowSlotInfos[depthSlot] = info;
		// Omni lights (type 2) occupy 2 depth-texture slices; all others use 1.
		s_shadowSlotUsage += (info.type == 2) ? 2 : 1;
		s_knownLights[info.lightKey] = info;
	}

	bool IsSuppressed(uintptr_t lightKey)
	{
		if (s_suppressedLights.count(lightKey))
			return true;
		// Solo: every key except the soloed one is implicitly suppressed.
		if (s_soloLight != 0 && s_soloLight != lightKey)
			return true;
		return false;
	}

	bool IsPinnedShadow(uintptr_t lightKey) { return s_pinShadow.count(lightKey) > 0; }
	bool IsPinnedConvert(uintptr_t lightKey) { return s_pinConvert.count(lightKey) > 0; }

	void SetPinnedShadow(uintptr_t lightKey, bool pinned)
	{
		if (pinned) {
			s_pinShadow.insert(lightKey);
			s_pinConvert.erase(lightKey);  // mutually exclusive
			s_suppressedLights.erase(lightKey);
		} else {
			s_pinShadow.erase(lightKey);
		}
	}

	void SetPinnedConvert(uintptr_t lightKey, bool pinned)
	{
		if (pinned) {
			s_pinConvert.insert(lightKey);
			s_pinShadow.erase(lightKey);
			s_suppressedLights.erase(lightKey);
		} else {
			s_pinConvert.erase(lightKey);
		}
	}

	uintptr_t GetSoloLight() { return s_soloLight; }
	void SetSoloLight(uintptr_t lightKey) { s_soloLight = lightKey; }

	uintptr_t GetHoveredLight() { return s_hoverLightKey; }
	void SetHoveredLight(uintptr_t lightKey) { s_hoverLightKey = lightKey; }

	void ClearAllOverrides()
	{
		s_suppressedLights.clear();
		s_pinShadow.clear();
		s_pinConvert.clear();
		s_soloLight = 0;
		// Hover key is transient (per-draw); not part of "overrides".
	}

	bool HasAnyOverrides()
	{
		return !s_suppressedLights.empty() || !s_pinShadow.empty() ||
		       !s_pinConvert.empty() || s_soloLight != 0;
	}

	bool HasSuppressedLights()
	{
		return !s_suppressedLights.empty();
	}

	uint32_t GetSlotUsage()
	{
		return s_shadowSlotUsage;
	}

	uint32_t GetHighImportanceCount()
	{
		return s_highImportanceLightCount;
	}

	const std::vector<ShadowSlotInfo>& GetSlotInfos()
	{
		return s_shadowSlotInfos;
	}

	const char* GetShadowTypeName(uint32_t type)
	{
		return kShadowTypeNames[std::min(type, 2u)];
	}

	// =========================================================================
	// DrawShadowLightTable
	// =========================================================================
	// Interactive shadow caster table: suppress/re-enable per light or by type,
	// filter by type name/range/address, sort by any column.
	// Rows are keyed by lightKey (light object pointer) so suppression persists
	// across slot reassignments as the player moves around.
	//
	// compact=true  -> auto-sizes height (up to 15 rows visible)
	// compact=false -> fills available window height (resizable overlay window)
	// showColor     -> adds a golden-ratio hue swatch column (visualization mode 8)
	// =========================================================================

	void DrawShadowLightTable(bool compact, bool showColor, bool sceneOnly, bool readOnly)
	{
		// Hover key is set per-row inside this function and consumed by the
		// cluster light builder (LightLimitFix::UpdateLights addLight) for the
		// debug pulse. The consume call at the end of UpdateLights clears it
		// each frame, so when this function runs again next frame the key
		// starts at 0 and only gets re-set if a row is currently Shift-hovered.
		// We deliberately do NOT reset at the top here -- when both the
		// settings-menu table and the overlay table render in the same frame
		// (overlay-window open while settings-menu is also open), a reset here
		// would let the second-drawn table clobber the hover set by the first.
		// Letting both calls coexist means hovering in either one fires the
		// pulse (only one row can be hovered at a time, so the writes never
		// fight).

		struct SlotRow
		{
			uint32_t idx;    // shadow slot index; only meaningful when inScene=true
			bool inScene;    // currently occupies a shadow slot this frame
			bool converted;  // demoted to non-shadow rendering via ConvertExcessToNormal
			ShadowSlotInfo info;
			float importance{ 0.0f };  // contribution-weighted importance (luminance × fade × attenuation²)
			bool highImp{ false };     // importance > 0.1 — light meaningfully illuminates the viewer area
		};

		// Build index of lights currently in scene (slot -> info).
		// Static containers avoid per-frame heap allocation.
		static std::unordered_map<uintptr_t, uint32_t> sceneSlot;
		sceneSlot.clear();
		for (uint32_t i = 0; i < static_cast<uint32_t>(s_shadowSlotInfos.size()); ++i)
			if (s_shadowSlotInfos[i].valid)
				sceneSlot[s_shadowSlotInfos[i].lightKey] = i;

		// Build lightKey -> LightEntry* lookup for debug columns.
		static std::unordered_map<uintptr_t, const LightEntry*> lightEntryByKey;
		lightEntryByKey.clear();
		for (int li = 0; li < s_lights.Size; ++li) {
			const auto& e = s_lights.Lights[li];
			if (e.Light)
				lightEntryByKey[reinterpret_cast<uintptr_t>(e.Light)] = &e;
		}

		auto applyEntryDebug = [&](SlotRow& row) {
			auto it = lightEntryByKey.find(row.info.lightKey);
			if (it != lightEntryByKey.end()) {
				row.importance = it->second->lastImportance;
				row.highImp = row.importance > 0.1f;
			}
		};

		// Build set of converted-light keys (shadow lights demoted to non-shadow
		// rendering via ConvertExcessToNormal). These don't occupy a shadow slot
		// this frame but are still active in the scene as normal lights — we want
		// them visible in the table with a "Conv" indicator and the same suppress
		// toggle so users can hide them like any other shadow caster.
		static std::unordered_set<uintptr_t> convertedKeys;
		convertedKeys.clear();
		ForEachConvertedLight([&](RE::BSShadowLight* light) {
			convertedKeys.insert(reinterpret_cast<uintptr_t>(light));
		});

		// Build row list.
		static std::vector<SlotRow> rows;
		rows.clear();
		auto addConvertedRows = [&]() {
			for (uintptr_t key : convertedKeys) {
				if (sceneSlot.count(key))
					continue;  // simultaneously a shadow caster this frame
				SlotRow r{ 0, false, true, {} };
				auto it = s_knownLights.find(key);
				if (it != s_knownLights.end()) {
					r.info = it->second;
					r.info.valid = false;  // no shadow slot this frame
				} else {
					// First-frame convert: no cached metadata yet. Surface a minimal
					// row so the user can still toggle suppression by address.
					r.info.lightKey = key;
				}
				applyEntryDebug(r);
				rows.push_back(r);
			}
		};
		if (sceneOnly) {
			rows.reserve(sceneSlot.size() + convertedKeys.size());
			for (auto& [key, idx] : sceneSlot) {
				SlotRow r{ idx, true, false, s_shadowSlotInfos[idx] };
				applyEntryDebug(r);
				rows.push_back(r);
			}
			addConvertedRows();
		} else {
			// All scene lights first, then converted lights, then suppressed lights
			// not currently in scene at all.
			rows.reserve(sceneSlot.size() + convertedKeys.size() + s_suppressedLights.size());
			for (auto& [key, idx] : sceneSlot) {
				SlotRow r{ idx, true, false, s_shadowSlotInfos[idx] };
				applyEntryDebug(r);
				rows.push_back(r);
			}
			addConvertedRows();
			for (uintptr_t key : s_suppressedLights) {
				if (sceneSlot.count(key) || convertedKeys.count(key))
					continue;
				auto it = s_knownLights.find(key);
				if (it != s_knownLights.end()) {
					SlotRow r{ 0, false, false, it->second };
					applyEntryDebug(r);
					rows.push_back(r);
				}
			}
		}

		if (rows.empty()) {
			ImGui::TextDisabled("No shadow slots this frame.");
			return;
		}

		// -- Header: active count + suppression badge ----------------------
		ImGui::Text("Shadow slots: %u active", s_shadowSlotUsage);
		if (!s_suppressedLights.empty()) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "  %zu suppressed", s_suppressedLights.size());
		}

		// -- Group toggle buttons ------------------------------------------
		// green = at least one unsuppressed; grey = all suppressed; click flips.
		// Predicate-based so we can mix type filters (Spot/Hemi/Omni) with state
		// filters (Conv = converted-to-normal lights).
		{
			using RowPred = std::function<bool(const SlotRow&)>;
			auto allSuppressedMatching = [&](const RowPred& pred) {
				bool sawAny = false;
				for (auto& r : rows) {
					if (!pred(r))
						continue;
					sawAny = true;
					if (!s_suppressedLights.count(r.info.lightKey))
						return false;
				}
				// If nothing matches, treat as "all suppressed" so the button shows
				// grey/disabled (clicking a no-op button does nothing).
				return sawAny;
			};
			auto toggleMatching = [&](const RowPred& pred) {
				if (allSuppressedMatching(pred)) {
					for (auto& r : rows)
						if (pred(r))
							s_suppressedLights.erase(r.info.lightKey);
				} else {
					for (auto& r : rows)
						if (pred(r))
							s_suppressedLights.insert(r.info.lightKey);
				}
			};
			auto groupButton = [&](const char* label, const RowPred& pred, const char* tooltip) {
				bool allOff = allSuppressedMatching(pred);
				ImGui::PushStyleColor(ImGuiCol_Button,
					allOff ? ImVec4(0.35f, 0.35f, 0.35f, 1) : ImVec4(0.15f, 0.5f, 0.15f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
					allOff ? ImVec4(0.5f, 0.5f, 0.5f, 1) : ImVec4(0.2f, 0.7f, 0.2f, 1));
				if (ImGui::SmallButton(label))
					toggleMatching(pred);
				ImGui::PopStyleColor(2);
				if (tooltip && ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", tooltip);
			};
			auto typePred = [](uint32_t type) {
				return [type](const SlotRow& r) { return r.info.type == type; };
			};
			groupButton(
				"All", [](const SlotRow&) { return true; }, nullptr);
			ImGui::SameLine();
			groupButton("Spot", typePred(0), "Toggle all spot/frustum shadow lights");
			ImGui::SameLine();
			groupButton("Hemi", typePred(1), "Toggle all hemisphere shadow lights");
			ImGui::SameLine();
			groupButton("Omni", typePred(2), "Toggle all omni (paraboloid) shadow lights");
			ImGui::SameLine();
			groupButton(
				"Conv", [](const SlotRow& r) { return r.converted; },
				"Toggle all lights currently demoted from shadow to normal\n"
				"(ConvertExcessToNormal). Hides their cluster-light contribution.");

			// "Clear All": resets every debug override (suppress / pin shadow /
			// pin convert / solo) so the table returns to scheduler-auto. Only
			// shown when overrides are active so it doesn't take up space when
			// there's nothing to reset.
			if (HasAnyOverrides()) {
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.25f, 0.25f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.35f, 0.35f, 1));
				if (ImGui::SmallButton("Clear All"))
					ClearAllOverrides();
				ImGui::PopStyleColor(2);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(
						"Reset every debug override:\n"
						"  - clear suppression\n"
						"  - clear shadow / convert pins\n"
						"  - clear solo\n"
						"Returns the table to scheduler-auto behaviour.");
			}

			// Help marker: explains the per-row debug controls so users aren't
			// surprised by states / pulses they didn't know they could trigger.
			ImGui::SameLine();
			Util::HelpMarker(
				"Per-row controls:\n"
				"  *  Cycle button (col 1): click to rotate this light through\n"
				"     Auto -> Shadow pin (S) -> Convert pin (C) -> Suppress (X) -> Auto.\n"
				"  *  Solo button (col 2): isolate this light against a black scene.\n"
				"     Click again to clear; only one light may be soloed at a time.\n"
				"  *  Hold Shift while hovering a row to highlight that light in the\n"
				"     world with a pulsing magenta tint. Release Shift or move the\n"
				"     cursor away to stop. Useful when you can't tell which entry\n"
				"     corresponds to which physical light. Does not affect rendering\n"
				"     when Shift is not held.\n\n"
				"Group buttons toggle suppression for every matching row at once.\n"
				"Clear All appears when any override is active and resets everything.");
		}

		// -- Filter input --------------------------------------------------
		static std::string s_filterText;
		{
			char buf[128] = {};
			strncpy_s(buf, s_filterText.c_str(), sizeof(buf) - 1);
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputText("##slotfilter", buf, sizeof(buf)))
				s_filterText = buf;
			ImGui::SameLine();
			ImGui::TextDisabled(sceneOnly ? "filter (yes/conv/type/range/addr)" : "filter (yes/conv/no/type/range/addr)");
		}

		// Apply filter.
		static std::vector<SlotRow> filteredRows;
		filteredRows.clear();
		if (s_filterText.empty()) {
			filteredRows = rows;
		} else {
			std::string lower = s_filterText;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
			char addrBuf[16];
			for (auto& r : rows) {
				std::string typeName = kShadowTypeNames[std::min(r.info.type, 2u)];
				std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);
				// Range filter matches both raw units and rounded meters.
				char rangeBuf[32];
				snprintf(rangeBuf, sizeof(rangeBuf), "%.0f %.0f",
					r.info.range, Util::Units::GameUnitsToMeters(r.info.range));
				snprintf(addrBuf, sizeof(addrBuf), "%08x", static_cast<uint32_t>(r.info.lightKey & 0xFFFFFFFF));
				const char* statusStr = r.inScene ? "yes" : (r.converted ? "conv" : "no");
				if (typeName.find(lower) != std::string::npos ||
					std::string(rangeBuf).find(lower) != std::string::npos ||
					std::string(addrBuf).find(lower) != std::string::npos ||
					lower == statusStr)
					filteredRows.push_back(r);
			}
		}

		// -- Column layout -------------------------------------------------
		// Interactive (settings menu, or overlay with menu open):
		//     [Mode] [Solo] [Status] [Address] [Color?] [Type] [Range] [Imp]
		// Read-only (overlay with menu closed -- buttons would be dead pixels):
		//                   [Status] [Address] [Color?] [Type] [Range] [Imp]
		//
		// Status merges the old "In Scene" + "Slot" columns into one cell
		// showing one of: "Slot N" / "Conv" / "Out" / "Suppr". The old "Hi"
		// boolean column is gone -- highImp now tints the row instead, which
		// is what the column was being used for visually.
		const bool showButtons = !readOnly;
		const int modeColIdx = showButtons ? 0 : -1;
		const int soloColIdx = showButtons ? 1 : -1;
		const int statusColIdx = showButtons ? 2 : 0;
		const int addrColIdx = statusColIdx + 1;
		const int typeColIdx = addrColIdx + (showColor ? 2 : 1);
		const int radColIdx = typeColIdx + 1;
		const int centrColIdx = radColIdx + 1;

		std::vector<std::string> headers;
		if (showButtons) {
			headers.push_back("Mode");  // cycle: Auto / Pin-S / Pin-C / Suppress
			headers.push_back("Solo");
		}
		headers.push_back("Status");
		headers.push_back("Address");
		if (showColor)
			headers.push_back("Color");
		headers.push_back("Type");
		headers.push_back("Range");
		headers.push_back("Imp");

		using SortFn = std::function<bool(const SlotRow&, const SlotRow&, bool)>;
		std::vector<SortFn> sorts(headers.size(), nullptr);
		// Status sort: in-scene shadow casters → converted → out-of-scene.
		// Suppressed lights sort to the end (treated as worst rank).
		sorts[statusColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			auto rank = [](const SlotRow& r) -> int {
				bool sup = s_suppressedLights.count(r.info.lightKey) > 0;
				if (sup)
					return 3;
				return r.inScene ? 0 : (r.converted ? 1 : 2);
			};
			int ra = rank(a), rb = rank(b);
			if (ra != rb)
				return asc ? ra < rb : ra > rb;
			return asc ? a.idx < b.idx : a.idx > b.idx;
		};
		sorts[addrColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			return asc ? a.info.lightKey < b.info.lightKey : a.info.lightKey > b.info.lightKey;
		};
		sorts[typeColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			return asc ? a.info.type < b.info.type : a.info.type > b.info.type;
		};
		sorts[radColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			return asc ? a.info.range < b.info.range : a.info.range > b.info.range;
		};
		sorts[centrColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			return asc ? a.importance < b.importance : a.importance > b.importance;
		};

		// outerSize logic:
		//   * compact      auto-size up to 15 rows (handled by
		//                  ShowSortedStringTableCustom when y==0). Used in
		//                  the menu's Active Casters block where the table
		//                  is one of several elements in a long settings
		//                  list and shouldn't grab unbounded vertical space.
		//   * non-compact  fill remaining vertical space. The table itself
		//                  scrolls internally (ScrollY flag in the shared
		//                  helper) so summary stats above stay visible
		//                  regardless of how many lights exist or how the
		//                  user has sized the host window.
		ImVec2 outerSize = compact ? ImVec2(0, 0) : ImVec2(0, ImGui::GetContentRegionAvail().y);

		Util::ShowSortedStringTableCustom<SlotRow>(
			"##ShadowLightTbl",
			headers,
			filteredRows,
			static_cast<size_t>(statusColIdx),  // default sort: Status
			true,                               // ascending
			sorts,
			[&](int /*rowIdx*/, int col, const SlotRow& row) {
				const uintptr_t key = row.info.lightKey;
				const bool suppressed = s_suppressedLights.count(key) > 0;
				const bool pinShadow = s_pinShadow.count(key) > 0;
				const bool pinConvert = s_pinConvert.count(key) > 0;
				const bool isSolo = (s_soloLight == key && key != 0);

				// Helper: shift-gated debug pulse. Setting s_hoverLightKey makes
				// the cluster light builder replace this light's colour with a
				// 1Hz magenta pulse — useful for finding which light a row
				// corresponds to in 3D, but visually startling if it triggered
				// every time the cursor crossed a cell. Requiring Shift+hover
				// means a user clicking through the cycle/solo buttons doesn't
				// see lights randomly turn purple, while debugging is one
				// modifier away.
				auto noteHover = [&]() {
					if (ImGui::IsItemHovered() && ImGui::GetIO().KeyShift)
						s_hoverLightKey = key;
				};

				// Row tint: highImp lights get a subtle yellow background so
				// the eye can pick out the lights actually contributing to the
				// frame at a glance. Replaces the dropped "Hi" column. Set on
				// col 0 so it applies to the whole row.
				if (col == 0 && row.highImp) {
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
						ImGui::GetColorU32(ImVec4(0.30f, 0.30f, 0.10f, 0.35f)));
				}

				// === Mode column: state cycle button =======================
				// Cycle: Auto (·) -> PinShadow (S) -> PinConvert (C) -> Suppress (X) -> Auto
				// Mutually exclusive (SetPinned* / suppressed.erase enforce that).
				// Hidden in readOnly mode (overlay with menu closed).
				if (showButtons && col == modeColIdx) {
					ImGui::PushID(static_cast<int>(key & 0xFFFFFFFF));
					const char* label = "·";
					ImVec4 col4 = ImVec4(0.15f, 0.6f, 0.15f, 1);  // green = auto/active
					ImVec4 colH = ImVec4(0.2f, 0.75f, 0.2f, 1);
					const char* tip = "Auto (scheduler decides)\nClick: pin as shadow caster";
					if (pinShadow) {
						label = "S";
						col4 = ImVec4(0.20f, 0.40f, 0.85f, 1);  // blue
						colH = ImVec4(0.30f, 0.55f, 1.0f, 1);
						tip = "Pinned: forced shadow caster\nClick: pin as converted (non-shadow)";
					} else if (pinConvert) {
						label = "C";
						col4 = ImVec4(0.85f, 0.55f, 0.15f, 1);  // amber
						colH = ImVec4(1.0f, 0.7f, 0.25f, 1);
						tip = "Pinned: forced converted (non-shadow)\nClick: suppress entirely";
					} else if (suppressed) {
						label = "X";
						col4 = ImVec4(0.45f, 0.25f, 0.25f, 1);  // dim red
						colH = ImVec4(0.6f, 0.35f, 0.35f, 1);
						tip = "Suppressed (hidden)\nClick: return to auto";
					}
					ImGui::PushStyleColor(ImGuiCol_Button, col4);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colH);
					if (ImGui::SmallButton(label)) {
						// Cycle to next state.
						if (pinShadow) {
							SetPinnedShadow(key, false);
							SetPinnedConvert(key, true);
						} else if (pinConvert) {
							SetPinnedConvert(key, false);
							s_suppressedLights.insert(key);
						} else if (suppressed) {
							s_suppressedLights.erase(key);
						} else {
							SetPinnedShadow(key, true);
						}
					}
					ImGui::PopStyleColor(2);
					noteHover();
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", tip);
					ImGui::PopID();
					return;
				}

				// === Solo column ==========================================
				// Hidden in readOnly mode.
				if (showButtons && col == soloColIdx) {
					ImGui::PushID(static_cast<int>((key & 0xFFFFFFFF) ^ 0xA1));
					ImVec4 col4 = isSolo ?
				                      ImVec4(0.85f, 0.7f, 0.15f, 1) :  // bright yellow when active
				                      ImVec4(0.30f, 0.30f, 0.30f, 1);
					ImVec4 colH = isSolo ? ImVec4(1.0f, 0.85f, 0.25f, 1) : ImVec4(0.45f, 0.45f, 0.45f, 1);
					ImGui::PushStyleColor(ImGuiCol_Button, col4);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colH);
					if (ImGui::SmallButton(isSolo ? "!" : "·"))
						SetSoloLight(isSolo ? 0 : key);
					ImGui::PopStyleColor(2);
					noteHover();
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s",
							isSolo ?
								"Solo: this light is shown alone\nClick: clear solo" :
								"Solo this light\n(suppresses every other light\nuntil cleared)");
					ImGui::PopID();
					return;
				}

				if (suppressed || (s_soloLight != 0 && !isSolo))
					ImGui::BeginDisabled();
				bool dimmed = suppressed || (s_soloLight != 0 && !isSolo);
				if (col == statusColIdx) {
					// Merged "In Scene" + "Slot" column. Four mutually-exclusive
					// states; suppressed wins because the user explicitly hid it.
					if (suppressed) {
						ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1), "Suppr");
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Suppressed by debug override.\nClick the Mode button to clear.");
					} else if (row.inScene) {
						ImGui::Text("Slot %u", row.idx);
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Casting shadows this frame in slot %u.", row.idx);
					} else if (row.converted) {
						ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1), "Conv");
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(
								"Demoted to a normal (non-shadow) light this frame.\n"
								"Cluster lighting still illuminates it; no shadow-map cost.");
					} else {
						ImGui::TextDisabled("Out");
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Out of range / not active in the current frame.");
					}
				} else if (col == addrColIdx) {
					char addrFull[20];
					snprintf(addrFull, sizeof(addrFull), "0x%016llX", static_cast<unsigned long long>(row.info.lightKey));
					ImGui::Selectable(addrFull + 10, false, ImGuiSelectableFlags_None);
					if (ImGui::IsItemClicked())
						ImGui::SetClipboardText(addrFull);
					noteHover();
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Click to copy: %s", addrFull);
				} else if (showColor && col == addrColIdx + 1) {
					ImVec4 c = ShadowSlotHueColor(row.idx);
					auto ri = static_cast<uint8_t>(c.x * 255.0f);
					auto gi = static_cast<uint8_t>(c.y * 255.0f);
					auto bi = static_cast<uint8_t>(c.z * 255.0f);
					ImGui::ColorButton("##col", c,
						ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(22, 16));
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("#%02X%02X%02X", ri, gi, bi);
				} else if (col == typeColIdx) {
					ImGui::TextUnformatted(kShadowTypeNames[std::min(row.info.type, 2u)]);
					noteHover();
				} else if (col == radColIdx) {
					ImGui::Text("%.0f u", row.info.range);
					noteHover();
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", Util::Units::FormatDistance(row.info.range).c_str());
				} else if (col == centrColIdx) {
					// Importance score: luminance × fade × attenuation² at viewer.
					// White (0) → bright green (1+) as contribution increases.
					float imp = row.importance;
					float t = std::min(imp, 1.0f);
					ImVec4 colour = ImVec4(1.0f - t * 0.7f, 1.0f, 1.0f - t * 0.7f, 1.0f);  // white → green
					ImGui::TextColored(colour, "%.2f", imp);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(
							"Contribution importance score:\n"
							"  luminance(diffuse * fade)\n"
							"  * max(att_camera, att_player)\n"
							"  where att = (1 - (dist/radius)^2)^2\n\n"
							"Higher = light strongly illuminates the viewer area.\n"
							"Drives interval multiplier (configurable in Advanced settings).\n"
							"Default: 0 => x2.0, 0.5 => x0.32, 1 => x0.05\n\n"
							"Rows tinted yellow are high-importance (>0.1)\n"
							"-- they deliver meaningful illumination near the camera\n"
							"or player and receive accelerated shadow redraw scheduling.");
				}
				// Hi column dropped -- highImp now tints the row background
				// (see TableSetBgColor at the top of this lambda) so the visual
				// signal is preserved without consuming a column.
				if (dimmed)
					ImGui::EndDisabled();
			},
			{},
			outerSize);
	}

	void DrawShadowSummary(uint32_t clusterCount, uint32_t clusterMax, uint32_t shadowUnshadowedLightCount)
	{
		// Canonical "where are we vs the limits" panel. Used by both the menu's
		// Active Casters block and the overlay header so testers see the same
		// numbers in the same format regardless of which view they're in.
		const uint32_t slotUsage = s_shadowSlotUsage;
		const uint32_t slots = GetInstalledSlotCount();
		// "Wanted" = total shadow-eligible demand this frame (active + dropped).
		// We don't track demand separately, but slotUsage + dropped is the
		// observable proxy that matches the user-visible "X dropped" signal.
		const uint32_t requested = slotUsage + shadowUnshadowedLightCount;

		if (clusterCount >= clusterMax)
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Cluster lights : %u / %u (overflow)", clusterCount, clusterMax);
		else
			ImGui::Text("Cluster lights : %u / %u", clusterCount, clusterMax);

		// "lights" rather than "slots" matches the Shadow Light Count
		// setting name -- users think in lights, the engine thinks in
		// texture slots, so we use the user's word.
		if (shadowUnshadowedLightCount > 0)
			ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
				"Shadow lights  : %u / %u  (%u wanted, %u dropped, %zu converted)",
				slotUsage, slots, requested, shadowUnshadowedLightCount, s_normalConvert.size());
		else
			ImGui::Text("Shadow lights  : %u / %u  (%u wanted, 0 dropped, %zu converted)",
				slotUsage, slots, requested, s_normalConvert.size());

		if (s_highImportanceLightCount > 0 && ImGui::IsItemHovered())
			ImGui::SetTooltip("%u high-importance (near camera/player).",
				s_highImportanceLightCount);
	}

	void DrawShadowSchedulerStats()
	{
		// Avg redraws/frame: rolling average of how many shadow casters per frame
		// the scheduler decided to (re)render. Bounded by MaxRedrawPerFrame.
		float avgRedraws = static_cast<float>(s_redrawSum) / static_cast<float>(kRedrawHistorySize);
		ImGui::Text("Avg redraws/frame : %.1f  (cap: %d)", avgRedraws, s_settings.MaxRedrawPerFrame);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Rolling average over the last %d frames.", kRedrawHistorySize);

		// Avg per-light cost: budget tracker's measured GPU cost per shadow caster.
		// Used by the formula budget mode to decide how many casters fit in the
		// per-frame time budget.
		int32_t avgCost = s_budget.GetAverageCostUs();
		if (avgCost > 0)
			ImGui::Text("Avg light cost    : %.2f ms", avgCost / 1000.0f);

		// ---- Budget verdict ---------------------------------------------
		// Cross-checks measured shadow cost against the user-chosen budget
		// to surface "is your setup actually working?" without making the
		// user math it out themselves. We compare measured shadow time to
		// the user's chosen shadow budget -- not to total frame time -- so
		// this is "are we honouring your settings?" not "are your settings
		// right for your hardware?". The latter genuinely needs data we
		// don't own (frame target, GPU headroom, async overlap).
		const float budgetMs = s_autoBudgetMs;  // active budget (Manual = slider, Formula = computed)
		const float costMs = avgCost / 1000.0f;
		const float usedMs = avgRedraws * costMs;
		const int32_t cap = s_settings.MaxRedrawPerFrame;
		const bool capLimited = avgCost > 0 && avgRedraws >= static_cast<float>(cap) * 0.95f;
		const bool slotLimited = (s_shadowSlotUsage + 0u) >= GetInstalledSlotCount();
		const bool overBudget = avgCost > 0 && budgetMs > 0.0f && usedMs > budgetMs * 1.0f;
		const bool headroom = avgCost > 0 && budgetMs > 0.0f && usedMs < budgetMs * 0.5f && !capLimited;

		if (avgCost <= 0 || budgetMs <= 0.0f) {
			ImGui::TextDisabled("Budget usage      : (warming up)");
			return;
		}

		// Verdicts named after the user-visible settings, not internal
		// engineering terms. Tooltips kept to one short line each so the
		// hover doesn't grow into a wall of text.
		ImVec4 col;
		const char* verdict;
		const char* tip;
		if (overBudget) {
			col = ImVec4(0.95f, 0.35f, 0.35f, 1);
			verdict = "OVER BUDGET";
			tip = "Shadow time exceeds Redraw Budget. Lower Max Redraws or raise Redraw Budget.";
		} else if (capLimited && slotLimited) {
			col = ImVec4(0.95f, 0.65f, 0.25f, 1);
			verdict = "AT LIMITS";
			tip = "Both Max Redraws and Shadow Light Count are full. Enable Convert to Normal or raise Shadow Light Count.";
		} else if (slotLimited) {
			col = ImVec4(0.95f, 0.65f, 0.25f, 1);
			verdict = "LIGHT LIMITED";
			tip = "Shadow Light Count is full. Enable Convert to Normal or raise Shadow Light Count.";
		} else if (capLimited) {
			col = ImVec4(0.95f, 0.85f, 0.25f, 1);
			verdict = "REDRAW LIMITED";
			tip = "Hitting Max Redraws Per Frame. Raise it to spend the unused Redraw Budget.";
		} else if (headroom) {
			col = ImVec4(0.55f, 0.85f, 0.55f, 1);
			verdict = "HEADROOM";
			tip = "Under half the Redraw Budget is being used. Raise Max Redraws or accept the slack.";
		} else {
			col = ImVec4(0.55f, 0.85f, 0.55f, 1);
			verdict = "OK";
			tip = "Within Redraw Budget; no limits hit.";
		}
		// Budget gauge: progress bar tinted by the verdict colour so the
		// state is readable at a glance, with the numeric reading and
		// verdict label inside the bar. One widget replaces the old
		// separate progress bar (in SCM settings) + verdict text line.
		const float fraction = std::min(usedMs / budgetMs, 1.0f);
		char overlay[80];
		snprintf(overlay, sizeof(overlay), "%.2f / %.2f ms  -  %s", usedMs, budgetMs, verdict);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
		ImGui::Text("Budget usage      :");
		ImGui::SameLine();
		ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tip);

		// ---- Shadow VRAM progress bar ----
		// Bar fills `currentUsage / budget` (process headroom); overlay text
		// shows the kSHADOWMAPS array's share of that. Same DXGI data source
		// as PerformanceOverlay.
		auto vinfo = GetVRAMInfo();
		if (vinfo.valid && vinfo.budgetBytes > 0) {
			const std::uint64_t freeBytes = vinfo.budgetBytes > vinfo.currentUsageBytes ? vinfo.budgetBytes - vinfo.currentUsageBytes : 0;
			const float arrayMB = static_cast<float>(vinfo.shadowArrayBytes) / (1024.f * 1024.f);
			const float freeMB = static_cast<float>(freeBytes) / (1024.f * 1024.f);
			const float usageMB = static_cast<float>(vinfo.currentUsageBytes) / (1024.f * 1024.f);
			const float budgetMBf = static_cast<float>(vinfo.budgetBytes) / (1024.f * 1024.f);
			const float perSliceMB = static_cast<float>(vinfo.bytesPerSlice) / (1024.f * 1024.f);
			// Disambiguated from the budget-verdict string above.
			const VRAMVerdict vramVerdict = EvaluateVRAMVerdict(vinfo.shadowArrayBytes, freeBytes, vinfo.budgetBytes);
			const float fillFraction = std::min(1.0f,
				static_cast<float>(vinfo.currentUsageBytes) / static_cast<float>(vinfo.budgetBytes));
			char overlayText[96];
			snprintf(overlayText, sizeof(overlayText),
				"%.0f / %.0f MB  -  shadows %.0f MB (%u slices)",
				usageMB, budgetMBf, arrayMB, vinfo.shadowSlices);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, vramVerdict.colour);
			ImGui::Text("Shadow VRAM       :");
			ImGui::SameLine();
			ImGui::ProgressBar(fillFraction, ImVec2(-1.0f, 0.0f), overlayText);
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(
					"Bar fill = process VRAM usage / DXGI budget (same data the\n"
					"performance overlay reports). Overlay text shows the shadow\n"
					"array's contribution to that usage.\n"
					"\n"
					"Slices  : %u  (sun lives in its own kSHADOWMAPS_ESRAM texture)\n"
					"Per slice : %.2f MB  (%u x %u @ %u B/pixel)\n"
					"Shadow array : %.1f MB\n"
					"Free in budget : %.1f MB\n"
					"\n"
					"Green when free VRAM and shadow share are comfortable.\n"
					"Yellow when free < 512 MB or shadow array > 25%% of budget.\n"
					"Red when free < 128 MB or shadow array > 50%% of budget --\n"
					"lower Shadow Light Count or iShadowMapResolution.",
					vinfo.shadowSlices, perSliceMB,
					vinfo.shadowWidth, vinfo.shadowHeight,
					vinfo.shadowWidth && vinfo.shadowHeight ? vinfo.bytesPerSlice / (vinfo.shadowWidth * vinfo.shadowHeight) : 0u,
					arrayMB, freeMB);
			}
		}
	}

	void DrawOverlayShadowModeInfo(uint32_t mode, uint32_t /*shadowUnshadowedLightCount*/, uint32_t /*totalLightCount*/)
	{
		// Cluster light count, slot usage, requested/dropped/converted are all
		// covered by DrawShadowSummary above this in the overlay header. This
		// function now carries only mode-specific information that wouldn't be
		// meaningful elsewhere -- channel meanings, heatmap legends, etc.
		if (mode == 3) {
			ImGui::Text("R channel  = directional soft shadow");
			ImGui::Text("G channel  = directional detailed shadow");
			ImGui::TextDisabled("(B = unused)");
		} else if (mode == 4) {
			ImGui::TextDisabled("Pixel heatmap: 0=blue  8+=red");
		} else if (mode == 5) {
			ImGui::TextDisabled("White = fully lit,  black = fully in shadow");
		} else if (mode == 6) {
			ImGui::TextDisabled("Pixel heatmap: 0=blue  8+=red (lights without shadow maps)");
		} else if (mode == 7) {
			ImGui::TextDisabled("Cool  Turbo[0.0-0.3] = 1-4 shadows");
			ImGui::TextDisabled("Warm  Turbo[0.3-0.8] = 5-%u shadows", GetInstalledSlotCount());
			ImGui::TextDisabled("Red                  = overflow");
		} else if (mode == 9) {
			uint32_t spotC = 0, hemiC = 0, omniC = 0;
			for (const auto& info : GetSlotInfos()) {
				if (!info.valid)
					continue;
				if (info.type == 0)
					spotC++;
				else if (info.type == 1)
					hemiC++;
				else
					omniC++;
			}
			ImGui::Text("R  Spot (frustum)   : %u", spotC);
			ImGui::Text("G  Hemisphere       : %u", hemiC);
			ImGui::Text("B  Omni (paraboloid): %u", omniC);
		}
	}

	void DrawVisualisationTooltipShadowModes()
	{
		ImGui::Text(
			"\n"
			"Shadow Mask: R=directional soft shadow, G=directional detailed shadow.\n"
			"\n"
			"Shadow Light Count: Heatmap of shadow-casting point/spot lights per pixel (blue=0, red=8+).\n"
			"Use to gauge shadow density; high counts indicate expensive shadow sampling.\n"
			"\n"
			"Point Light Shadow Factor: Brightness shows the darkest shadow value from any point/spot\n"
			"light. White=fully lit, black=fully shadowed. Shows where PCF/PCSS filtering is active.\n"
			"\n"
			"Unshadowed Point Lights: Heatmap of point/spot lights without shadow maps (blue=0, red=8+).\n"
			"High values where lights are bright indicate where the shadow slot limit is costing quality.\n"
			"\n"
			"Shadow Caster Density: Custom Turbo ranges show how heavily shadow slots are used.\n"
			"  Cool (Turbo 0.0-0.3): 1-4 shadow lights per pixel.\n"
			"  Warm (Turbo 0.3-0.8): 5 to ShadowMapSlots lights (dynamic range).\n"
			"  Bright red: overflow - a light wanted a shadow slot but none was available.\n"
			"\n"
			"Shadow Slot Index Color: Assigns each shadow-map slot a unique high-contrast hue\n"
			"(golden-ratio sequence) so you can identify which slot is casting the primary shadow.\n"
			"First valid shadow light index per pixel is shown. Bright red = slot overflow.\n"
			"\n"
			"Light Type Visualization: RGB channels encode shadow light types per pixel.\n"
			"  R = spot/frustum lights (ShadowParam.x == 0).\n"
			"  G = hemisphere/paraboloid lights (ShadowParam.x == 1).\n"
			"  B = omnidirectional/full-paraboloid lights (ShadowParam.x == 2).\n"
			"  Dark grey = unshadowed lights only (no shadow maps assigned).\n"
			"  Bright red = overflow (slot capacity exceeded).\n"
			"Intensity scales with count (up to 4); channels blend for mixed-type pixels.");
	}

	void DrawSettings(Settings& settings)
	{
		ImGui::SeparatorText("Shadow Limit Fix");

		// ---- External conflict banner --------------------------------------
		if (s_externalConflict) {
			const auto& theme = Menu::GetSingleton()->GetTheme();
			ImGui::TextColored(theme.StatusPalette.Error, "%s", s_conflictMessage.c_str());
			ImGui::BeginDisabled();
		}

		// ---- Enable toggle (requires restart) ------------------------------
		ImGui::Checkbox("Enable Shadow Limit Fix", &settings.Enabled);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Extends Skyrim's hard limit of 4 simultaneous shadow-casting lights.\n"
				"Intelligently selects which lights cast shadows each frame based on\n"
				"distance, intensity, and a configurable priority formula.\n\n"
				"Based on Intellightent by meh321.\n"
				"https://www.nexusmods.com/skyrimspecialedition/mods/172423\n\n"
				"Restart required to take effect in either direction. The boot-time\n"
				"patches (extended atlas slices, depth buffer creation loop, color-mask\n"
				"pass replacement) cannot be safely reversed at runtime -- vanilla\n"
				"shadow scheduling crashes when run on top of them. Toggle and restart.");
		// Either direction requires restart -- the boot-time patches modify
		// the engine's shadow texture array, depth buffer creation, and
		// color-mask pass. Vanilla scheduling cannot run on top of those
		// (verified by AV in BSShadowDirectionalLight processing during a
		// runtime-disable test, 2026-05-17 crash logs).
		if (settings.Enabled != s_settings.Enabled) {
			const auto& theme = Menu::GetSingleton()->GetTheme();
			ImGui::TextColored(theme.StatusPalette.RestartNeeded,
				"Restart required -- currently %s.", s_settings.Enabled ? "enabled" : "disabled");
		}

		if (!settings.Enabled)
			ImGui::BeginDisabled();

		// ---- Shadow Light Count (requires restart) -------------------------
		// Upper bound of 127: the engine refuses to render any shadow caster
		// when ShadowLightCount >= 128 even though kSHADOWMAPS allocates
		// successfully -- some internal limit (likely an 8-bit shadow index
		// somewhere we haven't patched) silently disables shadow rendering.
		// 127 is the highest value that actually works.
		ImGui::SliderInt("Shadow Light Count", &settings.ShadowLightCount, 0, 127);
		// Compute projected VRAM for the slider's current value so the user
		// can see the cost of a higher count *before* committing the restart.
		// kSHADOWMAPS holds exactly ShadowLightCount slices -- the sun lives
		// in its own kSHADOWMAPS_ESRAM texture, so there's no +1.
		auto sliderVram = GetVRAMInfo();
		std::uint64_t projectedBytes = 0;
		std::uint64_t projectedFreeBytes = 0;
		bool projectionValid = sliderVram.valid;
		if (projectionValid) {
			projectedBytes = ProjectShadowArrayBytes(static_cast<std::uint32_t>(settings.ShadowLightCount));
			std::int64_t projectedUsage = static_cast<std::int64_t>(sliderVram.currentUsageBytes) -
			                              static_cast<std::int64_t>(sliderVram.shadowArrayBytes) +
			                              static_cast<std::int64_t>(projectedBytes);
			if (projectedUsage < 0)
				projectedUsage = 0;
			projectedFreeBytes = (static_cast<std::int64_t>(sliderVram.budgetBytes) > projectedUsage) ? static_cast<std::uint64_t>(sliderVram.budgetBytes - projectedUsage) : 0;
		}
		if (ImGui::IsItemHovered()) {
			constexpr const char* kSliderBase =
				"Maximum simultaneous shadow-casting point/spot lights (directional sun not counted).\n"
				"  0  = scheduler runs but selects no point lights (sun/directional unaffected).\n"
				"  4  = vanilla point light count with intelligent selection.\n"
				"  >4 = extended mode; depth buffer expanded when >8. Max 127\n"
				"       (VRAM is the practical limit -- watch the projected-VRAM bar).\n"
				"Requires a game restart to take effect.";
			if (projectionValid) {
				ImGui::SetTooltip(
					"%s\n"
					"\n"
					"Projected kSHADOWMAPS array at %d slots: %.1f MB\n"
					"Per-slice cost: %.2f MB  (%u x %u, %u B/pixel)\n"
					"Projected free VRAM after restart: %.1f MB",
					kSliderBase,
					settings.ShadowLightCount,
					static_cast<float>(projectedBytes) / (1024.f * 1024.f),
					static_cast<float>(sliderVram.bytesPerSlice) / (1024.f * 1024.f),
					sliderVram.shadowWidth, sliderVram.shadowHeight,
					sliderVram.shadowWidth && sliderVram.shadowHeight ?
						sliderVram.bytesPerSlice / (sliderVram.shadowWidth * sliderVram.shadowHeight) :
						0u,
					static_cast<float>(projectedFreeBytes) / (1024.f * 1024.f));
			} else {
				ImGui::SetTooltip("%s", kSliderBase);
			}
		}
		// Custom-drawn stacked bar against DXGI budget showing non-shadow /
		// current-shadow / projected-shadow segments. ImGui::ProgressBar
		// can't multi-segment.
		if (projectionValid && sliderVram.budgetBytes > 0) {
			const VRAMVerdict verdict = EvaluateVRAMVerdict(projectedBytes, projectedFreeBytes, sliderVram.budgetBytes);
			const float budgetMBf = static_cast<float>(sliderVram.budgetBytes) / (1024.f * 1024.f);
			const float nonShadowMB = std::max(0.0f,
				(static_cast<float>(sliderVram.currentUsageBytes) - static_cast<float>(sliderVram.shadowArrayBytes)) / (1024.f * 1024.f));
			const float currentShadowMB = static_cast<float>(sliderVram.shadowArrayBytes) / (1024.f * 1024.f);
			const float projectedShadowMB = static_cast<float>(projectedBytes) / (1024.f * 1024.f);

			ImGui::Text("Projected shadow VRAM :");
			ImGui::SameLine();
			const ImVec2 cursor = ImGui::GetCursorScreenPos();
			const float fullWidth = ImGui::GetContentRegionAvail().x;
			const float barHeight = ImGui::GetFrameHeight();
			const float scale = fullWidth / budgetMBf;
			auto* draw = ImGui::GetWindowDrawList();
			// Background frame, then non-shadow / current / projected segments.
			draw->AddRectFilled(cursor, ImVec2(cursor.x + fullWidth, cursor.y + barHeight),
				ImGui::GetColorU32(ImGuiCol_FrameBg));
			const float nonShadowEndX = cursor.x + nonShadowMB * scale;
			draw->AddRectFilled(cursor, ImVec2(nonShadowEndX, cursor.y + barHeight),
				IM_COL32(120, 120, 120, 200));
			const float currentEndX = std::min(cursor.x + fullWidth, nonShadowEndX + currentShadowMB * scale);
			draw->AddRectFilled(ImVec2(nonShadowEndX, cursor.y),
				ImVec2(currentEndX, cursor.y + barHeight),
				IM_COL32(80, 130, 200, 220));
			// Projection outline anchored at the same start as current, so
			// the visual delta IS the difference. Solid fill for grow, dark
			// stripe for shrink.
			const float projectedEndX = std::min(cursor.x + fullWidth, nonShadowEndX + projectedShadowMB * scale);
			const ImU32 verdictColU32 = ImGui::GetColorU32(verdict.colour);
			draw->AddRect(ImVec2(nonShadowEndX, cursor.y), ImVec2(projectedEndX, cursor.y + barHeight),
				verdictColU32, 0.0f, 0, 2.0f);
			if (projectedShadowMB > currentShadowMB) {
				draw->AddRectFilled(ImVec2(currentEndX, cursor.y), ImVec2(projectedEndX, cursor.y + barHeight),
					(verdictColU32 & 0x00FFFFFFu) | 0xA0000000u);
			} else if (projectedShadowMB < currentShadowMB) {
				draw->AddRectFilled(ImVec2(projectedEndX, cursor.y), ImVec2(currentEndX, cursor.y + barHeight),
					IM_COL32(80, 80, 80, 120));
			}

			char overlay[128];
			snprintf(overlay, sizeof(overlay),
				"shadows %.0f -> %.0f MB  (%d slots,  %.0f MB free after restart)",
				currentShadowMB, projectedShadowMB,
				settings.ShadowLightCount,
				static_cast<float>(projectedFreeBytes) / (1024.f * 1024.f));
			const ImVec2 textSize = ImGui::CalcTextSize(overlay);
			const ImVec2 textPos(cursor.x + (fullWidth - textSize.x) * 0.5f,
				cursor.y + (barHeight - textSize.y) * 0.5f);
			draw->AddText(textPos, IM_COL32(240, 240, 240, 255), overlay);
			ImGui::Dummy(ImVec2(fullWidth, barHeight));  // reserve layout space
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(
					"Stacked VRAM bar against DXGI budget.\n"
					"  Grey block    : process VRAM not counted as shadow array\n"
					"  Blue block    : current kSHADOWMAPS allocation this session\n"
					"  Outlined block: what the slider's value would allocate\n"
					"                  after restart (colour reflects verdict)\n"
					"\n"
					"Solid colour past the blue: shadow array would GROW by that\n"
					"amount. Dark stripe inside the blue: shadow array would\n"
					"SHRINK by that amount.\n"
					"\n"
					"Slots requested  : %d (sun lives in kSHADOWMAPS_ESRAM)\n"
					"Per-slice cost   : %.2f MB  (%u x %u @ %u B/pixel)\n"
					"Current array    : %.1f MB\n"
					"Projected array  : %.1f MB\n"
					"Free after restart : %.1f MB / %.0f MB budget\n"
					"%s",
					settings.ShadowLightCount,
					static_cast<float>(sliderVram.bytesPerSlice) / (1024.f * 1024.f),
					sliderVram.shadowWidth, sliderVram.shadowHeight,
					sliderVram.shadowWidth && sliderVram.shadowHeight ?
						sliderVram.bytesPerSlice / (sliderVram.shadowWidth * sliderVram.shadowHeight) :
						0u,
					currentShadowMB,
					projectedShadowMB,
					static_cast<float>(projectedFreeBytes) / (1024.f * 1024.f),
					budgetMBf,
					verdict.over ?
						"\nRED: this projection won't fit in the current VRAM budget.\n"
						"The driver will page or refuse the allocation, leaving the\n"
						"shadow array smaller than requested -- shadows will silently\n"
						"break. Lower the slot count or reduce iShadowMapResolution." :
					verdict.tight ?
						"\nYELLOW: tight headroom. A driver or OS spike could push\n"
						"shadow allocation into paging. Safe for testing, risky for\n"
						"long sessions or heavily-modded scenes." :
						"");
			}
		}

		// ---- Allocation mismatch banner ----
		// Surface kSHADOWMAPS truncation visibly so users hit by a silent
		// "shadows don't work at high slot counts" failure can see why.
		// Reads the verified count directly (not the GetInstalledSlotCount
		// accessor, which falls back to the requested value).
		{
			uint32_t installed = s_installedSlotCount;
			uint32_t requested = s_requestedSlotCount;
			if (installed > 0 && requested > 0 && installed < requested) {
				ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1),
					"VRAM exhausted: requested %u slots, GPU allocated %u.",
					requested, installed);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(
						"The engine tried to create kSHADOWMAPS with %u slices but\n"
						"the GPU / driver returned a smaller array (likely out of\n"
						"VRAM at the configured iShadowMapResolution). The scheduler\n"
						"has clamped itself to the actual count so the existing %u\n"
						"slices work correctly, but to reach the requested %u you'll\n"
						"need to free VRAM (lower resolution, other features, etc).",
						requested, installed, requested);
			} else if (installed == 0 && s_settings.Enabled && !s_externalConflict) {
				ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.25f, 1),
					"Shadow array not yet verified -- load a save to confirm allocation.");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(
						"kSHADOWMAPS isn't readable yet (main menu / loading screen).\n"
						"Once you reach gameplay the scheduler verifies the actual\n"
						"slice count against your requested value. If they disagree\n"
						"this banner turns red.");
			}
		}

		if (settings.ShadowLightCount != s_installedShadowLightCount) {
			const auto& theme = Menu::GetSingleton()->GetTheme();
			ImGui::TextColored(theme.StatusPalette.RestartNeeded,
				"Restart required -- current session uses %d lights.", s_installedShadowLightCount);
		}

		// ---- Shadow Map Resolution (requires restart) ---------------------
		// Mirrors the launcher's resolution tiers (the four power-of-two values
		// Skyrim itself offers). Mutates the live iShadowMapResolution:Display
		// RE::Setting immediately; persistence to SkyrimPrefs.ini happens in
		// SCM::SaveINISettings (called from LightLimitFix::SaveSettings).
		if (auto* prefColl = RE::INIPrefSettingCollection::GetSingleton()) {
			if (auto* setting = prefColl->GetSetting("iShadowMapResolution:Display")) {
				static constexpr struct
				{
					const char* label;
					std::int32_t value;
				} kResTiers[] = {
					{ "Low (1024)", 1024 },
					{ "Medium (2048)", 2048 },
					{ "High (4096)", 4096 },
					{ "Ultra (8192)", 8192 },
				};
				constexpr int kTierCount = static_cast<int>(sizeof(kResTiers) / sizeof(kResTiers[0]));

				const std::int32_t currentRes = setting->GetInteger();
				int tierIdx = -1;
				for (int i = 0; i < kTierCount; ++i) {
					if (kResTiers[i].value == currentRes) {
						tierIdx = i;
						break;
					}
				}
				// Non-tier values (manual INI edits / third-party tools)
				// surface as "Custom (N)" so the user sees what the engine is
				// actually using, but we don't offer it as a selectable tier.
				char previewBuf[32];
				const char* preview;
				if (tierIdx >= 0) {
					preview = kResTiers[tierIdx].label;
				} else {
					snprintf(previewBuf, sizeof(previewBuf), "Custom (%d)", currentRes);
					preview = previewBuf;
				}

				if (ImGui::BeginCombo("Shadow Map Resolution", preview)) {
					for (int i = 0; i < kTierCount; ++i) {
						const bool selected = (i == tierIdx);
						if (ImGui::Selectable(kResTiers[i].label, selected) &&
							kResTiers[i].value != currentRes) {
							setting->SetInteger(kResTiers[i].value);
							s_shadowResolutionDirty = true;
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip(
						"Drives iShadowMapResolution:Display in SkyrimPrefs.ini.\n"
						"Affects both omni/spot shadow slices and the sun cascade\n"
						"texture; per-slice VRAM scales as resolution^2 * 4 bytes\n"
						"(4 / 16 / 64 / 256 MB at 1024 / 2048 / 4096 / 8192).\n"
						"Requires a game restart to take effect.");
				}

				if (s_initialShadowMapResolution > 0 && currentRes != s_initialShadowMapResolution) {
					const auto& theme = Menu::GetSingleton()->GetTheme();
					ImGui::TextColored(theme.StatusPalette.RestartNeeded,
						"Restart required -- current session uses %d px shadow maps.",
						s_initialShadowMapResolution);
				}
			}
		}

		// ---- Temporal budget (dynamic) ------------------------------------

		// Migrate legacy Auto saves silently. Manual is now the default and the
		// closest match in spirit to what most users actually wanted from Auto:
		// a predictable budget that doesn't ping-pong. Power users can switch
		// back to Formula manually if they want the adaptive default expression.
		if (settings.BudgetMode == BudgetModeEnum::Auto)
			settings.BudgetMode = BudgetModeEnum::Manual;

		// Budget mode selector — Manual or Formula. Auto was removed: it was an
		// opaque DRS controller that confused users when the budget moved without
		// a visible cause. The default Formula expresses the same behaviour
		// transparently and stays editable.
		static const char* budgetModeNames[] = { "Manual", "Formula" };
		int budgetModeIdx = (settings.BudgetMode == BudgetModeEnum::Manual) ? 0 : 1;
		if (ImGui::Combo("Budget Mode", &budgetModeIdx, budgetModeNames, 2))
			settings.BudgetMode = (budgetModeIdx == 0) ? BudgetModeEnum::Manual : BudgetModeEnum::Formula;
		if (ImGui::IsItemHovered()) {
			if (budgetModeIdx == 0)
				ImGui::SetTooltip(
					"Manual (default): fixed per-frame GPU time budget for shadow re-renders.\n"
					"Predictable; doesn't oscillate. Adjust the slider to trade FPS for shadow quality.");
			else
				ImGui::SetTooltip(
					"Formula: user-editable exprtk expression for per-frame budget.\n"
					"Default expression matches Intellightent's original behaviour\n"
					"(1 ms outdoors, 2 ms indoors). Edit the expression in the\n"
					"Advanced section below.\n"
					"\n"
					"Caveat: adaptive expressions referencing `frametime` tend to\n"
					"ping-pong because rendering shadows raises frametime, removing\n"
					"the headroom that allowed the budget. Stick to static or\n"
					"slowly-varying inputs (`isinterior`, `frametarget`).");
		}

		// Per-mode controls.
		if (budgetModeIdx == 0) {
			ImGui::SliderFloat("Redraw Budget (ms)", &settings.RedrawBudgetMs, 0.1f, 32.0f, "%.2f ms");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Per-frame GPU time budget for shadow re-renders (milliseconds).\n"
					"Lights whose estimated render cost exceeds the remaining budget are deferred.\n"
					"The first eligible light always renders regardless of budget (starvation prevention).\n"
					"\n"
					"Reference points:\n"
					"  1-2 ms: Intellightent's original (1 outdoors, 2 indoors)\n"
					"  5 ms : default — comfortable for typical scenes (~5-8 lights at ~1 ms each)\n"
					"  16 ms: full 60 fps frame; shadows can saturate the frame here\n"
					"  32 ms: extreme — only useful for very high light counts on fast GPUs\n"
					"\n"
					"Higher = more shadow lights redraw per frame, fewer stale shadow maps,\n"
					"at the cost of frametime. The Budget verdict in the Active Casters\n"
					"section shows whether the current setting has headroom to spare.");
		} else {
			ImGui::Text("Budget from formula: %.2f ms", s_autoBudgetMs);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Edit the Redraw Budget formula in the Advanced section below.");
		}

		// Budget consumption visualisation lives in the Active Casters block
		// (DrawShadowSchedulerStats) alongside the verdict, so the bar, the
		// numeric reading and the actionable state appear in one place
		// instead of being split between two sections.

		// ---- Frame-target diagnostic (Formula mode only) ------------------
		// `frametarget` is an exprtk variable available to the Redraw Budget
		// formula -- in Formula mode the user needs to see what it evaluates
		// to in order to write/debug expressions that reference it. In Manual
		// mode the user's chosen RedrawBudgetMs has nothing to do with frame
		// timing, so this block would just be noise -- the new Budget verdict
		// (in the Active Casters block) covers the "headroom / saturated"
		// signal more actionably for both modes, and DrawShadowSummary covers
		// the rendered/dropped lights count without duplication.
		if (settings.BudgetMode == BudgetModeEnum::Formula) {
			const float currentFrameMs = *globals::game::deltaTime * 1000.0f;
			const float currentFPS = 1000.0f / std::max(currentFrameMs, 1.0f);
			const float targetMs = ComputeFrameTimePercentile90();
			const float targetFPS = targetMs > 0.0f ? 1000.0f / targetMs : 0.0f;
			const float rawHeadroom = targetMs - s_ftEMA;
			const float headroomMs = rawHeadroom - kFrameHeadroomSafetyMs;

			const char* state = "steady";
			if (rawHeadroom > kFrameHeadroomSafetyMs + kFrameHeadroomDeadZoneMs)
				state = "growing";
			else if (rawHeadroom < -kFrameHeadroomDeadZoneMs)
				state = "throttling";

			ImGui::Text("Frame: %.1f FPS (%.1f ms) | frametarget: %.0f FPS (%.1f ms) | headroom: %+.1f ms | %s",
				currentFPS, currentFrameMs, targetFPS, targetMs, headroomMs, state);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Live values of the exprtk variables exposed to the Redraw\n"
					"Budget formula. `frametarget` is the rolling 90th-percentile\n"
					"frame time, used as a self-measured ceiling -- not a vsync\n"
					"target. State indicator:\n"
					"  steady     -- within +/-%.1f ms of target\n"
					"  growing    -- frametime well below target; headroom available\n"
					"  throttling -- frametime over target; expressions returning\n"
					"                 nonzero values here will keep frametime high",
					kFrameHeadroomDeadZoneMs);
		}
		{
			// Use ShadowLightCount as the slider upper bound when the scheduler hasn't
			// run yet (s_totalShadowLightsThisFrame == 0 on the first menu open).
			// Never clamp the stored setting here — the scheduling code already applies
			// the live cap.  Clamping here caused MaxRedrawPerFrame to be permanently
			// written to 1 on the first DrawSettings call before the hook fired.
			// Track active shadow lights this frame, falling back to the
			// configured ShadowLightCount when the scheduler hasn't run yet.
			// No artificial 64 cap -- if the user dialled in 128 lights, the
			// redraw cap should be allowed to follow.
			int maxRedraws = s_totalShadowLightsThisFrame > 0 ? s_totalShadowLightsThisFrame : settings.ShadowLightCount;
			maxRedraws = std::max(maxRedraws, Settings::kMinMaxRedrawPerFrame);
			ImGui::SliderInt("Max Redraws Per Frame", &settings.MaxRedrawPerFrame,
				Settings::kMinMaxRedrawPerFrame, maxRedraws);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Hard cap on how many shadow lights may re-render their shadow maps in one frame.\n"
					"Acts as a safety valve regardless of budget -- the budget controls time spent,\n"
					"this controls count. The sun directional light always counts as one redraw.\n"
					"Minimum is %d (lower values cause shadow flicker as redraw rotation outpaces TAA).\n"
					"Upper bound tracks the number of active shadow lights this frame (%d).",
					Settings::kMinMaxRedrawPerFrame, maxRedraws);
		}

		// ---- Light conversion (requires restart for hooks) -----------------
		if (ImGui::TreeNode("Light Conversion##LightConv")) {
			ImGui::Checkbox("Convert Excess Lights to Normal", &settings.ConvertExcessToNormal);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Shadow lights that exceed the active shadow caster limit are demoted to\n"
					"normal (unshadowed) lights so they still contribute diffuse and specular\n"
					"lighting at no shadow-map cost. Lights that fail culling are dropped entirely.\n"
					"Requires a game restart to change.");

			// No texture-array cost -- converted lights flow through the cluster
			// pipeline as ordinary non-shadow lights. Match the ShadowLightCount
			// max so users can pair a large shadow pool with a matching converted
			// pool without the slider lying about the upper bound.
			ImGui::SliderInt("Converted Shadow Slots", &settings.ConvertedShadowSlots, 0, 127);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Extra pool slots for lights converted to normal (unshadowed) mode.\n"
					"Increase if Convert Excess Lights drops lights you expect to see.");

			ImGui::Checkbox("Promote Normal Lights to Shadow Casters", &settings.PromoteNormalToShadow);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Experimental: elevate high-scoring unshadowed lights to shadow casters\n"
					"when shadow slots are available.\n"
					"Requires a game restart to change.");

			ImGui::SeparatorText("Portal-Strict Enforcement");
			// Three-way toggle plus master row. SCM forces the engine's
			// portal-strict flag on shadow casters at creation time, gated
			// per shadow type (FOV-derived). Defaults enforce on omni and
			// hemisphere, leave spotlights alone -- portal-strict on spots
			// drops culled-but-visible spots entirely (cone test rejects
			// spots whose origin is behind a portal even when the beam
			// sweeps into a visible room).
			{
				const bool allOn = settings.ForceEnablePortalStrictOmni &&
				                   settings.ForceEnablePortalStrictHemi &&
				                   settings.ForceEnablePortalStrictSpot;
				const bool allOff = !settings.ForceEnablePortalStrictOmni &&
				                    !settings.ForceEnablePortalStrictHemi &&
				                    !settings.ForceEnablePortalStrictSpot;
				bool master = allOn;
				bool indeterminate = !allOn && !allOff;
				if (indeterminate) {
					// Render the master checkbox as visually mixed via a
					// muted alpha so the row still functions as a "set all"
					// control without misrepresenting state.
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.6f);
				}
				if (ImGui::Checkbox("Force Enable Portal Strict (All)", &master)) {
					settings.ForceEnablePortalStrictOmni = master;
					settings.ForceEnablePortalStrictHemi = master;
					settings.ForceEnablePortalStrictSpot = master;
				}
				if (indeterminate)
					ImGui::PopStyleVar();
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(
						"Master toggle for the three per-type rows below.\n"
						"Checked when all three are enforced, unchecked when none are,\n"
						"and rendered translucent when mixed.\n"
						"Requires a game restart to change.");
			}

			ImGui::Indent();
			ImGui::Checkbox("Force Portal Strict on Omni Lights", &settings.ForceEnablePortalStrictOmni);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Force-enable portal-strict on dual-paraboloid (omnidirectional)\n"
					"shadow casters. Recommended on -- tightens portal-graph visibility\n"
					"culling for full-sphere shadow lights without side effects.\n"
					"Requires a game restart to change.");
			ImGui::Checkbox("Force Portal Strict on Hemisphere Lights", &settings.ForceEnablePortalStrictHemi);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Force-enable portal-strict on single-paraboloid (hemisphere)\n"
					"shadow casters. Recommended on -- behaves like the omni case\n"
					"under portal culling.\n"
					"Requires a game restart to change.");
			ImGui::Checkbox("Force Portal Strict on Spot Lights", &settings.ForceEnablePortalStrictSpot);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Force-enable portal-strict on perspective (frustum/spot) shadow\n"
					"casters. Off by default: the cone test rejects spots whose\n"
					"origin sits behind a portal even when their beam sweeps into a\n"
					"visible room, which drops culled-but-visible spots entirely.\n"
					"Enable only for debugging.\n"
					"Requires a game restart to change.");
			ImGui::Unindent();

			ImGui::TreePop();
		}

		// ---- Advanced (dynamic) -------------------------------------------
		if (ImGui::TreeNode("Advanced##ShadowScheduling")) {
			ImGui::Checkbox("Allow Immediate Draw for New Lights", &settings.AllowDrawNewLight);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Allow a light just added to the active pool to render its shadow map this frame.\n"
					"Prevents a one-frame shadow-map gap when new lights enter view.");

			// ---- Importance scheduling curve ------------------------------
			ImGui::SeparatorText("Importance Scheduling");
			ImGui::SliderFloat("Max Interval Scale", &settings.ImportanceMaxScale, 0.5f, 5.0f, "%.2f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Interval multiplier applied to unimportant lights (importance = 0).\n"
					"Higher values defer dim or distant lights more aggressively.\n"
					"Default: 2.0");
			settings.ImportanceMaxScale = std::max(settings.ImportanceMaxScale, settings.ImportanceMinScale);

			ImGui::SliderFloat("Min Interval Scale", &settings.ImportanceMinScale, 0.01f, 1.0f, "%.3f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Interval multiplier applied to high-importance lights (importance >= 1).\n"
					"Lower values make bright/close lights update shadows more frequently.\n"
					"The ratio Max/Min defines the scheduling dynamic range.\n"
					"Default: 0.05  (40x range at default Max=2.0)");
			settings.ImportanceMinScale = std::min(settings.ImportanceMinScale, settings.ImportanceMaxScale);

			{
				float ratio = settings.ImportanceMaxScale / std::max(settings.ImportanceMinScale, 0.001f);
				ImGui::Text("Dynamic range: %.0fx  (unimportant lights wait %.0fx longer)", ratio, ratio);
			}

			if (ImGui::Button("Reset Importance Defaults")) {
				settings.ImportanceMinScale = 0.05f;
				settings.ImportanceMaxScale = 2.0f;
			}

			// ---- Formula editor ------------------------------------------
			if (ImGui::TreeNode("Formula Editor##Formulas")) {
				// Build variable reference from the DRY table.
				if (ImGui::TreeNode("Available Variables##FormulaVars")) {
					if (ImGui::BeginTable("##FormulaVarTable", 2,
							ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
								ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
							ImVec2(0, std::min(static_cast<float>(IM_ARRAYSIZE(kFormulaVars)) * 20.0f + 28.0f, 320.0f)))) {
						ImGui::TableSetupColumn("Variable");
						ImGui::TableSetupColumn("Description");
						ImGui::TableHeadersRow();
						for (const auto& v : kFormulaVars) {
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::TextUnformatted(v.name);
							ImGui::TableSetColumnIndex(1);
							ImGui::TextUnformatted(v.description);
						}
						ImGui::EndTable();
					}
					ImGui::TreePop();
				}

				static char scoreBuf[512];
				static char scoreErr[256] = {};
				static char redrawIntervalBuf[512];
				static char redrawIntervalErr[256] = {};
				static char redrawBudgetBuf[512];
				static char redrawBudgetErr[256] = {};
				static bool formulaBufsInited = false;
				if (!formulaBufsInited) {
					snprintf(scoreBuf, sizeof(scoreBuf), "%s", settings.ScoreFormula.c_str());
					snprintf(redrawIntervalBuf, sizeof(redrawIntervalBuf), "%s", settings.RedrawIntervalFormula.c_str());
					snprintf(redrawBudgetBuf, sizeof(redrawBudgetBuf), "%s", settings.RedrawBudgetFormula.c_str());
					formulaBufsInited = true;
				}

				// Helper lambda: validate, apply live, revert buffer on error.
				auto applyFormula = [](const char* label, char* buf, size_t bufSize,
										std::string& settingStr, char* errBuf, size_t errBufSize,
										std::unique_ptr<FormulaHelper>& helper) {
					ImGui::InputText(label, buf, bufSize);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						std::string err;
						if (FormulaHelper::Validate(buf, err)) {
							settingStr = buf;
							errBuf[0] = '\0';
							if (helper)
								helper->Reparse(settingStr);
							else {
								helper = std::make_unique<FormulaHelper>();
								helper->Parse(settingStr);
							}
						} else {
							snprintf(errBuf, errBufSize, "Parse error: %s", err.c_str());
							snprintf(buf, bufSize, "%s", settingStr.c_str());
						}
					}
					if (errBuf[0])
						ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", errBuf);
				};

				applyFormula("Score", scoreBuf, sizeof(scoreBuf),
					settings.ScoreFormula, scoreErr, sizeof(scoreErr), s_formulaScore);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Light priority scoring formula. Higher score = more likely to get a shadow slot.");

				applyFormula("Redraw Interval", redrawIntervalBuf, sizeof(redrawIntervalBuf),
					settings.RedrawIntervalFormula, redrawIntervalErr, sizeof(redrawIntervalErr), s_formulaRedrawInterval);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Per-light redraw interval formula. Higher = less frequent shadow map updates.");
				applyFormula("Redraw Budget", redrawBudgetBuf, sizeof(redrawBudgetBuf),
					settings.RedrawBudgetFormula, redrawBudgetErr, sizeof(redrawBudgetErr), s_formulaRedrawBudget);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Per-frame redraw budget formula (ms). Empty = use the Redraw Budget (ms) slider value.");

				ImGui::TreePop();
			}

			ImGui::TreePop();
		}

		// Active casters table + scheduler stats are rendered by LightLimitFix
		// alongside its own quick-stats line, so the table area has full
		// testing context (cluster light count, shadow slot usage, etc.) in
		// one place. See LightLimitFix::DrawSettings.

		if (!settings.Enabled)
			ImGui::EndDisabled();

		if (s_externalConflict)
			ImGui::EndDisabled();
	}
}
