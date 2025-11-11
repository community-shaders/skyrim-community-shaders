namespace DX12
{
	template <typename T>
	concept EnumUInt32 = std::is_enum_v<T> && std::is_same_v<std::underlying_type_t<T>, uint32_t>;

	template <EnumUInt32 T>
	class DescriptorTable
	{
	public:
		DescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE type) :
			type(type) {};

		void AddRange(T slot, UINT numDescriptors, UINT registerSpace = 0, D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE)
		{
			slots.push_back(slot);
			ranges.push_back(CD3DX12_DESCRIPTOR_RANGE1(type, numDescriptors, static_cast<uint>(ranges.size()), registerSpace, flags));
		};

		T FirstSlot()
		{
			return slots.front();
		}

		CD3DX12_ROOT_PARAMETER1 GetRootParameter()
		{
			CD3DX12_ROOT_PARAMETER1 rootParam;
			rootParam.InitAsDescriptorTable(static_cast<uint>(ranges.size()), ranges.data()); 			
			return rootParam;
		}
	private:
		D3D12_DESCRIPTOR_RANGE_TYPE type;
		eastl::vector<T> slots;
		eastl::vector<CD3DX12_DESCRIPTOR_RANGE1> ranges;
	};

	template <EnumUInt32 TSlot, EnumUInt32 TType>
	class DescriptorHeap
	{
	public:
		DescriptorHeap(ID3D12Device5* device, D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc) :
			descriptorHeapDesc(descriptorHeapDesc)
		{
			DX::ThrowIfFailed(device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap)));
			descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(descriptorHeapDesc.Type);
		}

		ID3D12DescriptorHeap* Heap()
		{
			return descriptorHeap.get();
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle(TSlot item, uint offset = 0)
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), item + offset, descriptorIncrementSize);
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE GPUHandle(TSlot item, uint offset = 0)
		{
			return CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), item + offset, descriptorIncrementSize);
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE TableGPUHandle(TType type, uint offset = 0)
		{
			TSlot item;

			if (auto it = descriptorRanges.find(type); it != descriptorRanges.end())
			{
				auto& descriptorTable = it->second;
				item = descriptorTable->FirstSlot();
			}

			return CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), item + offset, descriptorIncrementSize);
		}

		void CreateTable(TType type, D3D12_DESCRIPTOR_RANGE_TYPE rangeType)
		{
			descriptorRanges.emplace(type, eastl::make_unique<DescriptorTable<TSlot>>(rangeType));
		}

		void AddDescriptor(TType type, TSlot slot, UINT numDescriptors, UINT registerSpace = 0, D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE)
		{
			if (auto it = descriptorRanges.find(type); it != descriptorRanges.end())
			{
				it->second->AddRange(slot, numDescriptors, registerSpace, flags);
			}
		}

		CD3DX12_ROOT_PARAMETER1 GetRootParameter(TType type)
		{
			if (auto it = descriptorRanges.find(type); it != descriptorRanges.end()) {
				return it->second->GetRootParameter();
			}

			return CD3DX12_ROOT_PARAMETER1();
		}

		eastl::vector<CD3DX12_ROOT_PARAMETER1> GetAllRootParameters()
		{
			eastl::vector<CD3DX12_ROOT_PARAMETER1> rootParams;

			for (const auto& value : magic_enum::enum_values<TType>()) {
				rootParams.push_back(GetRootParameter(value));
			}

			return rootParams;
		}
	

	private:
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc;
		winrt::com_ptr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
		uint descriptorIncrementSize;
		eastl::unordered_map<TType, eastl::unique_ptr<DescriptorTable<TSlot>>> descriptorRanges;
	};
}