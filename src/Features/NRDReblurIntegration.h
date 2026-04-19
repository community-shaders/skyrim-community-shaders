#pragma once

#include <NRD.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <EASTL/vector.h>
#include <EASTL/array.h>
#include <winrt/base.h>

// Lightweight DX11-only NRD dispatcher — no NRI dependency.
// Manages internal pool textures, pipelines (DXBC), and the constant buffer.
// Callers bind named input/output resources via SetNamedResource before calling Dispatch.

class NRDReblurIntegration
{
public:
	NRDReblurIntegration() = default;
	~NRDReblurIntegration() { Shutdown(); }

	// identifier is an arbitrary uint32 tag for this denoiser (e.g. 0)
	bool Init(uint32_t halfWidth, uint32_t halfHeight, nrd::Denoiser denoiser, uint32_t identifier);
	void Shutdown();

	// Call when the render resolution changes (half-res width/height)
	void Resize(uint32_t halfWidth, uint32_t halfHeight);

	void SetCommonSettings(const nrd::CommonSettings& settings);
	void SetDenoiserSettings(const void* settings);

	// Bind a named NRD resource (SRV for inputs, UAV for outputs) before each Dispatch call.
	// Named resources = everything that is NOT PERMANENT_POOL / TRANSIENT_POOL.
	void SetNamedSRV(nrd::ResourceType type, ID3D11ShaderResourceView* srv);
	void SetNamedUAV(nrd::ResourceType type, ID3D11UnorderedAccessView* uav);

	// Execute all compute dispatches for this denoiser.
	void Dispatch();

	bool IsValid() const { return m_instance != nullptr; }

private:
	struct PoolTexture
	{
		winrt::com_ptr<ID3D11Texture2D> texture;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	};

	DXGI_FORMAT NRDFormatToDXGI(nrd::Format fmt);
	void CreatePoolTextures(uint32_t w, uint32_t h);
	void DestroyPoolTextures();
	void CreatePipelines();
	void DestroyPipelines();
	void CreateConstantBuffer(uint32_t maxSize);
	void CreateSamplers();

	ID3D11View* ResolveResource(const nrd::ResourceDesc& res);

	nrd::Instance* m_instance = nullptr;
	nrd::Identifier m_identifier = 0;
	uint32_t m_width = 0, m_height = 0;

	// Permanent pool (history) + transient pool (can alias between frames)
	eastl::vector<PoolTexture> m_permanentPool;
	eastl::vector<PoolTexture> m_transientPool;

	// Per-pipeline DX11 compute shaders (indexed by PipelineDesc order)
	eastl::vector<winrt::com_ptr<ID3D11ComputeShader>> m_pipelines;

	// Single constant buffer, updated per-dispatch
	winrt::com_ptr<ID3D11Buffer> m_constantBuffer;
	uint32_t m_cbMaxSize = 0;

	// Samplers: [0]=NEAREST_CLAMP, [1]=LINEAR_CLAMP
	winrt::com_ptr<ID3D11SamplerState> m_samplers[2];

	// Named resource bindings provided by the caller
	ID3D11ShaderResourceView* m_namedSRV[static_cast<uint32_t>(nrd::ResourceType::TRANSIENT_POOL)] = {};
	ID3D11UnorderedAccessView* m_namedUAV[static_cast<uint32_t>(nrd::ResourceType::TRANSIENT_POOL)] = {};
};
