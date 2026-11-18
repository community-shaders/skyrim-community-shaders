#pragma once
#include <array>
#include <d3d11.h>

namespace PhysicalLensGhostWave
{
#ifndef PHYSICAL_LENS_GHOST_WAVE_SIZE
#	define PHYSICAL_LENS_GHOST_WAVE_SIZE 512
#endif
	inline constexpr unsigned Size = PHYSICAL_LENS_GHOST_WAVE_SIZE;
	struct Parameters
	{
		std::array<float, 4> aperture{}, surface{};
		std::array<unsigned, 4> stage{};
	};
	// The caller owns the cache key, resource lifetime and pipeline state guard.
	// Scratch 0/1 are complex RGB arrays; material and atlas are 2D float4.
	inline void Bake(ID3D11DeviceContext* ctx, const std::array<ID3D11ComputeShader*, 4>& shaders,
		ID3D11Buffer* cb, Parameters parameters,
		const std::array<ID3D11ShaderResourceView*, 2>& scratchSRV,
		const std::array<ID3D11UnorderedAccessView*, 2>& scratchUAV,
		ID3D11ShaderResourceView* materialSRV, ID3D11UnorderedAccessView* materialUAV,
		ID3D11ShaderResourceView* atlasSRV, ID3D11UnorderedAccessView* atlasUAV)
	{
		ctx->CSSetConstantBuffers(1, 1, &cb);
		ID3D11ShaderResourceView* emptySRV[2]{};
		ID3D11UnorderedAccessView* emptyUAV[3]{};
		ctx->CSSetShaderResources(0, 2, emptySRV);
		ctx->CSSetUnorderedAccessViews(0, 3, emptyUAV, nullptr);
		ctx->UpdateSubresource(cb, 0, nullptr, &parameters, 0, 0);
		ctx->CSSetUnorderedAccessViews(2, 1, &materialUAV, nullptr);
		ctx->CSSetShader(shaders[0], nullptr, 0);
		ctx->Dispatch(Size / 8, Size / 8, 1);
		ctx->CSSetUnorderedAccessViews(0, 3, emptyUAV, nullptr);
		ctx->CSSetShaderResources(1, 1, &materialSRV);
		for (unsigned sample = 0; sample < 11; ++sample) {
			parameters.stage = { sample, 0, 0, 0 };
			ctx->UpdateSubresource(cb, 0, nullptr, &parameters, 0, 0);
			ctx->CSSetUnorderedAccessViews(0, 1, &scratchUAV[0], nullptr);
			ctx->CSSetShader(shaders[1], nullptr, 0);
			ctx->Dispatch(Size / 8, Size / 8, 3);
			ctx->CSSetUnorderedAccessViews(0, 3, emptyUAV, nullptr);
			for (unsigned pass = 0; pass < 4; ++pass) {
				parameters.stage = { sample, pass >= 2 ? 1u : 0u, pass == 2 ? 1u : 0u, 0 };
				ctx->UpdateSubresource(cb, 0, nullptr, &parameters, 0, 0);
				ctx->CSSetShaderResources(0, 1, &scratchSRV[pass % 2]);
				ctx->CSSetUnorderedAccessViews(0, 1, &scratchUAV[1 - pass % 2], nullptr);
				ctx->CSSetShader(shaders[2], nullptr, 0);
				ctx->Dispatch(Size, 3, 1);
				ctx->CSSetShaderResources(0, 1, emptySRV);
				ctx->CSSetUnorderedAccessViews(0, 1, emptyUAV, nullptr);
			}
			ctx->CSSetShaderResources(0, 1, &scratchSRV[0]);
			ctx->CSSetUnorderedAccessViews(1, 1, &atlasUAV, nullptr);
			ctx->CSSetShader(shaders[3], nullptr, 0);
			ctx->Dispatch(Size / 8, Size / 8, 1);
			ctx->CSSetShaderResources(0, 1, emptySRV);
			ctx->CSSetUnorderedAccessViews(1, 1, emptyUAV, nullptr);
		}
		ctx->CSSetShaderResources(0, 2, emptySRV);
		ctx->GenerateMips(atlasSRV);
	}
}
