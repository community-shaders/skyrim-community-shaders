#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/Utils.h"
#include "PCH.h"
#include <d3d12.h>

#include "Features/Raytracing/Types.h"

#include "Raytracing/Denoiser/SVGF/SVGF.hlsli"

struct SVGFSpatialHeapDef
{
	enum class Table
	{
		UAV,
		SRV
	};

	enum class Slot
	{
		ColorVariance,
		History,
		MotionVectors,
		NormalRoughness,
		VarianceColor,
		Depth,
		NumDescriptors,
		None
	};
};
using SVGFSpatialHeap = Heap<SVGFSpatialHeapDef::Table, SVGFSpatialHeapDef::Slot>;

struct SVGFSpatial : ComputePipeline<SVGFSpatialHeap>
{
	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void Dispatch(ID3D12GraphicsCommandList4* commandList, uint atrousIterations, uint2 dispatchCount, DX12::StructuredBufferUpload<SVGF>* frameBuffer, ID3D12Resource* varianceResource, ID3D12Resource* colorResource);
	void RegisterResources(ID3D12Device5* device,
		ID3D12Resource* colorResource,
		DX12::Texture2D* historyTexture,
		ID3D12Resource* motionVectorResource,
		ID3D12Resource* normalRoughnessResource,
		DX12::Texture2D* varianceTexture,
		ID3D12Resource* depthResource);
};