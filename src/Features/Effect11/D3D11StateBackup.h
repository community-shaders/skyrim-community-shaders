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
	static constexpr UINT kMaxSOTargets = 4;

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

		// Hull Shader
		ID3D11HullShader* hs = nullptr;
		ID3D11Buffer* hsCBs[kMaxCBs] = {};
		ID3D11ShaderResourceView* hsSRVs[kMaxSRVs] = {};
		ID3D11SamplerState* hsSamplers[kMaxSamplers] = {};

		// Domain Shader
		ID3D11DomainShader* ds = nullptr;
		ID3D11Buffer* dsCBs[kMaxCBs] = {};
		ID3D11ShaderResourceView* dsSRVs[kMaxSRVs] = {};
		ID3D11SamplerState* dsSamplers[kMaxSamplers] = {};

		// Geometry Shader
		ID3D11GeometryShader* gs = nullptr;
		ID3D11Buffer* gsCBs[kMaxCBs] = {};
		ID3D11ShaderResourceView* gsSRVs[kMaxSRVs] = {};
		ID3D11SamplerState* gsSamplers[kMaxSamplers] = {};

		// Stream Output
		ID3D11Buffer* soTargets[kMaxSOTargets] = {};

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

			ctx->HSGetShader(&hs, nullptr, nullptr);
			ctx->HSGetConstantBuffers(0, kMaxCBs, hsCBs);
			ctx->HSGetShaderResources(0, kMaxSRVs, hsSRVs);
			ctx->HSGetSamplers(0, kMaxSamplers, hsSamplers);

			ctx->DSGetShader(&ds, nullptr, nullptr);
			ctx->DSGetConstantBuffers(0, kMaxCBs, dsCBs);
			ctx->DSGetShaderResources(0, kMaxSRVs, dsSRVs);
			ctx->DSGetSamplers(0, kMaxSamplers, dsSamplers);

			ctx->GSGetShader(&gs, nullptr, nullptr);
			ctx->GSGetConstantBuffers(0, kMaxCBs, gsCBs);
			ctx->GSGetShaderResources(0, kMaxSRVs, gsSRVs);
			ctx->GSGetSamplers(0, kMaxSamplers, gsSamplers);

			ctx->SOGetTargets(kMaxSOTargets, soTargets);

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

			ctx->HSSetShader(hs, nullptr, 0);
			ctx->HSSetConstantBuffers(0, kMaxCBs, hsCBs);
			ctx->HSSetShaderResources(0, kMaxSRVs, hsSRVs);
			ctx->HSSetSamplers(0, kMaxSamplers, hsSamplers);

			ctx->DSSetShader(ds, nullptr, 0);
			ctx->DSSetConstantBuffers(0, kMaxCBs, dsCBs);
			ctx->DSSetShaderResources(0, kMaxSRVs, dsSRVs);
			ctx->DSSetSamplers(0, kMaxSamplers, dsSamplers);

			ctx->GSSetShader(gs, nullptr, 0);
			ctx->GSSetConstantBuffers(0, kMaxCBs, gsCBs);
			ctx->GSSetShaderResources(0, kMaxSRVs, gsSRVs);
			ctx->GSSetSamplers(0, kMaxSamplers, gsSamplers);

			UINT soOffsets[kMaxSOTargets] = {};
			ctx->SOSetTargets(kMaxSOTargets, soTargets, soOffsets);

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

			SafeRelease(hs);
			SafeReleaseArray(hsCBs);
			SafeReleaseArray(hsSRVs);
			SafeReleaseArray(hsSamplers);

			SafeRelease(ds);
			SafeReleaseArray(dsCBs);
			SafeReleaseArray(dsSRVs);
			SafeReleaseArray(dsSamplers);

			SafeRelease(gs);
			SafeReleaseArray(gsCBs);
			SafeReleaseArray(gsSRVs);
			SafeReleaseArray(gsSamplers);

			SafeReleaseArray(soTargets);

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
}
