#pragma once

#include "Feature.h"
#include <d3d12.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include "Features/Upscaling/DX12SwapChain.h"
#include <dxcapi.h>
#include "Features/Raytracing/Buffer.h"
#include "LightLimitFix.h"
#include <DirectXTex.h>
#include <shared_mutex>
#include <EASTL/deque.h>
//#include <half.hpp>

#include "Features/Raytracing/IrradianceCache.h"
#include "Features/Raytracing/Allocator.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/RTPipelineBuilder.h"
#include "Features/Raytracing/ShaderBindingTable.h"
#include "Features/Raytracing/Types.h"

#include "Raytracing/Includes/Types/Vertex.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Material.hlsli"
#include "Raytracing/Includes/Types/Light.hlsli"
#include "Raytracing/Includes/Types/GIFrameData.hlsli"
#include "Raytracing/Includes/Types/ShadowsFrameData.hlsli"

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

//using half_float::half;

struct Raytracing : public Feature
{
	static constexpr uint MAX_TEXTURES = 512;
	static constexpr uint MAX_MESHES = 1024;
	static constexpr uint MAX_SUBMESHES = 2048;
	static constexpr uint MAX_MATERIALS = MAX_SUBMESHES;
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
				Textures
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
				Materials,
				Instances,
				Vertices,
				Triangles = Vertices + MAX_SUBMESHES,
				Textures = Triangles + MAX_SUBMESHES,
				NumDescriptors = Textures + MAX_TEXTURES,
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

	void AddInstance(RE::NiNode* pNiNode, const char* path);

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
		bool EnablePIXCapture = false;
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

	bool pixCapture = false;
	bool pixCaptureStarted = false;
	bool pixMultiFrame = false;
	bool pixTDR = false;

	bool releaseBufferHooked = false;
	bool releaseHooked = false;
	HANDLE fenceEvent;

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

	static inline float3 Normalize(float3 vector)
	{
		vector.Normalize();
		return vector;
	}

	struct Skinning
	{
		half weight[4];
		uint8_t bone[4];

		Skinning() = default;

		Skinning(eastl::vector<half> weights, eastl::vector<uint8_t> boneIds)
		{
			auto weightCount = weights.size();
			auto boneIdsCount = boneIds.size();

			for (size_t i = 0; i < 4; i++) {
				weight[i] = i < weightCount ? weights[i] : half(0.0f);
				bone[i] = i < boneIdsCount ? boneIds[i] : 0;
			}
		}
	};

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

	struct TextureReference
	{
		ID3D12Resource* resource = nullptr;
		uint16_t registerIndex;
	};

	struct Mesh
	{
		enum Flags : uint8_t
		{
			None = 0,
			Alpha = 1 << 0,
			Skinned = 1 << 1,
			Dynamic = 1 << 2
		};
		//DEFINE_ENUM_FLAG_OPERATORS(Flags);

		uint registerIndex; // The position of this meshes SRV in the register stack
		uint vertexCount = 0;
		uint triangleCount = 0;
		eastl::vector<Vertex> vertices;
		eastl::vector<float4> dynamic;
		eastl::vector<Skinning> skinning;
		eastl::vector<Triangle> triangles;
		eastl::unique_ptr<DX12::StructuredBufferUpload<Vertex>> vertexBuffer = nullptr;
		eastl::unique_ptr<DX12::StructuredBufferUpload<Triangle>> triangleBuffer = nullptr;
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;
		Material material;
		eastl::vector<RE::BSTriShape*> instances;

		Flags flags = Flags::None;

		Mesh() = default;

		Mesh(uint registerIndex, eastl::vector<float4> dynamic, Flags flags = Flags::None) :
			registerIndex(registerIndex), dynamic(dynamic), flags(flags)
		{

		}
	};

	struct GeometryData
	{
		eastl::vector<Mesh> meshes;
		winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;
		//RE::NiBound localBound;

		/*bool HasFeature(RE::BSShaderMaterial::Feature feature)
		{
			for (auto& mesh : meshes)
				if (mesh.material.feature == feature)
					return true;

			return false;
		}*/

		bool HasShaderType(RE::BSShader::Type shaderType)
		{
			for (auto& mesh : meshes)
				if (mesh.material.ShaderType == shaderType)
					return true;

			return false;
		}
	};

	// Appends RE::BSGraphics::TriShape data into Mesh
	void BuildMesh(Mesh& meshData, RE::BSGraphics::TriShape* rendererData, const std::uint32_t& vertexCount, const std::uint16_t& triangleCount, const std::uint16_t& bonesPerVertex, const float4x4& transform);
	
	// Reads material data into Mesh
	void BuildMaterial(Mesh& meshData, const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, const char* name);

	// Creates Vertex, Triangle buffer and appends material into material buffer
	void CreateBuffers(Mesh& meshData, const std::wstring& name);

	// Creates a single BLAS for a collection of Mesh
	void CommitGeometry(GeometryData& geometryData);
	
	void RegisterInstance(RE::BSFadeNode* pOriginal, RE::NiObject* pInstance);

	// Creates mesh buffers for all graph TriShapes, handles materials and builds a single BLAS for the node
	void CreateGeometry(const char* path, RE::NiNode* pRoot);

	uint16_t GetTextureRegister(ID3D11Texture2D* texture, bool whiteDefault = true);

	Allocator registers = Allocator(MAX_SUBMESHES);
	Allocator textureRegisters = Allocator(MAX_TEXTURES);

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

	// This seems wasteful
	eastl::unique_ptr<DX12::StructuredBufferUpload<Material>> materialBuffer = nullptr;

	// Instance buffer
	struct Instance
	{
		uint MeshID;
		LightData LightData;
	};

	eastl::vector<Instance> instanceData;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Instance>> instanceBuffer = nullptr;

	Util::FrameChecker shadowFrameChecker;

	// Textures
	eastl::hash_set<eastl::string> texturesToShare;

	// Textures that have been shared with DX12
	eastl::unordered_map<ID3D11Texture2D*, winrt::com_ptr<ID3D12Resource>> sharedTextures;

	// Textures we have actually placed in a heap as SRV
	eastl::unordered_map<ID3D11Texture2D*, TextureReference> textures;

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

	static inline bool IsShareableFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_BC4_UNORM:
			return false;
			break;
		case DXGI_FORMAT_BC4_SNORM:
			return false;
			break;
		case DXGI_FORMAT_BC7_UNORM:
			return false;
			break;
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return false;
			break;
		default:
			return true;
			break;
		}
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

	template <typename T>
	static inline std::string GetFlags(auto value)
	{
		using N = decltype(value);

		const auto& entries = magic_enum::enum_entries<T>();

		std::string flags;

		for (const auto& [flag, name] : entries) {
			if (static_cast<N>(value) & static_cast<N>(flag)) {
				flags += fmt::format("{} ", name);
			}
		}

		return flags;
	};

	static inline std::string ToLower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return std::tolower(c); });
		return s;
	}

	static inline bool ShareableTexture(const char* path)
	{
		if (!path)
			return false;

		auto pathLower = ToLower(path);

		//if (pathLower.ends_with("_d.dds"))
		//	return true;
		
		if (pathLower.ends_with("_n.dds"))
			return false;

		if (pathLower.ends_with("_p.dds"))
			return false;

		if (pathLower.ends_with("_s.dds"))
			return false;

		if (pathLower.ends_with("_sk.dds"))
			return false;

		if (pathLower.ends_with("_msn.dds"))
			return false;

		if (pathLower.ends_with("_rmaos.dds"))
			return false;

		return true;
	}

	template <class T>
	void detour_thunk(size_t offset)
	{
		T::func = REL::Module::get().base() + offset;
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk));
		DetourTransactionCommit();
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

				bool share = rt.settingSharedTexture || pDesc && IsShareableFormat(pDesc->Format);

				if (rt.loaded && share && pDesc && pInitialData && pDesc->ArraySize == 1 && pDesc->Usage == D3D11_USAGE_DEFAULT && pDesc->BindFlags == D3D11_BIND_SHADER_RESOURCE && pDesc->MiscFlags == 0 && pDesc->CPUAccessFlags == 0) {
					bool recompress = rt.settings.RecompressTextures;

					descCopy.Format = GetCompatibleFormat(pDesc->Format, recompress);
	
					logger::trace("[RT] ID3D11Device::CreateTexture2D - Sharing Texture - Original Format: {}, Target Format: {}", magic_enum::enum_name(pDesc->Format), magic_enum::enum_name(descCopy.Format));

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
				auto& rt = globals::features::raytracing;

				if (rt.Active()) {
					rt.BSShader_SetupGeometry(This, Pass, RenderFlags);

					if (rt.renderingCubemap)
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
				auto& rt = globals::features::raytracing;

				rt.renderingCubemap = true;

				func(camera, a2, a3, a4, a5);

				rt.renderingCubemap = false;
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

		// Land
		struct BGSTextureSet_SetTexture
		{
			static void thunk(RE::BGSTextureSet* oThis, RE::BSTextureSet::Texture a_texture, RE::NiSourceTexturePtr& a_srcTexture)
			{
				auto& rt = globals::features::raytracing;

				// True PBR uses Displacement as kGlowMap, need a way to tell apart from vanilla material, maybe I could use BGSTextureSet::flags?
				rt.settingSharedTexture = a_texture == RE::BSTextureSet::Texture::kDiffuse || a_texture == RE::BSTextureSet::Texture::kGlowMap;

				if (rt.settingSharedTexture)
					logger::debug(fmt::runtime("[RT] BGSTextureSet::SetTexture - Texture: {}"), magic_enum::enum_name(a_texture));

				func(oThis, a_texture, a_srcTexture);

				rt.settingSharedTexture = false;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Actors and the rest
		struct BSShaderTextureSet_SetTexture
		{
			static void thunk(RE::BSShaderTextureSet* oThis, RE::BSTextureSet::Texture a_texture, RE::NiSourceTexturePtr& a_srcTexture)
			{
				auto& rt = globals::features::raytracing;
				rt.settingSharedTexture = a_texture == RE::BSTextureSet::Texture::kDiffuse || a_texture == RE::BSTextureSet::Texture::kGlowMap;

				func(oThis, a_texture, a_srcTexture);

				rt.settingSharedTexture = false;
			};
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

		//__int64 __fastcall sub_7FF62400F840(__int64 a1, __int64 a2, char* a3, __int64 a4, int a5)

		struct sub_7FF62400F840
		{
			static void* thunk(void* oThis, void* a2, char* path, void* a4, uint32_t a5)
			{
				logger::info("[RT] sub_7FF62400F840 Begin - Path {}, a5: [0x{:8X}]", path ? path : "", a5);
				auto* result = func(oThis, a2, path, a4, a5);
				logger::info("[RT] sub_7FF62400F840 End");

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		struct sub_7FF62400F3D0
		{
			static void* thunk(void* oThis, char* path, int64_t a3, void* a4, int64_t a5)
			{
				logger::info("[RT] sub_7FF62400F3D0 - Path {}", path ? path : "");
				auto* result = func(oThis, path, a3, a4, a5);
				logger::info("[RT] sub_7FF62400F3D0 - Path {}", path ? path : "");

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};	

		static void Install()
		{
			stl::write_vfunc<0x6A, Load3D<RE::TESObjectREFR>>(RE::VTABLE_TESObjectREFR[0]);

			stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));

			stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Lighting>>(RE::VTABLE_BSLightingShader[0]);
			stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Effect>>(RE::VTABLE_BSEffectShader[0]);
			//stl::write_vfunc<0x6, BSSkyShader_SetupGeometry>(RE::VTABLE_BSSkyShader[0]);

			stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

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

			stl::write_vfunc<0xA, BSShadowDirectionalLight_RenderShadowmaps>(RE::VTABLE_BSShadowDirectionalLight[0]);

			stl::write_vfunc<0x29, BSShaderAccumulator_StartAccumulating>(RE::VTABLE_BSShaderAccumulator[0]);
			stl::write_vfunc<0x2A, BSShaderAccumulator_FinishAccumulatingDispatch>(RE::VTABLE_BSShaderAccumulator[0]);

			stl::write_vfunc<0x26, BGSTextureSet_SetTexture>(RE::VTABLE_BGSTextureSet[1]);
			stl::write_vfunc<0x26, BSShaderTextureSet_SetTexture>(RE::VTABLE_BSShaderTextureSet[0]);

			logger::info("[RT] Installed hooks");
		}

		static void InstallD3D11Hooks(ID3D11Device* pDevice)
		{
			stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(pDevice);
			stl::detour_vfunc<7, ID3D11Device_CreateShaderResourceView>(pDevice);

			logger::info("[RT] Installed D3D11 hooks - {}", reinterpret_cast<uintptr_t>(pDevice));
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

inline Raytracing::Mesh::Flags operator|(Raytracing::Mesh::Flags lhs, Raytracing::Mesh::Flags rhs)
{
	return static_cast<Raytracing::Mesh::Flags>(lhs | rhs);
}

inline Raytracing::Mesh::Flags& operator|=(Raytracing::Mesh::Flags& lhs, Raytracing::Mesh::Flags rhs)
{
	lhs = static_cast<Raytracing::Mesh::Flags>(lhs | rhs);
	return lhs;
}

inline Raytracing::Mesh::Flags& operator&=(Raytracing::Mesh::Flags& lhs, Raytracing::Mesh::Flags rhs)
{
	lhs = lhs & rhs;
	return lhs;
}
