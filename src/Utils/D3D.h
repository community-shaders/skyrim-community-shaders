#pragma once
#include <array>
#include <d3d11.h>
#include <winrt/base.h>

namespace Util
{
	ID3D11ShaderResourceView* GetSRVFromRTV(ID3D11RenderTargetView* a_rtv);
	ID3D11RenderTargetView* GetRTVFromSRV(ID3D11ShaderResourceView* a_srv);
	std::string GetNameFromSRV(ID3D11ShaderResourceView* a_srv);
	std::string GetNameFromRTV(ID3D11RenderTargetView* a_rtv);
	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...);

	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program = "main");

	// Texture manipulation utilities
	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted, const std::array<float, 4>& highlightColor = { 1.0f, 0.5f, 0.0f, 0.3f });
	HRESULT CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV);

	// VR-aware counts for render targets
	inline int GetRenderTargetCount()
	{
		return REL::Module::IsVR() ? RE::RENDER_TARGETS::kVRTOTAL : RE::RENDER_TARGETS::kTOTAL;
	}

	inline int GetDepthStencilCount()
	{
		return REL::Module::IsVR() ? RE::RENDER_TARGETS_DEPTHSTENCIL::kVRTOTAL : RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL;
	}

	HRESULT SaveTextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, ID3D11Texture2D* tex);
	HRESULT LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& path, ID3D11Texture2D** outTex,  ID3D11ShaderResourceView** outSRV);
}  // namespace Util
