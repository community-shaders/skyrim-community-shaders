#pragma once

#include "Features/Raytracing.h"
#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "PCH.h"
#include <d3d12.h>

#include "Features/Raytracing/Types.h"

#include "Raytracing/Includes/RT/SHaRC/SharcTypes.h"
#include "Raytracing/Includes/Types/FrameData.hlsli"

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
		LocalToRoot = Output + Raytracing::MAX_SHAPES,
		UpdateData,
		BoneMatrices,
		DynamicVertices,
		SkinningData = DynamicVertices + Raytracing::MAX_SHAPES,
		NumDescriptors = SkinningData + Raytracing::MAX_SHAPES,
		None
	};
};
using SkinningHeap = Heap<SkinningHeapDef::Table, SkinningHeapDef::Slot>;

struct SkinningPipeline : ComputePipeline<SkinningHeap>
{
	static constexpr uint THREAD_SIZE = 16;

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

	struct alignas(16) FrameData
	{
		uint Count;
		uint3 Pad;
	};

	eastl::unique_ptr<FrameData> constantBufferData = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUpload<FrameData>> constantBuffer = nullptr;

	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void SetupResources(ID3D12Device5* device) override;
	void QueueUpdate(Flags updateFlags, eastl::string name, Shape* shape);
	bool PrepareResources(uint& count);
	void Dispatch(ID3D12GraphicsCommandList4* commandList);
};