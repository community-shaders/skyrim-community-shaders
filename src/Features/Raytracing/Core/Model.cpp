#include "Model.h"

#include "Features/Raytracing.h"
#include "Features/Raytracing/Helpers/ModelSpaceToTangent.h"

void Model::ConvertMSN()
{
	eastl::unordered_map<uint16_t, eastl::vector<Shape*>> msnMaps;

	uint vertexCount = 0;
	uint triangleCount = 0;

	for (auto& shape : shapes) {
		auto& material = shape->material;

		if (material.shaderFlags.none(RE::BSShaderProperty::EShaderPropertyFlag::kModelSpaceNormals))
			continue;

		vertexCount = std::max(vertexCount, shape->vertexCount);
		triangleCount = std::max(triangleCount, shape->triangleCount);

		auto key = material.Textures.at(RTConstants::MATERIAL_NORMALMAP_ID)->GetIndex();

		if (auto msnMap = msnMaps.find(key); msnMap != msnMaps.end()) {
			msnMap->second.push_back(shape.get());
		} else {
			msnMaps.emplace(key, eastl::vector<Shape*>{ shape.get() });
		}
	}

	if (msnMaps.empty())
		return;

	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	// Vertex Buffer
	winrt::com_ptr<ID3D11Buffer> vertexBuffer;
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = sizeof(ModelSpaceToTangent::UnpackedVertex) * vertexCount;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, vertexBuffer.put()));
	}

	// Index Buffer
	winrt::com_ptr<ID3D11Buffer> indexBuffer;
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = sizeof(uint16_t) * triangleCount * 3;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, indexBuffer.put()));
	}

	eastl::vector<ModelSpaceToTangent::UnpackedVertex> vertices;
	auto& rt = globals::features::raytracing;

	for (auto& [allocation, msnShapes] : msnMaps) {
		auto msnIt = rt.allocationMSNormalMaps.find(allocation);

		if (msnIt == rt.allocationMSNormalMaps.end())
			continue;

		auto* msnMap = msnIt->second;

		auto normalMapIt = rt.normalMaps.find(msnMap);

		if (normalMapIt == rt.normalMaps.end())
			continue;

		auto* convertedNormalMap = normalMapIt->second.get();

		if (convertedNormalMap->converted)
			continue;

		rt.normalMapConverter->Setup(msnMap);

		context->PSSetShaderResources(0, 1, &convertedNormalMap->OriginalSRV);

		ID3D11RenderTargetView* rtv = convertedNormalMap->Texture->rtv.get();
		context->OMSetRenderTargets(1, &rtv, nullptr);

		// We will continuously render and blend the final result to the same texture
		for (auto* shape : msnShapes) {
			// Update Vertex Buffer
			{
				vertices.resize(shape->vertexCount);

				for (size_t i = 0; i < shape->vertexCount; i++) {
					vertices[i] = shape->vertices[i];
				}

				D3D11_MAPPED_SUBRESOURCE mapped;
				context->Map(vertexBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

				memcpy(mapped.pData, vertices.data(), sizeof(ModelSpaceToTangent::UnpackedVertex) * shape->vertexCount);

				context->Unmap(vertexBuffer.get(), 0);
			}

			// Update Index Buffer
			{
				D3D11_MAPPED_SUBRESOURCE mapped;
				context->Map(indexBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

				memcpy(mapped.pData, shape->triangles.data(), sizeof(Triangle) * shape->triangleCount);

				context->Unmap(indexBuffer.get(), 0);
			}

			rt.normalMapConverter->SetVertexShader(shape->flags & Shape::Flags::Dynamic);

			rt.normalMapConverter->Draw(vertexBuffer.get(), indexBuffer.get(), shape->triangleCount);
		}

		convertedNormalMap->converted = true;

		rt.allocationMSNormalMaps.erase(allocation);
	}
}

void Model::BuildBLAS(ID3D12GraphicsCommandList4* commandList)
{
	auto& rt = globals::features::raytracing;

	std::lock_guard lock{ rt.renderMutex };

	static eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
	geometryDescs.clear();

	if (geometryDescs.capacity() < shapes.size())
		geometryDescs.reserve(shapes.size());

	for (auto& shape : shapes) {
		geometryDescs.push_back(shape->GeometryDesc());
	}

	auto modelFlags = GetFlags();

	bool updatable = (modelFlags & Shape::Flags::Skinned) || (modelFlags & Shape::Flags::Dynamic);

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

	if (updatable)
		buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
	else
		buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = buildFlags,
		.NumDescs = static_cast<uint>(geometryDescs.size()),
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	rt.d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	D3D12_RESOURCE_DESC desc = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = prebuildInfo.ScratchDataSizeInBytes,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.SampleDesc = Raytracing::NO_AA,
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	};

	auto blasScratchDesc = Raytracing::DEFAULT_HEAP_MA;
	blasScratchDesc.CustomPool = rt.blasScratchPool.get();

	winrt::com_ptr<D3D12MA::Allocation> scratch = nullptr;
	DX::ThrowIfFailed(rt.allocator->CreateResource(&blasScratchDesc, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, scratch.put(), IID_NULL, NULL));

	auto blasDesc = Raytracing::DEFAULT_HEAP_MA;
	blasDesc.CustomPool = rt.blasPool.get();

	desc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
	DX::ThrowIfFailed(rt.allocator->CreateResource(&blasDesc, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, blasBuffer.put(), IID_NULL, NULL));

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = blasBuffer->GetResource()->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.ScratchAccelerationStructureData = scratch->GetResource()->GetGPUVirtualAddress()
	};

	commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// Register frame that BLAS was created
	blasBuildFrame = globals::state->frameCount;

	const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(blasBuffer->GetResource());
	commandList->ResourceBarrier(1, &asBarrier);

	if (updatable)
		blasScratchBuffer = std::move(scratch);
	else
		rt.tempGPUData.emplace_back(std::move(scratch), rt.fenceValue);
}

void Model::UpdateBLAS(ID3D12GraphicsCommandList4* commandList)
{
	auto gpuVirtualAddr = blasBuffer->GetResource()->GetGPUVirtualAddress();

	if (!BLASBuildExecuted())
		return;

	if (!BLASUpdateExecuted())
		return;
	
	static eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
	geometryDescs.clear();

	if (geometryDescs.capacity() < shapes.size())
		geometryDescs.reserve(shapes.size());

	for (auto& shape : shapes) {
		geometryDescs.push_back(shape->GeometryDesc());
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE,
		.NumDescs = static_cast<uint>(geometryDescs.size()),
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = gpuVirtualAddr,
		.Inputs = inputs,
		.SourceAccelerationStructureData = gpuVirtualAddr,
		.ScratchAccelerationStructureData = blasScratchBuffer->GetResource()->GetGPUVirtualAddress()
	};

	commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// Register frame that BLAS was updated
	blasUpdateFrame = globals::state->frameCount;
}