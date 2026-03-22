// ShadowCasterManager.cpp
// Shadow caster scheduling for LightLimitFix.
//
// Based on Intellightent by meh321
//   https://www.nexusmods.com/skyrimspecialedition/mods/172423
//
// Ported and adapted for Community Shaders by the Community Shaders team with permission.

#include "ShadowCasterManager.h"

#include <exprtk.hpp>

// Convenience: cast an address to a writable byte pointer.
static void* ToPtr(uintptr_t a) { return reinterpret_cast<void*>(a); }

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

	static void InitFormulaSystem()
	{
		if (s_formulaInited)
			return;
		s_formulaInited = true;

		memset(s_formulaParams, 0, sizeof(double) * kFormulaParam_Max);

		double* p = s_formulaParams;
		s_symbolTable.add_variable("lightindex", p[kFormulaParam_LightIndex]);
		s_symbolTable.add_variable("lightdistance", p[kFormulaParam_LightDistance]);
		s_symbolTable.add_variable("lightintensity", p[kFormulaParam_LightIntensity]);
		s_symbolTable.add_variable("lightradius", p[kFormulaParam_LightRadius]);
		s_symbolTable.add_variable("lightx", p[kFormulaParam_LightX]);
		s_symbolTable.add_variable("lighty", p[kFormulaParam_LightY]);
		s_symbolTable.add_variable("lightz", p[kFormulaParam_LightZ]);
		s_symbolTable.add_variable("lightr", p[kFormulaParam_LightR]);
		s_symbolTable.add_variable("lightg", p[kFormulaParam_LightG]);
		s_symbolTable.add_variable("lightb", p[kFormulaParam_LightB]);
		s_symbolTable.add_variable("lightambientr", p[kFormulaParam_LightAmbientR]);
		s_symbolTable.add_variable("lightambientg", p[kFormulaParam_LightAmbientG]);
		s_symbolTable.add_variable("lightambientb", p[kFormulaParam_LightAmbientB]);
		s_symbolTable.add_variable("lightchosenlastframe", p[kFormulaParam_LightChosenLastFrame]);
		s_symbolTable.add_variable("lightneverfades", p[kFormulaParam_LightNeverFades]);
		s_symbolTable.add_variable("lightportalstrict", p[kFormulaParam_LightPortalStrict]);
		s_symbolTable.add_variable("lightns", p[kFormulaParam_LightNS]);
		s_symbolTable.add_variable("lightconverted", p[kFormulaParam_LightConverted]);
		s_symbolTable.add_variable("camerax", p[kFormulaParam_CameraX]);
		s_symbolTable.add_variable("cameray", p[kFormulaParam_CameraY]);
		s_symbolTable.add_variable("cameraz", p[kFormulaParam_CameraZ]);
		s_symbolTable.add_variable("isinterior", p[kFormulaParam_IsInterior]);
		s_symbolTable.add_variable("timeofday", p[kFormulaParam_TimeOfDay]);
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

	void FormulaHelper::SetParam(int32_t index, double value) { s_formulaParams[index] = value; }
	double FormulaHelper::GetParam(int32_t index) { return s_formulaParams[index]; }

	// =========================================================================
	// Module-level state
	// =========================================================================

	static Settings s_settings;
	static LightContainer s_lights;
	static BudgetTracker s_budget;
	// Maximum ShadowLightCount the installed infrastructure supports.
	// Set once by Install(); Update() clamps to this.
	static int32_t s_installedShadowLightCount;

	// Formula instances (allocated at Init if formula strings are non-empty)
	static FormulaHelper* s_formulaScore = nullptr;
	static FormulaHelper* s_formulaAllowConvert = nullptr;
	static FormulaHelper* s_formulaAllowConvertShadow = nullptr;
	static FormulaHelper* s_formulaRedrawInterval = nullptr;
	static FormulaHelper* s_formulaRedrawBudget = nullptr;

	// Phase 4: lights converted to normal (non-shadow) lights
	struct ConvertedLight
	{
		RE::BSShadowLight* light;
		bool isNS;
	};
	static std::vector<ConvertedLight> s_normalConvert;
	static std::set<RE::NiLight*> s_shadowConvert;

	void** g_normalDepthBuffer = nullptr;
	void** g_readOnlyDepthBuffer = nullptr;

	// =========================================================================
	// Context-hook infrastructure
	// Adapts Intellightent's CONTEXT-capture hook pattern to use SKSE's
	// trampoline allocator instead of a private VirtualAlloc pool.
	//
	// Each installed context hook writes a JMP5 from the patch address to a
	// generated stub that:
	//   1. Calls RtlCaptureContext to snapshot all registers.
	//   2. Calls the user's hook(CONTEXT&) function.
	//   3. Uses RtlRestoreContext to reload (potentially modified) registers
	//      and resumes execution after the patched bytes.
	// =========================================================================
	namespace ContextHook
	{
		// Signature for a context hook callback.
		using Delegate = void (*)(CONTEXT&);

		// Called by the generated stub.  Invokes the user delegate then
		// restores registers via RtlRestoreContext so execution continues at
		// `resumeAddr`.
		static void Execute(CONTEXT* ctx, Delegate func, void* resumeAddr)
		{
			// Fix up Rsp, Rcx, Rax, EFlags from the pushed values above the CONTEXT.
			// The stub pushes (in order): rsp, rax, rcx, rflags, then a sentinel.
			// Sentinel is 0 (aligned) or [1,0] (not aligned), then the saved values.
			auto* after = reinterpret_cast<DWORD64*>(reinterpret_cast<uint8_t*>(ctx) + sizeof(CONTEXT));
			if (after[0] == 0)
				after += 1;  // skip type-0 sentinel
			else             // == 1
				after += 2;  // skip type-1 and 0 sentinels
			ctx->EFlags = static_cast<DWORD>(after[0]);
			ctx->Rcx = after[1];
			ctx->Rax = after[2];
			ctx->Rsp = after[3];
			ctx->Rip = reinterpret_cast<DWORD64>(resumeAddr);
			func(*ctx);
			RtlRestoreContext(ctx, nullptr);
		}

		// Writes a context-capture stub into trampoline memory, then installs
		// a JMP5 (or NOPs over `patchSize` bytes) from `patchAddr` to it.
		//
		//  patchAddr   - address of the instructions to replace
		//  patchSize   - bytes to overwrite (must be >= 5)
		//  func        - callback that receives and may modify the CONTEXT
		//  includeSize - if > 0: copy `includeSize` original bytes BEFORE the
		//                context stub so they run first (original prologue kept).
		//                if < 0: copy after (original epilogue kept).
		//                0: replace entirely.
		static bool Install(uintptr_t patchAddr, int patchSize, Delegate func, int includeSize = 0)
		{
			if (patchSize < 5) {
				logger::error("[SCM] ContextHook::Install: patchSize {} < 5", patchSize);
				return false;
			}

			void* resumeAddr = ToPtr(patchAddr + patchSize);

			// Include bytes that need to run alongside our stub.
			std::vector<uint8_t> includedBytes;
			if (includeSize != 0) {
				includedBytes.resize(std::abs(includeSize));
				memcpy(includedBytes.data(), ToPtr(patchAddr), std::abs(includeSize));
			}

			// ------------------------------------------------------------------
			// Build the context-capture stub:
			//
			//   [optional included bytes if includeSize > 0]
			//   push rsp              ; save original RSP before alignment
			//   push rax              ; scratch
			//   push rcx              ; scratch
			//   pushfq                ; flags
			//   sub rsp, sizeof(CONTEXT)
			//   mov rcx, rsp          ; arg1 = CONTEXT*
			//   mov rax, RtlCaptureContext
			//   call rax
			//   mov rcx, rsp          ; arg1 = CONTEXT* (again after call)
			//   mov rdx, func         ; arg2 = user delegate
			//   mov r8,  resumeAddr   ; arg3 = resume address
			//   sub rsp, 0x20         ; shadow space
			//   mov rax, Execute
			//   call rax
			//   ; Execute calls RtlRestoreContext which never returns here.
			//   [optional included bytes if includeSize < 0]
			//   [absolute jump back to resumeAddr]
			// ------------------------------------------------------------------

			// We use a simple flat byte array approach matching Intellightent's
			// mcode pattern, patching in the three absolute addresses at known offsets.
			uint8_t stub[] = {
				// push rsp          (offset 0)
				0x54,
				// push rax          (offset 1)
				0x50,
				// push rcx          (offset 2)
				0x51,
				// pushfq            (offset 3)
				0x9C,
				// -- stack alignment sentinel (24 bytes, offsets 4-27) --
				// xor rax, rax      (offset 4)
				0x48,
				0x31,
				0xC0,
				// push rax          (offset 7)  ; push 0 (type-0 sentinel)
				0x50,
				// mov rax, rsp      (offset 8)
				0x48,
				0x89,
				0xE0,
				// and rax, ~0xF     (offset 11)
				0x48,
				0x83,
				0xE0,
				0xF0,
				// cmp rax, rsp      (offset 15)
				0x48,
				0x39,
				0xE0,
				// je +8             (offset 18)  ; jump to offset 28 (sub rsp)
				0x74,
				0x08,
				// mov rax, 1        (offset 20)
				0x48,
				0xC7,
				0xC0,
				0x01,
				0x00,
				0x00,
				0x00,
				// push rax          (offset 27)  ; push 1 (type-1 sentinel, not aligned)
				0x50,
				// -- continue (offset 28) --
				// sub rsp, sizeof(CONTEXT)  (0x4D0 = 1232)  (offset 28)
				0x48,
				0x81,
				0xEC,
				0xD0,
				0x04,
				0x00,
				0x00,
				// mov rcx, rsp      (offset 35)
				0x48,
				0x89,
				0xE1,
				// movabs rax, RtlCaptureContext  (offset 38, imm64 at +2 = offset 40)
				0x48,
				0xB8,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				// call rax          (offset 48)
				0xFF,
				0xD0,
				// mov rcx, rsp      (offset 50)
				0x48,
				0x89,
				0xE1,
				// sub rsp, 0x20     (offset 53)
				0x48,
				0x83,
				0xEC,
				0x20,
				// movabs rdx, func  (offset 57, imm64 at +2 = offset 59)
				0x48,
				0xBA,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				// movabs r8, resumeAddr  (offset 67, imm64 at +2 = offset 69)
				0x49,
				0xB8,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				// movabs rax, Execute    (offset 77, imm64 at +2 = offset 79)
				0x48,
				0xB8,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				0x00,
				// call rax          (offset 87)
				0xFF,
				0xD0,
			};

			// Fill in absolute addresses at known offsets within the stub.
			auto write64 = [&](int off, uint64_t val) {
				memcpy(stub + off, &val, 8);
			};

			// &RtlCaptureContext under MSVC x64 dllimport gives the IAT slot address
			// (non-executable .idata data), not the callable function.  Use
			// GetProcAddress to obtain the actual ntdll entry point.
			static const auto s_rtlCaptureContext = reinterpret_cast<uint64_t>(
				GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCaptureContext"));
			write64(40, s_rtlCaptureContext);
			write64(59, reinterpret_cast<uint64_t>(func));
			write64(79, reinterpret_cast<uint64_t>(&Execute));

			// Allocate space in SKSE's trampoline.
			int totalSize = (int)std::abs(includeSize) + (int)sizeof(stub);
			if (includeSize < 0) {
				totalSize += 14;  // space for absolute jump back
			}
			auto& trampoline = SKSE::GetTrampoline();
			auto cave = static_cast<uint8_t*>(trampoline.allocate(totalSize));
			if (!cave) {
				logger::error("[SCM] ContextHook::Install: trampoline out of space");
				return false;
			}

			// If includeSize < 0, the resume address passed to Execute is the
			// start of the copied original bytes, which will execute and then jump back.
			void* afterStub = cave + sizeof(stub);
			if (includeSize < 0) {
				write64(69, reinterpret_cast<uint64_t>(afterStub));
			} else {
				write64(69, reinterpret_cast<uint64_t>(resumeAddr));
			}

			// Write included bytes before or after the stub.
			uint8_t* writePtr = cave;
			if (includeSize > 0) {
				memcpy(writePtr, includedBytes.data(), includeSize);
				writePtr += includeSize;
			}
			memcpy(writePtr, stub, sizeof(stub));
			if (includeSize < 0) {
				writePtr += sizeof(stub);
				memcpy(writePtr, includedBytes.data(), -includeSize);
				writePtr += -includeSize;

				// Write an absolute JMP to resumeAddr
				uint8_t jmp[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
				auto resumeAddr64 = reinterpret_cast<uint64_t>(resumeAddr);
				memcpy(jmp + 6, &resumeAddr64, 8);
				memcpy(writePtr, jmp, 14);
			}

			// Write JMP5 from patchAddr to cave, NOP remaining bytes.
			SKSE::GetTrampoline().write_branch<5>(patchAddr, reinterpret_cast<uintptr_t>(cave));
			if (patchSize > 5) {
				std::vector<uint8_t> nops(patchSize - 5, 0x90);
				REL::safe_write(patchAddr + 5, nops.data(), nops.size());
			}

			return true;
		}
	}

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
	// Phase 1a: Expanded accumulated-lights array
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
	// Phase 1b: Redirect depth-stencil-view creation to our extended arrays
	// The game loops 0..7 creating depth stencil views and stores each pointer
	// in a game-managed struct at R9.  We redirect R9 to our own arrays so
	// views >= 8 land in g_normalDepthBuffer / g_readOnlyDepthBuffer.
	// -------------------------------------------------------------------------
	static void Hook_CreateNormalDepthBuffer(CONTEXT& ctx)
	{
		// R12 (SE/AE) or R13 (VR) holds a_target * 0x13; value 4*19=76 identifies
		// the shadow-map depth target.  RDI (SE) / RBX (AE/VR) is the loop index.
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;
		int idx = (int)REL::Relocate(ctx.Rdi, ctx.Rbx, ctx.Rbx);
		ctx.R9 = reinterpret_cast<DWORD64>(&g_normalDepthBuffer[idx]);
	}

	static void Hook_CreateReadOnlyDepthBuffer(CONTEXT& ctx)
	{
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;
		int idx = (int)REL::Relocate(ctx.Rdi, ctx.Rbx, ctx.Rbx);
		ctx.R9 = reinterpret_cast<DWORD64>(&g_readOnlyDepthBuffer[idx]);
	}

	// -------------------------------------------------------------------------
	// Phase 1c: Copy first 8 views into the game's own DepthStencilData array
	// Called after the creation loop finishes; syncs the game struct so existing
	// code reading depthStencils[4].views[0..7] still works correctly.
	// -------------------------------------------------------------------------
	static void Hook_SetupGameArray(CONTEXT& ctx)
	{
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;
		auto* renderer = reinterpret_cast<RE::BSGraphics::Renderer*>(ctx.R15);
		for (int i = 0; i < 8; i++) {
			renderer->GetDepthStencilData().depthStencils[4].views[i] = reinterpret_cast<ID3D11DepthStencilView*>(g_normalDepthBuffer[i]);
			renderer->GetDepthStencilData().depthStencils[4].readOnlyViews[i] = reinterpret_cast<ID3D11DepthStencilView*>(g_readOnlyDepthBuffer[i]);
		}
	}

	// -------------------------------------------------------------------------
	// Phase 1d: Redirect depth-buffer selection at draw time
	// When the active depth target is type 4 (shadow maps), route sub-index
	// lookups through our extended arrays instead of the game struct.
	// Hook #1: renderer in R8, result → RBX.
	// -------------------------------------------------------------------------
	static void Hook_SelectDepthBuffer1(CONTEXT& ctx)
	{
		auto* data = reinterpret_cast<RE::BSGraphics::RendererData*>(ctx.R8);
		int type = GetDepthTargetType();
		int sub = GetDepthTargetSubIndex();

		if (type == 4) {
			ctx.Rbx = data->readOnlyDepth ? reinterpret_cast<DWORD64>(g_readOnlyDepthBuffer[sub]) : reinterpret_cast<DWORD64>(g_normalDepthBuffer[sub]);
		} else {
			ctx.Rbx = data->readOnlyDepth ? reinterpret_cast<DWORD64>(RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[type].readOnlyViews[sub]) : reinterpret_cast<DWORD64>(RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[type].views[sub]);
		}
	}

	// Hook #2: VR: renderer in R14, result → RBP; SE/AE: renderer in RBP, result → R14.
	static void Hook_SelectDepthBuffer2(CONTEXT& ctx)
	{
		bool isVR = REL::Module::IsVR();
		bool readOnly = isVR ? reinterpret_cast<RE::BSGraphics::Renderer*>(ctx.R14)->GetRuntimeData().readOnlyDepth : reinterpret_cast<RE::BSGraphics::Renderer*>(ctx.Rbp)->GetRuntimeData().readOnlyDepth;

		int type = GetDepthTargetType();
		int sub = GetDepthTargetSubIndex();

		DWORD64 result;
		if (type == 4) {
			result = readOnly ? reinterpret_cast<DWORD64>(g_readOnlyDepthBuffer[sub]) : reinterpret_cast<DWORD64>(g_normalDepthBuffer[sub]);
		} else {
			result = readOnly ? reinterpret_cast<DWORD64>(RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[type].readOnlyViews[sub]) : reinterpret_cast<DWORD64>(RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[type].views[sub]);
		}

		if (isVR)
			ctx.Rbp = result;
		else
			ctx.R14 = result;
	}

	// -------------------------------------------------------------------------
	// Phase 1e: Release extended depth buffers at renderer shutdown
	// -------------------------------------------------------------------------
	static void ReleaseExtendedDepthBuffers(int shadowCount)
	{
		for (int i = 8; i < shadowCount; i++) {
			if (g_normalDepthBuffer[i]) {
				reinterpret_cast<ID3D11DepthStencilView*>(g_normalDepthBuffer[i])->Release();
				g_normalDepthBuffer[i] = nullptr;
			}
			if (g_readOnlyDepthBuffer[i]) {
				reinterpret_cast<ID3D11DepthStencilView*>(g_readOnlyDepthBuffer[i])->Release();
				g_readOnlyDepthBuffer[i] = nullptr;
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
	// Phase 1f: Force each light to use its assigned shadow map slot
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
	// Phase 1g: Color-mask pass skip and overflow fix
	// -------------------------------------------------------------------------

	// ContextHook delegate — replaces the DrawColorMask call in 107140.
	// Must use ContextHook (not write_thunk_call) so RtlRestoreContext preserves
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
			for (int i = 0; i < shadowCount; i++)
				if (!Lights[i].Light)
					return i;
		} else {
			for (int i = shadowCount; i < shadowCount + convertCount; i++)
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

	// =========================================================================
	// Phase 2 — Game accessor helpers
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
		// world scene graph → camera
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

	static int32_t GetFrameCounter()
	{
		static REL::RelocationID uid(525008, 411489);
		return *reinterpret_cast<int32_t*>(uid.address());
	}
	static int GetViewWidth()
	{
		static REL::RelocationID uid(524978, 411459);
		return *reinterpret_cast<int*>(uid.address());
	}
	static int GetViewHeight()
	{
		static REL::RelocationID uid(524979, 411460);
		return *reinterpret_cast<int*>(uid.address());
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
	static bool& GetDrawStereo()
	{
		// g_drawStereo is BSRenderManager singleton pointer + 8 bytes
		static REL::RelocationID uid(524907, 411393);
		return *reinterpret_cast<bool*>(uid.address() + sizeof(void*));
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
	// Phase 2 — Formula helpers
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
	// Phase 2 — Light enable / disable helpers
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
		// Use light->cullingProcess directly — matches Intellightent's OnDecidedToDisable.
		auto* cull = light->cullingProcess;
		if (cull && cull->portalGraphEntry)
			GameClearPortalVisibility(reinterpret_cast<RE::BSPortalGraphEntry*>(cull->portalGraphEntry));
		light->ReturnShadowmaps();
	}

	// Activates a light as a normal (non-shadow) light by inserting it into
	// the scene's active-light list without allocating a shadow slot.
	// Mirrors Intellightent's addFrameConvert + OnDecidedToConvert pair.
	static void ConvertLight(RE::BSShadowLight* light, RE::ShadowSceneNode* ssn, bool isNS)
	{
		// Already converted: just re-enable so geometry picks it up this frame.
		for (auto& c : s_normalConvert) {
			if (c.light == light) {
				GameEnableLight(ssn, light);
				return;
			}
		}

		// Enforce MaxConvertCount for non-isNS lights.
		if (!isNS) {
			int count = 0;
			for (auto& x : s_normalConvert)
				if (!x.isNS)
					count++;
			if (count >= s_settings.MaxConvertCount)
				return;
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
	// Mirrors Intellightent's OnDecidedToEnable with isForCs=true, isSun=false, shadow=true.
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

				float vw = (float)GetViewWidth();
				float vh = (float)GetViewHeight();
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

		// Only apply lens flare if the light has lens flare data — mirrors Intellightent's
		// `if (light->lensFlareData && !IsVR())` guard in OnDecidedToEnable.
		// Calling it unconditionally on parabolic lights (null lensFlareData) registers them
		// into the lens flare system; Main::Draw then crashes in FUN_1414ce790 (AE 107148)
		// when the lens flare pass tries to dereference the null sprite data.
		if (light->lensFlareData)
			GameApplyLensFlare(light);
	}

	// =========================================================================
	// Phase 2 — Main shadow caster scheduler
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
		// Matches Intellightent: if (!REL::Module::IsVR() || GetVRDrawShadowsDisplay())
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

		int wantCount = sunLight ? 1 : 0;

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

		// Update sun slot (slot 0) -- mirrors Intellightent explicit slot-0 update.
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
			int maxRedraw = s_settings.MaxRedrawPerFrame;
			double budget = 2.0;
			int32_t budgetRemain = static_cast<int32_t>(budget * 1000.0);
			bool isFirst = true;
			int32_t now = GetFrameCounter();

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

				if (s_formulaRedrawBudget)
					budget = s_formulaRedrawBudget->Calculate();

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
		// NOTE: ConvertDistantToNormal processing removed for 1:1 Intellightent port.
		// Phase 4 light conversion (ConvertLight) is not in base Intellightent.

		ssn->GetRuntimeData().firstPersonShadowMask = *GetShadowMask();
		*GetFrameLightCount() = static_cast<uint32_t>(doneLightCount);
	}

	// =========================================================================
	// Phase 2 — Render hook: replaces RenderActiveShadowCasterLights
	// Iterates s_lights and calls Render() on lights flagged RedrawFrame.
	// Uses ContextHook at a specific call site in the render loop (see Install()).
	// =========================================================================

	static void RenderScheduledShadowLights()
	{
		// VR: RenderActiveShadowCasterLights normally saves+clears g_drawStereo before
		// iterating shadow casters, then restores it. Without this, each hemisphere
		// render is doubled for both eyes → 4-quadrant shadow map texture.
		bool savedStereo = false;
		if (REL::Module::IsVR()) {
			savedStereo = GetDrawStereo();
			GetDrawStereo() = false;
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
			GetDrawStereo() = savedStereo;
	}

	// ContextHook delegate for Hook_RenderShadowLights — replaces the call to
	// RenderActiveShadowCasterLights.  Using ContextHook (RtlRestoreContext) instead of
	// write_thunk_call is required so that all volatile registers (r8, etc.) are restored
	// to their pre-hook values before the game continues past the patched call site.
	// Intellightent uses the same WriteHook approach for this reason.
	//
	// Non-VR (SE/AE): set ctx.Rax = 0 so the conditional between 107133+0x192 and
	// +0x1AE skips "call [r8+0x50]" — r8 is loaded from rax there; if rax != 0,
	// r8 gets a stale pointer whose [+0x50] slot is null → crash at execute 0x0.
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
	// Phase 2 — Surface lights hook
	// Replaces CalculateActiveNonShadowCasterLights (ID 100997/107784).
	// Uses ContextHook::Install because the function has 10 args (11 in VR)
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
	// Phase 4 — Light conversion hooks
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
	// Optionally promotes normal light to shadow light, or sets portal-strict.
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
		if (s_settings.ForcePortalStrict)
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
	// Phase 4 partial — Stealth detection fix
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
	// RBP-33 holds the player's position (NiPoint3*); Intellightent offset confirmed.
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

		int total = std::max(4, settings.ShadowLightCount) + settings.ConvertedShadowSlots;
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
		if (!settings.AllowConvertFormula.empty()) {
			s_formulaAllowConvert = new FormulaHelper();
			if (!s_formulaAllowConvert->Parse(settings.AllowConvertFormula))
				logger::error("[SCM] Failed to parse AllowConvertFormula");
		}
		if (!settings.AllowConvertShadowFormula.empty()) {
			s_formulaAllowConvertShadow = new FormulaHelper();
			if (!s_formulaAllowConvertShadow->Parse(settings.AllowConvertShadowFormula))
				logger::error("[SCM] Failed to parse AllowConvertShadowFormula");
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

		bool extended = settings.ShadowLightCount > 4;
		bool needExtraBuffers = settings.ShadowLightCount > 8;

		// ---- Phase 1: extended depth buffer infrastructure ------------------

		if (needExtraBuffers) {
			g_normalDepthBuffer = static_cast<void**>(calloc(settings.ShadowLightCount, sizeof(void*)));
			g_readOnlyDepthBuffer = static_cast<void**>(calloc(settings.ShadowLightCount, sizeof(void*)));

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
				if (!ContextHook::Install(base + off, sz, Hook_CreateNormalDepthBuffer, sz))
					logger::error("[SCM] Failed to install Hook_CreateNormalDepthBuffer");
			}
			{
				// ReadOnly DSV creation: SE 140D6AB71 / VR 140DBCA24
				static REL::RelocationID uid(75469, 77255);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0xB71 - 0x9E0, 0x2FC - 0x180, 0x1c4);
				int sz = REL::Relocate(8, 7, 7);
				if (!ContextHook::Install(base + off, sz, Hook_CreateReadOnlyDepthBuffer, sz))
					logger::error("[SCM] Failed to install Hook_CreateReadOnlyDepthBuffer");
			}

			// Sync the first 8 slots into the game's own DepthStencilData array.
			{
				// SE 140D6AC00 / VR 140DBCAB0
				static REL::RelocationID uid(75469, 77255);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0xC00 - 0x9E0, 0x384 - 0x180, 0x250);
				if (!ContextHook::Install(base + off, 8, Hook_SetupGameArray, 8))
					logger::error("[SCM] Failed to install Hook_SetupGameArray");
			}

			// Depth-buffer selection at draw time.
			{
				// SE 140D70444
				static REL::RelocationID uid(75580, 77386);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0x444 - 0x2F0, 0x704 - 0x5B0, 0x1c3);
				if (!ContextHook::Install(base + off, 21, Hook_SelectDepthBuffer1))
					logger::error("[SCM] Failed to install Hook_SelectDepthBuffer1");
			}
			{
				// SE 140D6A1A5 / VR 140DBBFFC
				static REL::RelocationID uid(75462, 77247);
				uintptr_t base = uid.address();
				uintptr_t off = REL::Relocate(0x1A5 - 0x070, 0x985 - 0x850, 0x19c);
				int sz = REL::Relocate(10, 10, 0x2e);
				if (!ContextHook::Install(base + off, sz, Hook_SelectDepthBuffer2))
					logger::error("[SCM] Failed to install Hook_SelectDepthBuffer2");
			}

			// Release extended buffers at renderer shutdown.
			// SE: ZeroDepthStencilData; AE/VR: Renderer::Shutdown and related dtor paths.
			if (REL::Module::GetRuntime() != REL::Module::Runtime::AE) {
				// SE + VR share the same pattern.
				static REL::RelocationID uid(75628, 0 /*AE unused*/);
				uintptr_t addr = uid.address() + (0xE27 - 0xDD0);
				if (!ContextHook::Install(addr, 9, Hook_DeleteDepthBuffers_SE, -9))
					logger::error("[SCM] Failed to install Hook_DeleteDepthBuffers_SE");
			} else {
				// AE has three separate shutdown paths.
				static REL::RelocationID uid1(0, 77228);
				if (!ContextHook::Install(uid1.address() + (0x3195 - 0x2E10), 7, Hook_DeleteDepthBuffers_AE, 7))
					logger::error("[SCM] Failed to install Hook_DeleteDepthBuffers_AE (path 1)");

				static REL::RelocationID uid2(0, 77237);
				if (!ContextHook::Install(uid2.address() + (0x3B8C - 0x34A0), 7, Hook_DeleteDepthBuffers_AE, 7))
					logger::error("[SCM] Failed to install Hook_DeleteDepthBuffers_AE (path 2)");

				static REL::RelocationID uid3(0, 77238);
				if (!ContextHook::Install(uid3.address() + (0x3E79 - 0x3BC0), 6, Hook_DeleteDepthBuffers_AE, -6))
					logger::error("[SCM] Failed to install Hook_DeleteDepthBuffers_AE (path 3)");
			}
		}

		// Expanded accumulated-lights array (needed when ShadowLightCount > 4).
		if (extended) {
			// SE: BSShadowFrustumLight accumulation setup
			static REL::RelocationID uid(99686, 106320);
			uintptr_t base = uid.address();
			uintptr_t off = REL::Relocate(0xFCA4 - 0xF950, 0xF05 - 0xBB0, 0x387);
			if (!ContextHook::Install(base + off, 5, Hook_AccumulatedLightsArray, 5))
				logger::error("[SCM] Failed to install Hook_AccumulatedLightsArray");
		}

		// Force per-light shadow map slot assignment.
		// Required whenever our temporal scheduler is active (ShadowLightCount >= 4):
		// RenderCascade recalculates the slot from a global counter each call; without
		// this hook, a light not redrawn this frame gets a different slot than last
		// frame and corrupts another light's shadow map.  Intellightent installs this
		// unconditionally.
		{
			// SE: RenderCascade+0xBE; VR: RenderCascade+0xE0
			static REL::RelocationID uid(100820, 107604);
			uintptr_t base = uid.address();
			uintptr_t off = REL::Relocate(0xA9E - 0x9E0, 0xDB0 - 0xCF0, 0xe0);
			if (!ContextHook::Install(base + off, 0x25, Hook_OverwriteShadowMapIndex))
				logger::error("[SCM] Failed to install Hook_OverwriteShadowMapIndex");
		}

		// ---- Focus shadow disable (slots 4-7 conflict with extended lights) --
		if (extended) {
			// Patch two "get selected focus shadows" thunks to always return 0.
			// IDs 10209/10247 and 10207/10245 (SE/AE).  VR shares the same IDs.
			// Pattern "8B 05 xx xx xx xx" (MOV EAX, [rip+N]) → "48 31 C0 90 90 90" (XOR RAX,RAX + NOPs)
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
		// Both hooks are installed unconditionally (not just when extended) because
		// our shadow scheduler changes the light/slot state in a way that makes the
		// vanilla color-mask pass crash even at ShadowLightCount=4.
		// Intellightent also installs these regardless of light count.
		{
			// Replace the call to DrawColorMask with our thunk that calls
			// ReturnShadowmaps on each light instead.
			{
				static REL::RelocationID uid(100422, 107140);
				uintptr_t addr = uid.address() + REL::Relocate(0xF20 - 0xE90, 0x67E - 0x600, 0x9e);
				if (!ContextHook::Install(addr, 5, Hook_DisableColorMask))
					logger::error("[SCM] Failed to install Hook_DisableColorMask");
			}
		}

		// ---- Phase 2: shadow caster selection ----------------------------------

		// Replace CalculateActiveShadowCasterLights entirely (ID 100419/107137).
		// VR confirmed: 0x1413226e0
		stl::detour_thunk<Hook_CalculateActiveShadowCasters>(REL::RelocationID(100419, 107137));

		// Replace the CALL to RenderActiveShadowCasterLights inside the render loop.
		// ID 100415/107133; VR confirmed: 0x141322130
		// Offsets: SE = 0xF76-0xE30 (0x146), AE = 0xC17D-0xBFF0 (0x18D), VR = 0x1CA
		// Must use ContextHook (not write_thunk_call) so RtlRestoreContext restores
		// volatile registers (r8, etc.) before the game continues past the call site.
		{
			static REL::RelocationID uid(100415, 107133);
			uintptr_t addr = uid.address() + REL::Relocate(0xF76 - 0xE30, 0xC17D - 0xBFF0, 0x1CA);
			if (!ContextHook::Install(addr, 5, Hook_RenderShadowLights))
				logger::error("[SCM] Failed to install Hook_RenderShadowLights");
		}

		// Replace CalculateActiveNonShadowCasterLights (surface light injection).
		// ID 100997/107784; VR confirmed: 0x141354d20
		// Uses ContextHook because the function has 10 args (11 in VR) with
		// platform-specific stack layout. We write a RET at func+5 so
		// RtlRestoreContext lands on ret and the function returns cleanly.
		{
			static REL::RelocationID uid(100997, 107784);
			if (!ContextHook::Install(uid.address(), 5, Hook_CalculateActiveLightsForSurface))
				logger::error("[SCM] Failed to install Hook_CalculateActiveLightsForSurface");
			const uint8_t ret = 0xC3;
			REL::safe_write(uid.address() + 5, &ret, 1);
		}

		// ---- Phase 4 partial: stealth detection fix -------------------------
		// GetLightLevel (ID 38900/39946) iterates shadow lights to check which
		// affect the player. We replace that iteration with our own.
		// VR: 38900 confirmed (0x1406892e0); offsets assumed same as SE for VR.
		{
			static REL::RelocationID uid(38900, 39946);

			// Hook at the start of the affect-player loop.
			// Original bytes: "41 83 CE FF 33 C0" (6 bytes) — keep them running first.
			uintptr_t off1 = REL::Relocate(0x185 - 0x050, 0x847 - 0x710, 0x185 - 0x050);
			if (!ContextHook::Install(uid.address() + off1, 6, Hook_UpdateLightLevelPlayer, 6))
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
			if (!ContextHook::Install(uid.address() + off, 5, Hook_CheckLightLevelPlayer))
				logger::error("[SCM] Failed to install Hook_CheckLightLevelPlayer");
		}

		// ---- Phase 4: light conversion ----------------------------------------
		// Matches Intellightent Hook_ConvertLights():
		//   - IsShadowLight vtable slot 3: when bTryNormalLight=true (our ConvertDistantToNormal)
		//   - RemoveLight hook: when bTryNormalLight || bTryShadowLight
		//   - AddLight hook: when bTryShadowLight || bForcePortalStrict (our ForcePortalStrict)
		//   - SetLight hook: when bTryShadowLight (our PromoteNormalToShadow)
		// Hook_ConvertLights is called in BOTH simple and extended mode in Intellightent.

		if (settings.ConvertDistantToNormal || settings.PromoteNormalToShadow) {
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

		if (settings.ConvertDistantToNormal || settings.PromoteNormalToShadow) {
			// ShadowSceneNode::RemoveLight — fires at +0x9 (SE: 6 bytes, AE: 5 bytes)
			static REL::RelocationID uid(99697, 106331);
			int sz = REL::Relocate(6, 5, 6);
			if (!ContextHook::Install(uid.address() + REL::Relocate(0x9, 0x9, 0x9), sz, Hook_ConvertLights_Remove, sz))
				logger::error("[SCM] Failed to install Hook_ConvertLights_Remove");
		}

		if (settings.ForcePortalStrict || settings.PromoteNormalToShadow) {
			// ShadowSceneNode::AddLight — at function start (5 bytes)
			static REL::RelocationID uid(99692, 106326);
			if (!ContextHook::Install(uid.address(), 5, Hook_ConvertLights_Add, 5))
				logger::error("[SCM] Failed to install Hook_ConvertLights_Add");
		}

		if (settings.PromoteNormalToShadow) {
			// BSLight::SetLight — at function start (5 bytes)
			static REL::RelocationID uid(101302, 108289);
			if (!ContextHook::Install(uid.address(), 5, Hook_ConvertLights_SetLight, 5))
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

	void DrawSettings(Settings& settings)
	{
		ImGui::SeparatorText("Shadow Caster Scheduling");

		ImGui::SliderInt("Shadow Light Count", &settings.ShadowLightCount, 0, 32);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Maximum simultaneous shadow-casting point/spot lights.\n"
				"  4  = vanilla (intelligent selection still active).\n"
				"  >4 = extended mode; depth buffer is expanded beyond the game's 8-slot limit when >8.\n"
				"  0  = scheduling disabled.\n"
				"Requires a game restart to take effect.");
		if (settings.ShadowLightCount != s_installedShadowLightCount) {
			const auto& theme = Menu::GetSingleton()->GetTheme();
			ImGui::TextColored(theme.StatusPalette.RestartNeeded,
				"Restart required — current session uses %d lights.", s_installedShadowLightCount);
		}

		ImGui::SliderInt("Max Redraws Per Frame", &settings.MaxRedrawPerFrame, 1, 16);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Hard cap on how many shadow lights may re-render their shadow maps in one frame.\n"
				"Lower values save GPU time; higher values keep shadows more up-to-date.\n"
				"The sun directional light always counts as one redraw.");

		ImGui::SliderFloat("Redraw Budget (ms)", &settings.RedrawBudgetMs, 0.1f, 8.0f, "%.2f ms");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Per-frame GPU time budget for shadow re-renders (milliseconds).\n"
				"Lights whose estimated render cost exceeds the remaining budget are deferred.\n"
				"The first eligible light always renders regardless of budget (starvation prevention).\n"
				"Typical values: 1.0 ms exterior, 2.0 ms interior.");

		ImGui::Checkbox("Convert Distant Lights to Normal", &settings.ConvertDistantToNormal);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"When enabled, shadow lights that fail portal/frustum culling are demoted to\n"
				"normal (unshadowed) lights instead of being dropped entirely.\n"
				"They still contribute diffuse and specular lighting at no shadow-map cost.");

		if (ImGui::TreeNode("Advanced##ShadowScheduling")) {
			ImGui::SliderInt("Converted Shadow Slots", &settings.ConvertedShadowSlots, 0, 16);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Extra pool slots reserved for shadow\xe2\x86\x92normal converted lights (Phase 4).");

			ImGui::Checkbox("Allow Immediate Draw for New Lights", &settings.AllowDrawNewLight);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Allow a light that was just added to the active pool to render its shadow map\n"
					"this frame even if it hasn't been seen before.\n"
					"Prevents a one-frame shadow-map gap when new lights enter view.");

			ImGui::Checkbox("Force Portal Strict", &settings.ForcePortalStrict);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Apply portal-strict culling to all managed shadow lights.\n"
					"Prevents lights from shadowing geometry across room boundaries.");

			ImGui::Checkbox("Promote Normal Lights to Shadow Casters", &settings.PromoteNormalToShadow);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Experimental: elevate high-scoring unshadowed lights to shadow casters\nwhen shadow slots are available.");

			ImGui::SliderInt("Max Shadow\xe2\x86\x92Normal Conversions", &settings.MaxConvertCount, 0, 64);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Maximum shadow\xe2\x86\x92normal conversions per frame.");

			ImGui::SliderInt("Max Normal\xe2\x86\x92Shadow Promotions", &settings.MaxConvertCountShadow, 0, 16);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Maximum normal\xe2\x86\x92shadow promotions per frame (requires Promote Normal Lights).");

			if (ImGui::TreeNode("Formula Editor##Formulas")) {
				ImGui::TextWrapped(
					"Formulas use exprtk syntax.  Available variables:\n"
					"lightindex, lightintensity, lightdistance, lightradius, lightx/y/z,\n"
					"lightr/g/b, lightambientr/g/b, lightchosenlastframe, lightneverfades,\n"
					"lightportalstrict, lightns, lightconverted, camerax/y/z, isinterior, timeofday");

				static char scoreBuf[512];
				static char allowConvertBuf[512];
				static char allowConvertShadowBuf[512];
				static char redrawIntervalBuf[512];
				static char redrawBudgetBuf[512];
				static bool formulaBufsInited = false;
				if (!formulaBufsInited) {
					snprintf(scoreBuf, sizeof(scoreBuf), "%s", settings.ScoreFormula.c_str());
					snprintf(allowConvertBuf, sizeof(allowConvertBuf), "%s", settings.AllowConvertFormula.c_str());
					snprintf(allowConvertShadowBuf, sizeof(allowConvertShadowBuf), "%s", settings.AllowConvertShadowFormula.c_str());
					snprintf(redrawIntervalBuf, sizeof(redrawIntervalBuf), "%s", settings.RedrawIntervalFormula.c_str());
					snprintf(redrawBudgetBuf, sizeof(redrawBudgetBuf), "%s", settings.RedrawBudgetFormula.c_str());
					formulaBufsInited = true;
				}

				ImGui::InputText("Score", scoreBuf, sizeof(scoreBuf));
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Light priority scoring formula. Higher score = more likely to get a shadow slot.");
				if (ImGui::IsItemDeactivatedAfterEdit())
					settings.ScoreFormula = scoreBuf;

				ImGui::InputText("Allow Convert", allowConvertBuf, sizeof(allowConvertBuf));
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Per-light filter for shadow-to-normal conversion. Return >= 0.5 to allow. Empty = always allow.");
				if (ImGui::IsItemDeactivatedAfterEdit())
					settings.AllowConvertFormula = allowConvertBuf;

				ImGui::InputText("Allow Convert Shadow", allowConvertShadowBuf, sizeof(allowConvertShadowBuf));
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Per-light filter for normal-to-shadow promotion. Return >= 0.5 to allow. Empty = always allow.");
				if (ImGui::IsItemDeactivatedAfterEdit())
					settings.AllowConvertShadowFormula = allowConvertShadowBuf;

				ImGui::InputText("Redraw Interval", redrawIntervalBuf, sizeof(redrawIntervalBuf));
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Per-light redraw interval formula. Higher = less frequent shadow map updates.");
				if (ImGui::IsItemDeactivatedAfterEdit())
					settings.RedrawIntervalFormula = redrawIntervalBuf;

				ImGui::InputText("Redraw Budget", redrawBudgetBuf, sizeof(redrawBudgetBuf));
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Per-frame redraw budget formula (ms). Overrides the slider value above.");
				if (ImGui::IsItemDeactivatedAfterEdit())
					settings.RedrawBudgetFormula = redrawBudgetBuf;

				ImGui::TreePop();
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Scheduling Statistics")) {
			int active = 0, redrawing = 0;
			for (int i = 0; i < s_lights.Size; i++) {
				if (s_lights.Lights[i].Light) {
					active++;
					if (s_lights.Lights[i].RedrawFrame)
						redrawing++;
				}
			}
			ImGui::Text("Tracked lights : %d / %d slots", active, s_lights.Size);
			ImGui::Text("Redrawing      : %d this frame", redrawing);
			ImGui::TreePop();
		}
	}
}
