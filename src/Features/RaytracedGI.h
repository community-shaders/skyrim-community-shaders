
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
#include <directxpackedvector.h>

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

	void Main_RenderWorld(bool a1);
	void BSShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
	void BSBatchRenderer_RenderPassImmediately(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);

	//void RegisterVertexBuffer(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer);
	//void RegisterIndexBuffer(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer);
	void RegisterInputLayout(ID3D11InputLayout* pInputLayout, D3D11_INPUT_ELEMENT_DESC* pInputElementDescs, UINT NumElements);
	void UnregisterBuffer(ID3D11Buffer* ppBuffer);
	void CreateBuffers();

	template <typename T>
	void MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res);

	const bool Active() 
	{
		return loaded && settings.Enabled;
	};

	//void BSShader_RestoreGeometry(RE::BSShader* This, RE::BSRenderPass* Pass);

	//void BSTriShape_UpdateWorldData(RE::BSTriShape* This, RE::NiUpdateData* a_data);
	
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
		uint Enabled = true;
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

	bool renderingWorld = false;
	bool buffersCreated = false;

	struct VertexData
	{
		float4 Position;
		uint16_t Texcoord[2];
		uint8_t Normal[4];
		uint8_t Color[4];
	};

	struct InstanceData
	{
		float4x4 World;
	};

	eastl::unordered_map<ID3D11Buffer*, eastl::unique_ptr<uint8_t[]>> vertexData;
	eastl::unordered_map<ID3D11Buffer*, eastl::unique_ptr<uint8_t[]>> indexData;

	//eastl::unordered_map<RE::BSTriShape*, winrt::com_ptr<ID3D12Resource>> cachedTrishapes;

	eastl::unordered_map<ID3D11Buffer*, winrt::com_ptr<ID3D12Resource>> vertexBuffers;
	eastl::unordered_map<ID3D11Buffer*, winrt::com_ptr<ID3D12Resource>> indexBuffers;

	struct InputLayout {
		eastl::string SemanticName;
		UINT SemanticIndex;
		DXGI_FORMAT Format;
		UINT InputSlot;
		UINT AlignedByteOffset;
		D3D11_INPUT_CLASSIFICATION InputSlotClass;
		UINT InstanceDataStepRate;
	};

	eastl::unordered_map<ID3D11InputLayout*, eastl::vector<InputLayout>> inputLayouts;
	eastl::unordered_map<uint64_t, ID3D11InputLayout*> vertexDescToInputLayout;

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
		struct ID3D11Device_CreateBuffer
		{
			static HRESULT thunk(ID3D11Device* This, const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer)
			{
				HRESULT hr = func(This, pDesc, pInitialData, ppBuffer);

				auto& rtgi = globals::features::raytracedGI;

				if (rtgi.Active() && pInitialData) {
					bool vertexBuffer = pDesc->BindFlags & D3D11_BIND_VERTEX_BUFFER;
					bool indexBuffer = pDesc->BindFlags & D3D11_BIND_INDEX_BUFFER;

					if (vertexBuffer || indexBuffer) {
						size_t bufferSize = static_cast<size_t>(pDesc->ByteWidth);

						eastl::unique_ptr<uint8_t[]> data{ new uint8_t[bufferSize] };

						std::memcpy(data.get(), pInitialData->pSysMem, bufferSize);

						if (vertexBuffer) 
							rtgi.vertexData.emplace(*ppBuffer, eastl::move(data));
						else if (indexBuffer)
							rtgi.indexData.emplace(*ppBuffer, eastl::move(data));
					}
				}

				return hr;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11Device_CreateInputLayout
		{
			static HRESULT thunk(ID3D11Device* This, D3D11_INPUT_ELEMENT_DESC* pInputElementDescs, UINT NumElements, void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout** ppInputLayout)
			{
				HRESULT hr = func(This, pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);

				if (globals::features::raytracedGI.Active() && pInputElementDescs && NumElements > 0) {
					globals::features::raytracedGI.RegisterInputLayout(*ppInputLayout, pInputElementDescs, NumElements);
				}

				return hr;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11Buffer_Release
		{
			static void thunk(ID3D11Buffer* This)
			{
				if (globals::features::raytracedGI.Active())
					globals::features::raytracedGI.UnregisterBuffer(This);

				func(This);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		}; 

		struct DirtyStates_CreateInputLayoutFromVertexDesc
		{
			static ID3D11InputLayout* thunk(uint64_t a_vertexDesc)
			{
				auto inputLayout = func(a_vertexDesc);

				if (globals::features::raytracedGI.Active())
					globals::features::raytracedGI.vertexDescToInputLayout.emplace(a_vertexDesc, inputLayout);

				return inputLayout;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11Device_CreateTexture2D
		{
			static HRESULT WINAPI thunk(ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
			{
				const auto format = magic_enum::enum_name(pDesc->Format);
				const auto usage = magic_enum::enum_name(pDesc->Usage);

				const auto getFlags = [](uint value, const auto&entries) {
					std::string flags;

					for (const auto& [flag, name] : entries) {
						if ((value & static_cast<uint>(flag)) != 0) {
							flags += fmt::format("{} ", name);
						}
					}

					return flags;
				};

				auto bindEntries = magic_enum::enum_entries<D3D11_BIND_FLAG>();
				auto miscEntries = magic_enum::enum_entries<D3D11_RESOURCE_MISC_FLAG>();
				std::array cpuEntries = {
					std::pair{ D3D11_CPU_ACCESS_WRITE, "D3D11_CPU_ACCESS_WRITE" },
					std::pair{ D3D11_CPU_ACCESS_READ, "D3D11_CPU_ACCESS_READ" }
				};

				auto bindFlags = getFlags(pDesc->BindFlags, bindEntries);
				auto miscFlags = getFlags(pDesc->MiscFlags, miscEntries);
				auto cpuFlags = getFlags(pDesc->CPUAccessFlags, cpuEntries);

				logger::info(fmt::runtime("ID3D11Device_CreateTexture2D - Width: {}, Height: {} Format: {}, Usage: {}, Bind Flags: ({}), Misc Flags: ({}), CPU Flags: {} ({}), Mip levels: {}"), pDesc->Width, pDesc->Height, format, usage, bindFlags, miscFlags, pDesc->CPUAccessFlags, cpuFlags, pDesc->MipLevels);

				D3D11_TEXTURE2D_DESC descCopy = *pDesc;

				//if (pDesc->Usage == D3D11_USAGE_DEFAULT && (descCopy.BindFlags & static_cast<uint>(D3D11_BIND_RENDER_TARGET)) == 0 && (descCopy.MiscFlags & static_cast<uint>(D3D11_RESOURCE_MISC_GENERATE_MIPS)) == 0 && pDesc->MipLevels == 1)
				if (pDesc->Usage == D3D11_USAGE_DEFAULT && descCopy.BindFlags == D3D11_BIND_SHADER_RESOURCE)
					descCopy.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

				return func(This, &descCopy, pInitialData, ppTexture2D);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld
		{
			static void thunk(bool a1)
			{
				globals::features::raytracedGI.Main_RenderWorld(a1);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <RE::BSShader::Type ShaderType>
		struct BSShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
			{
				if (globals::features::raytracedGI.Active())
					globals::features::raytracedGI.BSShader_SetupGeometry(This, Pass, RenderFlags);

				func(This, Pass, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer_RenderPassImmediately
		{
			static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
			{
				if (globals::features::raytracedGI.Active())
					globals::features::raytracedGI.BSBatchRenderer_RenderPassImmediately(a_pass, a_technique, a_alphaTest, a_renderFlags);

				func(a_pass, a_technique, a_alphaTest, a_renderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));
			//stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Lighting>>(RE::VTABLE_BSLightingShader[0]);
			//stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

			stl::write_thunk_call<DirtyStates_CreateInputLayoutFromVertexDesc>(REL::RelocationID(75580, 75580).address() + REL::Relocate(0x465, 0x465));

			logger::info("[RTGI] Installed hooks");
		}

		static void InstallD3D11Hooks(ID3D11Device* ppDevice)
		{
			stl::detour_vfunc<3, ID3D11Device_CreateBuffer>(ppDevice);
			stl::detour_vfunc<11, ID3D11Device_CreateInputLayout>(ppDevice);

			//stl::detour_vfunc<2, ID3D11Buffer_Release>(*ppBuffer);

			//stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(ppDevice);

			logger::info("[RTGI] Installed D3D11 hooks - {}", reinterpret_cast<uintptr_t>(ppDevice));
		}
	};

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*);

		static bool Register()
		{
			static MenuOpenCloseEventHandler singleton;
			auto ui = globals::game::ui;

			if (!ui) {
				logger::error("UI event source not found");
				return false;
			}

			ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&singleton);

			logger::info("Registered {}", typeid(singleton).name());

			return true;
		}
	};
};