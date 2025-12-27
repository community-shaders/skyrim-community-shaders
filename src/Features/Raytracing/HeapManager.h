#pragma once

#include "Features/Raytracing/Allocator.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/ShaderBindingTable.h"

namespace DX12
{
	template <typename T>
	concept EnumUInt32 = std::is_enum_v<T> && std::is_same_v<std::underlying_type_t<T>, uint32_t>;

	//template <EnumUInt32 T>
	template <typename T>
	struct DescriptorDesc
	{
		T slot;
		UINT numDescriptors;
		UINT registerSpace = 0;
		D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

		DescriptorDesc(T slot, UINT numDescriptors, UINT registerSpace = 0, D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE) :
			slot(slot), numDescriptors(numDescriptors), registerSpace(registerSpace), flags(flags) {}
	};

	//template <EnumUInt32 T>
	template <typename T>
	class DescriptorTable
	{
	public:
		DescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE type, const eastl::vector<DescriptorDesc<T>>& descriptors) :
			type(type), rootParameter(new CD3DX12_ROOT_PARAMETER1())
		{
			ranges.reserve(descriptors.size());
			slots.reserve(descriptors.size());

			for (const auto& descriptor : descriptors) {
				slots.push_back(descriptor.slot);
				ranges.emplace_back(type, descriptor.numDescriptors,
					static_cast<uint>(ranges.size()),
					descriptor.registerSpace,
					descriptor.flags);
			}

			rootParameter->InitAsDescriptorTable(static_cast<uint>(ranges.size()), ranges.data());
		}

		T FirstSlot() const
		{
			return slots.front();
		}

		const CD3DX12_ROOT_PARAMETER1& GetRootParameter() const
		{
			return *rootParameter;
		}

	private:
		D3D12_DESCRIPTOR_RANGE_TYPE type;
		eastl::vector<T> slots;
		eastl::vector<CD3DX12_DESCRIPTOR_RANGE1> ranges;
		eastl::unique_ptr<CD3DX12_ROOT_PARAMETER1> rootParameter;
	};

	template <IsHeap HeapType>
	class DescriptorHeap
	{
	public:
		using Table = HeapType::Table;
		using Slot = HeapType::Slot;

		DescriptorHeap(ID3D12Device5* device, const D3D12_DESCRIPTOR_HEAP_DESC& descriptorHeapDesc) :
			descriptorHeapDesc(descriptorHeapDesc)
		{
			DX::ThrowIfFailed(device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap)));
			descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(descriptorHeapDesc.Type);
		}

		ID3D12DescriptorHeap* Heap() const
		{
			return descriptorHeap.get();
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle(Slot item, uint offset = 0) const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), HeapType::GetSlotValue(item) + offset, descriptorIncrementSize);
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle(Slot item, Allocation* allocation) const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), HeapType::GetSlotValue(item) + allocation->GetIndex(), descriptorIncrementSize);
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE GPUHandle(Slot item, uint offset = 0) const
		{
			return CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), HeapType::GetSlotValue(item) + offset, descriptorIncrementSize);
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE TableGPUHandle(Table type, uint offset = 0) const
		{
			auto it = descriptorRanges.find(type);
			if (it == descriptorRanges.end()) {
				throw std::out_of_range("[RT] DescriptorHeap::TableGPUHandle, Table not found.");
			}

			Slot firstSlot = it->second->FirstSlot();

			return CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), HeapType::GetSlotValue(firstSlot) + offset, descriptorIncrementSize);
		}

		void CreateTable(Table type, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, const eastl::vector<DescriptorDesc<Slot>>& descriptors)
		{
			descriptorRanges.emplace(type, eastl::unique_ptr<DescriptorTable<Slot>>(new DescriptorTable<Slot>(rangeType, descriptors)));
		}

		eastl::vector<CD3DX12_ROOT_PARAMETER1> GetRootParameters() const
		{
			eastl::vector<CD3DX12_ROOT_PARAMETER1> rootParams;
			for (const auto& type : magic_enum::enum_values<Table>()) {
				auto it = descriptorRanges.find(type);
				if (it != descriptorRanges.end()) {
					rootParams.push_back(it->second->GetRootParameter());
				} else {
					logger::error("[RT] DescriptorHeap::GetRootParameter Descriptor table {} not found.", magic_enum::enum_name(type));
				}
			}
			return rootParams;
		}

	private:
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
		winrt::com_ptr<ID3D12DescriptorHeap> descriptorHeap;
		uint descriptorIncrementSize{};
		eastl::unordered_map<Table, eastl::unique_ptr<DescriptorTable<Slot>>> descriptorRanges;
	};
}
