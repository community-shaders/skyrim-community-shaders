#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/Utils.h"
#include "PCH.h"
#include <d3d12.h>

#include "Features/Raytracing/Types.h"

struct SVGFVarianceHeapDef
{
	enum class Table
	{
		UAV,
		SRV
	};

	enum class Slot
	{
		Variance,
		History,
		Moments,
		NormalRoughness,
		Temporal,
		Depth,
		NumDescriptors,
		None
	};
};
using SVGFVarianceHeap = Heap<SVGFVarianceHeapDef::Table, SVGFVarianceHeapDef::Slot>;

struct SVGFVariance : ComputePipeline<SVGFVarianceHeap>
{
	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void Dispatch(ID3D12GraphicsCommandList4* commandList, uint2 dispatchCount, ID3D12Resource* frameBuffer);
	void RegisterResources(ID3D12Device5* device,
		DX12::Texture2D* varianceTexture,
		DX12::Texture2D* historyTexture,
		DX12::Texture2D* momentsTexture,
		ID3D12Resource* normalRoughnessResource,
		DX12::Texture2D* temporalTexture,
		ID3D12Resource* depthResource);
};