#include <d3d12.h>

namespace DX12
{
	// ======================================================================================
	// Base: GPU-only structured buffer
	// ======================================================================================

	template <typename T>
	class StructuredBuffer
	{
	public:
		explicit StructuredBuffer(ID3D12Device5* a_device, const uint64_t& a_count, bool uav = false) :
			count(a_count), device(a_device)
		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Alignment = 0;
			desc.Width = sizeof(T) * count;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

			const auto& defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			DX::ThrowIfFailed(device->CreateCommittedResource(
				&defaultHeap,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				state,
				nullptr,
				IID_PPV_ARGS(&buffer)));
		}

		virtual ~StructuredBuffer() = default;

		void Transition(ID3D12GraphicsCommandList4* commandList, D3D12_RESOURCE_STATES targetState, UINT subresource)
		{
			if (state == targetState)
				return;

			const auto& resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(buffer.get(), state, targetState, subresource);
			commandList->ResourceBarrier(1, &resourceBarrier);

			state = targetState;
		}

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

			device->CreateShaderResourceView(buffer.get(), &srvDesc, handle);
		}

		virtual void CreateUAV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = static_cast<uint>(count);
			uavDesc.Buffer.StructureByteStride = sizeof(T);
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

			device->CreateUnorderedAccessView(buffer.get(), nullptr, &uavDesc, handle);
		}

		winrt::com_ptr<ID3D12Resource> buffer = nullptr;

	protected:
		uint64_t count;
		ID3D12Device5* device;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
	};

	// ======================================================================================
	// Derived: StructuredBufferUpload (adds CPU upload buffer)
	// ======================================================================================

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
			this->Transition(commandList, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			commandList->CopyBufferRegion(this->buffer.get(), 0, uploadBuffer.get(), 0, sizeof(T) * this->count);
			this->Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		}

		winrt::com_ptr<ID3D12Resource> uploadBuffer = nullptr;
	};

	// ======================================================================================
	// Derived: StructuredAppendBuffer (adds counter buffer for Append/Consume UAVs)
	// ======================================================================================

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

		void CreateUAV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle) override
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = static_cast<uint>(this->count);
			uavDesc.Buffer.StructureByteStride = sizeof(T);
			uavDesc.Buffer.CounterOffsetInBytes = 0;
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

			this->device->CreateUnorderedAccessView(this->buffer.get(), counterBuffer.get(), &uavDesc, handle);
		}

		void CreateSRV(CD3DX12_CPU_DESCRIPTOR_HANDLE handle) override
		{
			StructuredBuffer<T>::CreateSRV(handle);
		}

		winrt::com_ptr<ID3D12Resource> counterBuffer = nullptr;
	};
}