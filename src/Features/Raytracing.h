#pragma once

#include <directx/d3d12.h>
#include <d3d11_4.h>

#include "Features/Upscaling/DX12SwapChain.h"

#include "Util.h"

#define NTDDI_VERSION NTDDI_WINBLUE

#include <DXProgrammableCapture.h>

struct CreationEngineRaytracing
{
	HMODULE handle = nullptr;

	using InitializeFn = bool (*)(ID3D11Device5*, ID3D12Device5*, ID3D12CommandQueue*, ID3D12CommandQueue*, ID3D12CommandQueue*);
	using WaitExecutionFn = void (*)();
	using GetResolutionFn = void (*)(uint32_t&, uint32_t&);
	using SetResolutionFn = void (*)(uint32_t, uint32_t);
	using SetCopyTargetFn = void (*)(ID3D12Resource*);

	InitializeFn Initialize = nullptr;
	WaitExecutionFn WaitExecution = nullptr;
	SetResolutionFn SetResolution = nullptr;
	SetCopyTargetFn SetCopyTarget = nullptr;

	CreationEngineRaytracing()
	{
		GetModuleHandleEx(
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			L"CreationEngineRaytracing.dll",
			&handle);

		if (!handle) {
			logger::critical("[Raytracing] 'CreationEngineRaytracing.dll' not found, make sure Creation Engine Raytracing is enabled in your mod manager.");
			return;
		}

		Initialize = reinterpret_cast<InitializeFn>(GetProcAddress(handle, "Initialize"));

		if (!Initialize)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' Initialize is nullptr");

		WaitExecution = reinterpret_cast<WaitExecutionFn>(GetProcAddress(handle, "WaitExecution"));

		if (!WaitExecution)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' WaitExecution is nullptr");

		SetResolution = reinterpret_cast<SetResolutionFn>(GetProcAddress(handle, "SetResolution"));

		if (!SetResolution)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetResolution is nullptr");

		SetCopyTarget = reinterpret_cast<SetCopyTargetFn>(GetProcAddress(handle, "SetCopyTarget"));

		if (!SetCopyTarget)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetCopyTarget is nullptr");
	}
};

struct uint2
{
	uint x;
	uint y;

	bool operator==(const uint2&) const = default;
	bool operator!=(const uint2&) const = default;
};
static_assert(sizeof(uint2) == 8);

struct Raytracing : public Feature
{
	// Metadata
	virtual inline std::string GetName() override { return "Raytracing"; }
	virtual inline std::string GetShortName() override { return "Raytracing"; }
	virtual inline std::string_view GetCategory() const override { return "Lighting"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"This is a terse description.",
			{
				"This is a subfeature.",
				"This is another subfeature.",
				"Cheese.",
			}
		};
	}

	// Functionality
	virtual bool inline SupportsVR() override { return false; }
	virtual inline std::string_view GetShaderDefineName() override { return "RAYTRACING"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; };

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	// Resources
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileShaders();

	void Load() override;
	void PostPostLoad() override;

	void CreateD3D12Device(ID3D11Device* device, ID3D11DeviceContext* immediateContext, IDXGIAdapter* adapter);
	void InitializeCERaytracing(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue);
	bool UpdateResolution();
	void DeferredPasses();

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool Enabled = true;
		bool EnablePIXCapture = false;
	} settings;

	bool initialized = false;
	bool forcedDisabled = false;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;

	bool pixCapture = false;
	bool pixCaptureStarted = false;

	uint2 m_Resolution;

	enum DisableReason
	{
		None,
		UnsupportedGPU,
		OutdatedDrivers,
		MissingPlugin,
		InitFailed,
	} disableReason = DisableReason::None;

	struct CbData
	{
		float3 ColorA;
		float _pad0;  // Padding to align to 16 bytes
		DirectX::XMUINT2 IdA;
		float2 UvA;
	};
	static_assert(sizeof(CbData) % 16 == 0,
		"CbData must be aligned to 16 bytes. "
		"Check out maraneshi.github.io/HLSL-ConstantBufferLayoutVisualizer/ if you're unsure.");

	eastl::unique_ptr<ConstantBuffer> cheeseCb = nullptr;  // Omit this if you want to put your CB in src/FeatureBuffer.cpp
	eastl::unique_ptr<Texture2D> cheeseTex = nullptr;
	winrt::com_ptr<ID3D11SamplerState> cheeseSampler = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> cheeseCs = nullptr;

	eastl::unique_ptr<WrappedResource> mainTexture = nullptr; 

	eastl::unique_ptr<CreationEngineRaytracing> creationEngineRaytracing = nullptr;

	// D3D11
	winrt::com_ptr<ID3D11Device5> m_D3D11Device = nullptr;
	winrt::com_ptr<ID3D11DeviceContext4> m_D3D11Context = nullptr;

	// D3D12
	winrt::com_ptr<ID3D12Device5> m_D3D12Device = nullptr;

	winrt::com_ptr<ID3D12CommandQueue> m_CommandQueue = nullptr;
	winrt::com_ptr<ID3D12CommandQueue> m_ComputeCommandQueue = nullptr;
	winrt::com_ptr<ID3D12CommandQueue> m_CopyCommandQueue = nullptr;

	//winrt::com_ptr<ID3D12CommandAllocator> commandAllocator = nullptr;
	//winrt::com_ptr<ID3D12GraphicsCommandList4> commandList = nullptr;

	winrt::com_ptr<ID3D11Fence> d3d11Fence = nullptr;
	winrt::com_ptr<ID3D12Fence> d3d12Fence = nullptr;
	uint64_t fenceValue = 0;

	struct Hooks
	{
		static void InstallD3D11Hooks(ID3D11Device* device) 
		{
			logger::info("[Raytracing] Installed D3D11 hooks - [0x{:08X}]", reinterpret_cast<uintptr_t>(device));
		}
	};
};