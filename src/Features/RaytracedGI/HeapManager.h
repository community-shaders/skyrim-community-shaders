namespace DX12
{
	template <typename T>
	concept EnumUInt32 = std::is_enum_v<T> && std::is_same_v<std::underlying_type_t<T>, uint32_t>;

	template <EnumUInt32 T>
	class DescriptorHeap
	{
	public:
		DescriptorHeap(ID3D12Device5* device, D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc) :
			descriptorHeapDesc(descriptorHeapDesc)
		{
			device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));	
			descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(descriptorHeapDesc.Type);
		}

		ID3D12DescriptorHeap* Heap()
		{
			return descriptorHeap.get();
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle(T item, uint offset=0)
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), item + offset, descriptorIncrementSize);
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE GPUHandle(T item, uint offset = 0)
		{
			return CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), item + offset, descriptorIncrementSize);
		}

	private:
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc;
		winrt::com_ptr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
		uint descriptorIncrementSize;
	};
}