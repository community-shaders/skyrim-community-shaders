
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
#include "Features/RaytracedGI/Buffer.h"
#include "LightLimitFix.h"
#include <DirectXTex.h>
#include <shared_mutex>

#define NTDDI_VERSION NTDDI_WINBLUE

#include <DXProgrammableCapture.h>

struct TriBufferPtrKey
{
	ID3D11Buffer* vertexBuffer;
	ID3D11Buffer* indexBuffer;

	bool operator==(const TriBufferPtrKey& other) const noexcept
	{
		return vertexBuffer == other.vertexBuffer &&
		       indexBuffer == other.indexBuffer;
	}
};

struct TriBufferPtrKeyHash
{
	size_t operator()(const TriBufferPtrKey& key) const noexcept
	{
		size_t h1 = eastl::hash<ID3D11Buffer*>()(key.vertexBuffer);
		size_t h2 = eastl::hash<ID3D11Buffer*>()(key.indexBuffer);
		return h1 ^ (h2 << 1);
	}
};

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
	void InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter);
	void CreateRootSignature();
	ID3D12Resource* MakeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize = nullptr);
	void Flush();
	ID3D12Resource* MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertices, ID3D12Resource* indexBuffer = nullptr, UINT indices = 0);
	ID3D12Resource* MakeTLAS(ID3D12Resource* instances, UINT numInstances, UINT64* updateScratchSize);
	void DrawRTGI();

	eastl::vector<LightLimitFix::LightData> GetPointLights();
	void UpdateLights();

	void Main_RenderWorld(bool a1);
	void BSShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
	void BSBatchRenderer_RenderPassImmediately(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);

	void CreateBuffers();

	template <typename T>
	void MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res);

	const bool Active() 
	{
		return loaded && settings.Enabled;
	};

	//void BSShader_RestoreGeometry(RE::BSShader* This, RE::BSRenderPass* Pass);

	//void BSTriShape_UpdateWorldData(RE::BSTriShape* This, RE::NiUpdateData* a_data);
	
	static constexpr uint MAX_LIGHTS = 32;
	static constexpr UINT64 NUM_SHADER_IDS = 5;

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
		bool Enabled = true;
		bool EnablePIXCapture = false;
	} settings;

	struct Light
	{
		float3 Vector;
		float Range;
		float3 Color;
		uint Pad;
	};

	eastl::vector<Light> lightData;
	eastl::unique_ptr<StructuredBufferDX12<Light>> lightBuffer = nullptr;

	struct alignas(16) FrameBuffer
	{
		float4x4 ViewInverse;
		float4x4 ProjInverse;
		float4 Position;
		Light DirectionalLight;
		uint FrameCount;
		uint Pad0;
		uint Pad1;
		uint Pad2;
	};
	static_assert(sizeof(FrameBuffer) % 16 == 0);
	FrameBuffer* frameBufferData;

	bool renderingWorld = false;
	bool buffersCreated = false;
	bool creatingBuffers = false;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;
	bool capture = false;

	bool releaseHooked = false;

	#pragma pack(push, 1)
	struct Vertex
	{
		float3 Position;
		uint16_t Texcoord0[2];
		uint8_t Normal[4];
		uint8_t Color[4];
	};
	#pragma pack(pop)

	struct Instance
	{
		uint MeshID;
	};

	// do I call them vanilla Diffuse/Glow or CS Albedo/Emissive??
	struct MaterialData
	{
		ID3D12Resource* diffuseTexture = nullptr;
		ID3D12Resource* glowTexture = nullptr;
	};

	struct MeshData
	{
		uint vertexCount;
		uint indexCount;
		winrt::com_ptr<ID3D12Resource> vertexBuffer = nullptr;
		winrt::com_ptr<ID3D12Resource> indexBuffer = nullptr;
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;
		MaterialData material;
	};

	eastl::vector<MeshData> meshVector;
	eastl::unordered_map<TriBufferPtrKey, size_t, TriBufferPtrKeyHash> meshMap;	

	eastl::vector<Instance> instanceMap;
	winrt::com_ptr<ID3D12Resource> instanceMapBuffer = nullptr;

	eastl::unordered_map<ID3D11Texture2D*, winrt::com_ptr<ID3D12Resource>> sharedTextures;

	struct InstanceData
	{
		TriBufferPtrKey triBufferPtrKey;
		float4x4 transform;
	};

	eastl::unordered_map<RE::BSTriShape*, InstanceData> instances;

	//eastl::unordered_map<RE::BSTriShape*, winrt::com_ptr<ID3D12Resource>> cachedTrishapes;

	eastl::unique_ptr<ConstantBuffer> cheeseCb = nullptr;  // Omit this if you want to put your CB in src/FeatureBuffer.cpp
	eastl::unique_ptr<Texture2D> cheeseTex = nullptr;
	winrt::com_ptr<ID3D11SamplerState> cheeseSampler = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> cheeseCs = nullptr;

	winrt::com_ptr<ID3D12Resource> instanceBuffer = nullptr;
	D3D12_RAYTRACING_INSTANCE_DESC* instanceData;

	winrt::com_ptr<ID3D12Resource> tlas = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasUpdateScratch = nullptr;

	winrt::com_ptr<ID3D12Resource> frameBuffer = nullptr;

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

	winrt::com_ptr<ID3D12DescriptorHeap> commonHeap = nullptr;	

	winrt::com_ptr<ID3D11Fence> d3d11Fence = nullptr;
	winrt::com_ptr<ID3D12Fence> d3d12Fence = nullptr;

	UINT64 fenceValue = 1;
	UINT handleIncrement;

	// D3D11
	winrt::com_ptr<ID3D11Device5> d3d11Device = nullptr;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context = nullptr;

	// Resources
	eastl::unique_ptr<WrappedResource> diffuseGITexture = nullptr;
	eastl::unique_ptr<WrappedResource> specularGITexture = nullptr;

	std::shared_mutex mutex;

	struct Hooks
	{
		struct ID3D11Buffer_Release
		{
			static void thunk(ID3D11Buffer* This)
			{
				/*if (globals::features::raytracedGI.Active())
					globals::features::raytracedGI.UnregisterBuffer(This);*/

				func(This);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		}; 

		struct ID3D11Device_CreateTexture2D
		{
			static HRESULT WINAPI thunk(ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
			{
				D3D11_TEXTURE2D_DESC descCopy = *pDesc;
				const D3D11_SUBRESOURCE_DATA* initialDataCopy = pInitialData;

				auto& rtgi = globals::features::raytracedGI;

				bool loaded = rtgi.loaded;

				bool shareTexture = false;

				std::vector<D3D11_SUBRESOURCE_DATA> initialDataLocal;
				std::vector<DirectX::ScratchImage> decompressedMips;

				if (loaded && pDesc && pInitialData && pDesc->ArraySize == 1 && pDesc->Usage == D3D11_USAGE_DEFAULT && pDesc->BindFlags == D3D11_BIND_SHADER_RESOURCE && pDesc->MiscFlags == 0 && pDesc->CPUAccessFlags == 0) {
					switch (pDesc->Format) {
						case DXGI_FORMAT_BC4_UNORM:
							descCopy.Format = DXGI_FORMAT_R8_UNORM;
							break;
						case DXGI_FORMAT_BC4_SNORM:
							descCopy.Format = DXGI_FORMAT_R8_SNORM;
							break;
						case DXGI_FORMAT_BC7_UNORM:
							descCopy.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
							break;
						case DXGI_FORMAT_BC7_UNORM_SRGB:
							descCopy.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
							break;
						default:
							break;
					}

					if (pDesc->Format != descCopy.Format) {
						initialDataLocal.resize(pDesc->MipLevels);
						decompressedMips.resize(pDesc->MipLevels);

						for (UINT mip = 0; mip < pDesc->MipLevels; ++mip) {
							DirectX::Image src;
							src.width = std::max(1u, pDesc->Width >> mip);
							src.height = std::max(1u, pDesc->Height >> mip);
							src.format = pDesc->Format;
							src.rowPitch = pInitialData[mip].SysMemPitch;
							src.slicePitch = pInitialData[mip].SysMemSlicePitch;
							src.pixels = (uint8_t*)pInitialData[mip].pSysMem;

							DX::ThrowIfFailed(DirectX::Decompress(src, descCopy.Format, decompressedMips[mip]));

							const DirectX::Image* img = decompressedMips[mip].GetImage(0, 0, 0);
							initialDataLocal[mip].pSysMem = img->pixels;
							initialDataLocal[mip].SysMemPitch = static_cast<UINT>(img->rowPitch);
							initialDataLocal[mip].SysMemSlicePitch = static_cast<UINT>(img->slicePitch);
						}

						initialDataCopy = initialDataLocal.data();  // point to new mip data
					}

					descCopy.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
					shareTexture = true;
				}

				HRESULT hr = func(This, &descCopy, initialDataCopy, ppTexture2D);

				if (SUCCEEDED(hr) && shareTexture) {
					winrt::com_ptr<IDXGIResource1> dxgiResource = nullptr;
					DX::ThrowIfFailed((*ppTexture2D)->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

					HANDLE sharedHandle = nullptr;
					DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &sharedHandle));

					winrt::com_ptr<ID3D12Resource> resource = nullptr;
					DX::ThrowIfFailed(rtgi.d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(resource.put())));
					CloseHandle(sharedHandle);

					rtgi.sharedTextures.emplace(*ppTexture2D, std::move(resource));

					std::lock_guard lock{ rtgi.mutex };

					if (!rtgi.releaseHooked) {
						rtgi.releaseHooked = true;
						stl::detour_vfunc<2, ID3D11Texture2D_Release>(*ppTexture2D);
					}
				}

				return hr;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11Device_CreateShaderResourceView
		{
			static HRESULT WINAPI thunk(ID3D11Device* This, ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRV)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC descCopy = {};
				const D3D11_SHADER_RESOURCE_VIEW_DESC* descPtr = pDesc;

				if (pDesc)
					descCopy = *pDesc;

				if (pResource && ppSRV && pDesc && pDesc->ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
					auto& rtgi = globals::features::raytracedGI;

					std::lock_guard lock{ rtgi.mutex };

					switch (pDesc->Format) {
						case DXGI_FORMAT_BC4_UNORM:
							descCopy.Format = DXGI_FORMAT_R8_UNORM;
							break;
						case DXGI_FORMAT_BC4_SNORM:
							descCopy.Format = DXGI_FORMAT_R8_SNORM;
							break;
						case DXGI_FORMAT_BC7_UNORM:
							descCopy.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
							break;
						case DXGI_FORMAT_BC7_UNORM_SRGB:
							descCopy.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
							break;
						default:
							break;
					}

					if (pDesc->Format != descCopy.Format) {
						if (rtgi.sharedTextures.find(static_cast<ID3D11Texture2D*>(pResource)) != rtgi.sharedTextures.end()) {
							descPtr = &descCopy;
						}
					}
				}

				return func(This, pResource, descPtr, ppSRV);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11Texture2D_Release
		{
			static ULONG WINAPI thunk(ID3D11Texture2D* This)
			{			
				ULONG refCount = func(This);

				if (refCount == 0) {
					globals::features::raytracedGI.sharedTextures.erase(This);
				}

				return refCount;
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

			logger::info("[RTGI] Installed hooks");
		}

		static void CreateDummyTexture(ID3D11Device* device, ID3D11Texture2D** texture)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 1;
			desc.Height = 1;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;
	
			DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, texture));
		}

		static void InstallD3D11Hooks(ID3D11Device* ppDevice)
		{
			//stl::detour_vfunc<2, ID3D11Buffer_Release>(*ppBuffer);
			stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(ppDevice);
			stl::detour_vfunc<7, ID3D11Device_CreateShaderResourceView>(ppDevice);

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