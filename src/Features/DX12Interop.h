#pragma once

#include <winrt/base.h>

#include <d3d11_4.h>
#include <directx/d3dx12.h>

#include "DX12Interop/WrappedResource.h"

#include "Feature.h"

#define NTDDI_VERSION NTDDI_WINBLUE

#include <DXProgrammableCapture.h>

struct DX12Interop : public Feature
{
	virtual inline std::string GetName() override { return "DirectX 12 Interoperability"; }
	virtual inline std::string GetShortName() override { return "DX12Interop"; }
	virtual bool IsCore() const override { return true; }

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	// Resources
	virtual void SetupResources() override;

	struct Settings
	{
		bool EnablePIXCapture = false;
		bool EnableDebugDevice = false;
	} settings;

	winrt::com_ptr<ID3D12Device5> d3d12Device;

	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	winrt::com_ptr<ID3D12CommandQueue> computeCommandQueue;
	winrt::com_ptr<ID3D12CommandQueue> copyCommandQueue;

	winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandList;

	struct SharedResources
	{
		WrappedResource* main = nullptr;
		WrappedResource* depth = nullptr;
		WrappedResource* motionVector = nullptr;
		WrappedResource* reactiveMask = nullptr;
	} sharedResources;

	winrt::com_ptr<ID3D11Device5> d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

	winrt::com_ptr<ID3D11Fence> d3d11Fence;
	winrt::com_ptr<ID3D12Fence> d3d12Fence;

	UINT64 fenceValue = 0;
	HANDLE fenceEvent;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;

	bool pixCapture = false;
	bool pixCaptureStarted = false;

	bool active = false;

	void CreateInterop();

	void SetUIBuffer();

	void CreateSharedResources();

	void Init(ID3D11Device* d3d11Device, ID3D11DeviceContext* immediateContext, IDXGIAdapter* adapter);

	template <typename Func>
	void Fence(Func func)
	{
		d3d11Context->Flush();

		// Wait for D3D11 to finish
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;

		// Execute
		func();

		// Wait for D3D12 to finish
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

		if (d3d12Fence->GetCompletedValue() < fenceValue) {
			DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, fenceEvent));
			WaitForSingleObject(fenceEvent, INFINITE);
		}

		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;
	}

	// Executes D3D12 commands mid D3D11 execution, probably huge overhead from wait commands so use sparsely and wisely
	template <typename Func>
	void Execute(Func func)
	{
		d3d11Context->Flush();

		// Wait for D3D11 to finish
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;

		// New frame, reset
		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));

		// Execute
		func(commandList.get());

		DX::ThrowIfFailed(commandList->Close());

		ID3D12CommandList* commandListsToExecute[] = { commandList.get() };
		commandQueue->ExecuteCommandLists(1, commandListsToExecute);

		// Wait for D3D12 to finish
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

		if (d3d12Fence->GetCompletedValue() < fenceValue) {
			DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, fenceEvent));
			WaitForSingleObject(fenceEvent, INFINITE);
		}

		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;
	}

	bool Active() const;
	
	// Wether DirectX 12 is required or not
	// True when Raytracing is loaded or Upscaling is loaded in frame generation mode
	static bool D3D12Mode();

private:
	void SetD3D11Device(ID3D11Device* a_d3d11Device);
	void SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context);
	void InitializePIX();
	void CreateD3D12Device(IDXGIAdapter* a_adapter);
};
