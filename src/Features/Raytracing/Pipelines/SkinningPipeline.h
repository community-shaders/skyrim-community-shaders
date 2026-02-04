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
		VertexBuffer,
		SkinningBuffer
	};

	enum class Slot
	{
		Output,
		UpdateData = Output + RTConstants::MAX_SHAPES,
		BoneMatrices,
		DynamicVertices,
		Vertices = DynamicVertices + RTConstants::MAX_SHAPES,
		SkinningData = Vertices + RTConstants::MAX_SHAPES,
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

	static constexpr uint MAX_GEOMETRY = 2048;

	static constexpr uint MAX_BONE_MATRICES = MAX_GEOMETRY * 10;

	struct Settings
	{
		bool OptimizedMapping = false;
		uint ThreadGroupSize = 32;
	} settings;

	struct QueuedShape
	{
		Shape::Flags updateFlags;
		eastl::string path;
	};

	eastl::unordered_map<Shape*, QueuedShape> queuedShapes;

	eastl::unique_ptr<DX12::StructuredBufferUpload<VertexUpdateData>> vertexUpdateBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUpload<float3x4>> boneMatricesBuffer = nullptr;

	eastl::vector<VertexUpdateData> vertexUpdateData;
	eastl::vector<float3x4> boneMatricesData;

	eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;

	Util::FrameChecker frameChecker;

	bool recompile;

	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void SetupResources(ID3D12Device5* device) override;
	void QueueUpdate(Shape::Flags updateFlags, eastl::string name, Shape* shape);
	bool PrepareResources(ID3D12GraphicsCommandList4* commandList, uint& count, uint& vertexCount);
	void RestoreResources(ID3D12GraphicsCommandList4* commandList);
	void ClearQueue();
	void Dispatch(ID3D12GraphicsCommandList4* commandList, ID3D12Device5* device);
};