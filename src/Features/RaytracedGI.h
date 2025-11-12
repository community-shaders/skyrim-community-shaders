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
#include "Features/RaytracedGI/IrradianceCache.h"
#include "Features/RaytracedGI/Allocator.h"
#include "Features/RaytracedGI/HeapManager.h"
#include "Features/RaytracedGI/RTPipelineBuilder.h"

#define NTDDI_VERSION NTDDI_WINBLUE

#include <DXProgrammableCapture.h>

//#define DLSS_RR

#ifdef DLSS_RR
#	define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_matrix_helpers.h>
#include <sl_nis.h>
#include <sl_version.h>
#pragma warning(pop)
#endif

struct RaytracedGI : public Feature
{
	static constexpr uint64_t NUM_SHADER_IDS = 7;

	static constexpr uint MAX_MESHES = 2048;
	static constexpr uint MAX_INSTANCES = 4096;
	
	static constexpr uint MAX_LIGHTS = 255;
	static constexpr uint MAX_IRRADIANCE_ENTRIES = 256 * 256 * 256;

	struct HeapSlot
	{
		enum Slot : uint32_t
		{
			Final,
			DiffuseGI,
			SpecularGI,
			SpecHitDist,
			Albedo,
			Reflectance,
			NormalRoughness,
			GeometryNormalDepth,
			TLAS,
			Lights,
			Instances,
			Vertices,
			Triangles = Vertices + MAX_MESHES,
			DiffuseTextures = Triangles + MAX_MESHES,
			NumDescriptors = DiffuseTextures + MAX_MESHES,
			None
		};
	};

	struct HeapType
	{
		enum Type : uint32_t
		{
			UAV,
			SRV,
			VertexBuffer,
			TriangleBuffer,
			DiffuseTextures,
			//GlowTextures,
			//CBV
		};
	};

	struct ComputeHeapSlot
	{
		enum Slot : uint32_t
		{
			Final,
			DiffuseGI,
			SpecularGI,
			SpecHitDist,
			Albedo,
			Reflectance,
			NumDescriptors,
			None
		};
	};

	struct ComputeHeapType
	{
		enum Type : uint32_t
		{
			UAV,
			SRV,
			CBV
		};
	};

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

	void ShareRT(ID3D11Texture2D* pTexture2D, const HeapSlot::Slot& target, const ComputeHeapSlot::Slot& cTarget, ID3D12Resource** ppResource);
	void SetupSharedRT();
	void CompileShaders();
	void CompileRaytracingShaders();
	void CompileDX12ComputeShaders();
	void CompileComputeShaders();

	void Initialize();
	void InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter);
	void CreateRootSignature();
	void CreateComputeRootSignature();
	ID3D12Resource* MakeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize = nullptr);
	void Flush();
	ID3D12Resource* MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertices, ID3D12Resource* indexBuffer, UINT indices);
	ID3D12Resource* MakeTLAS(ID3D12Resource* instances, UINT numInstances, UINT64* updateScratchSize);
	void DrawRTGI();

	eastl::vector<LightLimitFix::LightData> GetPointLights();
	void UpdateLights();

	void Main_RenderWorld(bool a1);
	void BSShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
	void BSBatchRenderer_RenderPassImmediately(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);

	void AddInstance(RE::BSTriShape* pTriShape);
	void AddUpdateInstance(RE::BSTriShape* pTriShape);
	void AddUpdateAllInstances();

	eastl::vector<size_t> GatherInstanceLights(RE::BSTriShape* pBSTriShape);

	void VertexBufferReleased(ID3D11Buffer* pBuffer);

	void UpdateInstances();

	template <typename T>
	void MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res);

	void DeviceRemovedHandler();

	void CopyDepth();

#ifdef DLSS_RR
	void InitRR();
	void CheckFrameConstants();
#endif

	const bool Active() 
	{
		return loaded && settings.Enabled;
	};

	//void BSShader_RestoreGeometry(RE::BSShader* This, RE::BSRenderPass* Pass);

	void BSTriShape_UpdateWorldData(RE::BSTriShape* This, RE::NiUpdateData* a_data);

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

	enum struct Denoiser : int32_t
	{
		None,
		Accumulation,
		DLSSRR
	};

	enum struct DebugOutput : int32_t
	{
		None,
		Indirect,
		Specular,
		Passthrough
	};

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool Enabled = true;
		Denoiser Denoiser = Denoiser::None;
		int Bounces = 2;
		int SamplesPerPixel = 1;
		float2 Roughness = {0.0f, 1.0f};
		float2 Specularity = {0.0f, 1.0f};
		float Diffuse = 1.0f;
		float Specular = 1.0f;
		float Directional = 1.0f;
		float Point = 1.0f;
		bool PointFade = true;
		DebugOutput DebugOutput = DebugOutput::None;
		bool EnablePIXCapture = true;
		bool EnableDebugDevice = false;
#ifdef SHARC
		float SHARCScale = 1.0f;
#endif
#ifdef DLSS_RR
		bool EnableRR = true;
#endif
	} settings;

	struct alignas(16) Light
	{
		float3 Vector;
		float Range;
		float3 Color;
		uint Pad;
	};
	static_assert(sizeof(Light) % 16 == 0);

	eastl::vector<Light> lights;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Light>> lightBuffer = nullptr;

	struct alignas(16) FrameBuffer
	{
		float4x4 ViewInverse;
		float4x4 ProjInverse;
		float4 CameraData;
		float4 NDCToView;
		Light Directional;
		float3 Position;
		uint FrameCount;
		float Diffuse;
		float Specular;
		uint Pad0;
#ifdef SHARC
		float SHARCScale;
#else
		uint Pad1;
#endif
	};
	static_assert(sizeof(FrameBuffer) % 16 == 0);
	FrameBuffer* frameBufferData;

	bool renderingWorld = false;
	bool lightsUpdated = false;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;
	bool capture = false;

	bool releaseBufferHooked = false;
	bool releaseHooked = false;
	HANDLE fenceEvent;

	bool addedAllInstances = false;

#pragma pack(push, 1)
	struct Vertex
	{
		float3 Position;
		uint16_t Texcoord0[2];
		uint8_t Normal[4];
		uint8_t Color[4];
	};

	struct Triangle
	{
		uint v0;
		uint v1;
		uint v2;
	};
#pragma pack(pop)

	struct LightData
	{
		uint Count;
		uint Data[4]; // Each byte stores the light ID from 0 to 255, with 16 bytes we get

		LightData() = default;

		LightData(const eastl::vector<size_t>& ids)
		{
			StoreIDs(ids);
		}

		uint GetGroup(uint index)
		{
			return index >> 2;
		}

		uint GetOffset(uint index)
		{
			return (index & 3) << 3;
		}

		uint GetID(uint index)
		{
			uint group = GetGroup(index);
			uint offset = GetOffset(index);

			return (Data[group] >> offset) & 0xFFu;
		}

		void SetID(uint index, uint val)
		{
			uint group = GetGroup(index);
			uint offset = GetOffset(index);
			uint mask = ~(0xFFu << offset);
			Data[group] = (Data[group] & mask) | ((val & 0xFFu) << offset);
		}

		void StoreIDs(const eastl::vector<size_t>& ids)
		{
			size_t count = std::min(ids.size(), static_cast<size_t>(16));
			Count = static_cast<uint32_t>(count);

			for (size_t i = 0; i < count; ++i) {
				uint32_t id = std::min(static_cast<uint32_t>(ids[i]), 255u);
				SetID(static_cast<uint32_t>(i), id);
			}
		}
	};

	// Mesh - do I call them vanilla Diffuse/Glow or CS Albedo/Emissive??
	struct MaterialData
	{
		ID3D12Resource* diffuseTexture = nullptr;
		ID3D12Resource* glowTexture = nullptr;
	};

	struct MeshData
	{
		uint registerIndex; // The position of this meshes SRV in the register stack
		uint vertexCount;
		uint triangleCount;
		eastl::unique_ptr<DX12::StructuredBufferUpload<Vertex>> vertexBuffer = nullptr;
		eastl::unique_ptr<DX12::StructuredBufferUpload<Triangle>> indexBuffer = nullptr;
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;
		MaterialData material;
		eastl::vector<RE::BSTriShape*> instances;
	};

	Allocator registers = Allocator(MAX_MESHES);

	// Key is RE::BSGraphics::TriShape.vertexBuffer (the original vertexBuffer)
	eastl::unordered_map<ID3D11Buffer*, MeshData> meshes;	

	// Instance
	struct InstanceData
	{
		ID3D11Buffer* meshKey;
		float4x4 transform;
		eastl::vector<size_t> lights;
	};

	eastl::unordered_map<RE::BSTriShape*, InstanceData> instances;

	// Instance buffer
	struct Instance
	{
		uint MeshID;
		LightData LightData;
	};

	eastl::vector<Instance> instanceData;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Instance>> instanceBuffer = nullptr;

	// Textures
	eastl::unordered_map<ID3D11Texture2D*, winrt::com_ptr<ID3D12Resource>> sharedTextures;

	eastl::unique_ptr<DX12::StructuredAppendBuffer<IrradianceCache::Entry<IrradianceCache::SH1Data>>> irradianceCacheBuffer = nullptr;	

	eastl::unique_ptr<ConstantBuffer> cheeseCb = nullptr;  // Omit this if you want to put your CB in src/FeatureBuffer.cpp
	eastl::unique_ptr<Texture2D> cheeseTex = nullptr;
	winrt::com_ptr<ID3D11SamplerState> cheeseSampler = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> copyDepthCS = nullptr;

	eastl::multimap<std::string, eastl::array<std::string, 2>> debugMultimap;

	winrt::com_ptr<ID3D12Resource> blasInstanceBuffer = nullptr;
	D3D12_RAYTRACING_INSTANCE_DESC* blasInstances;

	winrt::com_ptr<ID3D12Resource> tlas = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasUpdateScratch = nullptr;

	winrt::com_ptr<ID3D12Resource> frameBuffer = nullptr;

	// Shaders
	/*winrt::com_ptr<IDxcBlob> rayGenerationRT = nullptr;
	winrt::com_ptr<IDxcBlob> missRT = nullptr;
	winrt::com_ptr<IDxcBlob> closestHitRT = nullptr;*/

	winrt::com_ptr<ID3D12StateObject> pipelineRT = nullptr;
	winrt::com_ptr<ID3D12Resource> shaderIDs = nullptr;

	winrt::com_ptr<ID3D12PipelineState> pipelineCS = nullptr;

	// D3D12
	winrt::com_ptr<ID3D12Device5> d3d12Device = nullptr;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue = nullptr;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocator = nullptr;
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandList = nullptr;

	winrt::com_ptr<ID3D12RootSignature> rootSignature = nullptr;
	winrt::com_ptr<ID3D12RootSignature> rootSignatureCS = nullptr;

	eastl::unique_ptr<DX12::DescriptorHeap<HeapSlot::Slot, HeapType::Type>> commonHeap = nullptr;	
	eastl::unique_ptr<DX12::DescriptorHeap<ComputeHeapSlot::Slot, ComputeHeapType::Type>> computeHeap = nullptr;	

	winrt::com_ptr<ID3D11Fence> d3d11Fence = nullptr;
	winrt::com_ptr<ID3D12Fence> d3d12Fence = nullptr;

	UINT64 fenceValue = 0;

	eastl::vector<winrt::com_ptr<ID3D12Resource>> asScratchBuffers;
	eastl::vector<eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC>> rtGeometryDescs;
	eastl::vector<eastl::unique_ptr<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC>> rtASDescs;

	// D3D11
	winrt::com_ptr<ID3D11Device5> d3d11Device = nullptr;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context = nullptr;

	// Resources
	/*eastl::unique_ptr<WrappedResource> diffuseGITexture = nullptr;
	eastl::unique_ptr<WrappedResource> specularGITexture = nullptr;
	eastl::unique_ptr<WrappedResource> specularHitDistanceTexture = nullptr;*/

	eastl::unique_ptr<WrappedResource> finalTexture = nullptr;

	winrt::com_ptr<ID3D12Resource> diffuseGITexture = nullptr;
	winrt::com_ptr<ID3D12Resource> specularGITexture = nullptr;
	winrt::com_ptr<ID3D12Resource> specularHitDistanceTexture = nullptr;

	winrt::com_ptr<ID3D12Resource> albedoTexture = nullptr;
	winrt::com_ptr<ID3D12Resource> reflectanceTexture = nullptr;
	winrt::com_ptr<ID3D12Resource> normalRoughnessTexture = nullptr;
	winrt::com_ptr<ID3D12Resource> goemetryNormalDepthTexture = nullptr;

	std::shared_mutex meshMutex;
	std::shared_mutex bufferMutex;
	std::shared_mutex sharedTextureMutex;
	std::shared_mutex renderMutex;

#if defined(DLSS_RR)
	HMODULE interposer;
	PFun_slInit* slInit;
	PFun_slEvaluateFeature* slEvaluateFeature;
	PFun_slGetNewFrameToken* slGetNewFrameToken;
	PFun_slSetD3DDevice* slSetD3DDevice;

	PFun_slDLSSDGetOptimalSettings* slDLSSDGetOptimalSettings{};
	PFun_slDLSSDGetState* slDLSSDGetState{};
	PFun_slDLSSDSetOptions* slDLSSDSetOptions{};
	PFun_slSetConstants* slSetConstants;
	PFun_slGetFeatureFunction* slGetFeatureFunction;

	sl::ViewportHandle slViewportHandle{ 0 };

	Util::FrameChecker frameChecker;
	sl::FrameToken* frameToken = nullptr;

	float2 jitter = { 0, 0 };
#endif

	inline float3 Float3(const RE::NiPoint3& point3)
	{
		return float3(point3.x, point3.y, point3.z);
	}

	struct Hooks
	{
		struct ID3D11Device_CreateBuffer
		{
			static HRESULT thunk(ID3D11Device* oThis, const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer)
			{
				HRESULT hr = func(oThis, pDesc, pInitialData, ppBuffer);

				auto& rtgi = globals::features::raytracedGI;

				if (SUCCEEDED(hr) && *ppBuffer) {
					if (rtgi.loaded && !rtgi.releaseBufferHooked) {
						std::lock_guard lock{ rtgi.bufferMutex };

						if (!rtgi.releaseBufferHooked)
						{
							rtgi.releaseBufferHooked = true;
							stl::detour_vfunc<2, ID3D11Buffer_Release>(*ppBuffer);					
						}
					}
				}

				return hr;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11Buffer_Release
		{
			static void thunk(ID3D11Buffer* oThis)
			{
				auto& rtgi = globals::features::raytracedGI;

				if (rtgi.loaded) {
					/*D3D11_BUFFER_DESC desc{};
					oThis->GetDesc(&desc);

					if (desc.BindFlags & D3D11_BIND_VERTEX_BUFFER)*/

					rtgi.VertexBufferReleased(oThis);
				}

				func(oThis);
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

						initialDataCopy = initialDataLocal.data();
					}

					descCopy.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
					shareTexture = true;
				}

				HRESULT hr = func(This, &descCopy, initialDataCopy, ppTexture2D);

				if (SUCCEEDED(hr) && shareTexture) {
					if (!rtgi.releaseHooked) {
						std::lock_guard lock{ rtgi.sharedTextureMutex };

						if (!rtgi.releaseHooked) {
							rtgi.releaseHooked = true;
							stl::detour_vfunc<2, ID3D11Texture2D_Release>(*ppTexture2D);
						}					
					}

					winrt::com_ptr<IDXGIResource1> dxgiResource = nullptr;
					DX::ThrowIfFailed((*ppTexture2D)->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

					HANDLE sharedHandle = nullptr;
					DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &sharedHandle));

					winrt::com_ptr<ID3D12Resource> resource = nullptr;
					HRESULT hrOSH = rtgi.d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(resource.put()));

					CloseHandle(sharedHandle);

					if (SUCCEEDED(hrOSH)) {
						rtgi.sharedTextures.emplace(*ppTexture2D, std::move(resource));
					} else {
						logger::warn("[RTGI] Error creating shared texture - [0x{:x}]", hrOSH);
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

					std::lock_guard lock{ rtgi.sharedTextureMutex };

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

		struct BSTriShape_UpdateWorldData
		{
			static void thunk(RE::BSTriShape* This, RE::NiUpdateData* data)
			{
				globals::features::raytracedGI.BSTriShape_UpdateWorldData(This, data);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSTriShape_OnVisible
		{
			static void thunk(RE::BSTriShape* This, RE::NiCullingProcess& a_process)
			{
				//logger::info("[RTGI] BSTriShape_OnVisible");
				func(This, a_process);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSTriShape_LoadBinary
		{
			static void thunk(RE::BSTriShape* oThis, RE::NiStream& a_stream)
			{
				func(oThis, a_stream);
				
				auto& rtgi = globals::features::raytracedGI;

				if (rtgi.Active()) 
				{
					//logger::info("[RTGI] BSTriShape::LoadBinary {} - {}", oThis->name, a_stream.inputFilePath);
					//rtgi.AddInstance(oThis);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		struct BSTriShape_LinkObject
		{
			static void thunk(RE::BSTriShape* oThis, RE::NiStream& a_stream)
			{
				func(oThis, a_stream);

				auto& rtgi = globals::features::raytracedGI;

				if (rtgi.Active()) {
					//logger::info("[RTGI] BSTriShape::LinkObject {} - {}", oThis->name, a_stream.inputFilePath);
					//rtgi.AddInstance(oThis);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		
		struct BSLightingShaderProperty_SetupGeometry
		{
			static void thunk(RE::BSLightingShaderProperty* oThis, RE::BSGeometry* pGeometry, RE::NiStream& a_stream)
			{
				func(oThis, pGeometry, a_stream);

				auto& rtgi = globals::features::raytracedGI;

				if (rtgi.Active()) {
					//rtgi.AddInstance(netimmerse_cast<RE::BSTriShape*>(pGeometry));
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		static void Install()
		{
			stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));

			stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Lighting>>(RE::VTABLE_BSLightingShader[0]);
			//stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Effect>>(RE::VTABLE_BSEffectShader[0]);

			//stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

			if (REL::Module::IsAE()) {
				stl::write_vfunc<0x31, BSTriShape_UpdateWorldData>(RE::VTABLE_BSTriShape[0]);
			} else {
				stl::write_vfunc<0x30, BSTriShape_UpdateWorldData>(RE::VTABLE_BSTriShape[0]);
			}

			if (REL::Module::IsAE()) {
				stl::write_vfunc<0x35, BSTriShape_OnVisible>(RE::VTABLE_BSTriShape[0]);
			} else {
				stl::write_vfunc<0x34, BSTriShape_OnVisible>(RE::VTABLE_BSTriShape[0]);
			}

			//stl::write_vfunc<0x18, BSTriShape_LoadBinary>(RE::VTABLE_BSTriShape[0]);
			stl::write_vfunc<0x19, BSTriShape_LinkObject>(RE::VTABLE_BSTriShape[0]);

			stl::write_vfunc<0x27, BSLightingShaderProperty_SetupGeometry>(RE::VTABLE_BSLightingShaderProperty[0]); //FinishSetupGeometry = 0x28

			logger::info("[RTGI] Installed hooks");
		}

		static void InstallD3D11Hooks(ID3D11Device* pDevice)
		{
			stl::detour_vfunc<3, ID3D11Device_CreateBuffer>(pDevice);
			stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(pDevice);
			stl::detour_vfunc<7, ID3D11Device_CreateShaderResourceView>(pDevice);

			logger::info("[RTGI] Installed D3D11 hooks - {}", reinterpret_cast<uintptr_t>(pDevice));
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

	class TESLoadGameEventHandler : public RE::BSTEventSink<RE::TESLoadGameEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent* a_event, RE::BSTEventSource<RE::TESLoadGameEvent>*);

		static bool Register()
		{
			static TESLoadGameEventHandler singleton;

            auto scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
			scriptEventSourceHolder->GetEventSource<RE::TESLoadGameEvent>()->AddEventSink(&singleton);

			logger::info("Registered {}", typeid(singleton).name());

			return true;
		}
	};
};