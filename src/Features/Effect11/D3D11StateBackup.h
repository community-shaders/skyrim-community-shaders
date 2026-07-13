#pragma once

#include <d3d11.h>

namespace Effect11Util
{
	static constexpr UINT kMaxSRVs = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
	static constexpr UINT kMaxSamplers = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
	static constexpr UINT kMaxCBs = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
	static constexpr UINT kMaxUAVs = D3D11_1_UAV_SLOT_COUNT;
	static constexpr UINT kMaxVBs = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
	static constexpr UINT kMaxRTVs = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
	static constexpr UINT kMaxViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

	template <typename T>
	inline void SafeRelease(T*& ptr)
	{
		if (ptr) {
			ptr->Release();
			ptr = nullptr;
		}
	}

	template <typename T, size_t N>
	inline void SafeReleaseArray(T* (&arr)[N])
	{
		for (size_t i = 0; i < N; ++i)
			SafeRelease(arr[i]);
	}

	// Saves/restores the D3D11 pipeline state around a post-processing chain so the game's own
	// bindings survive it. Post-processing only ever uses VS/PS/CS full-screen passes, so the
	// tessellation (HS/DS), geometry (GS) and stream-out (SO) stages are deliberately NOT captured:
	// nothing here binds them, so there is nothing to restore, and skipping them removes ~half of
	// the per-frame Get*/Set* + COM AddRef/Release traffic. (kMaxSRVs is 128 per stage — capturing
	// every stage was the dominant CPU cost of this feature.)
	struct D3D11FullStateBackup
	{
		// Input Assembler
		ID3D11InputLayout* iaInputLayout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY iaTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11Buffer* iaVertexBuffers[kMaxVBs] = {};
		UINT iaVBStrides[kMaxVBs] = {};
		UINT iaVBOffsets[kMaxVBs] = {};
		ID3D11Buffer* iaIndexBuffer = nullptr;
		DXGI_FORMAT iaIndexFormat = DXGI_FORMAT_UNKNOWN;
		UINT iaIndexOffset = 0;

		// Vertex Shader
		ID3D11VertexShader* vs = nullptr;
		ID3D11Buffer* vsCBs[kMaxCBs] = {};
		ID3D11ShaderResourceView* vsSRVs[kMaxSRVs] = {};
		ID3D11SamplerState* vsSamplers[kMaxSamplers] = {};

		// Rasterizer
		ID3D11RasterizerState* rs = nullptr;
		UINT rsNumViewports = kMaxViewports;
		D3D11_VIEWPORT rsViewports[kMaxViewports] = {};
		UINT rsNumScissorRects = kMaxViewports;
		D3D11_RECT rsScissorRects[kMaxViewports] = {};

		// Pixel Shader
		ID3D11PixelShader* ps = nullptr;
		ID3D11Buffer* psCBs[kMaxCBs] = {};
		ID3D11ShaderResourceView* psSRVs[kMaxSRVs] = {};
		ID3D11SamplerState* psSamplers[kMaxSamplers] = {};

		// Output Merger
		ID3D11RenderTargetView* omRTVs[kMaxRTVs] = {};
		ID3D11DepthStencilView* omDSV = nullptr;
		ID3D11BlendState* omBlendState = nullptr;
		FLOAT omBlendFactor[4] = {};
		UINT omSampleMask = 0;
		ID3D11DepthStencilState* omDepthStencilState = nullptr;
		UINT omStencilRef = 0;

		// Compute Shader
		ID3D11ComputeShader* cs = nullptr;
		ID3D11Buffer* csCBs[kMaxCBs] = {};
		ID3D11ShaderResourceView* csSRVs[kMaxSRVs] = {};
		ID3D11SamplerState* csSamplers[kMaxSamplers] = {};
		ID3D11UnorderedAccessView* csUAVs[kMaxUAVs] = {};

		void Save(ID3D11DeviceContext* ctx)
		{
			ctx->IAGetInputLayout(&iaInputLayout);
			ctx->IAGetPrimitiveTopology(&iaTopology);
			ctx->IAGetVertexBuffers(0, kMaxVBs, iaVertexBuffers, iaVBStrides, iaVBOffsets);
			ctx->IAGetIndexBuffer(&iaIndexBuffer, &iaIndexFormat, &iaIndexOffset);

			ctx->VSGetShader(&vs, nullptr, nullptr);
			ctx->VSGetConstantBuffers(0, kMaxCBs, vsCBs);
			ctx->VSGetShaderResources(0, kMaxSRVs, vsSRVs);
			ctx->VSGetSamplers(0, kMaxSamplers, vsSamplers);

			ctx->RSGetState(&rs);
			rsNumViewports = kMaxViewports;
			ctx->RSGetViewports(&rsNumViewports, rsViewports);
			rsNumScissorRects = kMaxViewports;
			ctx->RSGetScissorRects(&rsNumScissorRects, rsScissorRects);

			ctx->PSGetShader(&ps, nullptr, nullptr);
			ctx->PSGetConstantBuffers(0, kMaxCBs, psCBs);
			ctx->PSGetShaderResources(0, kMaxSRVs, psSRVs);
			ctx->PSGetSamplers(0, kMaxSamplers, psSamplers);

			ctx->OMGetRenderTargets(kMaxRTVs, omRTVs, &omDSV);
			ctx->OMGetBlendState(&omBlendState, omBlendFactor, &omSampleMask);
			ctx->OMGetDepthStencilState(&omDepthStencilState, &omStencilRef);

			ctx->CSGetShader(&cs, nullptr, nullptr);
			ctx->CSGetConstantBuffers(0, kMaxCBs, csCBs);
			ctx->CSGetShaderResources(0, kMaxSRVs, csSRVs);
			ctx->CSGetSamplers(0, kMaxSamplers, csSamplers);
			ctx->CSGetUnorderedAccessViews(0, kMaxUAVs, csUAVs);
		}

		void Restore(ID3D11DeviceContext* ctx)
		{
			ctx->IASetInputLayout(iaInputLayout);
			ctx->IASetPrimitiveTopology(iaTopology);
			ctx->IASetVertexBuffers(0, kMaxVBs, iaVertexBuffers, iaVBStrides, iaVBOffsets);
			ctx->IASetIndexBuffer(iaIndexBuffer, iaIndexFormat, iaIndexOffset);

			ctx->VSSetShader(vs, nullptr, 0);
			ctx->VSSetConstantBuffers(0, kMaxCBs, vsCBs);
			ctx->VSSetShaderResources(0, kMaxSRVs, vsSRVs);
			ctx->VSSetSamplers(0, kMaxSamplers, vsSamplers);

			ctx->RSSetState(rs);
			ctx->RSSetViewports(rsNumViewports, rsViewports);
			ctx->RSSetScissorRects(rsNumScissorRects, rsScissorRects);

			ctx->PSSetShader(ps, nullptr, 0);
			ctx->PSSetConstantBuffers(0, kMaxCBs, psCBs);
			ctx->PSSetShaderResources(0, kMaxSRVs, psSRVs);
			ctx->PSSetSamplers(0, kMaxSamplers, psSamplers);

			ctx->OMSetRenderTargets(kMaxRTVs, omRTVs, omDSV);
			ctx->OMSetBlendState(omBlendState, omBlendFactor, omSampleMask);
			ctx->OMSetDepthStencilState(omDepthStencilState, omStencilRef);

			ctx->CSSetShader(cs, nullptr, 0);
			ctx->CSSetConstantBuffers(0, kMaxCBs, csCBs);
			ctx->CSSetShaderResources(0, kMaxSRVs, csSRVs);
			ctx->CSSetSamplers(0, kMaxSamplers, csSamplers);
			ctx->CSSetUnorderedAccessViews(0, kMaxUAVs, csUAVs, nullptr);
		}

		void Release()
		{
			SafeRelease(iaInputLayout);
			SafeReleaseArray(iaVertexBuffers);
			SafeRelease(iaIndexBuffer);

			SafeRelease(vs);
			SafeReleaseArray(vsCBs);
			SafeReleaseArray(vsSRVs);
			SafeReleaseArray(vsSamplers);

			SafeRelease(rs);

			SafeRelease(ps);
			SafeReleaseArray(psCBs);
			SafeReleaseArray(psSRVs);
			SafeReleaseArray(psSamplers);

			SafeReleaseArray(omRTVs);
			SafeRelease(omDSV);
			SafeRelease(omBlendState);
			SafeRelease(omDepthStencilState);

			SafeRelease(cs);
			SafeReleaseArray(csCBs);
			SafeReleaseArray(csSRVs);
			SafeReleaseArray(csSamplers);
			SafeReleaseArray(csUAVs);
		}
	};

	// Scoped backup for the volumetric-rays passes. Those four passes overwrite only a small, fixed
	// slice of pipeline state, so we save/restore exactly that slice instead of the whole pipeline.
	// A full D3D11FullStateBackup captures 128 SRVs per stage (~hundreds of COM AddRef/Release per
	// frame); this captures ~20 slots — the reason enabling volumetric rays no longer craters CPU
	// frame time. Keep the captured ranges in sync with DrawVolumetricRays if its bindings change.
	struct D3D11ScopedPostFxBackup
	{
		static constexpr UINT kPSSRVs = 16;  // DrawVolumetricRays: PSSetShaderResources(0, 16, ...)
		static constexpr UINT kCSSRVs = 2;   // CSSetShaderResources(0, 2, ...)
		static constexpr UINT kCSCBs = 2;    // CSSetConstantBuffers(0, 2, ...)

		// Input Assembler (only vertex buffer slot 0 + layout/topology are touched)
		ID3D11InputLayout* iaInputLayout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY iaTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11Buffer* iaVB0 = nullptr;
		UINT iaVB0Stride = 0;
		UINT iaVB0Offset = 0;

		// Vertex Shader (shader only)
		ID3D11VertexShader* vs = nullptr;

		// Rasterizer (state + viewports; scissors are untouched)
		ID3D11RasterizerState* rs = nullptr;
		UINT rsNumViewports = kMaxViewports;
		D3D11_VIEWPORT rsViewports[kMaxViewports] = {};

		// Pixel Shader (shader + SRVs 0..15 + sampler 0)
		ID3D11PixelShader* ps = nullptr;
		ID3D11ShaderResourceView* psSRVs[kPSSRVs] = {};
		ID3D11SamplerState* psSampler0 = nullptr;

		// Output Merger (render targets + blend + depth-stencil)
		ID3D11RenderTargetView* omRTVs[kMaxRTVs] = {};
		ID3D11DepthStencilView* omDSV = nullptr;
		ID3D11BlendState* omBlendState = nullptr;
		FLOAT omBlendFactor[4] = {};
		UINT omSampleMask = 0;
		ID3D11DepthStencilState* omDepthStencilState = nullptr;
		UINT omStencilRef = 0;

		// Compute Shader (shader + SRVs 0..1 + UAV 0 + CBs 0..1)
		ID3D11ComputeShader* cs = nullptr;
		ID3D11ShaderResourceView* csSRVs[kCSSRVs] = {};
		ID3D11UnorderedAccessView* csUAV0 = nullptr;
		ID3D11Buffer* csCBs[kCSCBs] = {};

		void Save(ID3D11DeviceContext* ctx)
		{
			ctx->IAGetInputLayout(&iaInputLayout);
			ctx->IAGetPrimitiveTopology(&iaTopology);
			ctx->IAGetVertexBuffers(0, 1, &iaVB0, &iaVB0Stride, &iaVB0Offset);

			ctx->VSGetShader(&vs, nullptr, nullptr);

			ctx->RSGetState(&rs);
			rsNumViewports = kMaxViewports;
			ctx->RSGetViewports(&rsNumViewports, rsViewports);

			ctx->PSGetShader(&ps, nullptr, nullptr);
			ctx->PSGetShaderResources(0, kPSSRVs, psSRVs);
			ctx->PSGetSamplers(0, 1, &psSampler0);

			ctx->OMGetRenderTargets(kMaxRTVs, omRTVs, &omDSV);
			ctx->OMGetBlendState(&omBlendState, omBlendFactor, &omSampleMask);
			ctx->OMGetDepthStencilState(&omDepthStencilState, &omStencilRef);

			ctx->CSGetShader(&cs, nullptr, nullptr);
			ctx->CSGetShaderResources(0, kCSSRVs, csSRVs);
			ctx->CSGetUnorderedAccessViews(0, 1, &csUAV0);
			ctx->CSGetConstantBuffers(0, kCSCBs, csCBs);
		}

		void Restore(ID3D11DeviceContext* ctx)
		{
			ctx->IASetInputLayout(iaInputLayout);
			ctx->IASetPrimitiveTopology(iaTopology);
			ctx->IASetVertexBuffers(0, 1, &iaVB0, &iaVB0Stride, &iaVB0Offset);

			ctx->VSSetShader(vs, nullptr, 0);

			ctx->RSSetState(rs);
			ctx->RSSetViewports(rsNumViewports, rsViewports);

			ctx->PSSetShader(ps, nullptr, 0);
			ctx->PSSetShaderResources(0, kPSSRVs, psSRVs);
			ctx->PSSetSamplers(0, 1, &psSampler0);

			ctx->OMSetRenderTargets(kMaxRTVs, omRTVs, omDSV);
			ctx->OMSetBlendState(omBlendState, omBlendFactor, omSampleMask);
			ctx->OMSetDepthStencilState(omDepthStencilState, omStencilRef);

			ctx->CSSetShader(cs, nullptr, 0);
			ctx->CSSetShaderResources(0, kCSSRVs, csSRVs);
			ctx->CSSetUnorderedAccessViews(0, 1, &csUAV0, nullptr);
			ctx->CSSetConstantBuffers(0, kCSCBs, csCBs);
		}

		void Release()
		{
			SafeRelease(iaInputLayout);
			SafeRelease(iaVB0);
			SafeRelease(vs);
			SafeRelease(rs);
			SafeRelease(ps);
			SafeReleaseArray(psSRVs);
			SafeRelease(psSampler0);
			SafeReleaseArray(omRTVs);
			SafeRelease(omDSV);
			SafeRelease(omBlendState);
			SafeRelease(omDepthStencilState);
			SafeRelease(cs);
			SafeReleaseArray(csSRVs);
			SafeRelease(csUAV0);
			SafeReleaseArray(csCBs);
		}
	};
}
