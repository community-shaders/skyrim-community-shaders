#pragma once

#include <d3d11_1.h>

#include "Buffer.h"
#include "GrassOptimizations/GrassBucketStore.h"
#include "GrassOptimizations/HiZPyramid.h"

/** @brief Rewrites vanilla grass rendering with a bucket system using indirect draws and compute culling of individual instances. */
struct GrassOptimizations : Feature
{
public:
	virtual inline std::string GetName() override { return "Grass Optimizations"; }
	virtual std::string GetDisplayName() override { return T("feature.grass_optimizations.name", "Grass Optimizations"); }
	virtual inline std::string GetShortName() override { return "GrassOptimizations"; }
	virtual inline std::string_view GetShaderDefineName() override { return "GRASS_OPTIMIZATIONS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kGrass; }

	/** @brief Returns true only for the Grass shader type. */
	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.grass_optimizations.description", "Rewrites grass rendering around GPU-driven culling and instancing, consolidating thousands of engine draw calls into a handful of indirect draws and removing hidden grass before it costs anything."),
			{ T("feature.grass_optimizations.key_feature_1", "Consolidates per-shape grass draws into one instanced indirect draw per grass type"),
				T("feature.grass_optimizations.key_feature_2", "GPU compute culling of individual instances by frustum, distance and projected size"),
				T("feature.grass_optimizations.key_feature_3", "Hi-Z occlusion culling skips grass hidden behind objects before the vertex shader runs"),
				T("feature.grass_optimizations.key_feature_4", "Configurable render distance beyond the vanilla INI cap with density scaling"),
				T("feature.grass_optimizations.key_feature_5", "Optional mesh-swap LOD and simplified shading for distant grass") } };
	};

	struct Settings
	{
		float MinPixelSize = 2.0f;
		float FullDetailPixelSize = 12.0f;
		float MinDensity = 0.07f;
		float MeshCostBias = 0.25f;
		float CostBiasStartDistance = 6000.0f;
		float InvisibleFadeCull = 0.0f;
		float RenderDistanceOverride = 0.0f;
		float EdgeFadeStart = 0.85f;
		bool EnableOcclusionCulling = true;
		float SimpleShadingPixelSize = 8.0f;
		float OcclusionBias = 0.001f;
		float CollisionDistance = 2048.0f;
		bool EnableMeshLOD = false;
		bool EnableMidLOD = true;
		float MidLODPixelSize = 8.0f;
		bool EnableFarLOD = true;
		float FarLODPixelSize = 4.0f;
		float MeshLODBandPixels = 3.0f;
	};

	Settings settings;

	/** @brief Draws the ImGui settings panel for grass optimizations configuration. */
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Creates the constant buffers and queries the immediate context for D3D11.1 support. */
	virtual void SetupResources() override;

	/** @brief Releases cached compute shaders so they recompile on next use. */
	virtual void ClearShaderCache() override;
	/** @brief Processes lifecycle events and completed LOD loads at Present. */
	virtual void Reset() override;

	/** @brief Installs the grass capture, culling and draw hooks after all plugins have loaded. */
	virtual void PostPostLoad() override;

	/** @brief Identifies who sets up a grass pass: the engine, an optimized bucket, or nobody because optimized buckets cover it. */
	enum class GrassPassSetup
	{
		kNative,
		kOptimized,
		kCovered
	};

	/** @brief Applies owned proxy setup or native coverage setup and returns who set up the pass. */
	GrassPassSetup SetupGrassPass(RE::BSShader* shader, RE::BSRenderPass* pass);

private:
	/** @brief Returns the instance culling compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetCullCS();

	struct alignas(16) CullParamsCB
	{
		float frustumPlanes[6][4];

		// Preserve the main camera's camera-relative view-projection rows from PerFrame.
		float cameraViewProj[4][4];
		float cameraPos[4];

		float minPixelSize;
		float fullDetailPixelSize;
		float lodMinKeep;
		float lodFadeBand;

		float meshCostBias;
		float projScale;
		float maxDistSq;
		float edgeFadeStart;

		float alphaParam1;
		float alphaParam2;
		float fadeNow;
		float fadeInTimeRcp;

		float invisibleFadeCull;
		float simpleShadingPixelSize;
		float collisionDistSq;
		float midLODPixelSize;

		float meshLODBandPx;
		float hiZEnabled;
		float hiZSizeX;
		float hiZSizeY;

		float projectionWLength;
		float hiZMipCount;
		float occlusionBias;
		float costBiasStartDist;

		float farLODPixelSize;
		float densityDistanceRcp;
		float pad0[2];
	};
	STATIC_ASSERT_ALIGNAS_16(CullParamsCB);

	struct alignas(16) CullBucketCB
	{
		uint32_t groupCount;
		uint32_t sliceTableOffset;
		uint32_t sliceCount;
		uint32_t dispatchWidth;

		uint32_t tierStride;
		uint32_t outputOffset;
		uint32_t argsOffset;
		uint32_t pad0;

		float boundExtent;
		float modelRadius;
		float distScale;
		float minPixelScale;

		float midLODEnabled;
		float farLODEnabled;
		float complexThreshold;
		float midLODMeshCost;

		float wavePeriod;
		float timeBase;
		float prevTimeBase;
		float farLODMeshCost;

		std::array<float, kTierCount> canopyHeights;
		float pad1;
	};
	STATIC_ASSERT_ALIGNAS_16(CullBucketCB);
	// RunGrass.hlsl reads the draw marker, texture threshold and shared-page offset through this slot.
	static_assert(sizeof(CullBucketCB) == 96 && offsetof(CullBucketCB, groupCount) == 0 && offsetof(CullBucketCB, complexThreshold) == 56 &&
				  offsetof(CullBucketCB, outputOffset) == 20 && offsetof(CullBucketCB, argsOffset) == 24 &&
				  offsetof(CullBucketCB, midLODMeshCost) == 60 && offsetof(CullBucketCB, farLODMeshCost) == 76 &&
				  offsetof(CullBucketCB, canopyHeights) == 80);

	// Match the c16-c21 fields read by GRASS_OPTIMIZATIONS in RunGrass.hlsl.
	struct alignas(16) GrassPerGeometry
	{
		float fogNearColor[4];
		float windVector[4];
		float dirLightDirection[4];
		float dirLightColor[4];
		float ambientColor[4];
		float scaleMask[4];
	};
	STATIC_ASSERT_ALIGNAS_16(GrassPerGeometry);

	// The b2 window begins at material, placing geometry at the shader's c16.
	struct CullBucketSlot
	{
		CullBucketCB cull;
		std::array<std::byte, 256 - sizeof(CullBucketCB)> cullPadding;
		TruePBR::GrassPBRConstants material;
		std::array<std::byte, 256 - sizeof(TruePBR::GrassPBRConstants)> materialPadding;
		GrassPerGeometry geometry;
		std::array<std::byte, 256 - sizeof(GrassPerGeometry)> geometryPadding;
	};

	/** @brief Stores six frustum planes in two padded SSE vectors. */
	struct FrustumSoA
	{
		__m128 nx[2], ny[2], nz[2], d[2];
		struct Plane
		{
			__m128 nx, ny, nz, d;
		};
		Plane planes[6];  // Broadcast each coefficient to test four runs at once.
	};

	/** @brief Four axis-aligned boxes in structure-of-arrays form, one per SSE lane. */
	struct BoxBatch
	{
		__m128 lx, ly, lz, hx, hy, hz;

		explicit BoxBatch(const GrassBucket::SliceRunBatch& batch) :
			lx(_mm_load_ps(batch.loX)), ly(_mm_load_ps(batch.loY)), lz(_mm_load_ps(batch.loZ)),
			hx(_mm_load_ps(batch.hiX)), hy(_mm_load_ps(batch.hiY)), hz(_mm_load_ps(batch.hiZ))
		{}

		/** @brief Squared distance from the camera to each box's nearest point. */
		__m128 NearestDistanceSq(__m128 camX, __m128 camY, __m128 camZ) const
		{
			const __m128 zero = _mm_setzero_ps();
			const __m128 dx = _mm_max_ps(_mm_max_ps(_mm_sub_ps(lx, camX), _mm_sub_ps(camX, hx)), zero);
			const __m128 dy = _mm_max_ps(_mm_max_ps(_mm_sub_ps(ly, camY), _mm_sub_ps(camY, hy)), zero);
			const __m128 dz = _mm_max_ps(_mm_max_ps(_mm_sub_ps(lz, camZ), _mm_sub_ps(camZ, hz)), zero);
			return _mm_add_ps(_mm_add_ps(_mm_mul_ps(dx, dx), _mm_mul_ps(dy, dy)), _mm_mul_ps(dz, dz));
		}

		/** @brief Squared distance from the camera to each box's farthest corner. */
		__m128 FarthestDistanceSq(__m128 camX, __m128 camY, __m128 camZ) const
		{
			const __m128 sign = _mm_set1_ps(-0.0f);
			const __m128 fx = _mm_max_ps(_mm_andnot_ps(sign, _mm_sub_ps(lx, camX)), _mm_andnot_ps(sign, _mm_sub_ps(hx, camX)));
			const __m128 fy = _mm_max_ps(_mm_andnot_ps(sign, _mm_sub_ps(ly, camY)), _mm_andnot_ps(sign, _mm_sub_ps(hy, camY)));
			const __m128 fz = _mm_max_ps(_mm_andnot_ps(sign, _mm_sub_ps(lz, camZ)), _mm_andnot_ps(sign, _mm_sub_ps(hz, camZ)));
			return _mm_add_ps(_mm_add_ps(_mm_mul_ps(fx, fx), _mm_mul_ps(fy, fy)), _mm_mul_ps(fz, fz));
		}

		/** @brief Returns the maximum plane distance for each box; negative values reject the box. */
		__m128 MaxPlaneDistance(const GrassOptimizations::FrustumSoA::Plane& plane) const
		{
			// blendv keys off the normal's sign bit.
			return PlaneDistance(plane, _mm_blendv_ps(hx, lx, plane.nx), _mm_blendv_ps(hy, ly, plane.ny), _mm_blendv_ps(hz, lz, plane.nz));
		}

		/** @brief Returns the minimum plane distance for each box; nonnegative values contain the box. */
		__m128 MinPlaneDistance(const GrassOptimizations::FrustumSoA::Plane& plane) const
		{
			return PlaneDistance(plane, _mm_blendv_ps(lx, hx, plane.nx), _mm_blendv_ps(ly, hy, plane.ny), _mm_blendv_ps(lz, hz, plane.nz));
		}

	private:
		static __m128 PlaneDistance(const GrassOptimizations::FrustumSoA::Plane& plane, __m128 x, __m128 y, __m128 z)
		{
			const __m128 dot = _mm_add_ps(_mm_add_ps(_mm_mul_ps(plane.nx, x), _mm_mul_ps(plane.ny, y)), _mm_mul_ps(plane.nz, z));
			return _mm_sub_ps(dot, plane.d);
		}
	};

	/** @brief Returns the occupied SSE lanes for up to four remaining boxes. */
	static uint32_t LaneMask(uint32_t remaining);
	/** @brief Splits a dispatch into rows within the per-dimension group limit. */
	static uint32_t DispatchWidth(uint32_t groupCount);

	/** @brief Transposes the frustum once per frame. Inactive planes become always-pass slots. */
	static void BuildFrustumSoA(FrustumSoA& out, const RE::NiFrustumPlanes& f);

	/** @brief Test a coarse AABB and return the planes its children still need to test. */
	static bool ClassifyAabb(const FrustumSoA& f, __m128 lo, __m128 hi, uint32_t& intersectingPlanes);

	/** @brief Uploads admitted data and culls grass once per frame using the validated main camera. */
	void UpdateGrass(const RE::NiCamera* cam);

	/** @brief Builds visibility and dispatches culling for the validated main camera. */
	void CullGrass(const RE::NiCamera* cam);

	/** @brief Returns the exterior directional light used by optimized grass constants. */
	static RE::NiDirectionalLight* GetSunLight();

	/** @brief Returns whether this frame has evaluated optimized coverage with the main camera. */
	bool HasCurrentCull() const;

	/** @brief Appends this bucket's visible slice runs to sliceTableCPU and records the window in the bucket. Expects GrassBucket::ResetCullState() first. */
	void CullBucketSlices(GrassBucket& b, const FrustumSoA& frustumSoA, __m128 camPosV);

	/** @brief Cull cached blocks of cell runs and test partially visible blocks with SSE. */
	void CullBucketSliceBlocks(GrassBucket& b, const FrustumSoA& frustumSoA, __m128 camPosV, uint32_t planeMask, bool allWithinDistance);

	/** @brief Test cell runs [firstRun, endRun) four at a time against the distance and the planes in planeMask. */
	void CullSliceRuns(GrassBucket& b, const FrustumSoA& frustumSoA, __m128 camPosV, uint32_t firstRun, uint32_t endRun, uint32_t planeMask, bool withinDistance);

	/** @brief Append visible groups to the bucket's slice table window, merging them into the last entry when contiguous. */
	void AppendVisibleGroups(GrassBucket& b, uint32_t firstGroup, uint32_t groupCount);

	/** @brief Drops stale or failed cull results so native coverage remains available. */
	void DisableOptimizedDraws();

	/** @brief Returns whether PerFrame contains the main camera rather than a reflection, cubemap or shadow camera. */
	static bool IsMainCameraPerFrame(const RE::NiPoint3& mainCameraPos);

	/** @brief Uploads the per-bucket cull constants and the slice table, then issues the cull dispatches. */
	void UploadCullState(ID3D11DeviceContext* ctx);

	/** @brief Fills every visible bucket's slot of the per-bucket constant buffer with one map. */
	bool UploadCullBucketConstants(ID3D11DeviceContext* ctx);

	/** @brief Uploads this frame's visible slice ranges, growing the table when needed. */
	bool UploadSliceTable(ID3D11DeviceContext* ctx);

	/** @brief Binds a bucket's resources and dispatches the instance culling compute shader. */
	void CullBucket(GrassBucket& b, ID3D11DeviceContext* ctx);

	/** @brief Grows the slotted per-bucket constant buffer to hold at least `slots` entries. */
	bool EnsureCullBucketCapacity(uint32_t slots);

	/** @brief Binds all geometry and material resources for a ready optimized bucket. */
	void SetupOptimizedPass(const GrassBucket& b);

	/** @brief Issue a bucket's indirect draw for each active tier. */
	void DrawBucket(GrassBucket& b);

	GrassBucketStore bucketStore;
	HiZPyramid hiZ;

	uint32_t lastFrame = UINT32_MAX;

	winrt::com_ptr<ID3D11DeviceContext1> ctx1;

	winrt::com_ptr<ID3D11ComputeShader> cullCS;
	// Cache invalidation permits another compile attempt after a failure.
	bool cullCSFailed = false;

	std::unique_ptr<ConstantBuffer> cullParamsCB;

	std::unique_ptr<ConstantBuffer> cullBucketCB;
	uint32_t cullBucketCBSlots = 0;
	inline static constexpr uint32_t kSlotBytes = sizeof(CullBucketSlot);
	inline static constexpr uint32_t kSlotMaterialOffset = offsetof(CullBucketSlot, material);
	static_assert(sizeof(CullBucketSlot) == 768 && offsetof(CullBucketSlot, cull) == 0 &&
				  kSlotMaterialOffset == 256 && offsetof(CullBucketSlot, geometry) == 512);

	std::unique_ptr<ConstantBuffer> vanillaDrawCB;

	// Shared per-frame table of visible slice ranges, indexed by each bucket's window.
	std::unique_ptr<Buffer> sliceTable;
	uint32_t sliceTableCapacity = 0;
	std::vector<std::pair<uint32_t, uint32_t>> sliceTableCPU;
	// Store this frame's visible buckets in cull-slot order.
	std::vector<GrassBucket*> visibleBuckets;
	// A spatial run rebuild re-sorts and re-uploads a whole bucket, so each frame allows one.
	uint32_t spatialRebuildBudget = 0;
	std::vector<uint32_t> cullArguments;

	float timeAccum = 0.0f;
	float fadeInTimeRcp = 0.0f;
	float timeBase = 0.0f;
	float prevTimeBase = 0.0f;
	float grassStartFadeDistance = 0.0f;
	float vanillaMaxDistance = 0.0f;
	float maxGrassDistance = 0.0f;
	float maxDistSq = 0.0f;

	struct Hooks
	{
		static void DrawVanilla(RE::BSMultiStreamInstanceTriShape* geometry);

		struct BSMultiStreamInstanceTriShape_dtor
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* This);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		// The engine parents every grass shape directly under one node, so culling visits each shape every pass.
		struct NiNode_OnVisible
		{
			static void thunk(RE::NiNode* This, RE::NiCullingProcess* process, std::int32_t alphaGroupIndex);
			inline static REL::Relocation<decltype(thunk)> func;
			inline static std::atomic<RE::NiNode*> grassRoot{ nullptr };
			inline static std::atomic<uint32_t> mainViewVisitFrame{ 0 };
		};

		// Vanilla grass shapes are shared by every cell in a 12x12 block, so a cell unload removes its groups here.
		struct RemoveGroup
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* shape, std::uint32_t index);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct AddGroup
		{
			static std::uint32_t thunk(RE::BSMultiStreamInstanceTriShape* shape, std::uint32_t count, std::uint16_t* instanceData, std::uint32_t instanceShorts, float boundPadding);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct DoneAddingInstances
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSTArray<std::uint32_t>& groupIndices);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct BSGrassShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* a2, std::uint32_t flags);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct BSGrassShader_SetupMaterial
		{
			static void thunk(RE::BSShader* This, const RE::BSLightingShaderMaterialBase* material);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct AddGroupGIDBuffer
		{
			static std::uint32_t thunk(RE::BSMultiStreamInstanceTriShape* a1, RE::BSMultiStreamInstanceTriShape::GroupHeader* a2, std::uint16_t* a3);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct AddQueuedGroupGIDBuffer
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSMultiStreamInstanceTriShape::GroupHeader* header, std::uint16_t* instanceData, RE::BSTArray<std::uint32_t>& groupIndices);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct AddQueuedGroupGIDFile
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSStream* stream, RE::BSTArray<std::uint32_t>& groupIndices);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct AttachQueuedGroups
		{
			static void thunk();
			inline static REL::Relocation<decltype(thunk)> func;
			inline static std::atomic<bool> pendingCapture{ false };
		};

		struct AddGroupGIDFile
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* a1, RE::BSStream* a2);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct DrawInstanceTriShape
		{
			static void thunk(RE::BSRenderPass* curPass, RE::BSMultiStreamInstanceTriShape* geometry);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		struct LoadGrassType
		{
			static RE::BSMultiStreamInstanceTriShape* thunk(RE::BGSGrassManager* grassManager, RE::GrassParam* a_param, uint32_t CellXDivided, uint32_t CellYDivided, uint64_t* typeKey, RE::BSFixedString* modelPath);
			inline static REL::Relocation<decltype(thunk)> func;
		};

		static void Install();
	};
};
