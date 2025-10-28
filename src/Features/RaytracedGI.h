
/*
* This file defines a new feature template for Community Shader.
* Copy the .h and .cpp files to src/Features and rename them to your feature's name.
* Replace all NewFeature occurances in both files as well, and change the metadata accordingly.
* Don't forget to add the feature singleton to src/Feature.cpp, Globals.h & Globals.cpp
* and copy and rename the "New Feature" folder and contents to features/ so it gets registered.
*
* The naming and coding style are adapted to my personal practice,
* but we don't really have a strict, solidified guideline on that.
* So take your liberties within reason.
*
* Cheers,
* ProfJack
* 2025-06-28
*/

#pragma once

#include "Feature.h"
#include <d3d12.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include "Features/Upscaling/DX12SwapChain.h"
#include <dxcapi.h>

struct RaytracedGI : public Feature
{
	////////////////////////////////////////////////// Boilerplate
	// Metadata
	virtual inline std::string GetName() override { return "Raytraced GI"; }
	virtual inline std::string GetShortName() override { return "RaytracedGI"; }
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
	virtual bool inline SupportsVR() override { return true; }
	virtual inline std::string_view GetShaderDefineName() override { return "RTGI"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; };

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	virtual void PostPostLoad() override;

	// Resources
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileShaders();
	void CompileRaytracingShaders();
	void CompileComputeShaders();

	void Initialize();
	void InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* ppImmediateContext, IDXGIAdapter* a_adapter);
	void CreateRootSignature();
	ID3D12Resource* MakeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize = nullptr);
	void Flush();
	ID3D12Resource* MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertexFloats, ID3D12Resource* indexBuffer = nullptr, UINT indices = 0);
	ID3D12Resource* MakeTLAS(ID3D12Resource* instances, UINT numInstances, UINT64* updateScratchSize);
	void UpdateTransforms();
	void DrawRTGI();

	static constexpr float quadVtx[] = { -1, 0, -1, -1, 0, 1, 1, 0, 1, -1, 0, -1, 1, 0, -1, 1, 0, 1 };
	static constexpr float cubeVtx[] = { -1, -1, -1, 1, -1, -1, -1, 1, -1, 1, 1, -1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1, 1 };
	static constexpr short cubeIdx[] = { 4, 6, 0, 2, 0, 6, 0, 1, 4, 5, 4, 1, 0, 2, 1, 3, 1, 2, 1, 3, 5, 7, 5, 3, 2, 6, 3, 7, 3, 6, 4, 5, 6, 7, 6, 5 };
	static constexpr UINT NUM_INSTANCES = 3;
	static constexpr UINT64 NUM_SHADER_IDS = 3;

	static constexpr DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };
	static constexpr D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };
	static constexpr D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };
	static constexpr D3D12_RESOURCE_DESC BASIC_BUFFER_DESC = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = 0,  // Will be changed in copies
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.SampleDesc = NO_AA,
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR
	};

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		float3 ColorA = { 0.3f, 0.5f, 0.7f };
		std::array<uint, 2> IdA = { 1, 2 };  // std::array is because we haven't defined XMUINT2 serialization yet
		float2 UvA = { 0.0f, 0.0f };
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

	winrt::com_ptr<ID3D12Resource> quadVB = nullptr;
	winrt::com_ptr<ID3D12Resource> cubeVB = nullptr;
	winrt::com_ptr<ID3D12Resource> cubeIB = nullptr;

	winrt::com_ptr<ID3D12Resource> quadBlas = nullptr;
	winrt::com_ptr<ID3D12Resource> cubeBlas = nullptr;

	winrt::com_ptr<ID3D12Resource> instances = nullptr;
	D3D12_RAYTRACING_INSTANCE_DESC* instanceData;
	
	winrt::com_ptr<ID3D12Resource> tlas = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasUpdateScratch = nullptr;

	// Shaders
	/*winrt::com_ptr<IDxcBlob> rayGenerationRT = nullptr;
	winrt::com_ptr<IDxcBlob> missRT = nullptr;
	winrt::com_ptr<IDxcBlob> closestHitRT = nullptr;*/

	winrt::com_ptr<ID3D12StateObject> pipelineRT = nullptr;
	winrt::com_ptr<ID3D12Resource> shaderIDs = nullptr;

	// D3D12
	winrt::com_ptr<ID3D12Device5> d3d12Device = nullptr;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue = nullptr;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocator = nullptr;
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandList = nullptr;

	winrt::com_ptr<ID3D12RootSignature> rootSignature = nullptr;

	winrt::com_ptr<ID3D12DescriptorHeap> uavHeap = nullptr;	

	winrt::com_ptr<ID3D11Fence> d3d11Fence = nullptr;
	winrt::com_ptr<ID3D12Fence> d3d12Fence = nullptr;

	UINT64 fenceValue = 1;

	// D3D11
	winrt::com_ptr<ID3D11Device5> d3d11Device = nullptr;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context = nullptr;

	// Resources
	eastl::unique_ptr<WrappedResource> diffuseGITexture = nullptr;
	eastl::unique_ptr<WrappedResource> specularGITexture = nullptr;

	struct Hooks
	{
		static void Install()
		{
			logger::info("[RTGI] Installed hooks");
		}
	};
};