#pragma once

#include "Feature.h"
#include <d3d12.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include "Features/Upscaling/DX12SwapChain.h"
#include <dxcapi.h>
#include <directxpackedvector.h>
#include "Features/Raytracing/Buffer.h"
#include "LightLimitFix.h"
#include <DirectXTex.h>
#include <shared_mutex>
#include "Features/Raytracing/IrradianceCache.h"
#include "Features/Raytracing/Allocator.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/RTPipelineBuilder.h"
#include "Features/Raytracing/ShaderBindingTable.h"

#include "Raytracing/Includes/Types/Light.hlsli"
#include "Raytracing/Includes/Types/GIFrameData.hlsli"
#include "Raytracing/Includes/Types/ShadowsFrameData.hlsli"

#define NTDDI_VERSION NTDDI_WINBLUE

#include <DXProgrammableCapture.h>

#define DLSS_RR

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

struct Raytracing : public Feature
{
	static constexpr uint MAX_MESHES = 2048;
	static constexpr uint MAX_INSTANCES = 4096;
	
	static constexpr uint MAX_LIGHTS = 255;
	static constexpr uint MAX_IRRADIANCE_ENTRIES = 256 * 256 * 256;

	static constexpr uint TLAS_BUFFER_SIZE_MULT = 2u;

	static constexpr uint SKY_CUBEMAP_SIZE = 256;

	struct GIHeap
	{
		struct Table
		{
			enum Values : uint32_t
			{
				UAV,
				SRV,
				VertexBuffer,
				TriangleBuffer,
				DiffuseTextures,
				GlowTextures
			};
		};

		struct Slot
		{
			enum Values : uint32_t
			{
				Output,
				Reflectance,
				SpecularHitDist,
				Main,
				Depth,
				Albedo,
				NormalRoughness,
				GNMD,
				TLAS,
				SkyHemisphere,
				Lights,
				Instances,
				Vertices,
				Triangles = Vertices + MAX_MESHES,
				DiffuseTextures = Triangles + MAX_MESHES,
				GlowTextures = DiffuseTextures + MAX_MESHES,
				NumDescriptors = GlowTextures + MAX_MESHES,
				None
			};
		};
	};
	struct ShadowsHeap
	{
		enum Table : uint32_t
		{
			UAV,
			SRV
		};

		enum Slot : uint32_t
		{
			ShadowMask,
			Depth,
			TLAS,
			NumDescriptors,
			None
		};
	};

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

	void ShareRT(ID3D11Texture2D* pTexture2D, const GIHeap::Slot::Values& target, const ShadowsHeap::Slot& cTarget, ID3D12Resource** ppResource);
	void SetupSharedRT();
	void CompileShaders();
	void CompileComputeShaders();

	void CompileRTGIShaders();
	void CompileRTShadowsShaders();

	void CompileDX12ComputeShaders();

	void Initialize();
	void InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter);
	void CreateRootSignature();
	void CreateShadowsRootSignature();
	ID3D12Resource* MakeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* scratchSize = nullptr, UINT64* updateScratchSize = nullptr);
	void Flush();
	ID3D12Resource* MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertices, ID3D12Resource* indexBuffer, UINT indices);
	ID3D12Resource* MakeTLAS(ID3D12Resource* instances, UINT numInstances, UINT64* scratchSize, UINT64* updateScratchSize);
	void DrawRTGI();
	void UpdateShadowsFrameBuffer();
	void RenderShadows();

	float3 GammaToLinear(float3 color);
	eastl::vector<LightLimitFix::LightData> GetPointLights();
	void UpdateLights();

	void Main_RenderWorld(bool a1);
	void BSShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
	void BSSkyShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
	void BSBatchRenderer_RenderPassImmediately(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);

	void SkyCubeToHemi();
	void CheckResourcesSide(int side);

	void BeginGeometry(RE::BSFadeNode* pFadeNode, RE::NiStream& rNiStream);
	void AddTriShape(RE::BSTriShape* pTriShape);
	void CommitGeometry();

	bool ValidTriShape(RE::BSTriShape* pTriShape);
	void AddInstance(RE::BSTriShape* pTriShape);
	void AddUpdateInstance(RE::BSTriShape* pTriShape);
	void AddUpdateAllInstances();

	eastl::vector<size_t> GatherInstanceLights(RE::BSTriShape* pBSTriShape);

	void VertexBufferReleased(ID3D11Buffer* pBuffer);

	void UpdateInstances();
	void UpdateShadowInstances();

	template <typename T>
	void MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res);

	void DeviceRemovedHandler();

	void CopyDepth();
	void ConvertNormalGlossiness();

#ifdef DLSS_RR
	void InitRR();
	void CheckFrameConstants();
	sl::DLSSMode GetDLSSMode();
	void SetDLSSRROptions();
	int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth);
	void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount);
	float Halton(int32_t index, int32_t base);
	float2 GetInputResolutionScaleRR(uint32_t outputWidth, uint32_t outputHeight);
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
#ifdef DLSS_RR
		DLSSRR
#endif	
	};

	enum struct DebugOutput : int32_t
	{
		None,
		Output,
		Reflectance,
		SpecularHitDistance,
		ReflectangeGBuffer,
		RoughnessGBuffer,
		Passthrough
	};

	enum struct DLSSRRQuality : int32_t
	{
		MaxPerformance,	
		Balanced,
		MaxQuality		
	};

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool Enabled = true;
		bool GlobalIllumination = true;
		Denoiser Denoiser = Denoiser::Accumulation;
		int Bounces = 2;
		int SamplesPerPixel = 1;
		float2 Roughness = {0.0f, 1.0f};
		float2 Metalness = {0.0f, 1.0f};
		float Diffuse = 1.0f;
		float Specular = 1.0f;
		float Emissive = 1.0f;
		float Effect = 1.0f;
		float Sky = 1.0f;
		float Directional = 1.0f;
		float Point = 1.0f;
		bool PointFade = true;
		bool GammaToLinear = false;
		bool RaytracedShadows = true;
		bool RecompressTextures = false;
#ifdef DLSS_RR
		DLSSRRQuality DLSSRRQualityMode = DLSSRRQuality::MaxQuality;
#endif
		DebugOutput DebugOutput = DebugOutput::None;
		bool EnablePIXCapture = true;
		bool EnableDebugDevice = false;
#ifdef SHARC
		float SHARCScale = 1.0f;
#endif
	} settings;

	bool settingSharedTexture = false;
	bool renderingWorld = false;
	bool lightsUpdated = false;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;

	bool pixCapture = false;
	bool pixCaptureStarted = false;

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
		uint8_t Tangent[4];
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

	struct MaterialData
	{
		RE::BSShaderMaterial::Feature feature;
		float4 texCoordOffsetScale;
		ID3D12Resource* diffuseTexture = nullptr;
		ID3D12Resource* effectTexture = nullptr;
		ID3D12Resource* rmaosTexture = nullptr;
		float4 effectColor;
		int shaderType;
	};

	struct MeshData
	{
		uint registerIndex; // The position of this meshes SRV in the register stack
		uint vertexCount;
		uint triangleCount;
		bool skinned;
		eastl::unique_ptr<DX12::StructuredBufferUpload<Vertex>> vertexBuffer = nullptr;
		eastl::unique_ptr<DX12::StructuredBufferUpload<Triangle>> triangleBuffer = nullptr;
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;
		MaterialData material;
		eastl::vector<RE::BSTriShape*> instances;
	};

	struct GeometryData
	{
		eastl::vector<MeshData> meshes;
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;
	};

	MeshData CreateTriShapeBuffers(RE::BSGraphics::TriShape* rendererData, const std::uint16_t& vertexCount, const std::uint16_t& triangleCount, const std::wstring& name);
	void CreateTriShapeMaterials(MeshData& meshData, const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, const char* name);

	Allocator registers = Allocator(MAX_MESHES);

	// Key is RE::BSGraphics::TriShape.vertexBuffer (the original vertexBuffer)
	eastl::unordered_map<ID3D11Buffer*, MeshData> meshes;	

	RE::BSFadeNode* currentFadeNode;

	// We'll group trishapes by their parent nodes, hopefully trishapes don't move on their own
	eastl::unordered_map<RE::BSFadeNode*, GeometryData> fadeNodeGeometry;

	// Instance
	struct InstanceData
	{
		ID3D11Buffer* meshKey;
		float4x4 transform;
	};

	eastl::unordered_map<RE::BSTriShape*, InstanceData> instances;

	// Instance material
	struct Material
	{
		float4 TexCoordOffsetScale;
		float4 EffectColor;
		float ShaderType;  
	};

	// Instance buffer
	struct Instance
	{
		uint MeshID;
		LightData LightData;
		Material Material;
	};

	eastl::vector<Instance> instanceData;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Instance>> instanceBuffer = nullptr;

	// Textures
	eastl::hash_set<eastl::string> texturesToShare;
	eastl::unordered_map<ID3D11Texture2D*, winrt::com_ptr<ID3D12Resource>> sharedTextures;

	eastl::unique_ptr<DX12::StructuredAppendBuffer<IrradianceCache::Entry<IrradianceCache::SH1Data>>> irradianceCacheBuffer = nullptr;	

	winrt::com_ptr<ID3D11SamplerState> samplerState = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> copyDepthCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> convertNormalGlossCS = nullptr;
	//eastl::unique_ptr<ConstantBuffer> frameBufferDX11CB = nullptr;

	//winrt::com_ptr<ID3D12Resource> blasInstanceBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>> blasInstanceBuffer = nullptr;	
	eastl::vector<D3D12_RAYTRACING_INSTANCE_DESC> blasInstances;

	winrt::com_ptr<ID3D12Resource> tlas = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasScratch = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasUpdateScratch = nullptr;

	eastl::vector<Light> lights;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Light>> lightBuffer = nullptr;

	// GI
	eastl::unique_ptr<DX12::StructuredBufferUpload<GIFrameData>> frameBuffer = nullptr;
	GIFrameData frameBufferData = {};

	// Shadows
	eastl::unique_ptr<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>> blasShadowInstanceBuffer = nullptr;
	eastl::vector<D3D12_RAYTRACING_INSTANCE_DESC> blasShadowInstances;

	RE::BSShadowDirectionalLight* shadowLight;

	eastl::unique_ptr<DX12::StructuredBufferUpload<ShadowsFrameData>> shadowsCB = nullptr;
	ShadowsFrameData shadowsCBData = {};

	// D3D12
	winrt::com_ptr<ID3D12Device5> d3d12Device = nullptr;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue = nullptr;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocator = nullptr;
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandList = nullptr;

	winrt::com_ptr<ID3D11Fence> d3d11Fence = nullptr;
	winrt::com_ptr<ID3D12Fence> d3d12Fence = nullptr;

	// GI
	winrt::com_ptr<ID3D12RootSignature> rootSignature = nullptr;
	winrt::com_ptr<ID3D12StateObject> pipelineRT = nullptr;
	eastl::unique_ptr<DX12::ShaderBindingTable> shaderBindingTable = nullptr;	
	eastl::unique_ptr<DX12::ResourceUpload> shaderBindingTableBuffer = nullptr;
	eastl::unique_ptr<DX12::DescriptorHeap<GIHeap::Slot::Values, GIHeap::Table::Values>> giHeap = nullptr;	

	// Shadows
	winrt::com_ptr<ID3D12RootSignature> shadowRS = nullptr;
	winrt::com_ptr<ID3D12StateObject> shadowPipeline = nullptr;
	eastl::unique_ptr<DX12::ResourceUpload> shadowSBTBuffer = nullptr;
	eastl::unique_ptr<DX12::DescriptorHeap<ShadowsHeap::Slot, ShadowsHeap::Table>> shadowHeap = nullptr;

	UINT64 fenceValue = 0;

	eastl::vector<winrt::com_ptr<ID3D12Resource>> asScratchBuffers;
	eastl::vector<eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC>> rtGeometryDescs;
	eastl::vector<eastl::unique_ptr<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC>> rtASDescs;

	// D3D11
	winrt::com_ptr<ID3D11Device5> d3d11Device = nullptr;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context = nullptr;

	// Sky Cubemap
	bool renderingCubemap = false;

	eastl::unique_ptr<WrappedResource> skyHemisphere = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> cubeToHemiCS = nullptr;
	
	// Shadow maps
	bool renderingShadowmap = false;
	eastl::unique_ptr<WrappedResource> shadowMaskTexture = nullptr;

	// Resources
	eastl::unique_ptr<DX12::Texture2D> outputTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> reflectanceTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> specularHitDistanceTexture = nullptr;

	eastl::unique_ptr<WrappedResource> depthTexture = nullptr;
	eastl::unique_ptr<WrappedResource> motionVectorsTexture = nullptr;

	winrt::com_ptr<ID3D12Resource> albedoTexture = nullptr;
	eastl::unique_ptr<WrappedResource> normalRoughnessTexture = nullptr;
	winrt::com_ptr<ID3D12Resource> GNMDTexture = nullptr;

	winrt::com_ptr<ID3D12Resource> gbufferReflectanceTexture = nullptr;

	eastl::unique_ptr<WrappedResource> mainTexture = nullptr;

	std::shared_mutex meshMutex;
	std::shared_mutex bufferMutex;
	std::shared_mutex sharedTextureMutex;
	std::shared_mutex renderMutex;

#if defined(DLSS_RR)
	HMODULE interposer = NULL;

	PFun_slInit* slInit{};
	PFun_slEvaluateFeature* slEvaluateFeature{};
	PFun_slGetNewFrameToken* slGetNewFrameToken{};
	PFun_slSetD3DDevice* slSetD3DDevice{};

	PFun_slDLSSDGetOptimalSettings* slDLSSDGetOptimalSettings{};
	PFun_slDLSSDGetState* slDLSSDGetState{};
	PFun_slDLSSDSetOptions* slDLSSDSetOptions{};

	PFun_slSetConstants* slSetConstants{};
	PFun_slGetFeatureFunction* slGetFeatureFunction{};
	PFun_slSetTag* slSetTag{};

	sl::ViewportHandle slViewportHandle{ 0 };

	Util::FrameChecker frameChecker;
	sl::FrameToken* frameToken = nullptr;

	float2 jitter = { 0, 0 };
#endif

	static inline float3 Float3(const RE::NiPoint3& point3)
	{
		return float3(point3.x, point3.y, point3.z);
	}

	static inline DXGI_FORMAT GetCompatibleFormat(DXGI_FORMAT format, bool recompress)
	{
		/*switch (format) {
		case DXGI_FORMAT_BC4_UNORM:
			return DXGI_FORMAT_R8_UNORM;
			break;
		case DXGI_FORMAT_BC4_SNORM:
			return DXGI_FORMAT_R8_SNORM;
			break;
		case DXGI_FORMAT_BC7_UNORM:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			break;
		default:
			return format;
			break;
		}*/

		switch (format) {
		case DXGI_FORMAT_BC4_UNORM:
			return recompress ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_R8_UNORM;
			break;
		case DXGI_FORMAT_BC7_UNORM:
			return recompress ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return recompress ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			break;
		default:
			return format;
			break;
		}
	}

	static inline bool EndsWith(const char* str, const char* suffix)
	{
		if (!str || !suffix)
			return false;

		size_t strLen = std::strlen(str);
		size_t suffixLen = std::strlen(suffix);

		if (suffixLen > strLen)
			return false;

		// Compare the end of str with suffix
		return std::strcmp(str + strLen - suffixLen, suffix) == 0;
	}

	struct Hooks
	{
		struct ID3D11Device_CreateBuffer
		{
			static HRESULT thunk(ID3D11Device* oThis, const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer)
			{
				HRESULT hr = func(oThis, pDesc, pInitialData, ppBuffer);

				auto& rtgi = globals::features::raytracing;

				if (SUCCEEDED(hr) && pInitialData && pDesc->BindFlags & D3D11_BIND_VERTEX_BUFFER) {
					if (rtgi.loaded && !rtgi.releaseBufferHooked) {
						std::lock_guard lock{ rtgi.bufferMutex };

						if (!rtgi.releaseBufferHooked)
						{
							stl::detour_vfunc<2, ID3D11Buffer_Release>(*ppBuffer);	
							rtgi.releaseBufferHooked = true;
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
				auto& rtgi = globals::features::raytracing;

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

				auto& rt = globals::features::raytracing;

				bool shareTexture = false;

				eastl::vector<D3D11_SUBRESOURCE_DATA> initialDataLocal;
				eastl::vector<DirectX::ScratchImage> outputMips;

				if (rt.loaded && rt.settingSharedTexture && pDesc && pInitialData && pDesc->ArraySize == 1 && pDesc->Usage == D3D11_USAGE_DEFAULT && pDesc->BindFlags == D3D11_BIND_SHADER_RESOURCE && pDesc->MiscFlags == 0 && pDesc->CPUAccessFlags == 0) {
					bool recompress = rt.settings.RecompressTextures;

					//logger::info("[RT] ID3D11Device::CreateTexture2D - Texture: [0x{:x}], [0x{:x}]", reinterpret_cast<uintptr_t>(*ppTexture2D), reinterpret_cast<uintptr_t>(ppTexture2D));

					descCopy.Format = GetCompatibleFormat(pDesc->Format, recompress);
	
					if (pDesc->Format != descCopy.Format) {
						initialDataLocal.resize(pDesc->MipLevels);
						outputMips.resize(pDesc->MipLevels);

						auto range = std::views::iota(0u, pDesc->MipLevels);

						auto decompressedFormat = GetCompatibleFormat(pDesc->Format, false);

						std::for_each(std::execution::par, range.begin(), range.end(), [&](uint mip) {
							DirectX::Image src;
							src.width = std::max(1u, pDesc->Width >> mip);
							src.height = std::max(1u, pDesc->Height >> mip);
							src.format = pDesc->Format;
							src.rowPitch = pInitialData[mip].SysMemPitch;
							src.slicePitch = pInitialData[mip].SysMemSlicePitch;
							src.pixels = (uint8_t*)pInitialData[mip].pSysMem;

							DirectX::ScratchImage decompressedScratch;
							DX::ThrowIfFailed(DirectX::Decompress(src, decompressedFormat, recompress ? decompressedScratch : outputMips[mip]));
							const DirectX::Image* decompressed = (recompress ? decompressedScratch : outputMips[mip]).GetImage(0, 0, 0);

							//logger::info("[RT] ID3D11Device::CreateTexture2D - Compressing: [0x{:x}]", reinterpret_cast<uintptr_t>(decompressed));

							if (recompress)
								DX::ThrowIfFailed(DirectX::Compress(*decompressed, descCopy.Format, DirectX::TEX_COMPRESS_DEFAULT, 0.5f, outputMips[mip]));

							const DirectX::Image* img = recompress ? outputMips[mip].GetImage(0, 0, 0) : decompressed;
							initialDataLocal[mip].pSysMem = img->pixels;
							initialDataLocal[mip].SysMemPitch = static_cast<UINT>(img->rowPitch);
							initialDataLocal[mip].SysMemSlicePitch = static_cast<UINT>(img->slicePitch);							
						});

						initialDataCopy = initialDataLocal.data();
					}

					descCopy.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
					shareTexture = true;
				}

				HRESULT hr = func(This, &descCopy, initialDataCopy, ppTexture2D);

				if (shareTexture) {
					if (SUCCEEDED(hr)) {
						if (!rt.releaseHooked) {
							std::lock_guard lock{ rt.sharedTextureMutex };

							if (!rt.releaseHooked) {
								rt.releaseHooked = true;
								stl::detour_vfunc<2, ID3D11Texture2D_Release>(*ppTexture2D);
							}
						}

						winrt::com_ptr<IDXGIResource1> dxgiResource = nullptr;
						DX::ThrowIfFailed((*ppTexture2D)->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

						HANDLE sharedHandle = nullptr;
						DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &sharedHandle));

						winrt::com_ptr<ID3D12Resource> resource = nullptr;
						HRESULT hrOSH = rt.d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(resource.put()));

						CloseHandle(sharedHandle);

						if (SUCCEEDED(hrOSH)) {
							rt.sharedTextures.emplace(*ppTexture2D, std::move(resource));
						} else {
							logger::warn("[RT] Error opening shared handle - [0x{:x}], Format: {}, Dimension: ({}, {}), MipLevels: {}", hrOSH, magic_enum::enum_name(pDesc->Format), pDesc->Width, pDesc->Height, pDesc->MipLevels);
						}
					} else {
						logger::warn("[RT] Error creating shareable texture - [0x{:x}], Format: {}, Dimension: ({}, {}), MipLevels: {}", hr, magic_enum::enum_name(pDesc->Format), pDesc->Width, pDesc->Height, pDesc->MipLevels);
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
					auto& rt = globals::features::raytracing;

					std::lock_guard lock{ rt.sharedTextureMutex };

					descCopy.Format = GetCompatibleFormat(pDesc->Format, rt.settings.RecompressTextures);

					if (pDesc->Format != descCopy.Format) {
						if (rt.sharedTextures.find(static_cast<ID3D11Texture2D*>(pResource)) != rt.sharedTextures.end()) {
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
					globals::features::raytracing.sharedTextures.erase(This);
				}

				return refCount;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld
		{
			static void thunk(bool a1)
			{
				globals::features::raytracing.Main_RenderWorld(a1);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <RE::BSShader::Type ShaderType>
		struct BSShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
			{
				auto& rtgi = globals::features::raytracing;

				if (rtgi.Active()) {
					rtgi.BSShader_SetupGeometry(This, Pass, RenderFlags);

					if (rtgi.renderingCubemap)
						return;
				}

				func(This, Pass, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSSkyShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
			{
				func(This, Pass, RenderFlags);

				if (globals::features::raytracing.Active())
					globals::features::raytracing.BSSkyShader_SetupGeometry(This, Pass, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSCubeMapCamera_RenderCubemap
		{
			static void thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5)
			{
				auto& rtgi = globals::features::raytracing;

				rtgi.renderingCubemap = true;

				func(camera, a2, a3, a4, a5);

				rtgi.renderingCubemap = false;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer_RenderPassImmediately
		{
			static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
			{
				if (globals::features::raytracing.Active())
					globals::features::raytracing.BSBatchRenderer_RenderPassImmediately(a_pass, a_technique, a_alphaTest, a_renderFlags);

				func(a_pass, a_technique, a_alphaTest, a_renderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSTriShape_UpdateWorldData
		{
			static void thunk(RE::BSTriShape* This, RE::NiUpdateData* data)
			{
				globals::features::raytracing.BSTriShape_UpdateWorldData(This, data);
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

#pragma region GeometryLoad

		struct NiStream_LoadObject
		{
			static bool thunk(RE::NiStream* oThis)
			{
				//logger::info("[RT] NiStream::LoadObject - Before - Path: {}", oThis->inputFilePath);

				auto& rt = globals::features::raytracing;

				auto result = func(oThis);

				rt.CommitGeometry();

				//logger::info("[RT] NiStream::LoadObject - After");
				//logger::info("[RT] NiStream::LoadObject | After - Buffers: {}", currentBuffers.size());

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSFadeNode_LoadBinary
		{
			static void thunk(RE::BSFadeNode* oThis, RE::NiStream& a_stream)
			{
				//logger::info("[RT] BSFadeNode::LoadBinary - Path: {}, Self Pointer: [0x{:x}]", a_stream.inputFilePath, reinterpret_cast<uintptr_t>(oThis));

				auto& rt = globals::features::raytracing;

				rt.BeginGeometry(oThis, a_stream);

				func(oThis, a_stream);

				//logger::info("[RT] BSFadeNode::LoadBinary - Name: {}, Self Pointer: [0x{:x}]", oThis->name, reinterpret_cast<uintptr_t>(oThis));

				if (rt.Active()) {
					auto& children = oThis->GetChildren();

					for (auto it = children.begin(); it != children.end(); it++) {
						logger::info("[RT] BSFadeNode::LoadBinary - Child: {}", it->get()->name);
					}

					/*RE::BSVisit::TraverseScenegraphGeometries(oThis, [&](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
						if (RE::BSTriShape* pTriShape = geometry->AsTriShape()) {
							auto effect = pTriShape->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
							auto shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(effect);

							auto& geometryRuntimeData = pTriShape->GetGeometryRuntimeData();
							RE::BSGraphics::TriShape* rendererData = geometryRuntimeData.rendererData;

							logger::info("[RT] BSFadeNode::TraverseScenegraphGeometries {} - TriShape: [0x{:x}], Shader Property: [0x{:x}]", geometry->name, reinterpret_cast<uintptr_t>(rendererData), reinterpret_cast<uintptr_t>(shaderProperty));
						} else {
							logger::info("[RT] BSFadeNode::TraverseScenegraphGeometries {}", geometry->name);
						}

						return RE::BSVisit::BSVisitControl::kContinue;
					});*/
					//rtgi.AddInstance(oThis);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		struct BSTriShape_LoadBinary
		{
			static void thunk(RE::BSTriShape* oThis, RE::NiStream& a_stream)
			{
				//logger::info("[RT] BSTriShape::LoadBinary | Before - Path: {}", a_stream.inputFilePath);

				func(oThis, a_stream);

				auto geometry = oThis->AsGeometry();
				auto geomRuntimeData = geometry->GetGeometryRuntimeData();

				//logger::info("[RT] BSTriShape::LoadBinary | After - Name: {}, Parent: [0x{:x}], Type: {}", oThis->name, reinterpret_cast<uintptr_t>(oThis->parent), magic_enum::enum_name(oThis->AsGeometry()->GetType().get()));
				/*logger::info(
					"[RT] BSTriShape::LoadBinary | After - Name: {}, Type: {}, Renderer Data: [0x{:x}]", 
					oThis->name,
					magic_enum::enum_name(oThis->AsGeometry()->GetType().get()),
					reinterpret_cast<uintptr_t>(geomRuntimeData.rendererData)
					);*/
				
				auto& rt = globals::features::raytracing;

				if (rt.Active()) 
				{
					rt.AddTriShape(oThis);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		struct BSTriShape_LinkObject
		{
			static void thunk(RE::BSTriShape* oThis, RE::NiStream& a_stream)
			{
				logger::info("[RT] BSTriShape::LinkObject | Before - Path: {}, Name: {}, Parent: [0x{:x}]", a_stream.inputFilePath, oThis->name, reinterpret_cast<uintptr_t>(oThis->parent));

				func(oThis, a_stream);

				logger::info("[RT] BSTriShape::LinkObject | Before - Parent Name: {}, Index: {}", oThis->parent->name, oThis->parentIndex);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct NiSkinPartition_LoadBinary
		{
			static void thunk(RE::NiSkinPartition* oThis, RE::NiStream& a_stream)
			{
				logger::info("[RT] NiSkinPartition::LoadBinary | Before");

				func(oThis, a_stream);

				logger::info("[RT] NiSkinPartition::LoadBinary | After");
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSGeometry_LoadBinary
		{
			static void thunk(RE::BSGeometry* oThis, RE::NiStream& a_stream)
			{
				logger::info("[RT] BSGeometry::LoadBinary | Before - Path: {}, Name: {}, Parent: [0x{:x}]", a_stream.inputFilePath, oThis->name, reinterpret_cast<uintptr_t>(oThis->parent));

				func(oThis, a_stream);

				logger::info("[RT] BSGeometry::LoadBinary | Before - Parent Name: {}, Index: {}, Type: {}", oThis->parent->name, oThis->parentIndex, magic_enum::enum_name(oThis->GetType().get()));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

#pragma endregion

		struct sub_7FF6F3FC3C00
		{
			static void* thunk(char* Source, __int64* a2, __int64 a3)
			{
				logger::info("[RT] sub_7FF6F3FC3C00 - Source: {}", Source);

				auto result = func(Source, a2, a3);

				logger::info("[RT] sub_7FF6F3FC3C00");

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct sub_1401CBF40
		{
			static void* thunk(char* Source)
			{
				logger::info("[RT] sub_1401CBF40 - Source: {}", Source);

				auto result = func(Source);

				logger::info("[RT] sub_1401CBF40");

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		
		struct BSLightingShaderProperty_SetupGeometry
		{
			static void thunk(RE::BSLightingShaderProperty* oThis, RE::BSGeometry* pGeometry, RE::NiStream& a_stream)
			{
				func(oThis, pGeometry, a_stream);

				auto& rtgi = globals::features::raytracing;

				if (rtgi.Active()) {
					//rtgi.AddInstance(netimmerse_cast<RE::BSTriShape*>(pGeometry));
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		struct BSShadowDirectionalLight_Cull
		{
			static void thunk(RE::BSShadowDirectionalLight* oThis, uint32_t& globalShadowLightCount, uint32_t shadowMaskChannel, RE::NiPointer<RE::NiAVObject> cullingScene)
			{
				auto& rt = globals::features::raytracing;

				if (rt.Active() && !rt.settings.RaytracedShadows)
					func(oThis, globalShadowLightCount, shadowMaskChannel, cullingScene);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShadowDirectionalLight_RenderShadowmaps
		{
			static void thunk(RE::BSShadowDirectionalLight* light, void* a2)
			{
				auto& rt = globals::features::raytracing;
				rt.renderingShadowmap = true;

				if (rt.Active() && rt.settings.RaytracedShadows)
					rt.UpdateShadowsFrameBuffer();

				// This is effectively bypassed (removing the call freezes the game...)
				func(light, a2);

				rt.renderingShadowmap = false;

				if (rt.Active() && rt.settings.RaytracedShadows) {
					rt.shadowLight = light;
					//rt.UpdateShadowInstances();
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderAccumulator_StartAccumulating
		{
			static void thunk(RE::BSGraphics::BSShaderAccumulator* shaderAccumulator, RE::NiCamera const* camera)
			{
				auto& rt = globals::features::raytracing;

				// Bypassing this alone does absolutely nothing.
				if (!rt.Active() || !rt.renderingShadowmap || !rt.settings.RaytracedShadows)
					func(shaderAccumulator, camera);			
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderAccumulator_FinishAccumulatingDispatch
		{
			static void thunk(RE::BSGraphics::BSShaderAccumulator* shaderAccumulator, uint32_t renderFlags)
			{
				auto& rt = globals::features::raytracing;

				if (!rt.Active() || !rt.renderingShadowmap || !rt.settings.RaytracedShadows)
					func(shaderAccumulator, renderFlags);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderPlayerView
		{
			static void thunk(void* a1, bool a2, bool a3)
			{
				auto& rt = globals::features::raytracing;

				if (rt.Active() && rt.settings.RaytracedShadows)
					rt.UpdateShadowsFrameBuffer();

				func(a1, a2, a3);
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderShadowmasks
		{
			static void thunk(bool a1)
			{
				auto& rt = globals::features::raytracing;

				if (rt.Active() && rt.settings.RaytracedShadows)
					rt.RenderShadows();
				else
					func(a1);
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};


		/*struct BSShaderResourceManager_LoadTexture
		{
			static void thunk(void* oThis, RE::NiSourceTexture* a_texture)
			{
				func(oThis, a_texture);
				logger::info("[RT] BSShaderResourceManager::LoadTexture - Texture: [0x{:x}] Name: {}", reinterpret_cast<uintptr_t>(a_texture), a_texture->name);
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderTextureSet_SetTexturePath
		{
			static void thunk(RE::BSShaderTextureSet* oThis, RE::BSTextureSet::Texture a_texture, const char* a_path)
			{
				//auto& rt = globals::features::raytracing;

				logger::info("[RT] BSShaderTextureSet::SetTexturePath - Texture: {}, Path: {}", magic_enum::enum_name(a_texture), a_path);
				func(oThis, a_texture, a_path);
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};*/

		/*struct BSShaderResourceManager_CreateTriShape
		{
			static RE::BSTriShape* thunk(void* oThis, RE::NiStream* pStream, void* a3, int a4, uint a5)
			{
				RE::BSTriShape* output = func(oThis, pStream, a3, a4, a5);

				//logger::info("[RT] BSShaderResourceManager::CreateTriShape - Name: {}", output->name);
				logger::info("[RT] BSShaderResourceManager::CreateTriShape - Stream: {}", pStream->filePath);

				//if (output) {
				//	auto geometry = output->AsGeometry();

				//	auto runtimeData = geometry->GetGeometryRuntimeData();

				//	logger::info("[RT] BSShaderResourceManager::CreateTriShape - Name: {}, TriShape: [0x{:x}]", output->name, reinterpret_cast<uintptr_t>(runtimeData.rendererData));
				//}
				//logger::info("[RT] BSShaderResourceManager::CreateTriShape - Name: {}", output->name);

				return output;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};*/

		/*struct NiSkinPartition_TriggersDataLoad
		{
			static void thunk(RE::NiSkinPartition* oThis, void* arg)
			{
				logger::info("[RT] NiSkinPartition::TriggersDataLoad - Nome {}, Index Buffer [0x{:x}]", oThis->AsDynamicTriShape, reinterpret_cast<uintptr_t>(output->indexBuffer));

				func(oThis, arg);

				logger::info("[RT] NiSkinPartition::TriggersDataLoad - Vertex Buffer [0x{:x}], Index Buffer [0x{:x}]", reinterpret_cast<uintptr_t>(output->vertexBuffer), reinterpret_cast<uintptr_t>(output->indexBuffer));
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};*/

		struct BSShaderResourceManager_CreateTriShapeRendererData
		{
			static RE::BSGraphics::TriShape* thunk(void* oThis, void* vertexBuffer, uint64_t vertexDesc, uint16_t* indices, uint indexCount)
			{
				auto output = func(oThis, vertexBuffer, vertexDesc, indices, indexCount);

				logger::info("[RT] BSShaderResourceManager::CreateTriShapeRendererData - Vertex Buffer [0x{:x}], Index Buffer [0x{:x}]", reinterpret_cast<uintptr_t>(output->vertexBuffer), reinterpret_cast<uintptr_t>(output->indexBuffer));

				return output;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---------

		// Only used by skinned meshes?
		struct BSShaderTextureSet_SetTexture
		{
			static void thunk(RE::BSShaderTextureSet* oThis, RE::BSTextureSet::Texture a_texture, RE::NiSourceTexturePtr& a_srcTexture)
			{
				auto& rt = globals::features::raytracing;
				rt.settingSharedTexture = a_texture == RE::BSTextureSet::Texture::kDiffuse || a_texture == RE::BSTextureSet::Texture::kGlowMap;

				logger::info(fmt::runtime("[RT] BSShaderTextureSet::SetTexture - Texture: {}, Src Texture: [0x{:x}] [0x{:x}]"), magic_enum::enum_name(a_texture), reinterpret_cast<uintptr_t>(&a_srcTexture), reinterpret_cast<uintptr_t>(a_srcTexture.get()));
				func(oThis, a_texture, a_srcTexture);
				logger::info("[RT] BSShaderTextureSet::SetTexture Src Texture: [0x{:x}] [0x{:x}]", reinterpret_cast<uintptr_t>(&a_srcTexture), reinterpret_cast<uintptr_t>(a_srcTexture.get()));
				
				rt.settingSharedTexture = false;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSTextureSet_SetTexture
		{
			static void thunk(RE::BSTextureSet* oThis, RE::BSTextureSet::Texture a_texture, RE::NiSourceTexturePtr& a_srcTexture)
			{
				auto& rt = globals::features::raytracing;
				rt.settingSharedTexture = a_texture == RE::BSTextureSet::Texture::kDiffuse || a_texture == RE::BSTextureSet::Texture::kGlowMap;

				logger::info(fmt::runtime("[RT] BSTextureSet::SetTexture - Texture: {}, Src Texture: [0x{:x}] [0x{:x}]"), magic_enum::enum_name(a_texture), reinterpret_cast<uintptr_t>(&a_srcTexture), reinterpret_cast<uintptr_t>(a_srcTexture.get()));
				func(oThis, a_texture, a_srcTexture);
				logger::info("[RT] BSTextureSet::SetTexture Src Texture: [0x{:x}] [0x{:x}]", reinterpret_cast<uintptr_t>(&a_srcTexture), reinterpret_cast<uintptr_t>(a_srcTexture.get()));

				rt.settingSharedTexture = false;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BGSTextureSet_SetTexture
		{
			static void thunk(RE::BGSTextureSet* oThis, __int64 a_texture, RE::NiSourceTexturePtr& a_srcTexture)
			{
				//auto& rt = globals::features::raytracing;
				//rt.settingSharedTexture = a_texture == RE::BSTextureSet::Texture::kDiffuse || a_texture == RE::BSTextureSet::Texture::kGlowMap;

				/*logger::info(fmt::runtime("[RT] BGSTextureSet::SetTexture - Texture: {}, Src Texture: [0x{:x}] [0x{:x}]"), magic_enum::enum_name(a_texture), reinterpret_cast<uintptr_t>(&a_srcTexture), reinterpret_cast<uintptr_t>(a_srcTexture.get()));
				func(oThis, a_texture, a_srcTexture);
				logger::info("[RT] BGSTextureSet::SetTexture Src Texture: [0x{:x}] [0x{:x}]", reinterpret_cast<uintptr_t>(&a_srcTexture), reinterpret_cast<uintptr_t>(a_srcTexture.get()));*/

				//logger::info(fmt::runtime("[RT] BGSTextureSet::SetTexture - Texture: {}"), magic_enum::enum_name(a_texture));

				auto a_texture64 = static_cast<RE::BSTextureSet::Texture>(a_texture);

				logger::info(fmt::runtime("[RT] BGSTextureSet::SetTexture - Before Texture: {}"), magic_enum::enum_name(a_texture64));
				func(oThis, a_texture, a_srcTexture);
				logger::info("[RT] BGSTextureSet::SetTexture - After");

				//rt.settingSharedTexture = false;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderTextureSet_SetTexturePath
		{
			static void thunk(RE::BSShaderTextureSet* oThis, RE::BSTextureSet::Texture a_texture, const char* a_path)
			{
				//auto& rt = globals::features::raytracing;

				logger::info("[RT] BSShaderTextureSet::SetTexturePath - Texture: {}, Path: {}", magic_enum::enum_name(a_texture), a_path);
				func(oThis, a_texture, a_path);
				logger::info("[RT] BSShaderTextureSet::SetTexturePath");
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderManager_GetTexture
		{
			static void thunk(const char* a_path, bool a_demand, RE::NiPointer<RE::NiTexture>& a_textureOut, bool a_isHeightMap)
			{
				
				auto path = std::string(a_path);

				auto& rt = globals::features::raytracing;
				rt.settingSharedTexture = path.ends_with("_d.dds") || path.ends_with("_p.dds");  // path.ends_with"_rmaos.dds")

				//logger::info("[RT] BSShaderManager::GetTexture - Path: {}, Demand: {}, Address: [0x{:x}], Address: [0x{:x}], Is HeightMap: {}", a_path, a_demand, reinterpret_cast<uintptr_t>(&a_textureOut), reinterpret_cast<uintptr_t>(a_textureOut.get()), a_isHeightMap);
				func(a_path, a_demand, a_textureOut, a_isHeightMap);
				//logger::info("[RT] BSShaderManager::GetTexture - Address: [0x{:x}], Address: [0x{:x}]", reinterpret_cast<uintptr_t>(&a_textureOut), reinterpret_cast<uintptr_t>(a_textureOut.get()));
			
				rt.settingSharedTexture = false;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// -------------

		/*struct BSLightingShaderMaterialBase_OnLoadTextureSet
		{
			static void thunk(RE::BSLightingShaderMaterialBase* oThis, std::uint64_t arg1, RE::BSTextureSet* inTextureSet)
			{
				//auto& rt = globals::features::raytracing;
				//RE::BSShaderTextureSet
				//RE::BGSTextureSet
				logger::info("[RT] BSLightingShaderMaterialBase::OnLoadTextureSet - Diffuse: [0x{:x}]", reinterpret_cast<uintptr_t>(oThis->diffuseTexture.get()));

				//rt.texturesToShare.emplace();
	
				//inTextureSet->

				func(oThis, arg1, inTextureSet);

				logger::info("[RT] BSLightingShaderMaterialBase::OnLoadTextureSet End - Diffuse: [0x{:x}]", reinterpret_cast<uintptr_t>(oThis->diffuseTexture.get()));

				if (inTextureSet) {
					auto diffusePath = inTextureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
					auto glowPath = inTextureSet->GetTexturePath(RE::BSTextureSet::Texture::kGloss);

					logger::info("[RT] BSLightingShaderMaterialBase::OnLoadTextureSet - Diffuse: {}, Glow: {}", diffusePath, glowPath);
				}

				//logger::info("[RT] BSShaderManager::OnLoadTextureSet - Address: {}, Address: {}", reinterpret_cast<uintptr_t>(&a_textureOut), reinterpret_cast<uintptr_t>(a_textureOut.get()));
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};*/
		
		static void Install()
		{
			stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));

			stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Lighting>>(RE::VTABLE_BSLightingShader[0]);
			stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Effect>>(RE::VTABLE_BSEffectShader[0]);
			//stl::write_vfunc<0x6, BSSkyShader_SetupGeometry>(RE::VTABLE_BSSkyShader[0]);

			stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

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

			stl::write_vfunc<0x18, BSTriShape_LoadBinary>(RE::VTABLE_BSTriShape[0]);
			stl::write_vfunc<0x18, BSFadeNode_LoadBinary>(RE::VTABLE_BSFadeNode[0]);
			stl::write_vfunc<0x18, NiSkinPartition_LoadBinary>(RE::VTABLE_NiSkinPartition[0]);
			stl::write_vfunc<0x18, BSGeometry_LoadBinary>(RE::VTABLE_BSGeometry[0]);

			//stl::write_vfunc<0x19, BSTriShape_LinkObject>(RE::VTABLE_BSTriShape[0]);


			//stl::detour_thunk<sub_7FF6F3FC3C00>(REL::Offset(0x7ff6eba40100).address());

			//auto VTABLE_NiStream = REL::VariantID(286064, 237126, 0x17ef860);
			//stl::write_vfunc<0x15, NiStream_LoadObject>(VTABLE_NiStream);

			/*stl::write_vfunc<0x15, NiStream_LoadObject>(RE::VTABLE_NiStream[0]);
			stl::write_vfunc<0x15, NiStream_LoadObject>(RE::VTABLE___DeepCopyStream[0]);
			stl::write_vfunc<0x15, BSStream_LoadObject>(RE::VTABLE_BSStream[0]);*/

			//stl::detour_thunk<sub_7FF6F3FC3C00>(REL::Offset(0x7ff6eba40100).address());

			logger::info("Base: [0x{:x}]", REL::Module::get().base());
			stl::detour_thunk<NiStream_LoadObject>(REL::Offset(0xd21790).address());
			//stl::detour_thunk<BSStream_LoadObject>(REL::Offset(0xe0cbb0).address());
			
			//stl::detour_thunk<sub_7FF6F3FC3C00>(0x7FF6F3FC3C00);
			//stl::detour_thunk<sub_1401CBF40>(0x1401CBF40);
			//sub_7FF6F35E1350(char *Source, volatile signed __int32 **a2)

			stl::detour_thunk<Main_RenderShadowmasks>(REL::RelocationID(100422, 107140));

			stl::write_vfunc<0x27, BSLightingShaderProperty_SetupGeometry>(RE::VTABLE_BSLightingShaderProperty[0]); //FinishSetupGeometry = 0x28
		
			//stl::write_vfunc<0x9, BSShadowDirectionalLight_Cull>(RE::VTABLE_BSShadowDirectionalLight[0]);
			stl::write_vfunc<0xA, BSShadowDirectionalLight_RenderShadowmaps>(RE::VTABLE_BSShadowDirectionalLight[0]);

			stl::write_vfunc<0x29, BSShaderAccumulator_StartAccumulating>(RE::VTABLE_BSShaderAccumulator[0]);
			stl::write_vfunc<0x2A, BSShaderAccumulator_FinishAccumulatingDispatch>(RE::VTABLE_BSShaderAccumulator[0]);

			//stl::detour_thunk<Main_RenderPlayerView>(REL::RelocationID(35560, 36559)); // Used to cache main camera frame buffer
			stl::detour_thunk<Main_RenderShadowmasks>(REL::RelocationID(100422, 107140));

			//stl::write_vfunc<0x8, BSLightingShaderMaterialBase_OnLoadTextureSet>(RE::VTABLE_BSLightingShaderMaterialBase[0]);

			//stl::write_vfunc<0x26, BSShaderTextureSet_SetTexture>(RE::VTABLE_BSShaderTextureSet[0]); // This is the golden standard

			//stl::write_vfunc<0x26, BSTextureSet_SetTexture>(RE::VTABLE_BSTextureSet[0]);
			//stl::write_vfunc<0x26, BGSTextureSet_SetTexture>(RE::VTABLE_BGSTextureSet[0]);

			//stl::write_vfunc<0x27, BSShaderTextureSet_SetTexturePath>(RE::VTABLE_BSShaderTextureSet[0]);

			stl::detour_thunk<BSShaderManager_GetTexture>(REL::RelocationID(98986, 105640));

			//stl::write_vfunc<0x1a, BSShaderResourceManager_LoadTexture>(RE::VTABLE_BSShaderResourceManager[0]);

			//stl::write_vfunc<0x2, BSShaderResourceManager_CreateTriShape>(RE::VTABLE_BSShaderResourceManager[0]);
			stl::write_vfunc<0x3, BSShaderResourceManager_CreateTriShapeRendererData>(RE::VTABLE_BSShaderResourceManager[0]);
			
			//NiSkinPartition::TriggersDataLoad
			//stl::write_vfunc<0x25, NiSkinPartition_TriggersDataLoad>(RE::VTABLE_NiSkinPartition[0]);

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