// ShadowCasterManager.cpp
// Shadow caster scheduling for LightLimitFix.
//
// Based on Intellightent by meh321
//   https://www.nexusmods.com/skyrimspecialedition/mods/172423
//
// Ported and adapted for Community Shaders by the Community Shaders team with permission.

#include "ShadowCasterManager.h"
#include "../../Globals.h"
#include "../../Utils/UI.h"

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
		{ "lightdistance", "camera-to-light distance (units)", kFormulaParam_LightDistance },
		{ "lightradius", "light radius (units)", kFormulaParam_LightRadius },
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
		{ "lightneverfades", "1 if lodFade disabled (permanent light)", kFormulaParam_LightNeverFades },
		{ "lightportalstrict", "1 if portal-strict (always 1 for shadow casters)", kFormulaParam_LightPortalStrict },
		{ "lightns", "1 if promoted from normal light (PromoteNormalToShadow)", kFormulaParam_LightNS },
		{ "lightconverted", "1 if light is in the converted (non-shadow) slot range", kFormulaParam_LightConverted },
		{ "camerax", "camera world X", kFormulaParam_CameraX },
		{ "cameray", "camera world Y", kFormulaParam_CameraY },
		{ "cameraz", "camera world Z", kFormulaParam_CameraZ },
		{ "isinterior", "1 in interior cells, 0 outdoors", kFormulaParam_IsInterior },
		{ "timeofday", "in-game hour (0.0–24.0)", kFormulaParam_TimeOfDay },
		{ "frametime", "EMA-smoothed frame time (ms)", kFormulaParam_FrameTime },
		{ "frametarget", "90th-percentile recent frame time (ms) — headroom ceiling", kFormulaParam_FrameTarget },
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

	static Settings s_settings;
	static LightContainer s_lights;
	static BudgetTracker s_budget;

	// Rolling redraw history (128-frame window) for DrawSettings statistics.
	static constexpr int kRedrawHistorySize = 128;
	static int32_t s_redrawHistory[kRedrawHistorySize] = {};
	static int32_t s_redrawHistoryPos = 0;
	static int32_t s_redrawSum = 0;

	// Rolling budget-consumed history (same window) for DrawSettings statistics.
	static int32_t s_budgetHistory[kRedrawHistorySize] = {};
	static int32_t s_budgetHistoryPos = 0;
	static int64_t s_budgetSum = 0;

	// Auto-budget: sliding window of recent frame times for 90th-percentile targeting.
	static constexpr int kAutoFrameWindow = 120;  // ~2 s at 60 fps
	static constexpr float kAutoSafetyMs = 0.5f;  // headroom reserved for the rest of the frame
	static constexpr int kAutoRampFrames = 45;    // consecutive stable frames before fully opening budget

	static float s_ftRing[kAutoFrameWindow]{};
	static int s_ftHead = 0;
	static int s_ftCount = 0;
	static float s_ftEMA = 0.0f;
	static int s_stableFrames = 0;
	static float s_autoBudgetMs = 0.0f;  // last computed value; used by UI and stats

	static float ComputeFrameTimePercentile90()
	{
		if (s_ftCount == 0)
			return 16.67f;  // fallback: 60 fps target
		const int n = std::min(s_ftCount, kAutoFrameWindow);
		float tmp[kAutoFrameWindow];
		std::copy(s_ftRing, s_ftRing + n, tmp);
		const int idx = static_cast<int>(n * 0.9f);
		std::nth_element(tmp, tmp + idx, tmp + n);
		return tmp[idx];
	}

	// Maximum ShadowLightCount the installed infrastructure supports.
	// Set once by Install(); Update() clamps to this.
	static int32_t s_installedShadowLightCount;

	// Formula instances (allocated at Init if formula strings are non-empty)
	static FormulaHelper* s_formulaScore = nullptr;
	static FormulaHelper* s_formulaRedrawInterval = nullptr;
	static FormulaHelper* s_formulaRedrawBudget = nullptr;

	// Lights converted to normal (non-shadow) lights for diffuse-only rendering
	struct ConvertedLight
	{
		RE::BSShadowLight* light;
		bool isNS;
	};
	static std::vector<ConvertedLight> s_normalConvert;
	static std::set<RE::NiLight*> s_shadowConvert;

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
		auto* light = reinterpret_cast<RE::BSShadowLight*>(REL::Relocate(ctx.R15, ctx.R15, ctx.R14));
		int32_t idx = s_lights.FindLight(light, s_settings.ShadowLightCount);
		if (idx < 0)
			idx = 0;  // should not happen; fail safe to slot 0

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
		for (auto* l : ssn->GetRuntimeData().shadowLightsAccum) {
			if (l)
				l->ReturnShadowmaps();
		}
	};

	// =========================================================================
	// LightContainer methods
	// =========================================================================

	int32_t LightContainer::FindFreeIndex(bool shadowSlot, int32_t shadowCount, int32_t convertCount) const
	{
		if (shadowSlot) {
			for (int i = 1; i <= shadowCount; i++)  // i=1: slot 0 is always the sun
				if (!Lights[i].Light)
					return i;
		} else {
			for (int i = shadowCount + 1; i <= shadowCount + convertCount; i++)
				if (!Lights[i].Light)
					return i;
		}
		return -1;
	}

	int32_t LightContainer::FindLight(RE::BSShadowLight* light, int32_t shadowCount) const
	{
		for (int i = 0; i < shadowCount; i++)
			if (Lights[i].Light == light)
				return i;
		return -1;
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
			Progress = static_cast<uint16_t>(std::min(diff, (int64_t)0xFFFF));
		} else if (step == 1) {
			diff += Progress;
			int32_t ix = TrackedCount % kBudgetWindowSize;
			Current -= Tracked[ix];
			Tracked[ix] = static_cast<uint16_t>(std::min(diff, (int64_t)0xFFFF));
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
			if ((_counter % 300) == 0)
				CleanupExpired();
		}
	}

	void BudgetTracker::BeginLight(RE::BSShadowLight* light, int32_t step)
	{
		uint64_t key = reinterpret_cast<uint64_t>(light);
		auto it = _map.find(key);
		BudgetEntry* e;
		if (it == _map.end()) {
			e = new BudgetEntry();
			e->Key = key;
			_map[key] = e;
		} else {
			e = it->second;
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
		if (it == _map.end() || it->second->TrackedCount == 0) {
			// Unknown light: return average cost across all tracked lights.
			int64_t sum = 0;
			int32_t count = 0;
			for (auto& [k, entry] : _map) {
				int32_t n = std::min(kBudgetWindowSize, entry->TrackedCount);
				if (n == 0)
					continue;
				int32_t avg = entry->Current;
				if (n > 1)
					avg /= n;
				sum += avg;
				count++;
			}
			if (count > 1)
				sum /= count;
			return static_cast<int32_t>(sum);
		}
		int32_t n = std::min(kBudgetWindowSize, it->second->TrackedCount);
		int32_t avg = it->second->Current;
		if (n > 1)
			avg /= n;
		return avg;
	}

	void BudgetTracker::CleanupExpired()
	{
		for (auto it = _map.begin(); it != _map.end();) {
			if (it->second->IsExpired(_counter)) {
				delete it->second;
				it = _map.erase(it);
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
		// SE/AE only — no VR equivalent (ID 100440)
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
		// VR:     (cam, coord, r1, r2, eyeIndex, eps)  — pass 0xffffffff for combined frustum
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

		// Chosen-last-frame bonus
		double chosenLastFrame = 0.0;
		for (int i = 0; i < s_settings.ShadowLightCount; i++) {
			if (s_lights.Lights[i].Light == light) {
				chosenLastFrame = 1.0;
				break;
			}
		}
		FormulaHelper::SetParam(kFormulaParam_LightChosenLastFrame, chosenLastFrame);

		FormulaHelper::SetParam(kFormulaParam_LightNeverFades, light->lodFade ? 0.0 : 1.0);
		FormulaHelper::SetParam(kFormulaParam_LightPortalStrict, light->portalStrict ? 1.0 : 0.0);
		FormulaHelper::SetParam(kFormulaParam_LightNS, 0.0);

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
	}

	static double CalculateLightScore(const RE::BSShadowLight* light, const RE::NiCamera* camera, int32_t index)
	{
		SetupLightFormula(light, camera, index);

		if (s_formulaScore)
			return s_formulaScore->Calculate();

		return 0.0;
	}

	// =========================================================================
	// Light enable / disable helpers
	// =========================================================================

	static void DisableLight(RE::BSShadowLight* light)
	{
		// Remove from conversion list if present.
		for (auto it = s_normalConvert.begin(); it != s_normalConvert.end(); ++it) {
			if (it->light == light) {
				GameClearGeometryList(light);
				s_normalConvert.erase(it);
				break;
			}
		}
		auto* cull = light->cullingProcess;
		if (cull && cull->portalGraphEntry)
			GameClearPortalVisibility(reinterpret_cast<RE::BSPortalGraphEntry*>(cull->portalGraphEntry));
		light->ReturnShadowmaps();
	}

	// Activates a light as a normal (non-shadow) light by inserting it into
	// the scene's active-light list without allocating a shadow slot.
	static void ConvertLight(RE::BSShadowLight* light, RE::ShadowSceneNode* ssn, bool isNS)
	{
		// Already converted: just re-enable so geometry picks it up this frame.
		for (auto& c : s_normalConvert) {
			if (c.light == light) {
				GameEnableLight(ssn, light);
				return;
			}
		}

		// Prepass: release shadow resources.
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
		for (auto it = s_normalConvert.begin(); it != s_normalConvert.end(); ++it) {
			if (it->light == light) {
				GameClearGeometryList(light);
				s_normalConvert.erase(it);
				break;
			}
		}

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
			float dx = lpos.x - cpos.x, dy = lpos.y - cpos.y, dz = lpos.z - cpos.z;
			float dist = sqrtf(dx * dx + dy * dy + dz * dz);
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

		// Extended mode: pre-set kNONE renderTarget so RenderCascade uses our slot index
		// instead of the global counter (which would corrupt all lights to slot 0).
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
	struct CandidateLight
	{
		RE::BSShadowLight* light{ nullptr };
		double score{ 0.0 };
		bool sun{ false };
		bool chosen{ false };
	};

	static void ScheduleShadowCasters()
	{
		// VR display guard: skip scheduling when the HMD display is not active.
		if (REL::Module::IsVR() && !GetVRDrawShadows())
			return;

		auto* ssn = GetShadowSceneNode();
		auto* camera = GetWorldCamera();
		if (!ssn || !camera)
			return;

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
		SetupSceneFormula(camera);

		std::vector<CandidateLight> candidates;
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

		// Sort descending by score (highest priority first); sun always first.
		std::sort(candidates.begin(), candidates.end(),
			[](const CandidateLight& a, const CandidateLight& b) {
				if (a.sun != b.sun)
					return a.sun;
				return a.score > b.score;
			});

		// ---- Select top N lights that pass portal culling ----
		auto* globalCull = *reinterpret_cast<RE::BSCullingProcess**>(
			*reinterpret_cast<uintptr_t**>(
				REL::RelocationID(528077, 415022).address()));

		// Slot 0 is reserved for the sun; point lights fill slots 1..ShadowLightCount.
		// Do not count the sun against ShadowLightCount — it uses focus cascade DSV slots,
		// not parabolic point-light slots.
		int wantCount = 0;

		for (auto& c : candidates) {
			auto* l = c.light;
			if (!l->UpdateCamera(camera)) {
				DisableLight(l);
				continue;
			}
			auto* cull = GetLightCullingProcess(l);
			auto* portal = cull ? reinterpret_cast<RE::BSPortalGraphEntry*>(cull->portalGraphEntry) : nullptr;
			if (!cull || !portal) {
				DisableLight(l);
				continue;
			}
			auto* gPortal = globalCull ? reinterpret_cast<RE::BSPortalGraphEntry*>(globalCull->portalGraphEntry) : nullptr;
			if (!gPortal || !GamePortalHasSharedVisibility(gPortal, portal)) {
				DisableLight(l);
				continue;
			}

			if (wantCount < s_settings.ShadowLightCount) {
				c.chosen = true;
				wantCount++;
			} else {
				DisableLight(l);
			}
		}

		// ---- Sync s_lights (our active pool) ----

		// Remove lights no longer chosen.
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

		// Add newly chosen lights.
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
			s_lights.Lights[0].Clear();
			s_lights.Sun = false;
		}

		// ---- Temporal budget: decide which lights redraw this frame ----
		{
			// Update frame-time ring buffer and derive formula params.
			// Always runs so frametime/frametarget/stableframes are current for any formula.
			{
				const float dtMs = *globals::game::deltaTime * 1000.0f;
				s_ftRing[s_ftHead] = dtMs;
				s_ftHead = (s_ftHead + 1) % kAutoFrameWindow;
				if (s_ftCount < kAutoFrameWindow)
					++s_ftCount;
				s_ftEMA = (s_ftCount == 1) ? dtMs : 0.1f * dtMs + 0.9f * s_ftEMA;

				const float target_ms = ComputeFrameTimePercentile90();
				if (s_ftEMA < target_ms)
					s_stableFrames = std::min(s_stableFrames + 1, kAutoRampFrames);
				else
					s_stableFrames = 0;

				FormulaHelper::SetParam(kFormulaParam_FrameTime, static_cast<double>(s_ftEMA));
				FormulaHelper::SetParam(kFormulaParam_FrameTarget, static_cast<double>(target_ms));
				FormulaHelper::SetParam(kFormulaParam_StableFrames, static_cast<double>(s_stableFrames));
			}

			// Evaluate the budget formula once for the whole frame.
			// Falls back to RedrawBudgetMs when AutoBudget is disabled or formula is unset.
			double budget = s_settings.RedrawBudgetMs;
			if (s_settings.AutoBudget && s_formulaRedrawBudget)
				budget = s_formulaRedrawBudget->Calculate();
			s_autoBudgetMs = static_cast<float>(budget);

			int maxRedraw = s_settings.MaxRedrawPerFrame;
			int32_t budgetRemain = static_cast<int32_t>(budget * 1000.0);
			bool isFirst = true;
			int32_t now = *globals::game::frameCounter;

			for (int i = s_settings.ShadowLightCount; i < s_lights.Size; i++)
				s_lights.Lights[i].RedrawFrame = false;

			for (int i = 0; i < s_lights.Size; i++) {
				auto& e = s_lights.Lights[i];
				if (!e.Light) {
					e.RedrawFrame = false;
					continue;
				}
				e.RedrawFrame = (i == 0 && s_lights.Sun) || (e.LastDrawnFrame < 0 && s_settings.AllowDrawNewLight);
				if (e.RedrawFrame) {
					e.LastDrawnFrame = now;
					isFirst = false;
					maxRedraw--;
					if (i != 0 || !s_lights.Sun) {
						int32_t estimatedBudget = s_budget.GetCost(e.Light);
						budgetRemain -= estimatedBudget;
					}
				}
			}

			if (maxRedraw > 0 && budgetRemain > 0) {
				std::vector<LightEntry*> pending;
				for (int i = 0; i < s_lights.Size; i++) {
					auto& e = s_lights.Lights[i];
					if (!e.Light || e.RedrawFrame)
						continue;
					pending.push_back(&e);
				}

				for (auto* e : pending) {
					double interval = 0.0;
					if (s_formulaRedrawInterval) {
						SetupLightFormula(e->Light, camera, 0);
						if (e->Index >= s_settings.ShadowLightCount)
							FormulaHelper::SetParam(kFormulaParam_LightConverted, 1.0);
						interval = s_formulaRedrawInterval->Calculate();
					}
					interval += 1.0;
					e->RedrawScore = e->LastDrawnFrame + interval;
				}

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
						isFirst = false;
						continue;
					}
					if (budgetEstimate <= budgetRemain) {
						budgetRemain -= budgetEstimate;
						maxRedraw--;
						e->RedrawFrame = true;
						e->LastDrawnFrame = now;
						continue;
					}
				}
			}
		}

		// ---- Activate selected lights ----
		for (int i = 0; i < s_lights.Size; i++) {
			auto& e = s_lights.Lights[i];
			if (!e.Light)
				continue;
			bool isSunSlot = (i == 0 && s_lights.Sun);

			if (e.RedrawFrame && i < s_settings.ShadowLightCount) {
				if (!isSunSlot) {
					e.Light->UpdateCamera(camera);
					s_budget.BeginLight(e.Light, 0);
					EnableLight(e.Light, camera, ssn, i);
					s_budget.EndLight(e.Light, 0);
				}
				ShadowField(e.Light, maskIndex) = static_cast<uint32_t>(i);
				doneLightCount++;
			} else {
				DisableLight(e.Light);
			}
		}

		// Non-redrawn chosen lights: insert at end of shadow caster array without rendering.
		// GetAccumLightSlot() already advanced past all EnableLight()-rendered slots.
		{
			int endIdx = (int)*GetAccumLightSlot();

			for (int i = 0; i < s_lights.Size; i++) {
				auto& e = s_lights.Lights[i];
				if (e.Light && (!e.RedrawFrame || i >= s_settings.ShadowLightCount)) {
					GameSetShadowCasterSlot(ssn, e.Light, endIdx, 1);
					endIdx += e.Light->shadowMapCount;
					ShadowField(e.Light, maskIndex) = static_cast<uint32_t>(i);
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

		s_budget.Begin(1);

		uint32_t tmp = 0;
		for (int i = 0; i < s_settings.ShadowLightCount; i++) {
			auto& e = s_lights.Lights[i];
			if (!e.Light || !e.RedrawFrame)
				continue;
			s_budget.BeginLight(e.Light, 1);
			e.Light->Render(tmp);
			s_budget.EndLight(e.Light, 1);
		}

		s_budget.Begin(1);  // cleanup tick for step 1

		if (REL::Module::IsVR())
			*globals::game::drawStereo = savedStereo;
	}

	// Replaces the call to RenderActiveShadowCasterLights.
	// install_context_hook (RtlRestoreContext) is required so all volatile registers (r8, etc.)
	// are restored before the game continues past the patched call site.
	//
	// Non-VR (SE/AE): set ctx.Rax = 0 so the conditional between 107133+0x192 and
	// +0x1AE skips "call [r8+0x50]" — r8 is loaded from rax there; if rax != 0,
	// r8 gets a stale pointer whose [+0x50] slot is null -> crash at execute 0x0.
	static void Hook_RenderShadowLights(CONTEXT& ctx)
	{
		if (!REL::Module::IsVR())
			ctx.Rax = 0;
		RenderScheduledShadowLights();
	};

	// Hook struct for stl::detour_thunk
	struct Hook_CalculateActiveShadowCasters
	{
		static void thunk() { ScheduleShadowCasters(); }
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// =========================================================================
	// Surface lights hook
	// Replaces CalculateActiveNonShadowCasterLights (ID 100997/107784).
	// Uses install_context_hook because the function has 10 args (11 in VR)
	// with VR-specific stack layout — CONTEXT is the simplest cross-runtime approach.
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
			for (int i = 0; i < s_settings.ShadowLightCount && added < maxCount; i++) {
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

	// Temporary set of lights that affect the player — populated each frame
	// in Hook_UpdateLightLevelPlayer, consumed in Hook_CheckLightLevelPlayer.
	static std::set<uint64_t> s_stealthDetectionTmp;

	static void* GetUnkDetectionGlobal()
	{
		// SE: 142F6DB98 — a ~80-byte detection struct; GetSingleton equivalent
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
	// ctx.Rip += 0x16 skips 0x16 bytes past the hook site — correct.
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

		int total = std::max(4, settings.ShadowLightCount) + 1 + settings.ConvertedShadowSlots;  // +1 for sun at slot 0
		s_lights.Size = total;
		s_lights.Sun = false;
		s_lights.Lights = static_cast<LightEntry*>(calloc(total, sizeof(LightEntry)));
		for (int i = 0; i < total; i++)
			s_lights.Lights[i].Index = i;

		// Parse formula strings
		if (!settings.ScoreFormula.empty()) {
			s_formulaScore = new FormulaHelper();
			if (!s_formulaScore->Parse(settings.ScoreFormula))
				logger::error("[SCM] Failed to parse ScoreFormula");
		}
		if (!settings.RedrawIntervalFormula.empty()) {
			s_formulaRedrawInterval = new FormulaHelper();
			if (!s_formulaRedrawInterval->Parse(settings.RedrawIntervalFormula))
				logger::error("[SCM] Failed to parse RedrawIntervalFormula");
		}
		if (!settings.RedrawBudgetFormula.empty()) {
			s_formulaRedrawBudget = new FormulaHelper();
			if (!s_formulaRedrawBudget->Parse(settings.RedrawBudgetFormula))
				logger::error("[SCM] Failed to parse RedrawBudgetFormula");
		}
	}

	void Install(const Settings& settings)
	{
		s_settings = settings;
		s_installedShadowLightCount = settings.ShadowLightCount;

		if (!settings.Enabled) {
			logger::info("[SCM] Shadow caster manager disabled — skipping hook installation.");
			return;
		}

		bool extended = settings.ShadowLightCount > 4;
		bool needExtraBuffers = settings.ShadowLightCount > 8;

		// ---- Extended depth buffer infrastructure -------------------------

		if (needExtraBuffers) {
			globals::features::llf::normalDepthBuffer = static_cast<void**>(calloc(settings.ShadowLightCount + 1, sizeof(void*)));
			globals::features::llf::readOnlyDepthBuffer = static_cast<void**>(calloc(settings.ShadowLightCount + 1, sizeof(void*)));

			// Patch the creation-loop count from 8 to ShadowLightCount.
			// SE/VR: pattern "C7 44 24 68 08 00 00 00" (+4 = the immediate 0x08)
			// AE:    same pattern at different offset
			{
				static REL::RelocationID uid(100458, 107175);
				uintptr_t addr = uid.address() + REL::Relocate(0xD326 - 0xC940, 0xBF6 - 0x210, 0xc91);
				int immOff = REL::Relocate(4, 4, 3);
				uint8_t newCount = static_cast<uint8_t>(settings.ShadowLightCount);
				REL::safe_write(addr + immOff, &newCount, 1);
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
			// Original bytes: "41 83 CE FF 33 C0" (6 bytes) — keep them running first.
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
		//   - IsShadowLight vtable slot 3: when ConvertExcessToNormal
		//   - RemoveLight hook: when ConvertExcessToNormal || PromoteNormalToShadow
		//   - AddLight hook: always (portal-strict is unconditional; also handles PromoteNormalToShadow)
		//   - SetLight hook: when PromoteNormalToShadow

		if (settings.ConvertExcessToNormal || settings.PromoteNormalToShadow) {
			// BSShadowLight vtable slot 3 = IsShadowLight; replace on all 4 shadow light types.
			REL::Relocation<uintptr_t> vtbl1{ RE::BSShadowLight::VTABLE[0] };
			vtbl1.write_vfunc(3, Hook_IsShadowLight);
			REL::Relocation<uintptr_t> vtbl2{ RE::BSShadowDirectionalLight::VTABLE[0] };
			vtbl2.write_vfunc(3, Hook_IsShadowLight);
			REL::Relocation<uintptr_t> vtbl3{ RE::BSShadowFrustumLight::VTABLE[0] };
			vtbl3.write_vfunc(3, Hook_IsShadowLight);
			REL::Relocation<uintptr_t> vtbl4{ RE::BSShadowParabolicLight::VTABLE[0] };
			vtbl4.write_vfunc(3, Hook_IsShadowLight);
		}

		if (settings.ConvertExcessToNormal || settings.PromoteNormalToShadow) {
			// ShadowSceneNode::RemoveLight — fires at +0x9 (SE: 6 bytes, AE: 5 bytes)
			static REL::RelocationID uid(99697, 106331);
			int sz = REL::Relocate(6, 5, 6);
			if (!SKSE::stl::install_context_hook(uid.address() + REL::Relocate(0x9, 0x9, 0x9), sz, Hook_ConvertLights_Remove, sz))
				logger::error("[SCM] Failed to install Hook_ConvertLights_Remove");
		}

		{
			// ShadowSceneNode::AddLight — at function start (5 bytes)
			static REL::RelocationID uid(99692, 106326);
			if (!SKSE::stl::install_context_hook(uid.address(), 5, Hook_ConvertLights_Add, 5))
				logger::error("[SCM] Failed to install Hook_ConvertLights_Add");
		}

		if (settings.PromoteNormalToShadow) {
			// BSLight::SetLight — at function start (5 bytes)
			static REL::RelocationID uid(101302, 108289);
			if (!SKSE::stl::install_context_hook(uid.address(), 5, Hook_ConvertLights_SetLight, 5))
				logger::error("[SCM] Failed to install Hook_ConvertLights_SetLight");
		}

		logger::info("[SCM] Hooks installed (ShadowLightCount={})", settings.ShadowLightCount);
	}

	void Update(const Settings& settings, RE::ShadowSceneNode* /*shadowSceneNode*/,
		RE::NiCamera* /*worldCamera*/)
	{
		Settings capped = settings;
		if (s_installedShadowLightCount > 0)
			capped.ShadowLightCount = std::min(settings.ShadowLightCount, s_installedShadowLightCount);

		int newTotal = std::max(4, capped.ShadowLightCount) + capped.ConvertedShadowSlots;
		if (newTotal != s_lights.Size) {
			auto* newLights = static_cast<LightEntry*>(calloc(newTotal, sizeof(LightEntry)));
			int copyCount = std::min(s_lights.Size, newTotal);
			for (int i = 0; i < copyCount; i++)
				newLights[i] = s_lights.Lights[i];
			for (int i = copyCount; i < newTotal; i++)
				newLights[i].Index = i;
			free(s_lights.Lights);
			s_lights.Lights = newLights;
			s_lights.Size = newTotal;
		}
		s_settings = capped;
	}

	const LightContainer& GetLights()
	{
		return s_lights;
	}

	// =========================================================================
	// Per-slot visualization state (owned by ShadowCasterManager)
	// =========================================================================

	static constexpr const char* kShadowTypeNames[] = { "Spot", "Hemisphere", "Omni" };

	static std::vector<ShadowSlotInfo> s_shadowSlotInfos;
	static uint32_t s_shadowSlotUsage = 0;
	static std::unordered_set<uintptr_t> s_suppressedLights;
	// Persists last-seen ShadowSlotInfo for every light ever recorded this session,
	// so suppressed lights that leave the active slots still have metadata for the settings table.
	static std::unordered_map<uintptr_t, ShadowSlotInfo> s_knownLights;

	/// Computes the golden-ratio hue color for a shadow-map slot (matches mode-8 shader).
	static ImVec4 ShadowSlotHueColor(uint32_t slotIdx)
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
		s_shadowSlotUsage++;
		s_knownLights[info.lightKey] = info;
	}

	bool IsSuppressed(uintptr_t lightKey)
	{
		return s_suppressedLights.count(lightKey) > 0;
	}

	bool HasSuppressedLights()
	{
		return !s_suppressedLights.empty();
	}

	uint32_t GetSlotUsage()
	{
		return s_shadowSlotUsage;
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
	// filter by type name/radius/address, sort by any column.
	// Rows are keyed by lightKey (light object pointer) so suppression persists
	// across slot reassignments as the player moves around.
	//
	// compact=true  -> auto-sizes height (up to 15 rows visible)
	// compact=false -> fills available window height (resizable overlay window)
	// showColor     -> adds a golden-ratio hue swatch column (visualization mode 8)
	// =========================================================================

	void DrawShadowLightTable(bool compact, bool showColor, bool sceneOnly)
	{
		struct SlotRow
		{
			uint32_t idx;  // shadow slot index; only meaningful when inScene=true
			bool inScene;  // currently occupies a shadow slot this frame
			ShadowSlotInfo info;
		};

		// Build index of lights currently in scene (slot → info).
		std::unordered_map<uintptr_t, uint32_t> sceneSlot;
		for (uint32_t i = 0; i < static_cast<uint32_t>(s_shadowSlotInfos.size()); ++i)
			if (s_shadowSlotInfos[i].valid)
				sceneSlot[s_shadowSlotInfos[i].lightKey] = i;

		// Build row list.
		std::vector<SlotRow> rows;
		if (sceneOnly) {
			rows.reserve(sceneSlot.size());
			for (auto& [key, idx] : sceneSlot)
				rows.push_back({ idx, true, s_shadowSlotInfos[idx] });
		} else {
			// All scene lights first, then suppressed lights not currently in scene.
			rows.reserve(sceneSlot.size() + s_suppressedLights.size());
			for (auto& [key, idx] : sceneSlot)
				rows.push_back({ idx, true, s_shadowSlotInfos[idx] });
			for (uintptr_t key : s_suppressedLights) {
				if (sceneSlot.count(key))
					continue;
				auto it = s_knownLights.find(key);
				if (it != s_knownLights.end())
					rows.push_back({ 0, false, it->second });
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
		{
			auto allSuppressedOfType = [&](int type) {
				for (auto& r : rows) {
					if (type >= 0 && static_cast<int>(r.info.type) != type)
						continue;
					if (!s_suppressedLights.count(r.info.lightKey))
						return false;
				}
				return true;
			};
			auto toggleType = [&](int type) {
				if (allSuppressedOfType(type)) {
					for (auto& r : rows)
						if (type < 0 || static_cast<int>(r.info.type) == type)
							s_suppressedLights.erase(r.info.lightKey);
				} else {
					for (auto& r : rows)
						if (type < 0 || static_cast<int>(r.info.type) == type)
							s_suppressedLights.insert(r.info.lightKey);
				}
			};
			auto typeButton = [&](const char* label, int type, const char* tooltip) {
				bool allOff = allSuppressedOfType(type);
				ImGui::PushStyleColor(ImGuiCol_Button,
					allOff ? ImVec4(0.35f, 0.35f, 0.35f, 1) : ImVec4(0.15f, 0.5f, 0.15f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
					allOff ? ImVec4(0.5f, 0.5f, 0.5f, 1) : ImVec4(0.2f, 0.7f, 0.2f, 1));
				if (ImGui::SmallButton(label))
					toggleType(type);
				ImGui::PopStyleColor(2);
				if (tooltip && ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", tooltip);
			};
			typeButton("All", -1, nullptr);
			ImGui::SameLine();
			typeButton("Spot", 0, "Toggle all spot/frustum shadow lights");
			ImGui::SameLine();
			typeButton("Hemi", 1, "Toggle all hemisphere shadow lights");
			ImGui::SameLine();
			typeButton("Omni", 2, "Toggle all omni (paraboloid) shadow lights");
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
			ImGui::TextDisabled(sceneOnly ? "filter (type/radius/addr)" : "filter (yes/no/type/radius/addr)");
		}

		// Apply filter.
		std::vector<SlotRow> filteredRows;
		if (s_filterText.empty()) {
			filteredRows = rows;
		} else {
			std::string lower = s_filterText;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
			char addrBuf[16];
			for (auto& r : rows) {
				std::string typeName = kShadowTypeNames[std::min(r.info.type, 2u)];
				std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);
				std::string radStr = std::to_string(static_cast<int>(r.info.radius));
				snprintf(addrBuf, sizeof(addrBuf), "%08x", static_cast<uint32_t>(r.info.lightKey & 0xFFFFFFFF));
				// Scene filter: "t" matches in-scene, "f" matches not in scene.
				const char* statusStr = r.inScene ? "yes" : "no";
				if (typeName.find(lower) != std::string::npos ||
					radStr.find(lower) != std::string::npos ||
					std::string(addrBuf).find(lower) != std::string::npos ||
					(!sceneOnly && lower == statusStr))
					filteredRows.push_back(r);
			}
		}

		// -- Column layout -------------------------------------------------
		// Settings (sceneOnly=false): [toggle] [Status] [Slot] [Addr] [Color?] [Type] [Radius]
		// Overlay  (sceneOnly=true):  [toggle]          [Slot] [Addr] [Color?] [Type] [Radius]
		const bool showStatus = !sceneOnly;
		const int slotColIdx = showStatus ? 2 : 1;
		const int addrColIdx = slotColIdx + 1;
		const int typeColIdx = addrColIdx + (showColor ? 2 : 1);
		const int radColIdx = typeColIdx + 1;

		std::vector<std::string> headers = { "" };
		if (showStatus)
			headers.push_back("In Scene");
		headers.push_back("Slot");
		headers.push_back("Address");
		if (showColor)
			headers.push_back("Color");
		headers.push_back("Type");
		headers.push_back("Radius");

		using SortFn = std::function<bool(const SlotRow&, const SlotRow&, bool)>;
		std::vector<SortFn> sorts(headers.size(), nullptr);
		if (showStatus) {
			sorts[1] = [](const SlotRow& a, const SlotRow& b, bool asc) {
				// Active (not suppressed) sorts before Disabled.
				bool sa = s_suppressedLights.count(a.info.lightKey) > 0;
				bool sb = s_suppressedLights.count(b.info.lightKey) > 0;
				return asc ? sa < sb : sa > sb;
			};
		}
		sorts[slotColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			// Out-of-scene rows (inScene=false) sort after in-scene rows.
			if (a.inScene != b.inScene)
				return a.inScene > b.inScene;
			return asc ? a.idx < b.idx : a.idx > b.idx;
		};
		sorts[addrColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			return asc ? a.info.lightKey < b.info.lightKey : a.info.lightKey > b.info.lightKey;
		};
		sorts[typeColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			return asc ? a.info.type < b.info.type : a.info.type > b.info.type;
		};
		sorts[radColIdx] = [](const SlotRow& a, const SlotRow& b, bool asc) {
			return asc ? a.info.radius < b.info.radius : a.info.radius > b.info.radius;
		};

		ImVec2 outerSize = compact ? ImVec2(0, 0) : ImVec2(0, ImGui::GetContentRegionAvail().y);

		Util::ShowSortedStringTableCustom<SlotRow>(
			"##ShadowLightTbl",
			headers,
			filteredRows,
			slotColIdx,  // default sort: Slot
			true,        // ascending
			sorts,
			[&](int /*rowIdx*/, int col, const SlotRow& row) {
				bool suppressed = s_suppressedLights.count(row.info.lightKey) > 0;
				if (col == 0) {
					ImGui::PushID(static_cast<int>(row.info.lightKey & 0xFFFFFFFF));
					if (!row.inScene)
						ImGui::BeginDisabled();
					ImGui::PushStyleColor(ImGuiCol_Button,
						suppressed ? ImVec4(0.35f, 0.35f, 0.35f, 1) : ImVec4(0.15f, 0.6f, 0.15f, 1));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
						suppressed ? ImVec4(0.5f, 0.5f, 0.5f, 1) : ImVec4(0.2f, 0.75f, 0.2f, 1));
					if (ImGui::SmallButton(suppressed ? "o" : "*"))
						suppressed ? (void)s_suppressedLights.erase(row.info.lightKey) : (void)s_suppressedLights.insert(row.info.lightKey);
					ImGui::PopStyleColor(2);
					if (!row.inScene)
						ImGui::EndDisabled();
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(row.inScene ? (suppressed ? "Re-enable" : "Suppress (hides this light's shadows)") : "Light is not currently in the scene");
					ImGui::PopID();
					return;
				}
				if (suppressed)
					ImGui::BeginDisabled();
				if (showStatus && col == 1) {
					ImGui::TextUnformatted(row.inScene ? "Yes" : "No");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(row.inScene ? "In scene this frame" : "Not in scene");
				} else if (col == slotColIdx) {
					if (row.inScene)
						ImGui::Text("%u", row.idx);
					else
						ImGui::TextDisabled("--");
				} else if (col == addrColIdx) {
					char addrFull[20];
					snprintf(addrFull, sizeof(addrFull), "0x%016llX", static_cast<unsigned long long>(row.info.lightKey));
					ImGui::Selectable(addrFull + 10, false, ImGuiSelectableFlags_None);
					if (ImGui::IsItemClicked())
						ImGui::SetClipboardText(addrFull);
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
				} else if (col == radColIdx) {
					ImGui::Text("%.0f", row.info.radius);
				}
				if (suppressed)
					ImGui::EndDisabled();
			},
			{},
			outerSize);
	}

	void DrawSettings(Settings& settings)
	{
		ImGui::SeparatorText("Shadow Limit Fix");

		// ---- Enable toggle (requires restart) ------------------------------
		ImGui::Checkbox("Enable Shadow Limit Fix", &settings.Enabled);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Extends Skyrim's hard limit of 4 simultaneous shadow-casting lights.\n"
				"Intelligently selects which lights cast shadows each frame based on\n"
				"distance, intensity, and a configurable priority formula.\n\n"
				"Based on Intellightent by meh321.\n"
				"https://www.nexusmods.com/skyrimspecialedition/mods/172423\n\n"
				"Requires a game restart to take effect.");
		if (settings.Enabled != s_settings.Enabled) {
			const auto& theme = Menu::GetSingleton()->GetTheme();
			ImGui::TextColored(theme.StatusPalette.RestartNeeded,
				"Restart required — currently %s.", s_settings.Enabled ? "enabled" : "disabled");
		}

		if (!settings.Enabled)
			ImGui::BeginDisabled();

		// ---- Shadow Light Count (requires restart) -------------------------
		ImGui::SliderInt("Shadow Light Count", &settings.ShadowLightCount, 0, 64);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Maximum simultaneous shadow-casting point/spot lights (directional sun not counted).\n"
				"  0  = scheduler runs but selects no point lights (sun/directional unaffected).\n"
				"  4  = vanilla point light count with intelligent selection.\n"
				"  >4 = extended mode; depth buffer expanded when >8. Max 64.\n"
				"Requires a game restart to take effect.");
		if (settings.ShadowLightCount != s_installedShadowLightCount) {
			const auto& theme = Menu::GetSingleton()->GetTheme();
			ImGui::TextColored(theme.StatusPalette.RestartNeeded,
				"Restart required — current session uses %d lights.", s_installedShadowLightCount);
		}

		// ---- Temporal budget (dynamic) ------------------------------------

		// Auto Budget checkbox + Apply Recommendations button on the same line.
		static constexpr int32_t kAutoRecommendedShadowCount = 32;
		ImGui::Checkbox("Auto Budget", &settings.AutoBudget);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Use the adaptive formula to derive the per-frame shadow redraw budget from spare\n"
				"frame time instead of a fixed value.\n"
				"Recommendation: increase Shadow Light Count to 32+ for best results (requires restart).");
		if (settings.AutoBudget && settings.ShadowLightCount < kAutoRecommendedShadowCount) {
			ImGui::SameLine();
			if (ImGui::Button("Apply Recommendations")) {
				settings.ShadowLightCount = kAutoRecommendedShadowCount;
				settings.MaxRedrawPerFrame = 32;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Sets Shadow Light Count to %d and Max Redraws Per Frame to 32.\n"
					"Shadow Light Count change requires a game restart.",
					kAutoRecommendedShadowCount);
		}

		// Budget progress bar — always visible.
		{
			const float effectiveBudgetMs = s_autoBudgetMs;
			const float avgConsumedUs = static_cast<float>(s_budgetSum) / static_cast<float>(kRedrawHistorySize);
			const float budgetUs = effectiveBudgetMs * 1000.0f;
			const float fraction = budgetUs > 0.0f ? avgConsumedUs / budgetUs : 0.0f;
			char overlay[64];
			snprintf(overlay, sizeof(overlay), "%.2f / %.2f ms", avgConsumedUs / 1000.0f, effectiveBudgetMs);
			ImGui::ProgressBar(std::min(fraction, 1.0f), ImVec2(-1.0f, 0.0f), overlay);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Estimated GPU shadow budget consumed this frame vs. effective budget.");
		}

		if (settings.AutoBudget) {
			ImGui::TextDisabled("Auto budget this frame: %.2f ms -- disable Auto Budget to set manually", s_autoBudgetMs);
		} else {
			ImGui::SliderFloat("Redraw Budget (ms)", &settings.RedrawBudgetMs, 0.1f, 8.0f, "%.2f ms");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Per-frame GPU time budget for shadow re-renders (milliseconds).\n"
					"Lights whose estimated render cost exceeds the remaining budget are deferred.\n"
					"The first eligible light always renders regardless of budget (starvation prevention).\n"
					"Typical values: 1.0 ms exterior, 2.0 ms interior.");
		}
		ImGui::SliderInt("Max Redraws Per Frame", &settings.MaxRedrawPerFrame, 1, 64);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Hard cap on how many shadow lights may re-render their shadow maps in one frame.\n"
				"Acts as a safety valve regardless of budget — the budget controls time spent,\n"
				"this controls count. The sun directional light always counts as one redraw.");

		// ---- Light conversion (requires restart for hooks) -----------------
		if (ImGui::TreeNode("Light Conversion##LightConv")) {
			ImGui::Checkbox("Convert Excess Lights to Normal", &settings.ConvertExcessToNormal);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Shadow lights that exceed the active shadow caster limit are demoted to\n"
					"normal (unshadowed) lights so they still contribute diffuse and specular\n"
					"lighting at no shadow-map cost. Lights that fail culling are dropped entirely.\n"
					"Requires a game restart to change.");

			ImGui::SliderInt("Converted Shadow Slots", &settings.ConvertedShadowSlots, 0, 64);
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

			ImGui::TreePop();
		}

		// ---- Advanced (dynamic) -------------------------------------------
		if (ImGui::TreeNode("Advanced##ShadowScheduling")) {
			ImGui::Checkbox("Allow Immediate Draw for New Lights", &settings.AllowDrawNewLight);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Allow a light just added to the active pool to render its shadow map this frame.\n"
					"Prevents a one-frame shadow-map gap when new lights enter view.");

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
										FormulaHelper*& helper) {
					ImGui::InputText(label, buf, bufSize);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						std::string err;
						if (FormulaHelper::Validate(buf, err)) {
							settingStr = buf;
							errBuf[0] = '\0';
							if (helper)
								helper->Reparse(settingStr);
							else {
								helper = new FormulaHelper();
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

		// ---- Active shadow casters table --------------------------------
		ImGui::SeparatorText("Shadow Limit Fix — Active Casters");
		DrawShadowLightTable(true, false);

		// Redraws/frame and per-light cost — always visible alongside the budget bar above.
		{
			float avgRedraws = static_cast<float>(s_redrawSum) / static_cast<float>(kRedrawHistorySize);
			ImGui::Text("Avg redraws/frame : %.1f  (cap: %d)", avgRedraws, s_settings.MaxRedrawPerFrame);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Rolling average over the last %d frames.", kRedrawHistorySize);

			int32_t avgCost = s_budget.GetAverageCostUs();
			if (avgCost > 0)
				ImGui::Text("Avg light cost    : %.2f ms", avgCost / 1000.0f);
		}

		if (!settings.Enabled)
			ImGui::EndDisabled();
	}
}
