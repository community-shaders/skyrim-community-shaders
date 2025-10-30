#include <d3d12.h>

template <typename T>
class StructuredBufferDX12
{
public:
	explicit StructuredBufferDX12(ID3D12Device5* a_device, const uint64_t& a_count) :
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
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		const auto& defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		const auto& uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

		device->CreateCommittedResource(
			&defaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&gpuBuffer));

		device->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadBuffer));
	}

	void Update(void const* src_data, [[maybe_unused]] size_t data_size)
	{
		void* pData;
		CD3DX12_RANGE readRange(0, 0);  // We do not intend to read from it on CPU
		uploadBuffer->Map(0, &readRange, &pData);
		memcpy(pData, src_data, sizeof(T) * count);
		uploadBuffer->Unmap(0, nullptr);
	}

	void UpdateList(T const& src_data, std::int64_t count)
	{
		Update(&src_data, sizeof(T) * count);
	}

	void Upload(ID3D12GraphicsCommandList4* commandList)
	{
		commandList->CopyResource(gpuBuffer.get(), uploadBuffer.get());
	}

	void CreateSRV(ID3D12DescriptorHeap* heap, UINT offset)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<uint>(count);
		srvDesc.Buffer.StructureByteStride = sizeof(T);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		auto cpuHandle = heap->GetCPUDescriptorHandleForHeapStart();
		cpuHandle.ptr += offset;

		device->CreateShaderResourceView(gpuBuffer.get(), &srvDesc, cpuHandle);
	}
	//

	winrt::com_ptr<ID3D12Resource> gpuBuffer;
	winrt::com_ptr<ID3D12Resource> uploadBuffer;

private:
	uint64_t count;
	ID3D12Device5* device;
};