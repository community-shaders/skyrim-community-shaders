#include "Features/ProceduralGrass.h"

#include "Features/GrassCollision.h"
#include "TerrainHeightMap.h"
#include "Utils/D3D.h"

using namespace PGrassCommon;

void ProceduralGrass::CreateIndexBuffers()
{
	const auto makeIndexBuffer = [](const std::vector<uint16_t>& indices, const char* name) {
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
		D3D11_SUBRESOURCE_DATA init{ indices.data(), 0, 0 };
		return new Buffer(desc, &init, name);
	};

	const auto makeBatchedIndexBuffer = [&](std::span<const uint16_t> indices, uint32_t verticesPerBlade, uint32_t bladeCount, const char* name) {
		std::vector<uint16_t> batchedIndices;
		batchedIndices.reserve(indices.size() * bladeCount);
		for (uint32_t blade = 0; blade < bladeCount; ++blade) {
			for (const auto index : indices)
				batchedIndices.push_back(static_cast<uint16_t>(blade * verticesPerBlade + index));
		}
		return makeIndexBuffer(batchedIndices, name);
	};

	vertexIndicesHighBuffer = makeIndexBuffer(CreateVertexIndicesArray(15), "PGrass::HighIndices");
	// Outer High is drawn twice, in depth and colour, and both passes are vertex-bound.
	const auto fiveVertexBladeIndices = CreateVertexIndicesArray(5);
	vertexIndicesHighOuterBuffer = makeIndexBuffer(fiveVertexBladeIndices, "PGrass::HighOuterIndices");

	const std::array<uint16_t, 6> lowBladeIndices = { 0, 1, 2, 2, 1, 3 };
	vertexIndicesLowBuffer = makeBatchedIndexBuffer(lowBladeIndices, 4u, LowBladeBatchSize, "PGrass::LowIndices");
	const std::array<uint16_t, 3> outerBladeIndices = { 0, 1, 2 };
	vertexIndicesLowOuterBuffer = makeBatchedIndexBuffer(outerBladeIndices, 3u, LowBladeBatchSize, "PGrass::LowOuterIndices");

	// Mid keeps one curve midpoint; Low uses a distant two-triangle ribbon.
	vertexIndicesMidBuffer = makeBatchedIndexBuffer(fiveVertexBladeIndices, 5u, MidBladeBatchSize, "PGrass::MidIndices");

	// Fully straightened Mid blades need only one triangle in both depth and colour passes.
	vertexIndicesMidOuterBuffer = makeBatchedIndexBuffer(outerBladeIndices, 3u, MidBladeBatchSize, "PGrass::MidOuterIndices");
}

void ProceduralGrass::CreatePipelineStates()
{
	auto device = globals::d3d::device;

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	if (SUCCEEDED(device->CreateSamplerState(&samplerDesc, &linearClampSampler)))
		Util::SetResourceName(linearClampSampler, "PGrass::LinearClampSampler");

	D3D11_SAMPLER_DESC shadowSamplerDesc = {};
	shadowSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	shadowSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	shadowSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	shadowSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	shadowSamplerDesc.MipLODBias = 0;
	shadowSamplerDesc.MaxAnisotropy = 1;
	shadowSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	shadowSamplerDesc.MinLOD = -FLT_MAX;
	shadowSamplerDesc.MaxLOD = 0;
	if (SUCCEEDED(device->CreateSamplerState(&shadowSamplerDesc, &shadowSampler)))
		Util::SetResourceName(shadowSampler, "PGrass::ShadowSampler");

	D3D11_RASTERIZER_DESC rd{};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.DepthClipEnable = TRUE;
	rd.DepthBiasClamp = -100.0f;
	if (SUCCEEDED(device->CreateRasterizerState(&rd, &noCullRS)))
		Util::SetResourceName(noCullRS, "PGrass::NoCullRS");

	D3D11_DEPTH_STENCIL_DESC dd{};
	dd.DepthEnable = TRUE;
	dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dd.DepthFunc = D3D11_COMPARISON_LESS;
	if (SUCCEEDED(device->CreateDepthStencilState(&dd, &depthWriteDS)))
		Util::SetResourceName(depthWriteDS, "PGrass::DepthWriteDS");
	dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (SUCCEEDED(device->CreateDepthStencilState(&dd, &depthEqualDS)))
		Util::SetResourceName(depthEqualDS, "PGrass::DepthTestDS");

	D3D11_BLEND_DESC bd{};
	if (SUCCEEDED(device->CreateBlendState(&bd, &depthOnlyBlend)))
		Util::SetResourceName(depthOnlyBlend, "PGrass::DepthOnlyBlend");

	// Grass clears terrain classification while inheriting the ground's vertex AO.
	bd.IndependentBlendEnable = TRUE;
	for (auto& target : bd.RenderTarget)
		target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	bd.RenderTarget[7].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_GREEN;
	if (SUCCEEDED(device->CreateBlendState(&bd, &defaultBlend)))
		Util::SetResourceName(defaultBlend, "PGrass::DefaultBlend");

	for (auto& target : bd.RenderTarget) {
		target.BlendEnable = TRUE;
		target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		target.BlendOp = D3D11_BLEND_OP_ADD;
		target.SrcBlendAlpha = D3D11_BLEND_ONE;
		target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	}
	if (SUCCEEDED(device->CreateBlendState(&bd, &terrainFadeBlend)))
		Util::SetResourceName(terrainFadeBlend, "PGrass::TerrainFadeBlend");
}

void ProceduralGrass::CreateGrassTextures()
{
	// SetupResources runs once, so every texture is created exactly once here.
	grassDensityTexture = Util::CreateSquareTexture(grassDensityDim, DXGI_FORMAT_R32_UINT, true, "PGrass::GrassDensity");
	distantAmbientLUT = Util::CreateSquareTexture(distantAmbientLUTDim, DXGI_FORMAT_R16G16B16A16_FLOAT, true, "PGrass::DistantAmbientLUT");
	// R32_FLOAT is the float format D3D11 guarantees for typed UAV reads.
	terrainLiftMeasuredTexture = Util::CreateSquareTexture(PGrassCommon::TerrainLiftDim, DXGI_FORMAT_R32_FLOAT, true, "PGrass::TerrainLiftMeasured");
	terrainLiftTexture = Util::CreateSquareTexture(PGrassCommon::TerrainLiftDim, DXGI_FORMAT_R32_FLOAT, true, "PGrass::TerrainLift");
	terrainLiftSurfaceTexture = Util::CreateSquareTexture(PGrassCommon::TerrainLiftDim, DXGI_FORMAT_R32_FLOAT, true, "PGrass::TerrainLiftSurface");
	terrainLiftOriginValid = false;
	// Rewritten via UpdateSubresource as the window scrolls with the player.
	grassPresenceTexture = Util::CreateSquareTexture(grassPresenceDim, DXGI_FORMAT_R8_UINT, false, "PGrass::GrassPresence");
	grassPresenceStaging.assign(static_cast<size_t>(grassPresenceDim) * grassPresenceDim, 0);
}

void ProceduralGrass::SetupResources()
{
	quadrantsHighLOD.reserve(HighTierQuadrantCap);
	quadrantsMidLOD.reserve(MidTierQuadrantCap);
	quadrantsLowLOD.reserve(LowTierQuadrantCap);
	quadrantsPresence.reserve(LowTierQuadrantCap);
	grassMapCache.reserve(grassMapCacheCapacity);

	globals::terrainHeightMap->Discover();

	grassGlobalsCB = new ConstantBuffer(ConstantBufferDesc<GrassGlobals>(), "PGrass::GlobalsCB");
	grassTypesArrayCB = new ConstantBuffer(ConstantBufferDesc<GrassTypesArray>(), "PGrass::TypesCB");
	grassGeneratorTypesCB = new ConstantBuffer(ConstantBufferDesc<GrassGeneratorTypesArray>(), "PGrass::GeneratorTypesCB");

	CreateIndexBuffers();

	// Each tier adds one steepness-gated slope-fill candidate per patch.
	using PGrassCommon::Tier;
	const bool cacheCollision = globals::features::grassCollision.loaded;
	const uint32_t highBladeStride = cacheCollision ? sizeof(PGrassCommon::BladeSkylitCollision) : sizeof(PGrassCommon::BladeSkylit);
	const uint32_t midBladeStride = cacheCollision ? sizeof(PGrassCommon::BladeMidCollision) : sizeof(PGrassCommon::BladeMid);
	grassRendererHighLOD = std::make_unique<PGrassRenderer<PGrassCommon::HighTierQuadrantCap, 4>>(Tier::High, QualityDensities[settings.Quality],
		vertexIndicesHighBuffer, vertexIndicesHighOuterBuffer, highBladeStride);
	grassRendererMidLOD = std::make_unique<PGrassRenderer<PGrassCommon::MidTierQuadrantCap, PGrassCommon::MidPatchBladeCount>>(Tier::Mid,
		static_cast<uint32_t>(settings.midGrassDensity), vertexIndicesMidBuffer, vertexIndicesMidOuterBuffer, midBladeStride);
	grassRendererLowLOD = std::make_unique<PGrassRenderer<PGrassCommon::LowTierQuadrantCap, 1>>(Tier::Low,
		static_cast<uint32_t>(settings.lowGrassDensity), vertexIndicesLowBuffer, vertexIndicesLowOuterBuffer, static_cast<uint32_t>(sizeof(PGrassCommon::Blade)));

	CreatePipelineStates();
	CreateGrassTextures();

	CompileSupportShaders();
}

void ProceduralGrass::CompileSupportShaders()
{
	if (!densityGatherCS)
		densityGatherCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ProceduralGrass\\PGrassDensityGatherCS.hlsl", {}, "cs_5_0"));
	if (!distantAmbientLUTCS)
		distantAmbientLUTCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ProceduralGrass\\PGrassAmbientLUTCS.hlsl", {}, "cs_5_0"));
	if (!terrainLiftCS)
		terrainLiftCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ProceduralGrass\\PGrassTerrainLiftCS.hlsl", {}, "cs_5_0"));
	if (!depthClipPS)
		depthClipPS = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\ProceduralGrass\\PGrassDepthPS.hlsl", {}, "ps_5_0"));
}

std::vector<uint16_t> ProceduralGrass::CreateVertexIndicesArray(uint16_t vertCount)
{
	assert(vertCount >= 3 && ((vertCount - 3) % 2) == 0);
	const uint16_t segments = (vertCount - 3) / 2;

	// Keep fold vertices from becoming provoking vertices so flat normals remain correct.
	const uint16_t fold0 = segments;
	const uint16_t fold1 = segments + 1;

	std::vector<uint16_t> indices;
	indices.reserve(segments * 6 + 3);

	// Rotate each triangle so a non-fold vertex is provoking while preserving winding.
	auto addTri = [&](uint16_t a, uint16_t b, uint16_t c) {
		if (a == fold0 || a == fold1) {
			if (b != fold0 && b != fold1) {
				// Use b as the provoking vertex.
				indices.push_back(b);
				indices.push_back(c);
				indices.push_back(a);
				return;
			}

			// Otherwise use c as the provoking vertex.
			indices.push_back(c);
			indices.push_back(a);
			indices.push_back(b);
			return;
		}

		indices.push_back(a);
		indices.push_back(b);
		indices.push_back(c);
	};

	for (uint16_t i = 0; i < segments; ++i) {
		uint16_t v0 = 2 * i + 0;
		uint16_t v1 = 2 * i + 1;
		uint16_t v2 = 2 * (i + 1) + 0;
		uint16_t v3 = 2 * (i + 1) + 1;

		addTri(v0, v1, v2);
		addTri(v2, v1, v3);
	}

	// Last cap triangle
	uint16_t base = segments * 2;
	addTri(base, base + 1, base + 2);

	return indices;
}
