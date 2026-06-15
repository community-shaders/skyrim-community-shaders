#pragma once

#include "DX12Interop/WrappedResource.h"
#include "Feature.h"
#include "State.h"

#include <winrt/base.h>

#include <d3d11_4.h>
#include <directx/d3dx12.h>
#include <type_traits>
#include <utility>

#define NTDDI_VERSION NTDDI_WINBLUE
#include <DXProgrammableCapture.h>

struct DX12Interop
{
	static constexpr UINT kMaxFramesInFlight = 2;

	winrt::com_ptr<ID3D12Device5> d3d12Device;

	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	winrt::com_ptr<ID3D12CommandQueue> computeCommandQueue;
	winrt::com_ptr<ID3D12CommandQueue> copyCommandQueue;

	UINT64 currentFenceValue = 0;
	HANDLE fenceEvent = nullptr;

	struct SharedResources
	{
		WrappedResource* main = nullptr;
		WrappedResource* upscaleOutput = nullptr;
		WrappedResource* depth = nullptr;
		WrappedResource* motionVector = nullptr;
		WrappedResource* reactiveMask = nullptr;
	} sharedResources;

	winrt::com_ptr<ID3D11Device5> d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

	winrt::com_ptr<ID3D11Fence> d3d11Fence;
	winrt::com_ptr<ID3D12Fence> d3d12Fence;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;

	static DX12Interop* GetSingleton()
	{
		static DX12Interop singleton;
		return &singleton;
	}

	~DX12Interop();

	void Init(ID3D11Device* d3d11Device, ID3D11DeviceContext* a_immediateContext, IDXGIAdapter* a_adapter);

	// Resources
	void SetupResources();

	bool Active() const;

	// Whether DirectX 12 is required or not
	// True when Upscaling is loaded in frame generation mode or Raytracing is loaded
	static bool D3D12Mode();

	UINT GetFrameContextIndex() const;

	// Fences the GPU, waits for D3D11 to be idle, executes the provided function, then waits for D3D12 to be idle before returning.
	template <typename Func>
	void Fence(Func func)
	{
		// Wait for D3D11 to finish
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), ++currentFenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), currentFenceValue));

		// Execute
		func();

		// Wait for D3D12 to finish
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), ++currentFenceValue));
		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), currentFenceValue));
	}

private:
	bool active = false;

	void CreateInterop();
	void SetD3D11Device(ID3D11Device* a_d3d11Device);
	void SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context);
	void InitializePIX();

	void CreateD3D12Device(IDXGIAdapter* a_adapter);
};
