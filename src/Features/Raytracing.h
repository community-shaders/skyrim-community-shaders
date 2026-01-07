#pragma once

#include "PCH.h"

#define DLSS_RR

#include "Features/Upscaling/DX12SwapChain.h"
#include "LightLimitFix.h"
#include "OverlayFeature.h"
#include <D3D12MemAlloc.h>
#include <DirectXTex.h>
#include <EASTL/deque.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <shared_mutex>

#include "State.h"

#include "Features/Raytracing/Allocator.h"
#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/BufferMA.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/Model.h"
#include "Features/Raytracing/Pipelines/SHaRCPipeline.h"
#include "Features/Raytracing/Pipelines/SVGFPipeline.h"
#include "Features/Raytracing/RTPipelineBuilder.h"
#include "Features/Raytracing/ShaderBindingTable.h"
#include "Features/Raytracing/Shape.h"
#include "Features/Raytracing/Types.h"
#include "Features/Raytracing/Utils.h"
#include "Features/Raytracing/TextureSharing.h"

#include "Raytracing/FeatureData.hlsli"
#include "Raytracing/Includes/Types/FrameData.hlsli"
#include "Raytracing/Includes/Types/Instance.hlsli"
#include "Raytracing/Includes/Types/Light.hlsli"
#include "Raytracing/Includes/Types/Material.hlsli"
#include "Raytracing/Includes/Types/ShadowsFrameData.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Vertex.hlsli"
#include "Raytracing/Includes/Types/VertexUpdate.hlsli"

#include "Raytracing/Denoiser/SVGF/SVGF.hlsli"

#define NTDDI_VERSION NTDDI_WINBLUE

#include <DXProgrammableCapture.h>

#ifdef DLSS_RR
#	define NV_WINDOWS
#	pragma warning(push)
#	pragma warning(disable: 4471)
#	include <sl.h>
#	include <sl_consts.h>
#	include <sl_dlss.h>
#	include <sl_dlss_d.h>
#	include <sl_matrix_helpers.h>
#	include <sl_nis.h>
#	include <sl_version.h>
#	pragma warning(pop)
#endif

using namespace magic_enum::bitwise_operators;

struct Raytracing : public OverlayFeature
{
	// DX12 will not like if we don't respect these numbers and try to write over the resource end
	static constexpr uint MAX_TEXTURES = 1024;
	static constexpr uint MAX_MODELS = 1024;
	static constexpr uint MAX_SHAPES = MAX_MODELS * 6;
	static constexpr uint MAX_MATERIALS = MAX_SHAPES;
	static constexpr uint MAX_INSTANCES = 4096;
	static constexpr uint MAX_LIGHTS = 255;

	static constexpr uint SKY_CUBEMAP_SIZE = 256;

	enum MarkerFlags : uint32_t
	{
		MapMarker = 1 << 22,
		HeadingMarker = 1 << 23
	};

	struct GIHeapDef
	{
		enum class Table
		{
			UAV,
			SRV,
			VertexBuffer,
			TriangleBuffer,
			Textures
		};

		enum class Slot
		{
			Output,
			DiffuseAlbedoPathTracing,
			NormalRoughnessPathTracing,
			Reflectance,
			SpecularHitDist,
			SHaRCHashEntries,
			SHaRCLock,
			SHaRCAccumulation,
			SHaRCResolved,
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
			Indirection,
			Vertices,
			Triangles = Vertices + Raytracing::MAX_SHAPES,
			Textures = Triangles + Raytracing::MAX_SHAPES,
			NumDescriptors = Textures + Raytracing::MAX_TEXTURES,
			None
		};
	};
	using GIHeap = Heap<GIHeapDef::Table, GIHeapDef::Slot>;

	struct SkinningHeapDef
	{
		enum class Table
		{
			UAV,
			SRV,
			DynamicBuffer,
			SkinningBuffer
		};

		enum class Slot
		{
			Output,
			LocalToRoot = Output + Raytracing::MAX_SHAPES,  // = Output + Raytracing::MAX_SHAPES
			UpdateData,
			BoneMatrices,
			DynamicVertices,
			SkinningData = DynamicVertices + Raytracing::MAX_SHAPES,
			NumDescriptors = SkinningData + Raytracing::MAX_SHAPES,
			None
		};
	};
	using SkinningHeap = Heap<SkinningHeapDef::Table, SkinningHeapDef::Slot>;

	struct ShadowsHeapDef
	{
		enum class Table
		{
			UAV,
			SRV
		};

		enum class Slot
		{
			ShadowMask,
			Depth,
			TLAS,
			NumDescriptors,
			None
		};
	};
	using ShadowsHeap = Heap<ShadowsHeapDef::Table, ShadowsHeapDef::Slot>;

	struct SVGFHeapDef
	{
		enum class Table
		{
			UAV,
			SRV
		};

		enum class Slot
		{
			ShadowMask,
			Depth,
			TLAS,
			NumDescriptors,
			None
		};
	};
	using SVGFHeap = Heap<SVGFHeapDef::Table, SVGFHeapDef::Slot>;

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
	virtual inline std::string_view GetShaderDefineName() override { return "RT"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; };

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	void DrawSHaRCSettings();
	void DrawSVGFSettings();
#ifdef DLSS_RR
	void DrawDLSSRRSettings();
#endif
	void DrawDenoiserSettings();
	void DrawLightingSettings();
	void DrawLightSettings();

	void DrawGeneralSettings();
	void DrawAdvancedSettings();
	void DrawDebugSettings();

	virtual void DrawOverlay() override;
	virtual void PostPostLoad() override;

	virtual bool IsOverlayVisible() const override { return settings.PerformanceOverlay; };

	// Resources
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	void SetupOutputRT();

	void ShareRT(ID3D11Texture2D* pTexture2D, const GIHeap::Slot& target, const ShadowsHeap::Slot& cTarget, ID3D12Resource** ppResource) const;
	void SetupSharedRT();
	void CompileShaders();
	void CompileComputeShaders();

	void CompileSkinningShaders();
	void CompileRTGIShaders();
	void CompileRTShadowsShaders();

	void Initialize();
	void InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter);
	void CreateRootSignature();
	void CreateShadowsRootSignature();
	void CreateSkinningRootSignature();
	void UpdateDynamicSkinning(ID3D12GraphicsCommandList4* pCommandList);
	void DrawRTGI();
	void UpdateShadowsFrameBuffer();
	void RenderShadows();

	eastl::vector<LightLimitFix::LightData> GetPointLights();
	void UpdateLights();

	void Main_RenderWorld(bool a1);
	void BSShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);

	void SkyCubeToHemi() const;
	void CheckResourcesSide(int side);

	void AddInstance(RE::FormID formID, RE::NiNode* pNiNode, eastl::string path);

	eastl::vector<size_t> GatherInstanceLights(RE::NiNode* pNiNode);

	void UpdateInstances();
	void UpdateShadowInstances();

	void AddInstances();
	void ClearInstances();

	template <typename T>
	void MakeAndCopy(const eastl::vector<T>& data, winrt::com_ptr<ID3D12Resource>& res);

	void DeviceRemovedHandler();

	void CopyDepth();
	void ConvertTextures() const;

	void ReleaseTempGPUData();

	void BuildTLAS();
	void RebuildTLAS(ID3D12GraphicsCommandList4* pCommandList, size_t numDescs, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs);

	uint2 GetScreenSize() const;
	uint2 GetRenderSize();
	bool UpdateRenderSize();

#ifdef DLSS_RR
	void InitRR();
	void CheckFrameConstants();
	sl::DLSSMode GetDLSSMode() const;
	sl::DLSSDOptions GetDLSSRROptions() const;
	void GetDLSSRROptimal();
	void SetDLSSRROptions();
	int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth);
	void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount);
	float Halton(int32_t index, int32_t base);
#endif

	const bool Active()
	{
		return loaded && settings.Enabled;
	};

	const auto& GetPipelines()
	{
		if (!sharcPipeline)
			sharcPipeline = eastl::make_unique<SHaRCPipeline>();

		static eastl::array<IPipeline*, 1> pipelines = {
			sharcPipeline.get()
		};

		return pipelines;
	};

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

	static constexpr D3D12MA::ALLOCATION_DESC UPLOAD_HEAP_MA = { .HeapType = D3D12_HEAP_TYPE_UPLOAD };
	static constexpr D3D12MA::ALLOCATION_DESC DEFAULT_HEAP_MA = { .HeapType = D3D12_HEAP_TYPE_DEFAULT };

	enum struct Denoiser : int32_t
	{
		None,
		SVGF,
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
		NormalRoughnessGbuffer,
		GeometryNormalMetalness,
		Albedo,
		Diffuse,
		Passthrough
	};

#ifdef DLSS_RR
	enum struct DLSSRRQuality : int32_t
	{
		MaxPerformance,
		Balanced,
		MaxQuality,
		NativeRes,
		DLAA
	};

	enum struct DLSSRRPreset : int32_t
	{
		D,
		E
	};
#endif

	enum struct PIXCaptureLocation : int32_t
	{
		GlobalIllumination,
		Shadows,
		AO
	};

	// TODO: Rename to ReflectanceModel?
	enum struct DiffuseBRDF : int32_t
	{
		Lambert,
		Burley,
		OrenNayar,
		Gotanda,
		Chan
	};

	enum struct LightEvalMode : int32_t
	{
		Diffuse,
		BRDF
	};

	static constexpr const char* LightEvalModeTooltips[] = {
		"Diffuse only, no specular.",
		"Diffuse and Specular with BRDF."
	};
	static_assert(_countof(LightEvalModeTooltips) == magic_enum::enum_count<LightEvalMode>());

	enum struct LightingMode : int32_t
	{
		Diffuse,
		PBR
	};

	static constexpr const char* LightingModeTooltips[] = {
		"Diffuse only, no reflections.",
		"Physically Based Rendering mode with diffuse and reflections."
	};
	static_assert(_countof(LightingModeTooltips) == magic_enum::enum_count<LightingMode>());

	enum struct TraceMode : int32_t
	{
		Reference,
		SHaRC
	};

	static constexpr const char* TraceModeTooltips[] = {
		"Reference mode with no cache.",
		"Enables Spatially Hashed Radiance Cache, a technique aimed at improving signal quality and performance."
	};
	static_assert(_countof(TraceModeTooltips) == magic_enum::enum_count<TraceMode>());

#ifdef DLSS_RR
	static constexpr Denoiser DefaultDenoiser = Denoiser::DLSSRR;
#else
	static constexpr Denoiser DefaultDenoiser = Denoiser::SVGF;
#endif

#ifdef DLSS_RR
	struct DLSSRRSettings
	{
		DLSSRRQuality QualityMode = DLSSRRQuality::MaxQuality;
		DLSSRRPreset Preset = DLSSRRPreset::E;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DLSSRRSettings, QualityMode, Preset)
	};
#endif

	// Resampled Importance Sampling
	struct RISSettings
	{
		bool Enabled = true;
		int MaxCandidates = 4;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RISSettings, Enabled, MaxCandidates)
	};

	// Reservoir-based Spatiotemporal Importance Resampling
	struct ReSTIRSettings
	{
		bool ReSTIRDI = true;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ReSTIRSettings, ReSTIRDI)
	};

	struct AdvancedSettings
	{
		RISSettings RIS;
		ReSTIRSettings ReSTIR;

		bool GGXEnergyConservation = true;

		DiffuseBRDF DiffuseBRDF = DiffuseBRDF::Burley;
		LightEvalMode LightEvalMode = LightEvalMode::BRDF;
		LightingMode LightingMode = LightingMode::PBR;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AdvancedSettings, RIS, ReSTIR, GGXEnergyConservation, DiffuseBRDF, LightEvalMode, LightingMode)
	};

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool Enabled = true;
		bool GlobalIllumination = true;
		AdvancedSettings AdvancedSettings;
		TraceMode TraceMode = TraceMode::SHaRC;
		Denoiser Denoiser = DefaultDenoiser;
		int Bounces = 2;
		int SamplesPerPixel = 1;
		float2 Roughness = { 0.0f, 1.0f };
		float2 Metalness = { 0.0f, 1.0f };
		float Emissive = 1.0f;
		float Effect = 1.0f;
		float Sky = 1.0f;
		float Directional = 1.0f;
		float Point = 1.0f;
		bool LodDimmer = true;
		bool RaytracedShadows = true;
		bool PathTracing = false;
		bool CullShadows = true;
		bool RussianRoulette = true;
		bool ConvertToGamma = true;
#ifdef DLSS_RR
		DLSSRRSettings DLSSRR;
#endif
		SVGFPipeline::Settings SVGF;
		bool PerformanceOverlay = false;
		std::string Defines = "";
		DebugOutput DebugOutput = DebugOutput::None;
		bool EnablePIXCapture = false;
		PIXCaptureLocation PIXCaptureLocation = PIXCaptureLocation::GlobalIllumination;
		bool EnableDebugDevice = false;
		bool WhiteFurnace = false;
		SHaRCPipeline::Settings SHaRC;
	} settings;

	enum class RecompileReason : uint32_t
	{
		None = 0,
		General = 1 << 0,
		Advanced = 1 << 1,
		Debug = 1 << 2,
		RestoreDefaultsSettings = 1 << 3,
		LoadSettings = 1 << 4
	} recompileReason = RecompileReason::None;

	bool shareTexture = false;
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

	struct TextureReference
	{
		ID3D12Resource* resource = nullptr;
		eastl::shared_ptr<Allocation> allocation;
	};

	// Creates a single BLAS for a collection of Shapes
	// TODO: Move to Model struct
	void CommitModel(Model* model);

	// Creates mesh buffers for all graph TriShapes, handles materials and builds a single BLAS for the node
	void CreateModel(RE::TESObjectREFR* refr, const char* path, RE::NiNode* pRoot);

	// Removes the instance and optionally also releases the model and all its buffers if refCount reaches 0
	bool RemoveInstance(RE::NiNode* pRoot, bool releaseModel);
	bool RemoveInstance(RE::FormID formID, bool releaseModel);

	// TODO: Move to Model struct
	void UpdateModelBLAS(Model* model) const;

	eastl::shared_ptr<Allocation> GetTextureRegister(ID3D11Texture2D* texture, eastl::shared_ptr<Allocation> defaultTexture);

	Allocator shapeRegisters = Allocator(MAX_SHAPES);
	Allocator textureRegisters = Allocator(MAX_TEXTURES);

	struct DefaultTexture
	{
		eastl::shared_ptr<Allocation> allocation = nullptr;
		eastl::unique_ptr<DX12::Texture2DUpload<uint8_t>> texture = nullptr;

		DefaultTexture(ID3D12Device5* device, Allocation* allocation) :
			allocation(allocation)
		{
			texture = eastl::make_unique<DX12::Texture2DUpload<uint8_t>>(device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
		}

		void UpdateAndUpload(ID3D12GraphicsCommandList4* commandList, uint8_t* pixel) const
		{
			D3D12_SUBRESOURCE_DATA srcData = {
				.pData = pixel,
				.RowPitch = 4,
				.SlicePitch = 4
			};

			UpdateSubresources(
				commandList,
				texture->resource.get(),
				texture->uploadResource.get(),
				0, 0, 1,
				&srcData);
		}

		template <IsHeap HeapType>
		void CreateSRV(DX12::DescriptorHeap<HeapType>* heap, HeapType::Slot item) const
		{
			auto handle = heap->CPUHandle(item, allocation.get());

			texture->CreateSRV(handle);
		}

		uint16_t GetIndex() const
		{
			return allocation->GetIndex();
		}
	};

	eastl::shared_ptr<DefaultTexture> defaultWhiteTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultNormalTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultBlackTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultRMAOSTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultSpecularTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultEnvTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultEnvMaskTexture = nullptr;

	// We'll group trishapes by their parent nodes, hopefully trishapes don't move on their own
	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model>> models;

	// Instance
	struct Instance
	{
		eastl::string filename;
		float3x4 transform;
		Util::FrameChecker frameChecker;
		//bool hasUpdated = false;

		bool Update(RE::NiNode* pNiNode, [[maybe_unused]] const eastl::pair<eastl::string, Model*>& modelPair)
		{
			// Instance was not changed by the game, so there is no need to update it
			// This doesn't work at all for actors
			/*if (pNiNode->lastUpdatedFrameCounter < globals::state->frameCount && hasUpdated)
				return true;*/

			// Instance has already been updated this frame
			if (!frameChecker.IsNewFrame())
				return true;

			XMStoreFloat3x4(&transform, GetXMFromNiTransform(pNiNode->world));

			auto& [path, model] = modelPair;

			if ((model->GetFlags() & Flags::Dynamic) || (model->GetFlags() & Flags::Skinned)) {
				for (auto& shape : model->shapes) {
					Flags updateFlags = Flags::None;

					// Updates Dynamic Vertex position (and Bitangent.x) buffer
					// TODO: Test performance and stability of using a upload heap buffer and keeping it mapped to dynamicData
					if ((shape->flags & Flags::Dynamic) && shape->geometry) {
						//auto* pDynamicTriShape = netimmerse_cast<RE::BSDynamicTriShape*>(shape->geometry);

						auto* pDynamicTriShape = skyrim_cast<RE::BSDynamicTriShape*>(shape->geometry);

						if (pDynamicTriShape) {
							const auto& dynTriShapeRuntime = pDynamicTriShape->GetDynamicTrishapeRuntimeData();

							// We'll test if dynamic data has changed before updating and uploading
							// It does mean we have to memcpy twice, but I suppose the GPU bandwith we save makes up for it
							if (dynTriShapeRuntime.dynamicData && std::memcmp(shape->dynamicPosition.data(), dynTriShapeRuntime.dynamicData, dynTriShapeRuntime.dataSize) != 0) {
								std::memcpy(shape->dynamicPosition.data(), dynTriShapeRuntime.dynamicData, dynTriShapeRuntime.dataSize);

								shape->dynamicPositionBuffer->Update(dynTriShapeRuntime.dynamicData, dynTriShapeRuntime.dataSize);

								// We'll barrier and upload ourselfs in batch
								//shape.dynamicPositionBuffer->Upload(commandList);
								updateFlags |= Flags::Dynamic;
							}
						}
					}

					// TODO: Handle skinned meshes
					if ((shape->flags & Flags::Skinned) && shape->geometry) {
						// Restore pre-skinning vertices
						//shape.vertexBuffer->Upload(commandList);

						updateFlags |= Flags::Skinned;
					}

					if ((updateFlags & Flags::Dynamic) || (updateFlags & Flags::Skinned)) {
						auto& rt = globals::features::raytracing;

						rt.modelUpdate.emplace_back(path);
						rt.vertexUpdate.emplace_back(shape->allocation->GetIndex(), updateFlags & Flags::Dynamic ? shape->dynamicPositionBuffer.get() : nullptr, shape->vertexBuffer.get(), shape->vertexCount, updateFlags);
					}
				}
			}

			return true;
		}
	};

	winrt::com_ptr<D3D12MA::Allocator> allocator = nullptr;

	winrt::com_ptr<D3D12MA::Pool> uploadPool = nullptr;

	winrt::com_ptr<D3D12MA::Pool> dynamicVertexPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> vertexPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> skinningPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> trianglePool = nullptr;

	winrt::com_ptr<D3D12MA::Pool> blasScratchPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> blasPool = nullptr;

	eastl::unordered_map<RE::NiNode*, Instance> instances;
	eastl::unordered_map<RE::FormID, RE::NiNode*> formIDNodes;

	eastl::unique_ptr<DX12::StructuredBufferUpload<MaterialData>> materialBuffer = nullptr;

	eastl::vector<InstanceData> instanceBufferData;
	eastl::unique_ptr<DX12::StructuredBufferUpload<InstanceData>> instanceBuffer = nullptr;

	// Maps geometry to their actual buffer SRV
	eastl::unique_ptr<DX12::ResourceUpload> indirectionBuffer = nullptr;

	Util::FrameChecker shadowFrameChecker;

	// Textures that have been shared with DX12
	eastl::unordered_map<ID3D11Texture2D*, winrt::com_ptr<ID3D12Resource>> sharedTextures;

	// Textures we have actually placed in a heap as SRV
	eastl::unordered_map<ID3D11Texture2D*, TextureReference> textures;

	winrt::com_ptr<ID3D11SamplerState> samplerState = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> copyDepthCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> convertTexturesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> trueLinearToGammaCS = nullptr;

	eastl::unique_ptr<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>> blasInstanceBuffer = nullptr;
	eastl::vector<D3D12_RAYTRACING_INSTANCE_DESC> blasInstances;

	winrt::com_ptr<ID3D12Resource> tlas = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasScratch = nullptr;
	winrt::com_ptr<ID3D12Resource> tlasUpdateScratch = nullptr;

	eastl::vector<Light> lights;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Light>> lightBuffer = nullptr;

	// GI
	eastl::unique_ptr<DX12::StructuredBufferUpload<FrameData>> frameBuffer = nullptr;
	eastl::unique_ptr<FrameData> frameData = nullptr;

	// Shadows
	eastl::unique_ptr<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>> blasShadowInstanceBuffer = nullptr;
	eastl::vector<D3D12_RAYTRACING_INSTANCE_DESC> blasShadowInstances;

	RE::BSShadowDirectionalLight* shadowLight;

	eastl::unique_ptr<DX12::StructuredBufferUpload<ShadowsFrameData>> shadowsCB = nullptr;
	eastl::unique_ptr<ShadowsFrameData> shadowsCBData = nullptr;

	// SVGF

	RaytracingFD::FeatureData GetCommonBufferData();

	// D3D12
	winrt::com_ptr<ID3D12Device5> d3d12Device = nullptr;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue = nullptr;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocator = nullptr;
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandList = nullptr;

	winrt::com_ptr<ID3D11Fence> d3d11Fence = nullptr;
	winrt::com_ptr<ID3D12Fence> d3d12Fence = nullptr;

	// Skinning
	winrt::com_ptr<ID3D12RootSignature> skinningRS = nullptr;
	winrt::com_ptr<ID3D12PipelineState> skinningPipeline = nullptr;
	eastl::unique_ptr<DX12::DescriptorHeap<SkinningHeap>> skinningHeap = nullptr;

	// TODO: Move other effects to their own pipelines as well
	//	eastl::unique_ptr<SkinningPipeline> skinningPipeline = nullptr;
	//	eastl::unique_ptr<RTPipeline> RTPipeline = nullptr;
	//	eastl::unique_ptr<ShadowPipeline> shadowPipeline = nullptr;

	// SHaRC (Radiance cache)
	eastl::unique_ptr<SHaRCPipeline> sharcPipeline = nullptr;

	// SVGF (denoiser)
	eastl::unique_ptr<SVGFPipeline> svgfDenoiser = nullptr;

	struct VertexUpdate
	{
		uint16_t allocatedIndex;
		DX12::StructuredBufferUploadMA<float4>* dynamicPositionBuffer = nullptr;
		DX12::StructuredBufferUploadMA<Vertex>* vertexBuffer = nullptr;
		uint16_t vertexCount;
		Flags flags;
	};

	eastl::vector<VertexUpdate> vertexUpdate;
	eastl::unique_ptr<DX12::StructuredBufferUpload<VertexUpdateData>> vertexUpdateBuffer = nullptr;

	eastl::vector<eastl::string> modelUpdate;

	// GI
	winrt::com_ptr<ID3D12RootSignature> rootSignature = nullptr;
	winrt::com_ptr<ID3D12StateObject> pipelineRT = nullptr;
#if defined(SHARC)
	winrt::com_ptr<ID3D12StateObject> pipelineSHaRCRT = nullptr;
#endif
	eastl::unique_ptr<DX12::ShaderBindingTable> shaderBindingTable = nullptr;
	eastl::unique_ptr<DX12::ResourceUpload> shaderBindingTableBuffer = nullptr;
	eastl::unique_ptr<DX12::DescriptorHeap<GIHeap>> giHeap = nullptr;

	// Shadows
	winrt::com_ptr<ID3D12RootSignature> shadowRS = nullptr;
	winrt::com_ptr<ID3D12StateObject> shadowPipeline = nullptr;
	eastl::unique_ptr<DX12::ResourceUpload> shadowSBTBuffer = nullptr;
	eastl::unique_ptr<DX12::DescriptorHeap<ShadowsHeap>> shadowHeap = nullptr;

	uint64_t fenceValue = 0;

	struct TempGPUData
	{
		winrt::com_ptr<D3D12MA::Allocation> scratchBuffers;
		uint64_t fenceValue;
	};

	eastl::deque<TempGPUData> tempGPUData;

	// D3D11
	winrt::com_ptr<ID3D11Device5> d3d11Device = nullptr;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context = nullptr;

	struct alignas(16) RenderResData
	{
		uint2 RenderRes;
		float2 RenderResRcp;
	};

	eastl::unique_ptr<RenderResData> renderResData = nullptr;
	eastl::unique_ptr<ConstantBuffer> renderResCB = nullptr;

	// Sky Cubemap
	bool renderingCubemap = false;

	eastl::unique_ptr<WrappedResource> skyHemisphere = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> cubeToHemiCS = nullptr;

	// Shadow maps
	bool renderingShadowmap = false;
	eastl::unique_ptr<WrappedResource> shadowMaskTexture = nullptr;

	// Resources
	eastl::unique_ptr<DX12::Texture2D> outputTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> diffuseAlbedoPathTracingTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> normalRoughnessPathTracingTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> specularAlbedoTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> specularHitDistanceTexture = nullptr;

	eastl::unique_ptr<WrappedResource> depthTexture = nullptr;
	eastl::unique_ptr<WrappedResource> motionVectorsTexture = nullptr;

	// True Albedo
	winrt::com_ptr<ID3D12Resource> albedoTexture = nullptr;

	// Metalness modulated albedo
	eastl::unique_ptr<WrappedResource> diffuseAlbedoTexture = nullptr;

	// World normal and roughness
	eastl::unique_ptr<WrappedResource> normalRoughnessTexture = nullptr;

	// Geometry normal, metalness and AO
	winrt::com_ptr<ID3D12Resource> GNMDTexture = nullptr;

	eastl::unique_ptr<WrappedResource> mainTexture = nullptr;

	std::shared_mutex geometryMutex;
	std::shared_mutex bufferMutex;
	std::shared_mutex renderMutex;

	std::shared_mutex textureRegisterMutex;
	std::recursive_mutex shareTextureMutex;

	uint2 renderSize;
	float2 dynamicResolutionRatio;

	// Timings
	float mainTime;
	float shadowsTime;

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

	sl::DLSSDOptions dlssdOptions{};
	sl::DLSSDOptimalSettings optimalSettings{};
#endif

	struct Hooks
	{
		struct ID3D11Device_CreateTexture2D
		{
			static HRESULT WINAPI thunk(ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
			{
				if (!pDesc || !pInitialData)
					return func(This, pDesc, pInitialData, ppTexture2D);

				auto& rt = globals::features::raytracing;
				std::lock_guard<std::recursive_mutex> lock(rt.shareTextureMutex);

				D3D11_TEXTURE2D_DESC descCopy = *pDesc;

				if (rt.shareTexture && !(pDesc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)) {
					descCopy.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
				}

				return func(This, &descCopy, pInitialData, ppTexture2D);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct NiSourceTexture_Destructor
		{
			static void thunk(RE::NiSourceTexture* oThis)
			{
				if (oThis && oThis->rendererTexture) {
					if (auto texture = oThis->rendererTexture->texture) {
						auto& rt = globals::features::raytracing;

						if (auto sharedIt = rt.sharedTextures.find(texture); sharedIt != rt.sharedTextures.end()) {
							if (auto textureIt = rt.textures.find(texture); textureIt != rt.textures.end()) {
								auto index = textureIt->second.allocation->GetIndex();

								logger::debug("[RT] NiSourceTexture::Destructor [0x{:8X}] - Register: {}", reinterpret_cast<uintptr_t>(texture), index);

								// I imagine this isn't fast but I'll keep this in until I'm sure everything has been fixed
								for (auto& [key, model] : rt.models) {
									for (auto& shape : model->shapes) {
										auto& material = shape->material;

										if (index == material.BaseTexture->GetIndex())
											logger::error("[RT]\t\t NiSourceTexture::Destructor - Found in: {}", key);
									}
								}
							}

							rt.sharedTextures.erase(sharedIt);
						}
					}
				}

				func(oThis);
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

					if (rt.renderingCubemap) {
						if (This->shaderType.get() != RE::BSShader::Type::Sky) {
							//Pass->geometry->CullGeometry(true);
							return;
						}
					}
				}

				func(This, Pass, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSCubeMapCamera_RenderCubemap
		{
			static void thunk(RE::NiCamera* camera, int a2, bool a3, bool a4, bool a5)
			{
				auto& rt = globals::features::raytracing;

				rt.renderingCubemap = true;

				func(camera, a2, a3, a4, a5);

				rt.renderingCubemap = false;
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

		template <typename T>
		struct Load3D
		{
			static RE::NiAVObject* thunk(T* oThis, bool a_backgroundLoading)
			{
				auto* result = func(oThis, a_backgroundLoading);

				if (auto& rt = globals::features::raytracing; rt.Active()) {
					auto* baseObject = oThis->GetBaseObject();

					auto flags = baseObject->GetFormFlags();
					RE::FormType type = baseObject->GetFormType();

					if (type == RE::FormType::IdleMarker)
						return result;

					if (flags & MarkerFlags::MapMarker || flags & MarkerFlags::HeadingMarker)
						return result;

					/*RE::FormID id = baseObject->GetFormID();
					logger::info("[RT] Load3DA - Name: {}, Flags [0x{:8X}]: {}", typeid(*baseObject).name(), flags, GetFlagsString<RE::TESObjectREFR::RecordFlags::RecordFlag>(flags));
					logger::info("[RT] Load3DA - FormID: [0x{:8X}], FormType: {}", id, magic_enum::enum_name(type));*/

					if (auto* model = baseObject->As<RE::TESModel>()) {
						rt.CreateModel(oThis, model->GetModel(), netimmerse_cast<RE::NiNode*>(result));
					}
				}

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <typename T>
		struct Release3DRelatedData
		{
			static void thunk(T* oThis)
			{
				if (auto& rt = globals::features::raytracing; rt.Active()) {
					if (auto* pNiAVObject = oThis->Get3D()) {
						rt.RemoveInstance(netimmerse_cast<RE::NiNode*>(pNiAVObject), true);
					}
				}

				func(oThis);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TESObjectREFR_Enable
		{
			static void thunk(RE::TESObjectREFR* oThis, bool a_resetInventory)
			{
				if (auto& rt = globals::features::raytracing; rt.Active()) {
					auto* baseObject = oThis->GetBaseObject();

					if (auto* model = baseObject->As<RE::TESModel>()) {
						logger::info("[RT] TESObjectREFR::Enable: {}", model->GetModel());
					}
				}

				func(oThis, a_resetInventory);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TESObjectREFR_Disable
		{
			static void thunk(RE::TESObjectREFR* oThis)
			{
				if (auto& rt = globals::features::raytracing; rt.Active()) {
					auto* baseObject = oThis->GetBaseObject();

					if (auto* model = baseObject->As<RE::TESModel>()) {
						logger::info("[RT] TESObjectREFR::Disable: {}", model->GetModel());
					}
				}

				func(oThis);
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
						rt.CreateModel(model->GetModel(), netimmerse_cast<RE::NiNode*>(result));
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
						rt.CreateModel(model->GetModel(), netimmerse_cast<RE::NiNode*>(result));
						logger::warn("[RT] {}::Clone3D Valid TESModel for {} - {}", clss, result->name, model->GetModel());
					} else {
						logger::warn("[RT] {}::Clone3D Invalid TESModel for {}", clss, result ? result->name : "nullptr");
					}
				}

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <typename T>
		struct Destructor
		{
			static void thunk(T* oThis)
			{
				if (auto& rt = globals::features::raytracing; rt.Active()) {
					rt.RemoveInstance(oThis, false);
				}

				func(oThis);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		
		struct CreateTextureFromDDS
		{
			static RE::NiSourceTexture* thunk(RE::BSResource::CompressedArchiveStream* a1, char* path, ID3D11ShaderResourceView* srv, char a4, bool a5)
			{
				auto& rt = globals::features::raytracing;

				std::lock_guard<std::recursive_mutex> lock(rt.shareTextureMutex);

				rt.shareTexture = true;

				auto* result =  func(a1, path, srv, a4, a5);

				rt.shareTexture = false;

				return result;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};
		
		static void Install()
		{
			stl::write_vfunc<0x6A, Load3D<RE::TESObjectREFR>>(RE::VTABLE_TESObjectREFR[0]);
			stl::write_vfunc<0x6B, Release3DRelatedData<RE::TESObjectREFR>>(RE::VTABLE_TESObjectREFR[0]);

			//stl::detour_thunk<TESObjectREFR_Enable>(REL::RelocationID(19373, 19800));
			//stl::write_vfunc<0x89, TESObjectREFR_Disable>(RE::VTABLE_TESObjectREFR[0]);

			// NiSourceTexture Destructor
			stl::write_vfunc<0x0, NiSourceTexture_Destructor>(RE::VTABLE_NiSourceTexture[0]);

			// Destructors to remove instances
			stl::write_vfunc<0x0, Destructor<RE::NiNode>>(RE::VTABLE_NiNode[0]);
			stl::write_vfunc<0x0, Destructor<RE::BSFadeNode>>(RE::VTABLE_BSFadeNode[0]);
			stl::write_vfunc<0x0, Destructor<RE::BSFadeNode>>(RE::VTABLE_BSLeafAnimNode[0]);
			//stl::write_vfunc<0x0, Destructor<RE::BSFadeNode>>(RE::VTABLE_BSTreeNode[0]);

			stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));

			// We use these to render only the sky to the cubemaps, maybe it would be cleaner if we could override cubemap renderpass?
			stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Lighting>>(RE::VTABLE_BSLightingShader[0]);
			//stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Effect>>(RE::VTABLE_BSEffectShader[0]);
			//stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::DistantTree>>(RE::VTABLE_BSDistantTreeShader[0]);

			//stl::write_vfunc<0x6, BSSkyShader_SetupGeometry>(RE::VTABLE_BSSkyShader[0]);

			stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

			if (REL::Module::IsAE()) {
				stl::write_vfunc<0x35, BSTriShape_OnVisible>(RE::VTABLE_BSTriShape[0]);
			} else {
				stl::write_vfunc<0x34, BSTriShape_OnVisible>(RE::VTABLE_BSTriShape[0]);
			}

			stl::detour_thunk<Main_RenderShadowmasks>(REL::RelocationID(100422, 107140));

			stl::write_vfunc<0xA, BSShadowDirectionalLight_RenderShadowmaps>(RE::VTABLE_BSShadowDirectionalLight[0]);

			stl::write_vfunc<0x29, BSShaderAccumulator_StartAccumulating>(RE::VTABLE_BSShaderAccumulator[0]);
			stl::write_vfunc<0x2A, BSShaderAccumulator_FinishAccumulatingDispatch>(RE::VTABLE_BSShaderAccumulator[0]);

			detour_thunk<CreateTextureFromDDS>(0xd2ef80);

			//logger::info("[RT] Base: [0x{:8X}]", REL::Module::get().base());

			logger::info("[RT] Installed hooks");
		}

		static void InstallD3D11Hooks(ID3D11Device* pDevice)
		{
			stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(pDevice);
			//stl::detour_vfunc<7, ID3D11Device_CreateShaderResourceView>(pDevice);

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