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

	struct Settings
	{
		bool EnablePIXCapture = false;
		bool EnableDebugDevice = false;
	} settings;

	winrt::com_ptr<ID3D12Device5> d3d12Device;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocators[2];
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandLists[2];

	WrappedResource* swapChainBufferWrapped;
	WrappedResource* uiBufferWrapped;

	// D3D12 interop resources for frame generation
	WrappedResource* depthBufferShared12 = nullptr;
	WrappedResource* motionVectorBufferShared12 = nullptr;

	winrt::com_ptr<ID3D11Device5> d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

	winrt::com_ptr<ID3D11Fence> d3d11Fence;
	winrt::com_ptr<ID3D12Fence> d3d12Fence;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;

	bool active = false;

	void CreateInterop();

	void SetUIBuffer();

	void CreateSharedResources();

	void Init(ID3D11Device* d3d11Device, ID3D11DeviceContext* immediateContext, IDXGIAdapter* adapter);

	bool Active() const;

private:
	void SetD3D11Device(ID3D11Device* a_d3d11Device);
	void SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context);
	void InitializePIX();
	void CreateD3D12Device(IDXGIAdapter* a_adapter);
};
