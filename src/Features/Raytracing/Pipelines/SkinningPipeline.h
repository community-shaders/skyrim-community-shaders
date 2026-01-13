#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/RTConstants.h"
#include "Features/Raytracing/Types.h"

#include "Features/Raytracing/Core/Shape.h"

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
	static constexpr uint MIN_THREAD_GROUP_SIZE = 4;
	static constexpr uint MAX_THREAD_GROUP_SIZE = 64;

	static constexpr uint MAX_BATCHES = 4;

	struct Settings
	{
		bool OptimizedMapping = false;
		uint ThreadGroupSize = 32;
		bool Dispatch = true;
		bool UpdateBLAS = true;
	} settings;

	struct QueuedShape
	{
		// Model path, where Model = collection of Shapes
		Flags updateFlags;
		eastl::string path;
		Shape* shape;
		float3x4 localToRoot;
	};

	eastl::vector<QueuedShape> queuedShapes;
	eastl::unique_ptr<DX12::StructuredBufferUpload<VertexUpdateData>> vertexUpdateBuffer = nullptr;

	Util::FrameChecker frameChecker;

	bool recompile;

	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void SetupResources(ID3D12Device5* device) override;
	void QueueUpdate(Flags updateFlags, eastl::string name, Shape* shape, const float3x4& localToRoot);
	bool PrepareResources(ID3D12GraphicsCommandList4* commandList, uint& count, uint& vertexCount);
	void UpdateBLASES(ID3D12GraphicsCommandList4* commandList);
	void RestoreResources(ID3D12GraphicsCommandList4* commandList);
	void ClearQueue();
	void Dispatch(ID3D12GraphicsCommandList4* commandList, ID3D12Device5* device);
};