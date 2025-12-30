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

	virtual void Initialize() {}
	virtual void CreateRootSignature([[maybe_unused]] ID3D12Device5* device) {}
	virtual void CompileShaders([[maybe_unused]] ID3D12Device5* device) {}
	virtual void SetupResources([[maybe_unused]] ID3D12Device5* device) {}
};

template <IsHeap HeapType>
struct Pipeline : IPipeline
{
	winrt::com_ptr<ID3D12RootSignature> rootSignature = nullptr;
	eastl::unique_ptr<DX12::DescriptorHeap<HeapType>> heap = nullptr;
};

struct MasterPipeline : IPipeline
{
	std::vector<eastl::unique_ptr<IPipeline>> subPipelines;
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