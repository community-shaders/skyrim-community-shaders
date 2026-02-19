#pragma once

#include <directx/d3d12.h>
#include <d3d11_4.h>

struct CreationEngineRaytracing
{
	HMODULE handle = nullptr;

	using InitializeFn = bool (*)(ID3D12Device5*, ID3D12CommandQueue*);
	using SetScreenSizeFn = bool (*)(uint32_t, uint32_t);
	using SetupResourcesFn = void (*)();
	using UpdateFrameBufferFn = void (*)(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position);
	using ExecuteFn = void (*)();
	using WaitExecutionFn = void (*)();
	using AttachModelFn = void (*)(RE::TESForm*);

	InitializeFn Initialize = nullptr;
	SetScreenSizeFn SetScreenSize = nullptr;
	SetupResourcesFn SetupResources = nullptr;
	UpdateFrameBufferFn UpdateFrameBuffer = nullptr;
	ExecuteFn Execute = nullptr;
	WaitExecutionFn WaitExecution = nullptr;
	AttachModelFn AttachModel = nullptr;

	CreationEngineRaytracing()
	{
		handle = LoadLibraryA("Data\\CreationEngineRaytracing.dll");

		if (!handle) {
			logger::error("[Raytracing] LoadLibrary failed for 'CreationEngineRaytracing.dll'");
			return;
		}

		logger::info("[Raytracing] 'CreationEngineRaytracing.dll' loaded");

		Initialize = reinterpret_cast<InitializeFn>(GetProcAddress(handle, "Initialize"));

		if (!Initialize)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' Initialize is nullptr");

		SetScreenSize = reinterpret_cast<SetScreenSizeFn>(GetProcAddress(handle, "SetScreenSize"));

		if (!SetScreenSize)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetScreenSize is nullptr");

		SetupResources = reinterpret_cast<SetupResourcesFn>(GetProcAddress(handle, "SetupResources"));

		if (!SetupResources)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' SetupResources is nullptr");

		UpdateFrameBuffer = reinterpret_cast<UpdateFrameBufferFn>(GetProcAddress(handle, "UpdateFrameBuffer"));

		if (!UpdateFrameBuffer)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' UpdateFrameBuffer is nullptr");

		Execute = reinterpret_cast<ExecuteFn>(GetProcAddress(handle, "Execute"));

		if (!Execute)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' Execute is nullptr");

		WaitExecution = reinterpret_cast<WaitExecutionFn>(GetProcAddress(handle, "WaitExecution"));

		if (!WaitExecution)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' WaitExecution is nullptr");

		AttachModel = reinterpret_cast<AttachModelFn>(GetProcAddress(handle, "AttachModel"));

		if (!AttachModel)
			logger::error("[Raytracing] 'CreationEngineRaytracing.dll' AttachModel is nullptr");
	}
};

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
	void InitializeCERaytracing(ID3D12Device5* device, ID3D12CommandQueue* commandQueue);
	void Main_RenderPlayerView_Before() const;
	void DeferredPasses() const;

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool Enabled = true;
	} settings;

	bool initialized = false;
	bool forcedDisabled = false;

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

	eastl::unique_ptr<CreationEngineRaytracing> creationEngineRaytracing = nullptr;

	// D3D11
	winrt::com_ptr<ID3D11Device5> d3d11Device = nullptr;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context = nullptr;

	// D3D12
	winrt::com_ptr<ID3D12Device5> d3d12Device = nullptr;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue = nullptr;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocator = nullptr;
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandList = nullptr;

	winrt::com_ptr<ID3D11Fence> d3d11Fence = nullptr;
	winrt::com_ptr<ID3D12Fence> d3d12Fence = nullptr;
	uint64_t fenceValue = 0;

	struct Hooks
	{
		struct TES_AttachModel
		{
			static void thunk(RE::TES* a1, RE::TESObjectREFR* refr, RE::TESObjectCELL* cell, void* queuedTree, char a5, RE::NiNode* a6)
			{
				func(a1, refr, cell, queuedTree, a5, a6);

				globals::features::raytracing.creationEngineRaytracing->AttachModel(refr);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderPlayerView
		{
			static void thunk(void* a1, bool a2, bool a3)
			{
				globals::features::raytracing.Main_RenderPlayerView_Before();

				func(a1, a2, a3);
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::detour_thunk<TES_AttachModel>(REL::RelocationID(13209, 13355));

			stl::detour_thunk<Main_RenderPlayerView>(REL::RelocationID(35560, 36559));

			logger::info("[Raytracing] Installed hooks");
		}

		static void InstallD3D11Hooks(ID3D11Device* device) 
		{
			logger::info("[Raytracing] Installed D3D11 hooks - [0x{:08X}]", reinterpret_cast<uintptr_t>(device));
		}
	};
};