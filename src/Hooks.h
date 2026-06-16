#pragma once

namespace Hooks
{
	/** @brief Hook for BSShader::BeginTechnique that intercepts shader selection to substitute custom-compiled shaders from the shader cache. */
	struct BSShader_BeginTechnique
	{
		static bool thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook for BSGraphics::SetDirtyStates that triggers per-draw feature updates after the engine applies its own state changes. */
	struct BSGraphics_SetDirtyStates
	{
		static void thunk(bool isCompute);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook for BSBatchRenderer::RenderPassImmediately that intercepts individual render pass dispatch. */
	struct BSBatchRenderer_RenderPassImmediately1
	{
		static void thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook for Sky::UpdateColors that forwards sky color updates to SkySync after the engine computes them. */
	struct Sky_UpdateColors
	{
		static void thunk(RE::Sky* sky, float a_delta);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Installs all hooks, detours, and memory patches for the rendering pipeline, input handling, and shader management. */
	void Install();

	/** @brief Installs early IAT hooks for D3D11 device/swapchain creation and DXGI factory, before the game initializes Direct3D. */
	void InstallEarlyHooks();
}
