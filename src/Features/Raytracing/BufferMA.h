#pragma once

#include <d3d12.h>
#include <directx/d3dx12.h>
#include <D3D12MemAlloc.h>

namespace DX12
{
	class ResourceMA
	{
	public:
		explicit ResourceMA(ID3D12Device5* device, D3D12MA::Allocator* allocator, D3D12MA::ALLOCATION_DESC allocDesc, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState) :
			device(device), allocator(allocator), pool(pool), desc(desc)
		{
			DX::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, initialState, nullptr, allocation.put(), IID_PPV_ARGS(&resource)));

			state = initialState;
		}

		virtual ~ResourceMA() = default;

		void SetName(LPCWSTR name) const
		{
			allocation->SetName(name);
		}

		virtual CD3DX12_RESOURCE_BARRIER GetTransitionBarrier(bool setState, D3D12_RESOURCE_STATES stateAfter, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			D3D12_RESOURCE_STATES stateBefore = state;

			if (setState)
				state = stateAfter;

			return CD3DX12_RESOURCE_BARRIER::Transition(resource.get(), stateBefore, stateAfter, subresource);
		}

		virtual void TransitionBarrier(ID3D12GraphicsCommandList4* commandList, D3D12_RESOURCE_STATES stateAfter, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			if (state == stateAfter)
				return;

			const auto& resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.get(), state, stateAfter, subresource);
			commandList->ResourceBarrier(1, &resourceBarrier);

			state = stateAfter;
		}

		virtual void UAVBarrier(ID3D12GraphicsCommandList4* commandList)
		{
			const auto& resourceBarrier = CD3DX12_RESOURCE_BARRIER::UAV(resource.get());
			commandList->ResourceBarrier(1, &resourceBarrier);
		}

		virtual void CreateSRV(D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			device->CreateShaderResourceView(resource.get(), &srvDesc, handle);
		}

		virtual void CreateUAV(D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			device->CreateUnorderedAccessView(resource.get(), nullptr, &uavDesc, handle);
		}

		virtual void CreateUAV(D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc, ID3D12Resource* counterResource, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			device->CreateUnorderedAccessView(resource.get(), counterResource, &uavDesc, handle);
		}

		virtual void CreateRTV(D3D12_RENDER_TARGET_VIEW_DESC rtvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			device->CreateRenderTargetView(resource.get(), &rtvDesc, handle);
		}

		virtual void CreateCBV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = resource->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = static_cast<uint>(desc.Width);

			device->CreateConstantBufferView(&cbvDesc, handle);
		}

		winrt::com_ptr<D3D12MA::Allocation> allocation = nullptr;
		winrt::com_ptr<ID3D12Resource> resource = nullptr;
	protected:
		ID3D12Device5* device;
		D3D12MA::Allocator* allocator;
		D3D12MA::Pool* pool;
		D3D12_RESOURCE_STATES state;
		D3D12_RESOURCE_DESC desc;
	};


	class ResourceUploadMA : public ResourceMA
	{
		static D3D12_RESOURCE_DESC Desc(UINT64 size, D3D12_RESOURCE_FLAGS flags)
		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Alignment = 0;
			desc.Width = size;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags = flags;

			return desc;
		}

	public:
		explicit ResourceUploadMA(ID3D12Device5* device, D3D12MA::Allocator* allocator, D3D12MA::ALLOCATION_DESC allocDesc, D3D12MA::ALLOCATION_DESC uploadAllocDesc, const uint64_t& size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) :
			ResourceMA(device, allocator, allocDesc, Desc(size, flags), D3D12_RESOURCE_STATE_COPY_DEST), size(size)
		{
			DX::ThrowIfFailed(allocator->CreateResource(&uploadAllocDesc, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, uploadAllocation.put(), IID_PPV_ARGS(&uploadResource)));
		}

		void Update(void const* src_data, size_t data_size) const
		{
			void* pData;
			DX::ThrowIfFailed(uploadResource->Map(0, &readRange, &pData));
			memcpy(pData, src_data, data_size);
			D3D12_RANGE writeRange = { 0, data_size };
			uploadResource->Unmap(0, &writeRange);
		}

		void Upload(ID3D12GraphicsCommandList4* commandList)
		{
			this->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_COPY_DEST);
			commandList->CopyBufferRegion(this->resource.get(), 0, uploadResource.get(), 0, size);
			this->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}

		winrt::com_ptr<D3D12MA::Allocation> uploadAllocation = nullptr;
		winrt::com_ptr<ID3D12Resource> uploadResource = nullptr;

	private:
		UINT64 size;
		D3D12_RANGE readRange = { 0, 0 };
	};

	template <typename T>
	class StructuredBufferMA : public ResourceMA
	{
	public:
		static D3D12_RESOURCE_DESC Desc(UINT64 width, bool uav = false)
		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Alignment = 0;
			desc.Width = width;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

			return desc;
		}

		explicit StructuredBufferMA(ID3D12Device5* device, D3D12MA::Allocator* allocator, D3D12MA::ALLOCATION_DESC allocDesc, const uint64_t& a_count, bool uav = false) :
			ResourceMA(device, allocator, allocDesc, Desc(sizeof(T) * a_count, uav), D3D12_RESOURCE_STATE_COPY_DEST), count(a_count) {}

		virtual ~StructuredBufferMA() = default;

		virtual void CreateSRV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = static_cast<uint>(count);
			srvDesc.Buffer.StructureByteStride = sizeof(T);
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			ResourceMA::CreateSRV(srvDesc, handle);
		}

		virtual void CreateUAV(ID3D12Resource* counterResource, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = static_cast<uint>(count);
			uavDesc.Buffer.StructureByteStride = sizeof(T);

			if (counterResource)
				uavDesc.Buffer.CounterOffsetInBytes = 0;

			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

			ResourceMA::CreateUAV(uavDesc, counterResource, handle);
		}

		virtual void CreateUAV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			StructuredBufferMA::CreateUAV(nullptr, handle);
		}
	protected:
		uint64_t count;
	};

	template <typename T>
	class StructuredBufferUploadMA : public StructuredBufferMA<T>
	{
	public:
		explicit StructuredBufferUploadMA(ID3D12Device5* a_device, D3D12MA::Allocator* allocator, D3D12MA::ALLOCATION_DESC allocDesc, D3D12MA::ALLOCATION_DESC uploadAllocDesc, const uint64_t& a_count, bool uav = false, uint uploadCount = 1) :
			StructuredBufferMA<T>(a_device, allocator, allocDesc, a_count, uav)
		{
			D3D12_RESOURCE_DESC desc = StructuredBufferMA<T>::Desc(ResourceMA::desc.Width);

			uploadAllocation.resize(uploadCount);
			uploadResource.resize(uploadCount);

			for (auto i = 0u; i < uploadCount; i++) {
				DX::ThrowIfFailed(allocator->CreateResource(&uploadAllocDesc, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, uploadAllocation[i].put(), IID_PPV_ARGS(&uploadResource[i])));
			}
		}

		void Update(void const* srcData, size_t dataSize, size_t begin = 0, uint uploadIndex = 0)
		{
			void* pData;
			DX::ThrowIfFailed(uploadResource[uploadIndex]->Map(0, &readRange, &pData));

			uint8_t* dst = static_cast<uint8_t*>(pData) + begin;
			memcpy(dst, srcData, dataSize);

			D3D12_RANGE writeRange = { begin, begin + dataSize };
			uploadResource[uploadIndex]->Unmap(0, &writeRange);
		}

		void UpdateAt(T const* srcData, size_t index = 0, uint uploadIndex = 0)
		{
			size_t begin = index * sizeof(T);

			void* pData;
			DX::ThrowIfFailed(uploadResource[uploadIndex]->Map(0, &readRange, &pData));

			uint8_t* dst = static_cast<uint8_t*>(pData) + begin;
			memcpy(dst, srcData, sizeof(T));

			D3D12_RANGE writeRange = { begin, begin + sizeof(T) };
			uploadResource[uploadIndex]->Unmap(0, &writeRange);
		}

		void UpdateList(T const* srcData, uint64_t localCount, uint uploadIndex = 0)
		{
			Update(srcData, sizeof(T) * localCount, uploadIndex);
		}

		void Upload(ID3D12GraphicsCommandList4* commandList, uint uploadIndex = 0)
		{
			D3D12_RESOURCE_STATES state = this->state;

			this->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_COPY_DEST);
			commandList->CopyResource(this->resource.get(), uploadResource[uploadIndex].get());			
			//commandList->CopyBufferRegion(this->resource.get(), 0, uploadBuffer.get(), 0, sizeof(T) * this->count);
			this->TransitionBarrier(commandList, state);
		}

		// dataSize, offset arguments order to match Update function
		void UploadRegion(ID3D12GraphicsCommandList4* commandList, uint64_t dataSize, uint64_t offset, uint uploadIndex = 0)
		{
			D3D12_RESOURCE_STATES state = this->state;

			this->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_COPY_DEST);
			commandList->CopyBufferRegion(this->resource.get(), offset, uploadResource[uploadIndex].get(), offset, dataSize);
			this->TransitionBarrier(commandList, state);
		}

		eastl::vector<winrt::com_ptr<D3D12MA::Allocation>> uploadAllocation;
		eastl::vector<winrt::com_ptr<ID3D12Resource>> uploadResource;

	private:
		D3D12_RANGE readRange = { 0, 0 };
	};
}