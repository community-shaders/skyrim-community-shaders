#pragma once

#include <winrt/base.h>

#include <d3d11_4.h>
#include <directx/d3dx12.h>

class WrappedResource
{
public:
	WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device);
	WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc);
	WrappedResource(ID3D12Resource* native, ID3D11Texture2D* shared);

	~WrappedResource();

	auto GetResource() { return resourcePtr ? resourcePtr : resource.get(); };

	ID3D11Texture2D* resource11 = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11UnorderedAccessView* uav = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;

private:
	winrt::com_ptr<ID3D12Resource> resource;
	ID3D12Resource* resourcePtr = nullptr;
};