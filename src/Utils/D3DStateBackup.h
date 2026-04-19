#pragma once

#include <d3d11.h>

namespace Util
{
	struct D3DStateBackup
	{
		static constexpr UINT kNumSRVSlots = 20;
		static constexpr UINT kNumSamplerSlots = 2;
		static constexpr UINT kNumCBSlots = 14;

		ID3D11InputLayout* iaInputLayout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY iaTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

		ID3D11VertexShader* vs = nullptr;
		ID3D11Buffer* vsCBs[kNumCBSlots] = {};

		ID3D11RasterizerState* rsState = nullptr;
		UINT rsNumViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_VIEWPORT rsViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};

		ID3D11PixelShader* ps = nullptr;
		ID3D11ShaderResourceView* psSRVs[kNumSRVSlots] = {};
		ID3D11SamplerState* psSamplers[kNumSamplerSlots] = {};
		ID3D11Buffer* psCBs[kNumCBSlots] = {};

		ID3D11RenderTargetView* omRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* omDSV = nullptr;
		ID3D11BlendState* omBlendState = nullptr;
		FLOAT omBlendFactor[4] = {};
		UINT omSampleMask = 0;
		ID3D11DepthStencilState* omDSState = nullptr;
		UINT omStencilRef = 0;

		void Backup(ID3D11DeviceContext* context)
		{
			context->IAGetInputLayout(&iaInputLayout);
			context->IAGetPrimitiveTopology(&iaTopology);

			context->VSGetShader(&vs, nullptr, nullptr);
			context->VSGetConstantBuffers(0, kNumCBSlots, vsCBs);

			context->RSGetState(&rsState);
			rsNumViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			context->RSGetViewports(&rsNumViewports, rsViewports);

			context->PSGetShader(&ps, nullptr, nullptr);
			context->PSGetShaderResources(0, kNumSRVSlots, psSRVs);
			context->PSGetSamplers(0, kNumSamplerSlots, psSamplers);
			context->PSGetConstantBuffers(0, kNumCBSlots, psCBs);

			context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, omRTVs, &omDSV);
			context->OMGetBlendState(&omBlendState, omBlendFactor, &omSampleMask);
			context->OMGetDepthStencilState(&omDSState, &omStencilRef);
		}

		void Restore(ID3D11DeviceContext* context)
		{
			context->IASetInputLayout(iaInputLayout);
			context->IASetPrimitiveTopology(iaTopology);

			context->VSSetShader(vs, nullptr, 0);
			context->VSSetConstantBuffers(0, kNumCBSlots, vsCBs);

			context->RSSetState(rsState);
			context->RSSetViewports(rsNumViewports, rsViewports);

			context->PSSetShader(ps, nullptr, 0);
			context->PSSetShaderResources(0, kNumSRVSlots, psSRVs);
			context->PSSetSamplers(0, kNumSamplerSlots, psSamplers);
			context->PSSetConstantBuffers(0, kNumCBSlots, psCBs);

			context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, omRTVs, omDSV);
			context->OMSetBlendState(omBlendState, omBlendFactor, omSampleMask);
			context->OMSetDepthStencilState(omDSState, omStencilRef);

			Release();
		}

		void Release()
		{
			if (iaInputLayout) {
				iaInputLayout->Release();
				iaInputLayout = nullptr;
			}
			if (vs) {
				vs->Release();
				vs = nullptr;
			}
			for (auto& cb : vsCBs) {
				if (cb) {
					cb->Release();
					cb = nullptr;
				}
			}
			if (rsState) {
				rsState->Release();
				rsState = nullptr;
			}
			if (ps) {
				ps->Release();
				ps = nullptr;
			}
			for (auto& srv : psSRVs) {
				if (srv) {
					srv->Release();
					srv = nullptr;
				}
			}
			for (auto& s : psSamplers) {
				if (s) {
					s->Release();
					s = nullptr;
				}
			}
			for (auto& cb : psCBs) {
				if (cb) {
					cb->Release();
					cb = nullptr;
				}
			}
			for (auto& rtv : omRTVs) {
				if (rtv) {
					rtv->Release();
					rtv = nullptr;
				}
			}
			if (omDSV) {
				omDSV->Release();
				omDSV = nullptr;
			}
			if (omBlendState) {
				omBlendState->Release();
				omBlendState = nullptr;
			}
			if (omDSState) {
				omDSState->Release();
				omDSState = nullptr;
			}
		}
	};
}
