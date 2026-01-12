#pragma once

#include "PCH.h"

#include "Features/Raytracing/RTConstants.h"
#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/Shape.h"
#include <d3d12.h>

#include "Features/Raytracing/Types.h"

#include "Raytracing/Includes/RT/SHaRC/SharcTypes.h"
#include "Raytracing/Includes/Types/FrameData.hlsli"
#include "Raytracing/Includes/Types/VertexUpdate.hlsli"

struct SkinningHeapDef
{
	enum class Table
	{
		UAV,
		SRV,
		DynamicBuffer,
		SkinningBuffer
	};

	enum class Slot
	{
		Output,
		LocalToRoot = Output + RTConstants::MAX_SHAPES,
		UpdateData,
		BoneMatrices,
		DynamicVertices,
		SkinningData = DynamicVertices + RTConstants::MAX_SHAPES,
		NumDescriptors = SkinningData + RTConstants::MAX_SHAPES,
		None
	};
};
using SkinningHeap = Heap<SkinningHeapDef::Table, SkinningHeapDef::Slot>;

struct SkinningPipeline : ComputePipeline<SkinningHeap>
{
	static constexpr uint THREAD_SIZE = 64;
	static constexpr uint MAX_BATCHES = 4;

	struct ModelUpdate
	{
		eastl::string name;
		uint16_t allocatedIndex;
		DX12::StructuredBufferUploadMA<float4>* dynamicPositionBuffer = nullptr;
		DX12::StructuredBufferUploadMA<Vertex>* vertexBuffer = nullptr;
		uint16_t vertexCount;
		Flags flags;
	};

	eastl::vector<ModelUpdate> queueModels;
	eastl::unique_ptr<DX12::StructuredBufferUpload<VertexUpdateData>> vertexUpdateBuffer = nullptr;

	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void SetupResources(ID3D12Device5* device) override;
	void QueueUpdate(Flags updateFlags, eastl::string name, Shape* shape);
	bool PrepareResources(ID3D12GraphicsCommandList4* commandList, uint& count, uint& vertexCount);
	void UpdateBLASES(ID3D12GraphicsCommandList4* commandList);
	void RestoreResources(ID3D12GraphicsCommandList4* commandList);
	void ClearQueue();
	void Dispatch(ID3D12GraphicsCommandList4* commandList);
};