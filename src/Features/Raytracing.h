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

#include <RE/T/TESWaterReflections.h>

#include "Features/Raytracing/Core/Instance.h"
#include "Features/Raytracing/Core/Model.h"
#include "Features/Raytracing/Core/Shape.h"

#include "Features/Raytracing/Helpers/ModelSpaceToTangent.h"

#include "Features/Raytracing/Allocator.h"
#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/BufferMA.h"
#include "Features/Raytracing/Heap.h"
#include "Features/Raytracing/HeapManager.h"
#include "Features/Raytracing/magic_enum_spec.h"
#include "Features/Raytracing/Pipelines/SHaRCPipeline.h"
#include "Features/Raytracing/Pipelines/SVGFPipeline.h"
#include "Features/Raytracing/Pipelines/SkinningPipeline.h"
#include "Features/Raytracing/RTConstants.h"
#include "Features/Raytracing/RTPipelineBuilder.h"
#include "Features/Raytracing/ShaderBindingTable.h"
#include "Features/Raytracing/TextureSharing.h"
#include "Features/Raytracing/Types.h"
#include "Features/Raytracing/Utils.h"

#include "Features/Raytracing/RE/CellAttachDetachEvent.h"

#include "Raytracing/FeatureData.hlsli"
#include "Raytracing/Includes/Types/FrameData.hlsli"
#include "Raytracing/Includes/Types/Instance.hlsli"
#include "Raytracing/Includes/Types/Light.hlsli"
#include "Raytracing/Includes/Types/Shape.hlsli"
#include "Raytracing/Includes/Types/Material.hlsli"
#include "Raytracing/Includes/Types/ShadowsFrameData.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Vertex.hlsli"

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

#define STATIC_ASSERT_ENUM_COUNT(EnumType, Array) \
	static_assert(_countof(Array) == magic_enum::enum_count<EnumType>(), "Array size must match enum count");

struct Raytracing : public OverlayFeature
{
	enum MarkerFlags : uint32_t
	{
		Compressed = 1 << 18,
		MapMarker = 1 << 22,	// TESObjectACTI
		IsMarker = 1 << 23		// TESObjectSTAT
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
			Shapes,
			Instances,
			Vertices,
			Triangles = Vertices + RTConstants::MAX_SHAPES,
			Textures = Triangles + RTConstants::MAX_SHAPES,
			NumDescriptors = Textures + RTConstants::MAX_TEXTURES,
			None
		};
	};
	using GIHeap = Heap<GIHeapDef::Table, GIHeapDef::Slot>;

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
	virtual std::vector<FeatureConstraints::Constraint> GetActiveConstraints() const override;

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	void DrawSHaRCSettings();
	void DrawSVGFSettings();
	void DrawSVGFInternalSettings(const char* name, SVGFPipeline::Settings& svgfSettings);
#ifdef DLSS_RR
	void DrawDLSSRRSettings();
#endif
	void DrawDenoiserSettings();
	void DrawResolutionSettings();
	void DrawLightingSettings();
	void DrawLightSettings();

	void DrawGeneralSettings();
	void DrawAdvancedSettings();
	void DrawDebugSettings();

	virtual void DrawOverlay() override;

	// SKSE kDataLoaded message
	virtual void DataLoaded() override;

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
	void CompileCompositeShader();

	void CompileRTGIShaders();
	void CompileRTShadowsShaders();

	void InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter);
	void CreateRootSignature();
	void CreateShadowsRootSignature();
	void DrawRTGI();
	void UpdateShadowsFrameBuffer();
	void RenderShadows();

	eastl::vector<LightLimitFix::LightData> GetPointLights();
	void UpdateLights();

	void ConvertMSN();

	void Main_RenderWorld(bool a1);
	void BSShader_SetupGeometry(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);

	void SkyCubeToHemi() const;
	void CheckResourcesSide(int side);

	void AddInstance(RE::FormID formID, RE::NiAVObject* pNiNode, eastl::string path);

	eastl::vector<size_t> GatherInstanceLights(RE::NiAVObject* pNiNode);

	void UpdateInstances();
	void UpdateBLASes();

	void UpdateShadowInstances();

	void DeviceRemovedHandler();

	void CopyDepth();
	void ConvertTextures() const;

	void PostRaytraceCleanup();

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

	const bool RaytracedShadows()
	{
		return settings.RaytracedShadows && !settings.PathTracing;
	}

	const auto& GetPipelines()
	{
		if (!skinningPipeline)
			skinningPipeline = eastl::make_unique<SkinningPipeline>();

		if (!sharcPipeline)
			sharcPipeline = eastl::make_unique<SHaRCPipeline>();

		static eastl::array<IPipeline*, 2> pipelines = {
			skinningPipeline.get(),
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
		Accumulation,
#ifdef DLSS_RR
		DLSSRR
#endif
	};

#ifdef DLSS_RR
	static constexpr Denoiser DefaultDenoiser = Denoiser::DLSSRR;
#else
	static constexpr Denoiser DefaultDenoiser = Denoiser::SVGF;
#endif

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
		Shadows
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

	enum struct HairBSDF : int32_t
	{
		None,
		ChiangBSDF,
		FarFieldBCSDF
	};

	static constexpr const char* LightEvalModeTooltips[] = {
		"Diffuse only, no specular.",
		"Diffuse and Specular with BRDF."
	};
	STATIC_ASSERT_ENUM_COUNT(LightEvalMode, LightEvalModeTooltips);

	enum struct LightingMode : int32_t
	{
		Diffuse,
		PBR
	};

	static constexpr const char* LightingModeTooltips[] = {
		"Diffuse only, no reflections.",
		"Physically Based Rendering mode with diffuse and reflections."
	};
	STATIC_ASSERT_ENUM_COUNT(LightingMode, LightingModeTooltips);

	enum struct TraceMode : int32_t
	{
		Reference,
		SHaRC
	};

	static constexpr const char* TraceModeTooltips[] = {
		"Reference mode with no cache.",
		"Enables Spatially Hashed Radiance Cache, a technique aimed at improving signal quality and performance."
	};
	STATIC_ASSERT_ENUM_COUNT(TraceMode, TraceModeTooltips);

	enum struct Resolution : int32_t
	{
		Full,
		Half,
		Quarter,
		Eighth
	};

	enum struct CullingMode : int32_t
	{
		None,
		Smart,
		Skyrim
	};

	static constexpr const char* CullingModeTooltips[] = {
		"Disables culling altogether.",
		"Configurable culling made for Ray Tracing.",
		"Relies on Skyrim's culling, will create light leaks from culled nodes behind the player."
	};
	STATIC_ASSERT_ENUM_COUNT(CullingMode, CullingModeTooltips);

	enum struct CullingDistanceMode : int32_t
	{
		Minimal,
		Ratio
	};

	static constexpr const char* CullingDistanceModeTooltips[] = {
		"Culls all geometry outside the view if distance is greater than 'Minimal Distance', regardless of their radius.",
		"When distance is greater than 'Start Distance' modulates 'Minimal Radius' by relative distance and ratio."
	};
	STATIC_ASSERT_ENUM_COUNT(CullingDistanceMode, CullingDistanceModeTooltips);

#ifdef DLSS_RR
	struct DLSSRRSettings
	{
		DLSSRRQuality QualityMode = DLSSRRQuality::MaxQuality;
		DLSSRRPreset Preset = DLSSRRPreset::E;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DLSSRRSettings, QualityMode, Preset)
	};
#endif

	struct CullingSettings
	{
		CullingMode Mode = CullingMode::Smart;
		int MinRadius = 1;

		CullingDistanceMode DistanceMode = CullingDistanceMode::Ratio;

		int MinDistance = 100;

		int StartDistance = 10;
		float DistanceRatio = 1.0f;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CullingSettings, Mode, MinRadius, DistanceMode, MinDistance, StartDistance, DistanceRatio)
	};

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
		CullingSettings Culling;

		bool VariableUpdateRate = true;

		RISSettings RIS;
		ReSTIRSettings ReSTIR;

		bool GGXEnergyConservation = true;
		HairBSDF HairBSDF = HairBSDF::ChiangBSDF;

		DiffuseBRDF DiffuseBRDF = DiffuseBRDF::Burley;
		LightEvalMode LightEvalMode = LightEvalMode::BRDF;
		LightingMode LightingMode = LightingMode::PBR;

		bool EnableSubsurfaceScattering = true;
		bool EnableSssTransmission = true;
		bool SSSMaterialOverride = false;
		int SSSSampleCount = 1;
		float SSSMaxSampleRadius = 1.0f;

		float3 OverrideSSSTransmissionColor = float3(1.0f, 0.735f, 0.612f);
		float3 OverrideSSSScatteringColor = float3(1.0f, 1.0f, 1.0f);
		float OverrideSSSScale = 40.0f;
		float OverrideSSSAnisotropy = -0.5f;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AdvancedSettings, Culling, VariableUpdateRate, RIS, ReSTIR, GGXEnergyConservation, HairBSDF, DiffuseBRDF, LightEvalMode, LightingMode, EnableSubsurfaceScattering, EnableSssTransmission, SSSMaterialOverride, SSSSampleCount, SSSMaxSampleRadius, OverrideSSSTransmissionColor, OverrideSSSScatteringColor, OverrideSSSScale, OverrideSSSAnisotropy)
	};

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool Enabled = true;
		bool GlobalIllumination = true;
		AdvancedSettings AdvancedSettings;
		TraceMode TraceMode = TraceMode::SHaRC;
		Denoiser Denoiser = DefaultDenoiser;
		Resolution Resolution = Resolution::Full;
		int Bounces = 2;
		int SamplesPerPixel = 1;
		float2 Roughness = { 0.0f, 1.0f };
		float2 Metalness = { 0.0f, 1.0f };
		float Emissive = 1.0f;
		float Effect = 1.0f;
		float Sky = 1.0f;
		float Directional = 1.0f;
		float Point = 1.0f;
		float TexLODBias = -1.0f;
		bool LodDimmer = true;
		bool RaytracedShadows = true;
		bool PathTracing = false;
		bool CullShadows = true;
		bool RussianRoulette = true;
		bool ConvertToGamma = true;
#ifdef DLSS_RR
		DLSSRRSettings DLSSRR;
#endif
		SVGFPipeline::Settings SVGFDiffuse;
		SVGFPipeline::Settings SVGFSpecular;
		bool PerformanceOverlay = false;
		DebugOutput DebugOutput = DebugOutput::None;
		bool EnablePIXCapture = false;
		PIXCaptureLocation PIXCaptureLocation = PIXCaptureLocation::GlobalIllumination;
		bool EnableDebugDevice = false;
		bool WhiteFurnace = false;
		bool DisableSkinned = false;
		bool InteriorSun = false;
		SHaRCPipeline::Settings SHaRC;
	} settings;

	// Debug variables
	std::string debugDefines = "";
	bool debugDisableTriShapesUpdate = false;
	bool debugDisableTextureSharing = false;
	uint debugNormalMap = 0;

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
		winrt::com_ptr<ID3D12Resource> resource;
		eastl::shared_ptr<Allocation> allocation;

		TextureReference(winrt::com_ptr<ID3D12Resource>&& res, eastl::shared_ptr<Allocation>&& alloc) :
			resource(eastl::move(res)), allocation(eastl::move(alloc)) {}
	};

	// Creates mesh buffers for all graph TriShapes, handles materials and builds a single BLAS for the node
	void CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root);
	void CreateModelInternal(RE::TESForm* refr, const char* path, RE::NiAVObject* root);

	// Removes the instance and optionally also releases the model and all its buffers if refCount reaches 0
	bool RemoveInstance(RE::NiAVObject* root, bool releaseModel);
	bool RemoveInstance(RE::FormID formID, bool releaseModel);

	void SetInstanceDetached(RE::NiAVObject* root, bool detached);
	void SetInstanceDetached(RE::FormID formID, bool detached);

	eastl::shared_ptr<Allocation> GetTextureRegister(ID3D11Texture2D* texture, eastl::shared_ptr<Allocation> defaultTexture);
	eastl::shared_ptr<Allocation> GetMSNormalMapRegister(Shape* shape, RE::BSGraphics::Texture* texture, eastl::shared_ptr<Allocation> defaultTexture);

	Allocator shapeRegisters = Allocator(RTConstants::MAX_SHAPES);
	Allocator textureRegisters = Allocator(RTConstants::MAX_TEXTURES);

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
	eastl::shared_ptr<DefaultTexture> defaultGrayTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultNormalTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultBlackTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultRMAOSTexture = nullptr;
	eastl::shared_ptr<DefaultTexture> defaultDetailTexture = nullptr;

	// TODO: Add cleanup for elements of this vector
	eastl::unordered_map<RE::BSDismemberSkinInstance*, eastl::vector<Shape*>> dismemberReferences;

	// We'll group trishapes by their parent nodes, hopefully trishapes don't move on their own
	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model>> models;

	winrt::com_ptr<D3D12MA::Allocator> allocator = nullptr;

	winrt::com_ptr<D3D12MA::Pool> uploadPool = nullptr;

	winrt::com_ptr<D3D12MA::Pool> dynamicVertexPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> vertexPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> vertexCopyPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> skinningPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> trianglePool = nullptr;

	winrt::com_ptr<D3D12MA::Pool> blasScratchPool = nullptr;
	winrt::com_ptr<D3D12MA::Pool> blasPool = nullptr;

	eastl::unordered_map<RE::NiAVObject*, Instance> instances;
	eastl::unordered_map<RE::FormID, eastl::vector<RE::NiAVObject*>> formIDNodes;

	// Transform buffer for BLAS build/rebuild
	eastl::unique_ptr<DX12::StructuredBufferUpload<float3x4>> transformBuffer = nullptr;

	// Indirection, Transform and Material buffers
	ShapeData* shapeData = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUpload<ShapeData>> shapeBuffer = nullptr;

	eastl::array<InstanceData, RTConstants::MAX_INSTANCES> instanceData;
	eastl::unique_ptr<DX12::StructuredBufferUpload<InstanceData>> instanceBuffer = nullptr;

	Util::FrameChecker frameChecker;
	uint64_t frameIndex;

	Util::FrameChecker shadowFrameChecker;

	// Textures that have been shared with DX12 and placed in a heap as SRV
	eastl::unordered_map<ID3D11Texture2D*, eastl::unique_ptr<TextureReference>> textures;

	struct ConvertedNormalMap
	{
		eastl::unique_ptr<TextureReference> Reference;
		eastl::unique_ptr<Texture2D> Texture;
		ID3D11ShaderResourceView* OriginalSRV = nullptr;
		bool converted = false;
	};

	eastl::deque<eastl::string> msnConvertionQueue;

	eastl::unordered_map<ID3D11Texture2D*, eastl::unique_ptr<ConvertedNormalMap>> normalMaps;

	eastl::unordered_map<uint16_t, ID3D11Texture2D*> allocationMSNormalMaps;

	winrt::com_ptr<ID3D11SamplerState> samplerState = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> copyDepthCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> convertTexturesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> convertTexturesPTCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> compositeCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> accumulationCS = nullptr;

	struct alignas(16) AccumulationCBData
	{
		uint AccumulatedFrames;
		float3 _padding;
	};
	eastl::unique_ptr<AccumulationCBData> accumulationCBData = nullptr;
	eastl::unique_ptr<ConstantBuffer> accumulationCB = nullptr;

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

	// Skinning (and dynamic TriShapes)
	eastl::unique_ptr<SkinningPipeline> skinningPipeline = nullptr;

	// SHaRC (Radiance cache)
	eastl::unique_ptr<SHaRCPipeline> sharcPipeline = nullptr;

	// SVGF (denoiser)
	eastl::unique_ptr<SVGFPipeline> svgfDenoiser = nullptr;

	// TODO: Move other effects to their own pipelines as well
	//	eastl::unique_ptr<RTPipeline> RTPipeline = nullptr;
	//	eastl::unique_ptr<ShadowPipeline> shadowPipeline = nullptr;

	// GI
	winrt::com_ptr<ID3D12RootSignature> rootSignature = nullptr;
	winrt::com_ptr<ID3D12StateObject> pipelineRT = nullptr;
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

	eastl::unique_ptr<ModelSpaceToTangent> normalMapConverter;

	eastl::unique_ptr<WrappedResource> skyHemisphere = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> cubeToHemiCS = nullptr;

	// Shadow maps
	bool renderingShadowmap = false;
	eastl::unique_ptr<WrappedResource> shadowMaskTexture = nullptr;

	// Resources
	eastl::unique_ptr<DX12::Texture2D> outputTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> diffuseAlbedoPathTracingTexture = nullptr;
	eastl::unique_ptr<DX12::Texture2D> normalRoughnessPathTracingTexture = nullptr;
	eastl::unique_ptr<WrappedResource> specularAlbedoTexture = nullptr;
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

	// Accumulation buffer for path tracing denoiser
	eastl::unique_ptr<WrappedResource> accumulationTexture = nullptr;
	eastl::unique_ptr<WrappedResource> accumulationTextureCopy = nullptr;

	std::shared_mutex modelMutex;
	std::shared_mutex landDetachMutex;
	std::shared_mutex bufferMutex;
	std::shared_mutex renderMutex;

	std::shared_mutex textureRegisterMutex;
	std::recursive_mutex shareTextureMutex;

	uint2 renderSize;
	float2 dynamicResolutionRatio;

	// Timings
	double captureInterval = 0.1;
	double lastTime = 0;
	bool canMeasure = false;

	// Accumulation denoiser state
	int accumulatedFrames = 0;
	bool cameraHasMoved = true;

	RE::NiPointer<RE::TESWaterReflections> waterReflections = nullptr;

	void UpdateMeasureTime(double currentTime)
	{
		double delta = currentTime - lastTime;

		if (delta > captureInterval) {
			lastTime = currentTime;
			canMeasure = true;
		} else
			canMeasure = false;
	}

	float mainCPUTime;
	float mainGPUTime;

	float shadowsCPUTime;
	float shadowsGPUTime;

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

	Util::FrameChecker dlssFrameChecker;
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
				if (!pDesc)
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
					if (auto resource = oThis->rendererTexture->texture) {
						auto& rt = globals::features::raytracing;

						ID3D11Texture2D* texture = nullptr;

						resource->QueryInterface(IID_PPV_ARGS(&texture));

						if (auto it = rt.textures.find(texture); it != rt.textures.end()) {
							auto index = it->second->allocation->GetIndex();

							logger::debug("[RT] NiSourceTexture::Destructor [0x{:8X}] - Register: {}", reinterpret_cast<uintptr_t>(texture), index);

							// I imagine this isn't fast but I'll keep this in until I'm sure everything has been fixed
							for (auto& [key, model] : rt.models) {
								for (auto& shape : model->shapes) {
									auto& material = shape->material;

									for (auto& materialTexture : material.Textures) {
										if (index == materialTexture->GetIndex())
											logger::critical("[RT]\t\t NiSourceTexture::Destructor - Found in: {}", key);
									}
								}
							}

							rt.textures.erase(it);
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
				}

				func(This, Pass, RenderFlags);
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

				if (rt.Active() && rt.RaytracedShadows()) {
					rt.UpdateShadowsFrameBuffer();

					auto& runtimeData = light->GetShadowDirectionalLightRuntimeData();
					for (size_t i = 0; i < 3; i++) {
						runtimeData.startSplitDistances[i] = 0;
						runtimeData.endSplitDistances[i] = 0;
					}
				}

				// This is effectively bypassed (removing the call freezes the game...)
				func(light, a2);

				rt.renderingShadowmap = false;

				if (rt.Active() && rt.RaytracedShadows()) {
					rt.shadowLight = light;
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderShadowmasks
		{
			static void thunk(bool a1)
			{
				auto& rt = globals::features::raytracing;

				if (rt.Active() && rt.RaytracedShadows())
					rt.RenderShadows();
				else
					func(a1);
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <typename T>
		struct Release3DRelatedData
		{
			static void thunk(T* oThis)
			{
				globals::features::raytracing.RemoveInstance(oThis->GetFormID(), true);

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

				rt.shareTexture = !rt.debugDisableTextureSharing;

				auto* result = func(a1, path, srv, a4, a5);

				rt.shareTexture = false;

				return result;
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TESObjectLAND_Attach3D
		{
			static void thunk(RE::TESObjectLAND* oThis, bool a2)
			{
				func(oThis, a2);

				logger::trace("[RT] TESObjectLAND_Attach3D - a2: {}, IsLODLand: {}", a2, oThis->QIsLODLandObject());

				if (!oThis)
					return;

				auto* cell = oThis->parentCell;

				if (!cell->IsExteriorCell())
					return;

				auto& runtimeData = cell->GetRuntimeData();

				auto* exteriorData = runtimeData.cellData.exterior;

				auto* loadedData = oThis->loadedData;

				if (!loadedData || !loadedData->mesh)
					return;

				logger::trace("[RT] TESObjectLAND_Attach3D - {}", std::format("Landscape_{}_{}", exteriorData->cellX, exteriorData->cellY).c_str());

				for (uint i = 0; i < 4; i++) {
					auto mesh = loadedData->mesh[i];

					if (!mesh)
						continue;

					globals::features::raytracing.CreateModelInternal(oThis, std::format("Landscape_{}_{}_Quad_{}", exteriorData->cellX, exteriorData->cellY, i).c_str(), mesh);
				}
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TESObjectLAND_Detach3D
		{
			static void thunk(RE::TESObjectLAND* oThis)
			{
				auto& rt = globals::features::raytracing;

				std::lock_guard lock{ rt.landDetachMutex };

				rt.RemoveInstance(oThis->GetFormID(), true);

				auto* cell = oThis->parentCell;

				if (cell->IsExteriorCell()) {
					auto& runtimeData = cell->GetRuntimeData();

					auto* exteriorData = runtimeData.cellData.exterior;

					logger::info("[RT] TESObjectLAND::Detach3D - {}", std::format("Landscape_{}_{}", exteriorData->cellX, exteriorData->cellY).c_str());
				}

				func(oThis);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TESObjectLAND_Destructor
		{
			static void thunk(RE::TESObjectLAND* oThis)
			{
				auto* cell = oThis->parentCell;

				if (cell->IsExteriorCell()) {
					auto& runtimeData = cell->GetRuntimeData();

					auto* exteriorData = runtimeData.cellData.exterior;

					logger::info("[RT] TESObjectLAND::Destructor - {}", std::format("Landscape_{}_{}", exteriorData->cellX, exteriorData->cellY).c_str());
				}

				func(oThis);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		
		struct AttachDistant3DTask_Attach
		{
			static void thunk(void* a1, float a2)
			{
				func(a1, a2);

				auto* refr = *reinterpret_cast<RE::TESObjectREFR**>(reinterpret_cast<uintptr_t>(a1) + 24);

				logger::info("[RT] AttachDistant3DTask::Attach {}", magic_enum::enum_name(refr->GetFormType()));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		
		struct TES_AttachModel
		{
			static void thunk(RE::TES* a1, RE::TESObjectREFR* refr, RE::TESObjectCELL *cell, void* queuedTree, char a5, RE::NiNode* a6)
			{
				auto* baseObject = refr->GetBaseObject();

				logger::debug("\tTES::AttachModel {} - {:08X}, {} - {:08X}",
					magic_enum::enum_name(refr->formType.get()), refr->GetFormID(),
					magic_enum::enum_name(baseObject->formType.get()), baseObject->GetFormID());

				func(a1, refr, cell, queuedTree, a5, a6);

				if (auto& rt = globals::features::raytracing; rt.Active()) {
					auto flags = baseObject->GetFormFlags();
					RE::FormType type = baseObject->GetFormType();

					if (type == RE::FormType::IdleMarker) {
						return;
					}

					if (flags & MarkerFlags::IsMarker) {
						if (type == RE::FormType::Static)
							return;

						if (type == RE::FormType::Door)
							return;

						if (type == RE::FormType::Action)
							return;

						if (type == RE::FormType::Furniture)
							return;
					}

					auto* pNiAVObject = refr->Get3D();

					if (!pNiAVObject) {
						logger::warn("\tTES::AttachModel - No 3D");
						return;
					}
	
					std::lock_guard lock{ rt.modelMutex };

					if (auto* model = baseObject->As<RE::TESModel>()) {
						rt.CreateModel(refr, model->GetModel(), pNiAVObject);
					} else {
						if (IsPlayer(refr)) {
							if (auto* player = reinterpret_cast<RE::PlayerCharacter*>(refr)) {
								const char* name = player->GetName();

								// First Person
								//rt.CreateModelInternal(refr, std::format("{}_1stPerson", name).c_str(), pNiAVObject);

								// Third Person
								rt.CreateModelInternal(refr, name, player->Get3D(false));

								return;
							}
						}
						
						if (auto* actor = refr->As<RE::Actor>()) {
							rt.CreateModelInternal(refr, actor->GetName(), pNiAVObject);
							return;
						}

						logger::warn("\tTES::AttachModel - No TESModel - {}, {:08X}", magic_enum::enum_name(refr->formType.get()), refr->GetFormID());
					}
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct CreateRenderTarget_PlayerFaceGenTint
		{
			static void thunk(RE::BSGraphics::Renderer* oThis, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
			{			
				auto& rt = globals::features::raytracing;

				std::lock_guard<std::recursive_mutex> lock(rt.shareTextureMutex);

				rt.shareTexture = !rt.debugDisableTextureSharing;

				func(oThis, a_target, a_properties);

				rt.shareTexture = false;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWaterEffects
		{
			static void thunk()
			{
				auto* tes = RE::TES::GetSingleton();
				if (tes->interiorCell) {
					if (tes->interiorCell->cellFlags.none(RE::TESObjectCELL::Flag::kHasWater))
						tes->interiorCell->cellFlags.set(true, RE::TESObjectCELL::Flag::kHasWater);

					globals::features::raytracing.waterReflections->flags.set(true, RE::TESWaterReflections::Flags::kDirty);
				}

				func();
			};
			static inline REL::Relocation<decltype(thunk)> func;
		};
		
		template <typename T>
		struct Set3D
		{
			static void thunk(T* oThis, RE::NiAVObject* a_object, bool a_queue3DTasks = true)
			{
				if (!a_object)
					globals::features::raytracing.RemoveInstance(oThis->GetFormID(), true);

				func(oThis, a_object, a_queue3DTasks);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSDismemberSkinInstance_UpdateDismemberPartion
		{
			static void thunk(RE::BSDismemberSkinInstance* oThis, std::uint16_t a_slot, bool a_enable)
			{
				func(oThis, a_slot, a_enable);

				auto& dismemberReferences = globals::features::raytracing.dismemberReferences;

				if (auto it = dismemberReferences.find(oThis); it != dismemberReferences.end()) {
					for (auto& shape : it->second) {
						if (a_slot == shape->slot) {
							logger::info("[RT] BSDismemberSkinInstance::UpdateDismemberPartion {} {} - 0x{:08X} 0x{:08X}", a_slot, a_enable, reinterpret_cast<uintptr_t>(oThis), reinterpret_cast<uintptr_t>(shape));
							shape->UpdateDismember(a_enable);
							break;
						}
					}
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		
		static void Install()
		{
			// Creates model and instances for all forms
			stl::detour_thunk<TES_AttachModel>(REL::RelocationID(13209, 13355));

			// Releases 3D resources (instances and models)
			{
				stl::write_vfunc<0x6B, Release3DRelatedData<RE::TESObjectREFR>>(RE::VTABLE_TESObjectREFR[0]);
				stl::detour_thunk<Set3D<RE::Actor>>(REL::RelocationID(36199, 37178));
			}
			
			// Makes Player FaceGenTint RenderTarget shareable
			stl::write_thunk_call<CreateRenderTarget_PlayerFaceGenTint>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x606, 0x605, 0x0));

			// Updates Shape dismember state
			stl::detour_thunk<BSDismemberSkinInstance_UpdateDismemberPartion>(REL::RelocationID(15576, 15753));

			//stl::detour_thunk<TESObjectREFR_Enable>(REL::RelocationID(19373, 19800));
			//stl::write_vfunc<0x89, TESObjectREFR_Disable>(RE::VTABLE_TESObjectREFR[0]);

			// NiSourceTexture Destructor
			stl::write_vfunc<0x0, NiSourceTexture_Destructor>(RE::VTABLE_NiSourceTexture[0]);

			// Destructors to remove instances (not models)
			{
				stl::write_vfunc<0x0, Destructor<RE::NiNode>>(RE::VTABLE_NiNode[0]);
				stl::write_vfunc<0x0, Destructor<RE::BSFadeNode>>(RE::VTABLE_BSFadeNode[0]);
				stl::write_vfunc<0x0, Destructor<RE::BSFadeNode>>(RE::VTABLE_BSLeafAnimNode[0]);
			}

			stl::detour_thunk<Main_RenderWaterEffects>(REL::RelocationID(35561, 36560));

			stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));
			
			// We use these to render only the sky to the cubemaps, maybe it would be cleaner if we could override cubemap renderpass?
			stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Lighting>>(RE::VTABLE_BSLightingShader[0]);

			if (REL::Module::IsAE()) {
				stl::write_vfunc<0x35, BSTriShape_OnVisible>(RE::VTABLE_BSTriShape[0]);
			} else {
				stl::write_vfunc<0x34, BSTriShape_OnVisible>(RE::VTABLE_BSTriShape[0]);
			}

			stl::detour_thunk<Main_RenderShadowmasks>(REL::RelocationID(100422, 107140));

			stl::write_vfunc<0xA, BSShadowDirectionalLight_RenderShadowmaps>(RE::VTABLE_BSShadowDirectionalLight[0]);

			stl::detour_thunk<CreateTextureFromDDS>(REL::RelocationID(69334, 70716));

			stl::detour_thunk<TESObjectLAND_Attach3D>(REL::RelocationID(18334, 18750));

			stl::detour_thunk<TESObjectLAND_Detach3D>(REL::RelocationID(18333, 18749)); // sub_1402A8A80
			//stl::detour_thunk<TESObjectLAND_Detach3D2>(REL::RelocationID(18334, 18750));  // sub_1402A8B00

			//stl::write_vfunc<0x6, AttachDistant3DTask_Attach>(RE::VTABLE_AttachDistant3DTask[0]);
			
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

	class CellAttachDetachEventHandler : public RE::BSTEventSink<RE::CellAttachDetachEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::CellAttachDetachEvent* a_event, RE::BSTEventSource<RE::CellAttachDetachEvent>*);

		static bool Register()
		{
			static CellAttachDetachEventHandler singleton;

			auto* tes = RE::TES::GetSingleton();
			tes->AddEventSink<RE::CellAttachDetachEvent>(&singleton);

			logger::info("Registered {}", typeid(singleton).name());

			return true;
		}
	};

	class BGSActorCellEventHandler : public RE::BSTEventSink<RE::BGSActorCellEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*);

		static bool Register()
		{
			static BGSActorCellEventHandler singleton;

			auto* player = RE::PlayerCharacter::GetSingleton();
			player->AsBGSActorCellEventSource()->AddEventSink(&singleton);

			logger::info("Registered {}", typeid(singleton).name());

			return true;
		}
	};
};