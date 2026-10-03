#pragma once

#include <d3d11.h>

#include <array>
#include <initializer_list>

struct ID3D11DeviceContext;
struct ID3D11VertexShader;
struct ID3D11PixelShader;

namespace PostProcessingRaster
{
	/**
	 * @brief RAII scope for the post-processing fullscreen-triangle raster passes.
	 *
	 * The per-pixel pipeline stages run as vertex + pixel shader fullscreen-triangle
	 * draws. This helper switches the pipeline from whatever the game or a neighbouring
	 * compute pass left behind into the state a fullscreen triangle draw needs, and
	 * restores everything on destruction:
	 *
	 *   - OM: render targets + DSV, blend state, depth-stencil state
	 *   - RS: rasterizer state, viewports, scissor rects
	 *   - IA: primitive topology + input layout (draws use no vertex buffer)
	 *   - VS/HS/DS/GS/PS: shader objects
	 *   - PS: resource slots 0-5, constant-buffer slots 0-5, sampler slot 0
	 */
	struct RasterPass
	{
		/**
		 * @brief Saves the pipeline state a raster pass touches and switches
		 *        IA/RS/OM/VS/PS into fullscreen-triangle draw state.
		 * @param a_context  Immediate device context.
		 */
		explicit RasterPass(ID3D11DeviceContext* a_context);

		/**
		 * @brief Restores everything the constructor saved and releases the
		 *        saved state references.
		 */
		~RasterPass();

		RasterPass(const RasterPass&) = delete;
		RasterPass& operator=(const RasterPass&) = delete;

		/**
		 * @brief Binds 1-2 render targets (MRT) and sets the viewport to the
		 *        pass resolution.
		 * @param a_rtvs  Render targets to bind.
		 * @param a_width  Viewport width in pixels.
		 * @param a_height  Viewport height in pixels.
		 */
		void SetTargets(std::initializer_list<ID3D11RenderTargetView*> a_rtvs, float a_width, float a_height);

		/**
		 * @brief Binds the shared fullscreen-triangle vertex shader and the
		 *        pass pixel shader.
		 * @param a_vs  Shared fullscreen-triangle VS owned by PostProcessing.
		 * @param a_ps  Pixel shader of the pass.
		 */
		void SetShaders(ID3D11VertexShader* a_vs, ID3D11PixelShader* a_ps);

		/**
		 * @brief Sets the blend state for subsequent draws (e.g. CODBloom mip
		 *        accumulation).
		 * @param a_blend  Blend state; nullptr restores plain replacement blending.
		 * @param a_blendFactor  Constant blend factor (used with
		 *        D3D11_BLEND_BLEND_FACTOR).
		 * @param a_sampleMask  Sample mask; defaults to all samples enabled.
		 */
		void SetBlendState(ID3D11BlendState* a_blend, const float (&a_blendFactor)[4], UINT a_sampleMask = 0xFFFFFFFFu);

		/**
		 * @brief Issues the fullscreen-triangle draw (3 vertices, no vertex buffer).
		 */
		void Draw();

	private:
		static constexpr UINT kPSSRVCount = 6;
		static constexpr UINT kPSCBCount = 6;
		static constexpr UINT kPSSamplerCount = 1;

		ID3D11DeviceContext* context;

		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* savedDSV = nullptr;
		ID3D11BlendState* savedBlendState = nullptr;
		FLOAT savedBlendFactor[4] = {};
		UINT savedSampleMask = 0;
		ID3D11DepthStencilState* savedDepthStencilState = nullptr;
		UINT savedStencilRef = 0;
		ID3D11RasterizerState* savedRasterizerState = nullptr;
		UINT savedViewportCount = 0;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> savedViewports = {};
		UINT savedScissorCount = 0;
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> savedScissors = {};
		D3D11_PRIMITIVE_TOPOLOGY savedTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11InputLayout* savedInputLayout = nullptr;
		ID3D11VertexShader* savedVS = nullptr;
		ID3D11HullShader* savedHS = nullptr;
		ID3D11DomainShader* savedDS = nullptr;
		ID3D11GeometryShader* savedGS = nullptr;
		ID3D11PixelShader* savedPS = nullptr;
		std::array<ID3D11ShaderResourceView*, kPSSRVCount> savedPSSRVs = {};
		std::array<ID3D11Buffer*, kPSCBCount> savedPSCBs = {};
		std::array<ID3D11SamplerState*, kPSSamplerCount> savedPSSamplers = {};
	};
}
