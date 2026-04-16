#include "D3D11StateBackup.h"

#include "State.h"
#include "Utils/D3D.h"

namespace Util
{
	// -----------------------------------------------------------------------
	// D3D11StateBackup
	// -----------------------------------------------------------------------

	D3D11StateBackup::D3D11StateBackup(ID3D11DeviceContext* a_context) :
		context(a_context)
	{
		Save();
	}

	D3D11StateBackup::~D3D11StateBackup()
	{
		Restore();
		Release();
	}

	void D3D11StateBackup::Save()
	{
		// Output Merger
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
		context->OMGetBlendState(&blendState, blendFactor, &sampleMask);
		context->OMGetDepthStencilState(&depthStencilState, &stencilRef);

		// Rasterizer
		context->RSGetState(&rasterizerState);
		numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		context->RSGetViewports(&numViewports, viewports);

		// Input Assembler
		context->IAGetInputLayout(&inputLayout);
		context->IAGetPrimitiveTopology(&topology);
		context->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertexBuffers, strides, offsets);
		context->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);

		// Shaders
		context->VSGetShader(&vs, nullptr, nullptr);
		context->PSGetShader(&ps, nullptr, nullptr);
		context->GSGetShader(&gs, nullptr, nullptr);
		context->HSGetShader(&hs, nullptr, nullptr);
		context->DSGetShader(&ds, nullptr, nullptr);

		// Pixel shader resources
		context->PSGetShaderResources(0, kMaxPSSRVSlots, psSRVs);
		context->PSGetSamplers(0, kMaxPSSamplerSlots, psSamplers);
		context->PSGetConstantBuffers(0, kMaxPSCBSlots, psCBs);
	}

	void D3D11StateBackup::Restore()
	{
		// Output Merger
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
		context->OMSetBlendState(blendState, blendFactor, sampleMask);
		context->OMSetDepthStencilState(depthStencilState, stencilRef);

		// Rasterizer
		context->RSSetState(rasterizerState);
		context->RSSetViewports(numViewports, viewports);

		// Input Assembler
		context->IASetInputLayout(inputLayout);
		context->IASetPrimitiveTopology(topology);
		context->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertexBuffers, strides, offsets);
		context->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);

		// Shaders
		context->VSSetShader(vs, nullptr, 0);
		context->PSSetShader(ps, nullptr, 0);
		context->GSSetShader(gs, nullptr, 0);
		context->HSSetShader(hs, nullptr, 0);
		context->DSSetShader(ds, nullptr, 0);

		// Pixel shader resources
		context->PSSetShaderResources(0, kMaxPSSRVSlots, psSRVs);
		context->PSSetSamplers(0, kMaxPSSamplerSlots, psSamplers);
		context->PSSetConstantBuffers(0, kMaxPSCBSlots, psCBs);
	}

	void D3D11StateBackup::Release()
	{
		// Output Merger
		for (auto& rtv : rtvs)
			if (rtv) {
				rtv->Release();
				rtv = nullptr;
			}
		if (dsv) {
			dsv->Release();
			dsv = nullptr;
		}
		if (blendState) {
			blendState->Release();
			blendState = nullptr;
		}
		if (depthStencilState) {
			depthStencilState->Release();
			depthStencilState = nullptr;
		}

		// Rasterizer
		if (rasterizerState) {
			rasterizerState->Release();
			rasterizerState = nullptr;
		}

		// Input Assembler
		if (inputLayout) {
			inputLayout->Release();
			inputLayout = nullptr;
		}
		for (auto& vb : vertexBuffers)
			if (vb) {
				vb->Release();
				vb = nullptr;
			}
		if (indexBuffer) {
			indexBuffer->Release();
			indexBuffer = nullptr;
		}

		// Shaders
		if (vs) {
			vs->Release();
			vs = nullptr;
		}
		if (ps) {
			ps->Release();
			ps = nullptr;
		}
		if (gs) {
			gs->Release();
			gs = nullptr;
		}
		if (hs) {
			hs->Release();
			hs = nullptr;
		}
		if (ds) {
			ds->Release();
			ds = nullptr;
		}

		// Pixel shader resources
		for (auto& srv : psSRVs)
			if (srv) {
				srv->Release();
				srv = nullptr;
			}
		for (auto& sampler : psSamplers)
			if (sampler) {
				sampler->Release();
				sampler = nullptr;
			}
		for (auto& cb : psCBs)
			if (cb) {
				cb->Release();
				cb = nullptr;
			}
	}

	// -----------------------------------------------------------------------
	// FullscreenPass
	// -----------------------------------------------------------------------

	ID3D11VertexShader* FullscreenPass::sharedVS = nullptr;

	void FullscreenPass::EnsureSharedResources()
	{
		if (!sharedVS) {
			logger::debug("Compiling shared FullscreenVS");
			sharedVS = static_cast<ID3D11VertexShader*>(
				Util::CompileShader(L"Data\\Shaders\\Common\\FullscreenVS.hlsl", {}, "vs_5_0"));
		}
	}

	void FullscreenPass::ClearSharedResources()
	{
		if (sharedVS) {
			sharedVS->Release();
			sharedVS = nullptr;
		}
	}

	FullscreenPass::FullscreenPass(ID3D11DeviceContext* a_context) :
		stateBackup(a_context), context(a_context)
	{
		EnsureSharedResources();

		// Input assembler: procedural fullscreen triangle, no buffers needed
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Vertex shader
		context->VSSetShader(sharedVS, nullptr, 0);

		// Clear unused shader stages
		context->GSSetShader(nullptr, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);

		// Viewport sized to screen
		D3D11_VIEWPORT viewport = {};
		viewport.Width = static_cast<FLOAT>(globals::state->screenSize.x);
		viewport.Height = static_cast<FLOAT>(globals::state->screenSize.y);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Disable depth testing
		context->OMSetDepthStencilState(nullptr, 0);
	}

	void FullscreenPass::Draw()
	{
		context->Draw(3, 0);
	}
}  // namespace Util
