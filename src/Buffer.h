#pragma once

#include <d3d11.h>

#include <Windows.Foundation.h>
#include <stdio.h>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

template <typename T>
D3D11_BUFFER_DESC StructuredBufferDesc(uint64_t count, bool uav = true, bool dynamic = false)
{
	D3D11_BUFFER_DESC desc{};
	desc.Usage = (uav || !dynamic) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (uav)
		desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.CPUAccessFlags = !dynamic ? 0 : D3D11_CPU_ACCESS_WRITE;
	desc.StructureByteStride = sizeof(T);
	desc.ByteWidth = (UINT)(sizeof(T) * count);
	return desc;
}

static constexpr std::uint32_t GetCBufferSize(std::uint32_t buffer_size)
{
	return (buffer_size + (64 - 1)) & ~(64 - 1);
}

inline D3D11_BUFFER_DESC ConstantBufferDesc(uint32_t size, bool dynamic = true)
{
	D3D11_BUFFER_DESC desc{};
	ZeroMemory(&desc, sizeof(desc));
	desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
	desc.ByteWidth = GetCBufferSize(size);
	return desc;
}

template <typename T>
D3D11_BUFFER_DESC ConstantBufferDesc(bool dynamic = true)
{
	return ConstantBufferDesc(sizeof(T), dynamic);
}

class ConstantBuffer
{
public:
	explicit ConstantBuffer(D3D11_BUFFER_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, resource.put()));
	}

	ID3D11Buffer* CB() const { return resource.get(); }

	void Update(void const* src_data, size_t data_size)
	{
		auto ctx = globals::d3d::context;
		if (desc.Usage & D3D11_USAGE_DYNAMIC) {
			D3D11_MAPPED_SUBRESOURCE mapped_buffer{};
			ZeroMemory(&mapped_buffer, sizeof(D3D11_MAPPED_SUBRESOURCE));
			DX::ThrowIfFailed(ctx->Map(resource.get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped_buffer));
			memcpy(mapped_buffer.pData, src_data, data_size);
			ctx->Unmap(resource.get(), 0);
		} else
			ctx->UpdateSubresource(resource.get(), 0, nullptr, src_data, 0, 0);
	}

	template <typename T>
	void Update(T const& src_data)
	{
		Update(&src_data, sizeof(T));
	}

private:
	winrt::com_ptr<ID3D11Buffer> resource;
	D3D11_BUFFER_DESC desc;
};

template <typename T>
D3D11_BUFFER_DESC StructuredBufferDesc(UINT a_count = 1, bool cpu_access = true)
{
	D3D11_BUFFER_DESC desc{};
	ZeroMemory(&desc, sizeof(desc));
	desc.Usage = cpu_access ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (!cpu_access)
		desc.BindFlags = desc.BindFlags | D3D11_BIND_UNORDERED_ACCESS;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof(T);
	desc.ByteWidth = sizeof(T) * a_count;
	return desc;
}

class StructuredBuffer
{
public:
	StructuredBuffer(D3D11_BUFFER_DESC const& a_desc, UINT a_count) :
		desc(a_desc), count(a_count)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, resource.put()));
	}

	ID3D11ShaderResourceView* SRV(size_t i = 0) const { return srvs[i].get(); }
	ID3D11UnorderedAccessView* UAV(size_t i = 0) const { return uavs[i].get(); }

	virtual void CreateSRV()
	{
		auto device = globals::d3d::device;
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
		srv_desc.Format = DXGI_FORMAT_UNKNOWN;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		srv_desc.Buffer.FirstElement = 0;
		srv_desc.Buffer.NumElements = count;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &srv_desc, srv.put()));
		srvs.push_back(srv);
	}

	virtual void CreateUAV()
	{
		auto device = globals::d3d::device;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
		uav_desc.Format = DXGI_FORMAT_UNKNOWN;
		uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav_desc.Buffer.Flags = 0;
		uav_desc.Buffer.FirstElement = 0;
		uav_desc.Buffer.NumElements = count;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &uav_desc, uav.put()));
		uavs.push_back(uav);
	}

	void Update(void const* src_data, [[maybe_unused]] size_t data_size)
	{
		auto ctx = globals::d3d::context;
		D3D11_MAPPED_SUBRESOURCE mapped_buffer{};
		ZeroMemory(&mapped_buffer, sizeof(D3D11_MAPPED_SUBRESOURCE));
		DX::ThrowIfFailed(ctx->Map(resource.get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped_buffer));
		memcpy(mapped_buffer.pData, src_data, desc.ByteWidth);
		ctx->Unmap(resource.get(), 0);
	}

	template <typename T>
	void UpdateList(T const& src_data, std::int64_t count)
	{
		Update(&src_data, sizeof(T) * count);
	}
	std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> srvs;
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> uavs;

private:
	winrt::com_ptr<ID3D11Buffer> resource;
	D3D11_BUFFER_DESC desc;
	UINT count;
};

class Buffer
{
public:
	explicit Buffer(D3D11_BUFFER_DESC const& a_desc, D3D11_SUBRESOURCE_DATA* a_init = nullptr) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateBuffer(&desc, a_init, resource.put()));
	}

	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}

	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	D3D11_BUFFER_DESC desc;
	winrt::com_ptr<ID3D11Buffer> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
};

class Texture1D
{
public:
	explicit Texture1D(D3D11_TEXTURE1D_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateTexture1D(&desc, nullptr, resource.put()));
	}

	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}

	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, rtv.put()));
	}

	D3D11_TEXTURE1D_DESC desc;
	winrt::com_ptr<ID3D11Texture1D> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
};

class Texture2D
{
public:
	explicit Texture2D(D3D11_TEXTURE2D_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, resource.put()));
	}

	explicit Texture2D(ID3D11Texture2D* a_resource)
	{
		a_resource->GetDesc(&desc);
		resource.attach(a_resource);
	}

	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}

	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, rtv.put()));
	}

	void CreateDSV(D3D11_DEPTH_STENCIL_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateDepthStencilView(resource.get(), &a_desc, dsv.put()));
	}

	D3D11_TEXTURE2D_DESC desc;
	winrt::com_ptr<ID3D11Texture2D> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
	winrt::com_ptr<ID3D11DepthStencilView> dsv;
};

class Texture3D
{
public:
	explicit Texture3D(D3D11_TEXTURE3D_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateTexture3D(&desc, nullptr, resource.put()));
	}

	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}

	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, rtv.put()));
	}

	D3D11_TEXTURE3D_DESC desc;
	winrt::com_ptr<ID3D11Texture3D> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
};