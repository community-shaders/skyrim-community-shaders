#pragma once

#include <d3d11_4.h>

struct Raytracing : public Feature
{
	////////////////////////////////////////////////// Boilerplate
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

	void CreateD3D12Device(ID3D11Device* device, ID3D11DeviceContext* immediateContext, IDXGIAdapter* adapter);
	void InitializeCERaytracing(ID3D12Device5* device, ID3D12CommandQueue* commandQueue);

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool Enabled = true;
	} settings;

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
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::detour_thunk<TES_AttachModel>(REL::RelocationID(13209, 13355));
			logger::info("[Raytracing] Installed hooks");
		}

		static void InstallD3D11Hooks(ID3D11Device* device) 
		{
			logger::info("[RT] Installed D3D11 hooks - {}", reinterpret_cast<uintptr_t>(device));
		}
	};
};