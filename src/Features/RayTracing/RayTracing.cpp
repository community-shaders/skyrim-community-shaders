#include "RayTracing.h"

void RayTracing::SetupResources()
{
	auto device = D3D12::GetDevice();

	// Initialize pipeline generator
	m_pipelineGenerator = std::make_unique<nv_helpers_dx12::RayTracingPipelineGenerator>(device);

	// Create BLAS for all geometry
	auto& geometries = RE::BSGeometry::GetAllGeometries();

	m_blasGenerator = nv_helpers_dx12::RayTracingAccelerationStructureGenerator(device);

	for (auto& geometry : geometries) {
		if (auto geom = geometry->AsGeometry()) {
			auto vertexDesc = geom->GetVertexDesc();
			auto indexDesc = geom->GetIndexDesc();

			// Add geometry to BLAS generator
			m_blasGenerator.AddVertexBuffer(
				vertexDesc->resource.Get(),
				vertexDesc->offset,
				vertexDesc->vertexCount,
				vertexDesc->vertexSize,
				indexDesc->resource.Get(),
				indexDesc->offset,
				indexDesc->indexCount,
				nullptr, 0,  // No transform buffer
				true         // Opaque geometry
			);
		}
	}

	// Create BLAS
	UINT64 blasScratchSize, blasResultSize;
	m_blasGenerator.ComputeASBufferSizes(device, false, &blasScratchSize, &blasResultSize);

	m_scratchBuffer = D3D12::CreateBuffer(
		blasScratchSize,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	m_blasBuffer = D3D12::CreateBuffer(
		blasResultSize,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	m_blasGenerator.Generate(
		D3D12::GetCommandList(),
		m_scratchBuffer.Get(),
		m_blasBuffer.Get(),
		false, nullptr);

	// Create TLAS
	m_tlasGenerator = nv_helpers_dx12::RayTracingAccelerationStructureGenerator(device);

	for (auto& geometry : geometries) {
		if (auto geom = geometry->AsGeometry()) {
			m_tlasGenerator.AddInstance(
				m_blasBuffer.Get(),
				geom->worldTransform,
				geom->GetFormID(),
				0  // Default hit group
			);
		}
	}

	UINT64 tlasScratchSize, tlasResultSize, instanceDescsSize;
	m_tlasGenerator.ComputeASBufferSizes(device, false, &tlasScratchSize, &tlasResultSize, &instanceDescsSize);

	m_scratchBuffer = D3D12::CreateBuffer(
		tlasScratchSize,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	m_tlasBuffer = D3D12::CreateBuffer(
		tlasResultSize,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	auto instanceDescs = D3D12::CreateBuffer(
		instanceDescsSize,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_FLAG_NONE);

	m_tlasGenerator.Generate(
		D3D12::GetCommandList(),
		m_scratchBuffer.Get(),
		m_tlasBuffer.Get(),
		instanceDescs.Get(),
		false, nullptr);

	// Create ray tracing pipeline
	CreateRayTracingPipeline();

	// Create shader tables
	CreateShaderBindingTables();
}

void RayTracing::Reset()
{
	m_blasBuffer.Reset();
	m_tlasBuffer.Reset();
	m_scratchBuffer.Reset();

	m_shaderTables.rayGen.Reset();
	m_shaderTables.miss.Reset();
	m_shaderTables.hitGroup.Reset();

	m_rtStateObject.Reset();
	m_pipelineGenerator.reset();
}

void RayTracing::CreateRayTracingPipeline()
{
	auto device = D3D12::GetDevice();

	// Load shader libraries (simplified - actual implementation would compile from HLSL)
	// m_rayGenLibrary = CompileShader(L"RayTracing.hlsl", L"RayGen");
	// m_missLibrary = CompileShader(L"RayTracing.hlsl", L"Miss");
	// m_hitLibrary = CompileShader(L"RayTracing.hlsl", L"ClosestHit");

	// Add shader libraries to pipeline
	m_pipelineGenerator->AddLibrary(m_rayGenLibrary.Get(), { L"RayGen" });
	m_pipelineGenerator->AddLibrary(m_missLibrary.Get(), { L"Miss" });
	m_pipelineGenerator->AddLibrary(m_hitLibrary.Get(), { L"ClosestHit", L"AnyHit" });

	// Add hit groups
	m_pipelineGenerator->AddHitGroup(L"HitGroup", L"ClosestHit", L"AnyHit");

	// Add root signature associations
	m_pipelineGenerator->AddRootSignatureAssociation(m_rayGenSignature.Get(), { L"RayGen" });
	m_pipelineGenerator->AddRootSignatureAssociation(m_missSignature.Get(), { L"Miss" });
	m_pipelineGenerator->AddRootSignatureAssociation(m_hitSignature.Get(), { L"HitGroup" });

	// Set pipeline parameters
	m_pipelineGenerator->SetMaxPayloadSize(4 * sizeof(float));    // RGB + distance
	m_pipelineGenerator->SetMaxAttributeSize(2 * sizeof(float));  // barycentric coordinates
	m_pipelineGenerator->SetMaxRecursionDepth(2);                 // Primary + shadow rays

	// Generate pipeline state object
	m_rtStateObject = m_pipelineGenerator->Generate();

	// Get shader identifiers
	auto GetShaderID = [&](LPCWSTR name) {
		void* id;
		ThrowIfFailed(m_rtStateObject->GetShaderIdentifier(name, &id));
		return id;
	};

	m_shaderIDs.rayGen = GetShaderID(L"RayGen");
	m_shaderIDs.miss = GetShaderID(L"Miss");
	m_shaderIDs.closestHit = GetShaderID(L"ClosestHit");
	m_shaderIDs.anyHit = GetShaderID(L"AnyHit");
}

void RayTracing::CreateShaderBindingTables()
{
	auto device = D3D12::GetDevice();

	// Get shader identifier size
	UINT shaderIDSize = D3D12::GetShaderIdentifierSize();

	// Create shader tables
	m_shaderTables.rayGenStride = shaderIDSize;
	m_shaderTables.missStride = shaderIDSize;
	m_shaderTables.hitGroupStride = shaderIDSize;

	// RayGen table (1 entry)
	m_shaderTables.rayGen = D3D12::CreateBuffer(
		shaderIDSize,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_FLAG_NONE);

	// Miss table (1 entry)
	m_shaderTables.miss = D3D12::CreateBuffer(
		shaderIDSize,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_FLAG_NONE);

	// HitGroup table (1 entry per material type)
	m_shaderTables.hitGroup = D3D12::CreateBuffer(
		shaderIDSize * 4,  // Room for 4 material types
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_FLAG_NONE);

	// Map and populate tables
	{
		// RayGen
		void* rayGenData;
		m_shaderTables.rayGen->Map(0, nullptr, &rayGenData);
		memcpy(rayGenData, m_shaderIDs.rayGen, shaderIDSize);
		m_shaderTables.rayGen->Unmap(0, nullptr);

		// Miss
		void* missData;
		m_shaderTables.miss->Map(0, nullptr, &missData);
		memcpy(missData, m_shaderIDs.miss, shaderIDSize);
		m_shaderTables.miss->Unmap(0, nullptr);

		// HitGroup (placeholder - will be filled per-material)
		void* hitGroupData;
		m_shaderTables.hitGroup->Map(0, nullptr, &hitGroupData);
		memset(hitGroupData, 0, shaderIDSize * 4);
		m_shaderTables.hitGroup->Unmap(0, nullptr);
	}
}