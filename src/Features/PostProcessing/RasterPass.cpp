#include "RasterPass.h"

namespace PostProcessingRaster
{
	RasterPass::RasterPass(ID3D11DeviceContext* a_context) :
		context(a_context)
	{
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMGetBlendState(&savedBlendState, savedBlendFactor, &savedSampleMask);
		context->OMGetDepthStencilState(&savedDepthStencilState, &savedStencilRef);

		context->RSGetState(&savedRasterizerState);
		savedViewportCount = static_cast<UINT>(savedViewports.size());
		context->RSGetViewports(&savedViewportCount, savedViewports.data());
		savedScissorCount = static_cast<UINT>(savedScissors.size());
		context->RSGetScissorRects(&savedScissorCount, savedScissors.data());

		context->IAGetPrimitiveTopology(&savedTopology);
		context->IAGetInputLayout(&savedInputLayout);

		context->VSGetShader(&savedVS, nullptr, nullptr);
		context->HSGetShader(&savedHS, nullptr, nullptr);
		context->DSGetShader(&savedDS, nullptr, nullptr);
		context->GSGetShader(&savedGS, nullptr, nullptr);
		context->PSGetShader(&savedPS, nullptr, nullptr);
		context->PSGetShaderResources(0, kPSSRVCount, savedPSSRVs.data());
		context->PSGetConstantBuffers(0, kPSCBCount, savedPSCBs.data());
		context->PSGetSamplers(0, kPSSamplerCount, savedPSSamplers.data());

		// Fullscreen triangle state: no vertex buffer, no input layout, no depth, no
		// scissor (default rasterizer state), blending off until a pass opts in.
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->IASetInputLayout(nullptr);
		context->RSSetState(nullptr);
		context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
		context->OMSetDepthStencilState(nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);
	}

	RasterPass::~RasterPass()
	{
		std::array<ID3D11ShaderResourceView*, kPSSRVCount> nullSRVs = {};
		context->PSSetShaderResources(0, kPSSRVCount, nullSRVs.data());

		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
		context->OMSetBlendState(savedBlendState, savedBlendFactor, savedSampleMask);
		context->OMSetDepthStencilState(savedDepthStencilState, savedStencilRef);

		context->RSSetState(savedRasterizerState);
		context->RSSetViewports(savedViewportCount, savedViewports.data());
		context->RSSetScissorRects(savedScissorCount, savedScissors.data());

		context->IASetPrimitiveTopology(savedTopology);
		context->IASetInputLayout(savedInputLayout);

		context->VSSetShader(savedVS, nullptr, 0);
		context->HSSetShader(savedHS, nullptr, 0);
		context->DSSetShader(savedDS, nullptr, 0);
		context->GSSetShader(savedGS, nullptr, 0);
		context->PSSetShader(savedPS, nullptr, 0);
		context->PSSetShaderResources(0, kPSSRVCount, savedPSSRVs.data());
		context->PSSetConstantBuffers(0, kPSCBCount, savedPSCBs.data());
		context->PSSetSamplers(0, kPSSamplerCount, savedPSSamplers.data());

		for (auto* rtv : savedRTVs)
			if (rtv)
				rtv->Release();
		if (savedDSV)
			savedDSV->Release();
		if (savedBlendState)
			savedBlendState->Release();
		if (savedDepthStencilState)
			savedDepthStencilState->Release();
		if (savedRasterizerState)
			savedRasterizerState->Release();
		if (savedInputLayout)
			savedInputLayout->Release();
		if (savedVS)
			savedVS->Release();
		if (savedHS)
			savedHS->Release();
		if (savedDS)
			savedDS->Release();
		if (savedGS)
			savedGS->Release();
		if (savedPS)
			savedPS->Release();
		for (auto* srv : savedPSSRVs)
			if (srv)
				srv->Release();
		for (auto* cb : savedPSCBs)
			if (cb)
				cb->Release();
		for (auto* sampler : savedPSSamplers)
			if (sampler)
				sampler->Release();
	}

	void RasterPass::SetTargets(std::initializer_list<ID3D11RenderTargetView*> a_rtvs, float a_width, float a_height)
	{
		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		UINT count = 0;
		for (auto* rtv : a_rtvs) {
			if (count >= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
				break;
			rtvs[count++] = rtv;
		}

		context->OMSetRenderTargets(count, rtvs, nullptr);

		D3D11_VIEWPORT viewport = {};
		viewport.Width = a_width;
		viewport.Height = a_height;
		viewport.MinDepth = 0.f;
		viewport.MaxDepth = 1.f;
		context->RSSetViewports(1, &viewport);
	}

	void RasterPass::SetShaders(ID3D11VertexShader* a_vs, ID3D11PixelShader* a_ps)
	{
		context->VSSetShader(a_vs, nullptr, 0);
		context->PSSetShader(a_ps, nullptr, 0);
	}

	void RasterPass::SetBlendState(ID3D11BlendState* a_blend, const float (&a_blendFactor)[4], UINT a_sampleMask)
	{
		context->OMSetBlendState(a_blend, a_blendFactor, a_sampleMask);
	}

	void RasterPass::Draw()
	{
		context->Draw(3, 0);
	}
}
