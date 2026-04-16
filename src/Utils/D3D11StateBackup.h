#pragma once

#include <d3d11.h>

namespace Util
{
	/**
	 * @brief RAII helper that snapshots the full D3D11 immediate-context pipeline
	 *        state on construction and restores it on destruction.
	 *
	 * Captured stages: OM (RTVs, DSV, blend, depth-stencil), RS (state, viewports),
	 * IA (layout, topology, VBs, IB), VS, PS, GS, HS, DS, and PS SRVs/samplers/CBs.
	 */
	class D3D11StateBackup
	{
	public:
		explicit D3D11StateBackup(ID3D11DeviceContext* a_context);
		~D3D11StateBackup();

		// Non-copyable, non-movable
		D3D11StateBackup(const D3D11StateBackup&) = delete;
		D3D11StateBackup& operator=(const D3D11StateBackup&) = delete;
		D3D11StateBackup(D3D11StateBackup&&) = delete;
		D3D11StateBackup& operator=(D3D11StateBackup&&) = delete;

	private:
		void Save();
		void Restore();
		void Release();

		ID3D11DeviceContext* context = nullptr;

		// Output Merger
		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* dsv = nullptr;
		ID3D11BlendState* blendState = nullptr;
		FLOAT blendFactor[4] = {};
		UINT sampleMask = 0;
		ID3D11DepthStencilState* depthStencilState = nullptr;
		UINT stencilRef = 0;

		// Rasterizer
		ID3D11RasterizerState* rasterizerState = nullptr;
		D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
		UINT numViewports = 0;

		// Input Assembler
		ID3D11InputLayout* inputLayout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11Buffer* vertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		ID3D11Buffer* indexBuffer = nullptr;
		DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
		UINT indexOffset = 0;

		// Shaders
		ID3D11VertexShader* vs = nullptr;
		ID3D11PixelShader* ps = nullptr;
		ID3D11GeometryShader* gs = nullptr;
		ID3D11HullShader* hs = nullptr;
		ID3D11DomainShader* ds = nullptr;

		// Pixel shader resources
		static constexpr UINT kMaxPSSRVSlots = 8;
		static constexpr UINT kMaxPSSamplerSlots = 4;
		static constexpr UINT kMaxPSCBSlots = 8;  // covers SharedData(b5), FeatureData(b6), Permutation(b4)

		ID3D11ShaderResourceView* psSRVs[kMaxPSSRVSlots] = {};
		ID3D11SamplerState* psSamplers[kMaxPSSamplerSlots] = {};
		ID3D11Buffer* psCBs[kMaxPSCBSlots] = {};
	};

	/**
	 * @brief RAII helper for executing fullscreen pixel-shader passes.
	 *
	 * On construction: backs up all D3D11 state, then configures the pipeline
	 * for fullscreen rendering (shared vertex shader, no vertex buffers,
	 * screen-sized viewport, depth testing disabled, unused shader stages cleared).
	 *
	 * The caller only needs to bind their pixel shader, SRVs, CBs, and render
	 * target before calling Draw().
	 *
	 * On destruction: restores the previously captured state.
	 *
	 * Usage:
	 *   {
	 *       Util::FullscreenPass pass(context);
	 *
	 *       context->PSSetShader(myHorizontalBlurPS, nullptr, 0);
	 *       context->PSSetShaderResources(0, count, srvs);
	 *       context->PSSetConstantBuffers(1, 1, &cb);
	 *       context->OMSetRenderTargets(1, &tempRTV, nullptr);
	 *       pass.Draw();
	 *
	 *       // unbind SRVs between passes to avoid read/write hazard
	 *       context->PSSetShaderResources(0, count, nullSRVs);
	 *
	 *       context->PSSetShader(myVerticalBlurPS, nullptr, 0);
	 *       context->PSSetShaderResources(0, count, srvs2);
	 *       context->OMSetRenderTargets(1, &outputRTV, nullptr);
	 *       pass.Draw();
	 *   }   // all state restored
	 */
	class FullscreenPass
	{
	public:
		explicit FullscreenPass(ID3D11DeviceContext* a_context);
		~FullscreenPass() = default;  // D3D11StateBackup handles restore

		/// Draws the fullscreen triangle (3 vertices, procedurally generated in VS).
		void Draw();

		// Non-copyable, non-movable
		FullscreenPass(const FullscreenPass&) = delete;
		FullscreenPass& operator=(const FullscreenPass&) = delete;
		FullscreenPass(FullscreenPass&&) = delete;
		FullscreenPass& operator=(FullscreenPass&&) = delete;

		/// Compiles the shared fullscreen vertex shader if not already compiled.
		/// Called automatically by the constructor; may also be called during
		/// feature setup to front-load compilation.
		static void EnsureSharedResources();

		/// Releases the shared vertex shader. Call from ClearShaderCache paths.
		static void ClearSharedResources();

	private:
		D3D11StateBackup stateBackup;
		ID3D11DeviceContext* context = nullptr;

		static ID3D11VertexShader* sharedVS;
	};
}  // namespace Util
