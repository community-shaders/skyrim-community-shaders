#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "PCH.h"
#include <d3d12.h>

#include "Features/Raytracing/Types.h"

#include "Raytracing/Includes/RT/SHaRC/SharcTypes.h"

struct SHaRCHeapDef
{
	enum class Table
	{
		UAV
	};

	enum class Slot
	{
		SHaRCHashEntries,
		SHaRCAccumulation,
		SHaRCResolved,
		NumDescriptors,
		None
	};
};
using SHaRCHeap = Heap<SHaRCHeapDef::Table, SHaRCHeapDef::Slot>;

struct SHaRCPipeline : ComputePipeline<SHaRCHeap>
{
	static constexpr uint GROUP_SIZE = 256;
	static constexpr size_t MAX_CAPACITY = 4 * 1024 * 1024;

	eastl::unique_ptr<DX12::StructuredBuffer<uint64_t>> sharcHashEntriesBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBuffer<uint>> sharcLockBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBuffer<SharcAccumulationData>> sharcAccumulationBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBuffer<SharcPackedData>> sharcResolvedBuffer = nullptr;

	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void SetupResources(ID3D12Device5* device) override;
	void Resolve(ID3D12GraphicsCommandList4* commandList);
	void CreateUAVs(CD3DX12_CPU_DESCRIPTOR_HANDLE hashEntries, CD3DX12_CPU_DESCRIPTOR_HANDLE lock, CD3DX12_CPU_DESCRIPTOR_HANDLE accumulation, CD3DX12_CPU_DESCRIPTOR_HANDLE resolved);
};