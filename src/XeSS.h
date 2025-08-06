#pragma once

#include "Buffer.h"
#include "State.h"
#include "DX12SwapChain.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

// Include XeSS headers
#include <xess/xess.h>
#include <xess/xess_d3d12.h>

using Microsoft::WRL::ComPtr;

// XeSS function pointers - matching exact signatures from xess.h and xess_d3d12.h
typedef xess_result_t (*xessGetVersionPtr)(xess_version_t* pVersion);
typedef xess_result_t (*xessD3D12CreateContextPtr)(ID3D12Device* pDevice, xess_context_handle_t* phContext);
typedef xess_result_t (*xessD3D12InitPtr)(xess_context_handle_t hContext, const xess_d3d12_init_params_t* pInitParams);
typedef xess_result_t (*xessD3D12ExecutePtr)(xess_context_handle_t hContext, ID3D12GraphicsCommandList* pCommandList, const xess_d3d12_execute_params_t* pExecParams);
typedef xess_result_t (*xessDestroyContextPtr)(xess_context_handle_t hContext);

class XeSS
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\SKSE\\Plugins\\XeSS";

	static XeSS* GetSingleton()
	{
		static XeSS singleton;
		return &singleton;
	}

	HMODULE module = nullptr;

	// XeSS function pointers
	xessGetVersionPtr xessGetVersion = nullptr;
	xessD3D12CreateContextPtr xessD3D12CreateContext = nullptr;
	xessD3D12InitPtr xessD3D12Init = nullptr;
	xessD3D12ExecutePtr xessD3D12Execute = nullptr;
	xessDestroyContextPtr xessDestroyContext = nullptr;

	xess_context_handle_t xessContext = nullptr;

	bool featureXeSS = false;  // whether enabled

	// XeSS-specific shared textures (input/output) using WrappedResource
	WrappedResource* inputColorTexture = nullptr;
	WrappedResource* outputColorTexture = nullptr;

	// Cached DLL version info for XeSS plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadXeSS();
	void CreateXeSSResources();
	void DestroyXeSSResources();
	void Upscale(ID3D11Resource* a_inputTexture, ID3D11Resource* a_outputTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_motionVectors, ID3D11Resource* a_depth, float2 a_jitter);
};