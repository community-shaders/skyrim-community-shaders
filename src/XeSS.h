#pragma once

#include "Buffer.h"
#include "State.h"

// Forward declarations for XeSS types
typedef struct xess_context_handle* xess_context_handle_t;
typedef struct xess_d3d11_init_params xess_d3d11_init_params_t;
typedef struct xess_d3d11_execute_params xess_d3d11_execute_params_t;

// XeSS function pointers
typedef int (*xessGetVersionPtr)();
typedef int (*xessD3D11CreateContextPtr)(ID3D11Device* pDevice, xess_context_handle_t* ppContext);
typedef int (*xessD3D11InitPtr)(xess_context_handle_t pContext, const xess_d3d11_init_params_t* pInitParams);
typedef int (*xessD3D11ExecutePtr)(xess_context_handle_t pContext, const xess_d3d11_execute_params_t* pExecParams);
typedef void (*xessDestroyContextPtr)(xess_context_handle_t pContext);

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
	xessD3D11CreateContextPtr xessD3D11CreateContext = nullptr;
	xessD3D11InitPtr xessD3D11Init = nullptr;
	xessD3D11ExecutePtr xessD3D11Execute = nullptr;
	xessDestroyContextPtr xessDestroyContext = nullptr;

	xess_context_handle_t xessContext = nullptr;

	bool featureXeSS = false;  // whether enabled

	// Cached DLL version info for XeSS plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadXeSS();

	void CreateXeSSResources();
	void DestroyXeSSResources();
	void Upscale(ID3D11Resource* a_inputTexture, ID3D11Resource* a_outputTexture, ID3D11Resource* a_motionVectors, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_depth, float2 a_jitter);
};