#pragma once

#include <d3d11.h>
#include <winrt/base.h>

namespace PhysicalLensInternal
{
	// RasterPass owns raster state; these are the extra CS and VS bindings used here.
	struct OpticalBindings
	{
		ID3D11DeviceContext* context;
		winrt::com_ptr<ID3D11ComputeShader> cs;
		winrt::com_ptr<ID3D11SamplerState> csSampler, vsSampler;
		winrt::com_ptr<ID3D11Buffer> csCB, csExposureCB, csWaveCB, vsCB, channelCB, indexBuffer;
		DXGI_FORMAT indexFormat;
		UINT indexOffset;
		ID3D11ShaderResourceView *csSRV[7]{}, *vsSRV[6]{};
		ID3D11UnorderedAccessView* uav[6]{};
		explicit OpticalBindings(ID3D11DeviceContext* ctx) :
			context(ctx)
		{
			context->IAGetIndexBuffer(indexBuffer.put(), &indexFormat, &indexOffset);
			context->CSGetShader(cs.put(), nullptr, nullptr);
			context->CSGetSamplers(0, 1, csSampler.put());
			context->VSGetSamplers(0, 1, vsSampler.put());
			context->CSGetConstantBuffers(0, 1, csCB.put());
			context->CSGetConstantBuffers(1, 1, csExposureCB.put());
			context->CSGetConstantBuffers(2, 1, csWaveCB.put());
			context->VSGetConstantBuffers(0, 1, vsCB.put());
			context->VSGetConstantBuffers(5, 1, channelCB.put());
			context->CSGetShaderResources(0, 7, csSRV);
			context->VSGetShaderResources(0, 6, vsSRV);
			context->CSGetUnorderedAccessViews(0, 6, uav);
		}
		~OpticalBindings()
		{
			ID3D11ShaderResourceView* nullSRV[7]{};
			ID3D11UnorderedAccessView* nullUAV[6]{};
			context->CSSetShaderResources(0, 7, nullSRV);
			context->VSSetShaderResources(0, 6, nullSRV);
			context->CSSetUnorderedAccessViews(0, 6, nullUAV, nullptr);
			context->IASetIndexBuffer(indexBuffer.get(), indexFormat, indexOffset);
			context->CSSetShader(cs.get(), nullptr, 0);
			auto* sampler = csSampler.get();
			context->CSSetSamplers(0, 1, &sampler);
			sampler = vsSampler.get();
			context->VSSetSamplers(0, 1, &sampler);
			ID3D11Buffer* cb = csCB.get();
			context->CSSetConstantBuffers(0, 1, &cb);
			cb = csExposureCB.get();
			context->CSSetConstantBuffers(1, 1, &cb);
			cb = csWaveCB.get();
			context->CSSetConstantBuffers(2, 1, &cb);
			cb = vsCB.get();
			context->VSSetConstantBuffers(0, 1, &cb);
			cb = channelCB.get();
			context->VSSetConstantBuffers(5, 1, &cb);
			context->CSSetUnorderedAccessViews(0, 6, uav, nullptr);
			context->CSSetShaderResources(0, 7, csSRV);
			context->VSSetShaderResources(0, 6, vsSRV);
			for (auto* p : csSRV)
				if (p)
					p->Release();
			for (auto* p : vsSRV)
				if (p)
					p->Release();
			for (auto* p : uav)
				if (p)
					p->Release();
		}
	};
}
