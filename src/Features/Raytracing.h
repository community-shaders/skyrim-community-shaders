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
#include <EASTL/deque.h>

#include "Features/Raytracing/IrradianceCache.h"
#include "Features/Raytracing/Allocator.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/RTPipelineBuilder.h"
#include "Features/Raytracing/ShaderBindingTable.h"

#include "Raytracing/Includes/Types/Light.hlsli"
#include "Raytracing/Includes/Types/GIFrameData.hlsli"
#include "Raytracing/Includes/Types/ShadowsFrameData.hlsli"

#define USE_PIX
#include <pix3.h>

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

struct Raytracing : public Feature
{
	static constexpr uint MAX_MESHES = 2048;
	static constexpr uint MAX_SUBMESHES = 2048;
	static constexpr uint MAX_INSTANCES = 2048;
	
	static constexpr uint MAX_LIGHTS = 255;

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
				Triangles = Vertices + MAX_SUBMESHES,
				DiffuseTextures = Triangles + MAX_SUBMESHES,
				GlowTextures = DiffuseTextures + MAX_SUBMESHES,
				NumDescriptors = GlowTextures + MAX_SUBMESHES,
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
	void DrawRTGI();
	void UpdateShadowsFrameBuffer();
	void RenderShadows();

	float3 GammaToLinear(float3 color);
	eastl::vector<LightLimitFix::LightData> GetPointLights();
	void UpdateLights();

	void Main_RenderWorld(bool a1);
	void BSShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);

	void SkyCubeToHemi();
	void CheckResourcesSide(int side);

	bool ValidTriShape(RE::NiNode* pNiNode);
	void AddInstance(RE::NiNode* pNiNode, const char* path);
	void AddUpdateAllInstances();

	eastl::vector<size_t> GatherInstanceLights(RE::NiNode* pNiNode);

	void UpdateInstances();
	void UpdateShadowInstances();

	template <typename T>
	void MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res);

	void DeviceRemovedHandler();

	void CopyDepth();
	void ConvertNormalGlossiness();

	void ReleaseTempGPUData();

	void BuildTLAS();
	void RebuildTLAS(ID3D12GraphicsCommandList4* pCommandList, size_t numDescs, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs);

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

	enum struct PIXCaptureLocation : int32_t
	{
		GlobalIllumination,
		Shadows,
		AO
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
		bool CullShadows = true;
		bool RecompressTextures = false;
#ifdef DLSS_RR
		DLSSRRQuality DLSSRRQualityMode = DLSSRRQuality::MaxQuality;
#endif
		DebugOutput DebugOutput = DebugOutput::None;
		bool EnablePIXCapture = true;
		PIXCaptureLocation PIXCaptureLocation = PIXCaptureLocation::GlobalIllumination;
		bool EnableDebugDevice = false;
#ifdef SHARC
		float SHARCScale = 1.0f;
#endif
	} settings;

	bool settingSharedTexture = false;
	bool renderingWorld = false;
	bool lightsUpdated = false;

	winrt::com_ptr<IDXGraphicsAnalysis> ga = nullptr;

	inline auto CaptureParams()
	{
		PIXCaptureParameters params{
			.GpuCaptureParameters = {
				.FileName = L"TDRCap.pix",
			}
		};

		return params;
	}

	bool pixCapture = false;
	bool pixCaptureStarted = false;
	bool pixMultiFrame = false;
	bool pixTDR = false;

	bool releaseBufferHooked = false;
	bool releaseHooked = false;
	HANDLE fenceEvent;

	bool addedAllInstances = false;

	static inline uint PackUByte4(float4 unpacked)
	{
		auto x = (uint)(unpacked.x * 255.0f) & 0xFF;
		auto y = (uint)(unpacked.y * 255.0f) & 0xFF;
		auto z = (uint)(unpacked.z * 255.0f) & 0xFF;
		auto w = (uint)(unpacked.w * 255.0f) & 0xFF;

		return (w << 24) | (z << 16) | (y << 8) | x;
	}

	static inline float4 UnpackUByte4(uint packed)
	{
		float4 result;
		result.x = (packed & 0xFF) / 255.0f;
		result.y = ((packed >> 8) & 0xFF) / 255.0f;
		result.z = ((packed >> 16) & 0xFF) / 255.0f;
		result.w = (packed >> 24) / 255.0f;
		return result;
	}

	static inline uint PackByte4(float4 unpacked)
	{
		return PackUByte4(unpacked * 0.5f + float4(0.5f, 0.5f, 0.5f, 0.5f));
	}

	static inline float4 UnpackByte4(uint packed)
	{
		return UnpackUByte4(packed) * 2.0f - float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

#pragma pack(push, 1)
	struct SkinData
	{
		uint16_t weight;
		uint8_t bone;
	};

	struct Vertex
	{
		float3 Position;
		uint16_t Texcoord0[2];
		uint32_t Normal;
		uint32_t Tangent;
		uint32_t Color;
	};

	struct Triangle
	{
		uint32_t v0;
		uint32_t v1;
		uint32_t v2;
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
		RE::BSShader::Type shaderType;
	};

	struct MeshData
	{
		uint registerIndex; // The position of this meshes SRV in the register stack
		uint vertexCount = 0;
		uint triangleCount = 0;
		eastl::vector<Vertex> vertices;
		eastl::vector<Triangle> triangles;
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
		//RE::NiBound localBound;

		bool HasFeature(RE::BSShaderMaterial::Feature feature)
		{
			for (auto& mesh : meshes)
				if (mesh.material.feature == feature)
					return true;

			return false;
		}

		bool HasShaderType(RE::BSShader::Type shaderType)
		{
			for (auto& mesh : meshes)
				if (mesh.material.shaderType == shaderType)
					return true;

			return false;
		}
	};

	// Appends RE::BSGraphics::TriShape data into MeshData
	void ReadRendererData(MeshData& meshData, RE::BSGraphics::TriShape* rendererData, const std::uint16_t& vertexCount, const std::uint16_t& triangleCount, const std::uint16_t& bonesPerVertex, const eastl::unordered_map<uint16_t, uint16_t>& vertexMap, const float4x4& transform);
	
	// Shortcut to ReadRendererData for RE::NiSkinPartition::Partition
	void ReadPartition(MeshData& meshData, RE::NiSkinPartition::Partition& partition, const float4x4& transform);

	// Reads material data
	void ReadMaterial(MeshData& meshData, const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, const char* name);

	// Creates Vertex and Triangle buffer
	void CreateBuffers(MeshData& meshData, const std::wstring& name);

	// Creates a single BLAS for a collection of MeshData
	void CommitGeometry(GeometryData& geometryData);
	
	void RegisterInstance(RE::BSFadeNode* pOriginal, RE::NiObject* pInstance);

	// Creates mesh buffers for all graph TriShapes, handles materials and builds a single BLAS for the node
	void CreateGeometry(const char* path, RE::NiNode* pRoot);

	Allocator registers = Allocator(MAX_SUBMESHES);

	// We'll group trishapes by their parent nodes, hopefully trishapes don't move on their own
	eastl::unordered_map<eastl::string, GeometryData> geometry;
	eastl::unordered_map<RE::NiNode*, eastl::string> inputPaths;

	// Instance
	struct InstanceData
	{
		uint lastUpdate;
		eastl::string filename;
		DirectX::XMFLOAT3X4 transform;
	};

	eastl::unordered_map<RE::NiNode*, InstanceData> instances;

	void UpdateInstanceTransform(RE::NiNode* pFadeNode, InstanceData& instanceData);

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

	Util::FrameChecker shadowFrameChecker;

	// Textures
	eastl::hash_set<eastl::string> texturesToShare;
	eastl::unordered_map<ID3D11Texture2D*, winrt::com_ptr<ID3D12Resource>> sharedTextures;

	eastl::unique_ptr<DX12::StructuredAppendBuffer<IrradianceCache::Entry<IrradianceCache::SH1Data>>> irradianceCacheBuffer = nullptr;	

	winrt::com_ptr<ID3D11SamplerState> samplerState = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> copyDepthCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> convertNormalGlossCS = nullptr;
	//eastl::unique_ptr<ConstantBuffer> frameBufferDX11CB = nullptr;

	eastl::unique_ptr<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>> blasInstanceBuffer = nullptr;	
	eastl::vector<D3D12_RAYTRACING_INSTANCE_DESC> blasInstances;

	winrt::com_ptr<ID3D12Resource> tlas = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasScratch = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasUpdateScratch = nullptr;

	eastl::vector<Light> lights;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Light>> lightBuffer = nullptr;

	// GI
	eastl::unique_ptr<DX12::StructuredBufferUpload<GIFrameData>> frameBuffer = nullptr;
	eastl::unique_ptr<GIFrameData> frameBufferData = nullptr;

	// Shadows
	eastl::unique_ptr<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>> blasShadowInstanceBuffer = nullptr;
	eastl::vector<D3D12_RAYTRACING_INSTANCE_DESC> blasShadowInstances;

	RE::BSShadowDirectionalLight* shadowLight;

	eastl::unique_ptr<DX12::StructuredBufferUpload<ShadowsFrameData>> shadowsCB = nullptr;
	eastl::unique_ptr<ShadowsFrameData> shadowsCBData = nullptr;

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

	uint64_t fenceValue = 0;

	struct TempGPUData
	{
		//std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
		winrt::com_ptr<ID3D12Resource> scratchBuffers;
		uint64_t fenceValue;
	};

	eastl::deque<TempGPUData> tempGPUData;

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

	std::shared_mutex geometryMutex;
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

	template <typename T, typename N>
	static inline auto GetFlags(N value)
	{
		const auto& entries = magic_enum::enum_entries<T>();

		std::string flags;

		for (const auto& [flag, name] : entries) {
			if ((static_cast<N>(value) & static_cast<N>(flag)) != 0) {
				flags += fmt::format("{} ", name);
			}
		}

		return flags;
	};

	template <typename T>
	static inline auto GetFlags(uint32_t value)
	{
		const auto& entries = magic_enum::enum_entries<T>();

		std::string flags;

		for (const auto& [flag, name] : entries) {
			if ((value & static_cast<uint32_t>(flag)) != 0) {
				flags += fmt::format("{} ", name);
			}
		}

		return flags;
	};

	template <typename T>
	static inline auto GetFlags(uint64_t value)
	{
		const auto& entries = magic_enum::enum_entries<T>();

		std::string flags;

		for (const auto& [flag, name] : entries) {
			if ((value & static_cast<uint64_t>(flag)) != 0) {
				flags += fmt::format("{} ", name);
			}
		}

		return flags;
	};

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
		
		struct TESProcessor_PostCreate
		{
			static void thunk(RE::TESModelDB::TESProcessor* oThis, const RE::BSModelDB::DBTraits::ArgsType& a_args, const char* path, RE::NiPointer<RE::NiNode>& a_root, std::uint32_t& typeOut)
			{
				func(oThis, a_args, path, a_root, typeOut);

				if (auto& rt = globals::features::raytracing; rt.Active()) {
					if (auto* pRoot = a_root.get(); pRoot) {
						//logger::info("[RT] TESProcessor::PostCreate {} - Path: {}, Args.Lodmult : {}, Args.texLoadLevel : {}, Args.postProcess : {}, Type Out: {}", pRoot->name, path, a_args.LODmult, a_args.texLoadLevel, a_args.postProcess, typeOut);

						rt.CreateGeometry(path, pRoot);
					}
				}
			};

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSModelDB_Demand
		{
			static void thunk(const char* a_modelPath, RE::NiPointer<RE::NiNode>& a_modelOut, const RE::BSModelDB::DBTraits::ArgsType& a_args)
			{
				func(a_modelPath, a_modelOut, a_args);
				logger::info("[RT] BSModelDB::Demand - Path: \"{}\", Model Out: {}", a_modelPath ? a_modelPath : "nullptr", a_modelOut.get() ? a_modelOut->name : "nullptr");
			};

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSFadeNode_CreateClone
		{
			static RE::NiObject* thunk(RE::BSFadeNode* oThis, RE::NiCloningProcess& a_cloning)
			{
				auto* result = func(oThis, a_cloning);

				if (auto& rt = globals::features::raytracing; rt.Active())
					rt.RegisterInstance(oThis, result);

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		struct TESObjectREFR_Load3D
		{
			static RE::NiAVObject* thunk(RE::TESObjectREFR* oThis, bool a_backgroundLoading)
			{
				auto* result = func(oThis, a_backgroundLoading);

				if (auto& rt = globals::features::raytracing; rt.Active()) {
					auto baseObject = oThis->GetBaseObject();

					if (auto model = baseObject->As<RE::TESModel>()) {
						rt.CreateGeometry(model->GetModel(), netimmerse_cast<RE::NiNode*>(result));
						logger::warn("[RT] Valid TESModel for {} - {}", result->name, model->GetModel());
					} else {
						logger::warn("[RT] Invalid TESModel for {}", result->name);
					}
				}

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		template <typename T>
		struct Load3DBase
		{
			static RE::NiAVObject* thunk(T* oThis, bool a_backgroundLoading)
			{
				auto* result = func(oThis, a_backgroundLoading);

				if (auto& rt = globals::features::raytracing; rt.Active()) {
					if (auto model = oThis->As<RE::TESModel>()) {
						rt.CreateGeometry(model->GetModel(), netimmerse_cast<RE::NiNode*>(result));
					}
				}

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <typename T>
		struct Load3D
		{
			static RE::NiAVObject* thunk(T* oThis, bool a_backgroundLoading)
			{
				auto* result = func(oThis, a_backgroundLoading);

				if (auto& rt = globals::features::raytracing; rt.Active()) {
					if (auto model = oThis->GetBaseObject()->As<RE::TESModel>()) {
						rt.CreateGeometry(model->GetModel(), netimmerse_cast<RE::NiNode*>(result));
					}
				}

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <typename T>
		struct Clone3DBase
		{
			static RE::NiAVObject* thunk(T* oThis, bool a_backgroundLoading)
			{
				auto* result = func(oThis, a_backgroundLoading);

				if (auto& rt = globals::features::raytracing; rt.Active()) {
					//auto clss = type_name<T>();
					auto clss = typeid(T).name();

					if (auto model = oThis->As<RE::TESModel>()) {
						rt.CreateGeometry(model->GetModel(), netimmerse_cast<RE::NiNode*>(result));
						logger::warn("[RT] {}::Clone3DBase Valid TESModel for {} - {}", clss, result->name, model->GetModel());
					} else {
						logger::warn("[RT] {}::Clone3DBase Invalid TESModel for {}", clss, result ? result->name : "nullptr");
					}
				}

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <typename T>
		struct Clone3D
		{
			static RE::NiAVObject* thunk(T* oThis, bool a_backgroundLoading)
			{
				auto* result = func(oThis, a_backgroundLoading);

				if (auto& rt = globals::features::raytracing; rt.Active()) {
					//auto clss = type_name<T>();
					auto clss = typeid(T).name();

					auto baseObject = oThis->GetBaseObject();

					if (auto model = baseObject->As<RE::TESModel>()) {
						rt.CreateGeometry(model->GetModel(), netimmerse_cast<RE::NiNode*>(result));
						logger::warn("[RT] {}::Clone3D Valid TESModel for {} - {}", clss, result->name, model->GetModel());
					} else {
						logger::warn("[RT] {}::Clone3D Invalid TESModel for {}", clss, result ? result->name : "nullptr");
					}
				}

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		static void Install()
		{
			// Handles model buffer creation
			//stl::detour_thunk<BSModelDB_Demand>(REL::RelocationID(74040, 75782));
			//stl::write_vfunc<0x1, TESProcessor_PostCreate>(RE::VTABLE_BSModelDB__BSModelProcessor[0]);
			//stl::write_vfunc<0x1, TESProcessor_PostCreate>(RE::VTABLE_TESModelDB____TESProcessor[0]);
			//stl::write_vfunc<0x17, BSFadeNode_CreateClone>(RE::VTABLE_BSFadeNode[0]);

			stl::write_vfunc<0x6A, Load3D<RE::TESObjectREFR>>(RE::VTABLE_TESObjectREFR[0]);

			/*stl::write_vfunc<0x6A, Load3D<RE::Character>>(RE::VTABLE_Character[0]);
			stl::write_vfunc<0x6A, Load3D<RE::PlayerCharacter>>(RE::VTABLE_PlayerCharacter[0]);*/

			/*stl::write_vfunc<0x6A, Load3D<RE::Actor>>(RE::VTABLE_Actor[0]);
			stl::write_vfunc<0x6A, Load3DBase<RE::TESObjectSTAT>>(RE::VTABLE_TESObjectSTAT[0]);
			stl::write_vfunc<0x6A, Load3DBase<RE::TESObjectMISC>>(RE::VTABLE_TESObjectMISC[0]);
			stl::write_vfunc<0x6A, Load3DBase<RE::TESRace>>(RE::VTABLE_TESRace[0]);
			stl::write_vfunc<0x6A, Load3DBase<RE::TESObjectARMA>>(RE::VTABLE_TESObjectARMA[0]);*/

			//stl::write_vfunc<0x4A, Clone3D<RE::Character>>(RE::VTABLE_Character[0]);

			/*stl::write_vfunc<0x4A, Clone3DBase<RE::TESBoundObject>>(RE::VTABLE_TESBoundObject[0]);
			stl::write_vfunc<0x4A, Clone3DBase<RE::TESBoundAnimObject>>(RE::VTABLE_TESBoundAnimObject[0]);
			stl::write_vfunc<0x4A, Clone3DBase<RE::TESActorBase>>(RE::VTABLE_TESActorBase[0]);
			stl::write_vfunc<0x4A, Clone3D<RE::Actor>>(RE::VTABLE_Actor[0]);*/

			//stl::write_vfunc<0x4A, Clone3DBase<RE::TESObjectARMA>>(RE::VTABLE_TESObjectARMA[0]); // Crashy crashy


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

			stl::detour_thunk<Main_RenderShadowmasks>(REL::RelocationID(100422, 107140));

			//stl::write_vfunc<0x27, BSLightingShaderProperty_SetupGeometry>(RE::VTABLE_BSLightingShaderProperty[0]); //FinishSetupGeometry = 0x28
		
			stl::write_vfunc<0xA, BSShadowDirectionalLight_RenderShadowmaps>(RE::VTABLE_BSShadowDirectionalLight[0]);

			stl::write_vfunc<0x29, BSShaderAccumulator_StartAccumulating>(RE::VTABLE_BSShaderAccumulator[0]);
			stl::write_vfunc<0x2A, BSShaderAccumulator_FinishAccumulatingDispatch>(RE::VTABLE_BSShaderAccumulator[0]);

			//stl::write_vfunc<0x1, BSModelProcessor_PostCreate>(RE::VTABLE_BSModelDB__BSModelProcessor[0]);

			stl::detour_thunk<BSShaderManager_GetTexture>(REL::RelocationID(98986, 105640));

			//stl::write_vfunc<0x3, BSShaderResourceManager_CreateTriShapeRendererData>(RE::VTABLE_BSShaderResourceManager[0]);

			logger::info("[RTGI] Installed hooks");
		}

		static void InstallD3D11Hooks(ID3D11Device* pDevice)
		{
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

	class TESObjectLoadedEventHandler : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*);

		static bool Register()
		{
			static TESObjectLoadedEventHandler singleton;

			auto scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
			scriptEventSourceHolder->GetEventSource<RE::TESObjectLoadedEvent>()->AddEventSink(&singleton);

			logger::info("Registered {}", typeid(singleton).name());

			return true;
		}
	};
};