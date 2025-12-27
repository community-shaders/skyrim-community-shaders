#pragma once

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/ShaderUtils.h"
#include "Features/Raytracing/Utils.h"
#include <d3d12.h>
#include <dxcapi.h>

struct IPipeline
{
	virtual ~IPipeline() = default;

	virtual void CreateRootSignature(ID3D12Device5* device) = 0;
	virtual void CompileShaders(ID3D12Device5* device) = 0;
	virtual void SetupResources(ID3D12Device5* device) = 0;
};

template <IsHeap HeapType>
struct Pipeline : IPipeline
{
	winrt::com_ptr<ID3D12RootSignature> rootSignature = nullptr;
	eastl::unique_ptr<DX12::DescriptorHeap<HeapType>> heap = nullptr;
};

template <IsHeap HeapType>
struct RaytracingPipeline : Pipeline<HeapType>
{
	winrt::com_ptr<ID3D12StateObject> stateObject = nullptr;
};

template <IsHeap HeapType>
struct ComputePipeline : Pipeline<HeapType>
{
	winrt::com_ptr<ID3D12PipelineState> pipelineState = nullptr;
};
