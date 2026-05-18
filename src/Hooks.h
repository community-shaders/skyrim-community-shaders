#pragma once

namespace Hooks
{
	struct BSShader_BeginTechnique
	{
		static bool thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct BSGraphics_SetDirtyStates
	{
		static void thunk(bool isCompute);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSBatchRenderer_RenderPassImmediately1
	{
		static void thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/**
	 * @brief Append-only registration list of function pointers for a single hook dispatch slot.
	 *
	 * Registration happens during `PostPostLoad` (single-threaded); dispatch is render-thread
	 * read-only, so no synchronization is needed. Function pointers (not `std::function`) keep the
	 * hot path free of allocation and type erasure.
	 */
	template <typename Fn>
	struct HookChain
	{
		std::vector<Fn> callbacks;
		void Register(Fn cb) { callbacks.push_back(cb); }
	};

	/**
	 * @brief Multi-subscriber dispatchers for engine functions that more than one feature wants to wrap.
	 *
	 * Both `TESObjectLAND::SetupMaterial` and `BSLightingShader::SetupMaterial` have multiple
	 * subscribers (currently TruePBR and TerrainHelper). Installing a `detour_thunk` per feature
	 * on the same address corrupts the first trampoline; installing competing `write_vfunc<0x4>`
	 * writes is last-writer-wins. Hooks.cpp owns a single install of each and dispatches through
	 * these slots so features stay decoupled from each other and from Hooks.cpp.
	 *
	 * Semantics (intentionally different between the two functions to preserve historical behavior):
	 *  - `TESObjectLAND::SetupMaterial`: vanilla always runs first; then post callbacks run in
	 *    registration order. A callback returning `true` claims the result (the hook returns `true`
	 *    and any later post callbacks are skipped). TruePBR uses claim to tag PBR land cells (see
	 *    `kPBRProcessedLandFlag` in `TruePBR.h`); TerrainHelper observes only and never claims.
	 *  - `BSLightingShader::SetupMaterial`: interceptors run before vanilla. Returning `true`
	 *    short-circuits — vanilla is skipped, no post callback runs. Post callbacks then run if
	 *    no interceptor claimed. TruePBR is an interceptor (replaces vanilla for PBR materials);
	 *    TerrainHelper is post.
	 */
	namespace MaterialHooks
	{
		// Return `true` to claim the result; the hook returns `true` and the rest of the chain is skipped.
		using TESObjectLANDPostCallback = bool (*)(RE::TESObjectLAND* land, bool vanillaResult);
		// Return `true` to skip vanilla (and the post chain).
		using BSLightingShaderInterceptor = bool (*)(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material);
		using BSLightingShaderPostCallback = void (*)(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material);

		HookChain<TESObjectLANDPostCallback>& TESObjectLANDPost();
		HookChain<BSLightingShaderInterceptor>& BSLightingShaderInterceptors();
		HookChain<BSLightingShaderPostCallback>& BSLightingShaderPost();
	}

	void Install();
	void InstallEarlyHooks();
}
