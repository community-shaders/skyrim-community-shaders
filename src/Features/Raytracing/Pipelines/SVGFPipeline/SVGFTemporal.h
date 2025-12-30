#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Pipeline.h"
#include "Features/Raytracing/Utils.h"
#include "PCH.h"
#include <d3d12.h>

#include "Features/Raytracing/Types.h"

struct SVGFTemporalHeapDef
{
	enum class Table
	{
		UAV,
		SRV
	};

	enum class Slot
	{
		Temporal,
		Moments,
		History,
		MotionVectors,
		NormalRoughness,
		Color,
		Depth,
		HistoryMoments,
		HistoryNormals,
		NumDescriptors,
		None
	};
};
using SVGFTemporalHeap = Heap<SVGFTemporalHeapDef::Table, SVGFTemporalHeapDef::Slot>;

struct SVGFTemporal : ComputePipeline<SVGFTemporalHeap>
{
	void CreateRootSignature(ID3D12Device5* device) override;
	void CompileShaders(ID3D12Device5* device) override;
	void Dispatch(ID3D12GraphicsCommandList4* commandList, uint2 dispatchCount, ID3D12Resource* frameBuffer);
	void RegisterResources(ID3D12Device5* device,
		DX12::Texture2D* temporalTexture,
		DX12::Texture2D* momentsTexture,
		DX12::Texture2D* historyTexture,
		ID3D12Resource* motionVectorResource,
		ID3D12Resource* normalRoughnessResource,
		ID3D12Resource* colorResource,
		ID3D12Resource* depthResource,
		DX12::Texture2D* historyMomentsTexture,
		DX12::Texture2D* historyNormalsTexture);
};