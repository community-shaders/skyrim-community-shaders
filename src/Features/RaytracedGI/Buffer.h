#include <d3d12.h>
#include <directx/d3dx12.h>

namespace DX12
{
	class Resource
	{
	public:
		explicit Resource(ID3D12Device5* device, D3D12_HEAP_TYPE heapType, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState) :
			device(device)
		{
			const auto& heapProps = CD3DX12_HEAP_PROPERTIES(heapType);
			DX::ThrowIfFailed(device->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				initialState,
				nullptr,
				IID_PPV_ARGS(&buffer)));

			state = initialState;
		}

		virtual ~Resource() = default;

		virtual void TransitionBarrier(ID3D12GraphicsCommandList4* commandList, D3D12_RESOURCE_STATES stateAfter, UINT subresource)
		{
			if (state == stateAfter)
				return;

			const auto& resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(buffer.get(), state, stateAfter, subresource);
			commandList->ResourceBarrier(1, &resourceBarrier);

			state = stateAfter;
		}

		virtual void UAVBarrier(ID3D12GraphicsCommandList4* commandList)
		{
			const auto& resourceBarrier = CD3DX12_RESOURCE_BARRIER::UAV(buffer.get());
			commandList->ResourceBarrier(1, &resourceBarrier);
		}

		virtual void CreateSRV(D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			device->CreateShaderResourceView(buffer.get(), &srvDesc, handle);
		}

		virtual void CreateUAV(D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc, ID3D12Resource* counterResource, CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			device->CreateUnorderedAccessView(buffer.get(), counterResource, &uavDesc, handle);
		}

		winrt::com_ptr<ID3D12Resource> buffer = nullptr;

	protected:
		ID3D12Device5* device;
		D3D12_RESOURCE_STATES state;
	};

	class TextureResource : public Resource
	{
		static D3D12_RESOURCE_DESC Desc(D3D12_RESOURCE_DIMENSION dimension, UINT64 width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = dimension;
			desc.Alignment = 0;
			desc.Width = width;
			desc.Height = height;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags = flags;

			return desc;
		}

	public:
		explicit TextureResource(
			ID3D12Device5* device, 
			D3D12_RESOURCE_DIMENSION dimension, UINT64 width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) :
			Resource(device, D3D12_HEAP_TYPE_DEFAULT, Desc(dimension, width, height, format, flags), D3D12_RESOURCE_STATE_COMMON) {}

		virtual ~TextureResource() = default;
	};

	template <typename T>
	class StructuredBuffer : public Resource
	{
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

	public:
		explicit StructuredBuffer(ID3D12Device5* device, const uint64_t& a_count, bool uav = false) :
			Resource(device, D3D12_HEAP_TYPE_DEFAULT, Desc(sizeof(T) * a_count, uav), D3D12_RESOURCE_STATE_COPY_DEST), count(a_count) {}

		virtual ~StructuredBuffer() = default;

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

			Resource::CreateSRV(srvDesc, handle);
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

			Resource::CreateUAV(uavDesc, counterResource, handle);
		}


		virtual void CreateUAV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			StructuredBuffer::CreateUAV(nullptr, handle);
		}
	protected:
		uint64_t count;
	};

	template <typename T>
	class StructuredBufferUpload : public StructuredBuffer<T>
	{
	public:
		explicit StructuredBufferUpload(ID3D12Device5* a_device, const uint64_t& a_count, bool uav = false) :
			StructuredBuffer<T>(a_device, a_count, uav)
		{
			D3D12_RESOURCE_DESC desc = this->buffer->GetDesc();
			const auto& uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

			DX::ThrowIfFailed(a_device->CreateCommittedResource(
				&uploadHeap,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&uploadBuffer)));
		}

		void Update(void const* src_data, size_t data_size)
		{
			void* pData;
			DX::ThrowIfFailed(uploadBuffer->Map(0, nullptr, &pData));
			memcpy(pData, src_data, data_size);
			uploadBuffer->Unmap(0, nullptr);
		}

		void UpdateList(T const* src_data, std::int64_t localCount)
		{
			Update(src_data, sizeof(T) * localCount);
		}

		void Upload(ID3D12GraphicsCommandList4* commandList)
		{
			this->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			commandList->CopyBufferRegion(this->buffer.get(), 0, uploadBuffer.get(), 0, sizeof(T) * this->count);
			this->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		}

		winrt::com_ptr<ID3D12Resource> uploadBuffer = nullptr;
	};

	template <typename T>
	class StructuredAppendBuffer : public StructuredBuffer<T>
	{
	public:
		explicit StructuredAppendBuffer(ID3D12Device5* a_device, const uint64_t& a_count, bool uav = true) :
			StructuredBuffer<T>(a_device, a_count, uav)
		{
			// Create 4-byte counter buffer
			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Alignment = 0;
			desc.Width = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			const auto& heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			DX::ThrowIfFailed(a_device->CreateCommittedResource(
				&heap,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(&counterBuffer)));
		}

		void CreateSRV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle) override
		{
			StructuredBuffer<T>::CreateSRV(handle);
		}

		void CreateUAV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle) override
		{
			StructuredBuffer<T>::CreateUAV(counterBuffer.get(), handle);
		}

		winrt::com_ptr<ID3D12Resource> counterBuffer = nullptr;
	};
}