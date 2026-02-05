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

			rt.normalMapConverter->SetVertexShader(shape->flags.any(Shape::Flags::Dynamic));

			rt.normalMapConverter->Draw(vertexBuffer.get(), indexBuffer.get(), shape->triangleCount);
		}

		convertedNormalMap->converted = true;

		rt.allocationMSNormalMaps.erase(allocation);
	}
}

bool Model::BLASBuildExecuted() const
{
	//logger::info("[RT] BLASBuildExecuted - Build Frame: {}, Current Frame: {} - {}", blasBuildFrame, globals::features::raytracing.frameIndex, blasBuildFrame < globals::features::raytracing.frameIndex);
	return blasBuildFrame < globals::features::raytracing.frameIndex;
}

bool Model::BLASUpdateExecuted() const
{
	//logger::info("[RT] BLASUpdateExecuted - Update Frame: {}, Current Frame: {} - {}", blasUpdateFrame, globals::features::raytracing.frameIndex, blasUpdateFrame < globals::features::raytracing.frameIndex);
	return blasUpdateFrame < globals::features::raytracing.frameIndex;
}

void Model::BuildBLAS(ID3D12GraphicsCommandList4* commandList)
{
	auto& rt = globals::features::raytracing;

	std::lock_guard lock{ rt.renderMutex };

	geometryDescs.resize(shapes.size());

	// Initial build with all shapes, visible or not, so the scratch buffer can be sized to fit all geometry
	for (size_t i = 0; i < shapes.size(); i++) {
		geometryDescs[i] = shapes[i]->GeometryDesc();
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = BuildFlags(),
		.NumDescs = static_cast<uint>(geometryDescs.size()),
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	rt.d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	D3D12_RESOURCE_DESC desc = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = std::max(prebuildInfo.ScratchDataSizeInBytes, prebuildInfo.UpdateScratchDataSizeInBytes) * 2,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.SampleDesc = Raytracing::NO_AA,
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	};

	auto blasScratchDesc = Raytracing::DEFAULT_HEAP_MA;
	blasScratchDesc.CustomPool = rt.blasScratchPool.get();

	DX::ThrowIfFailed(rt.allocator->CreateResource(&blasScratchDesc, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, blasScratchBuffer.put(), IID_NULL, NULL));

	auto blasDesc = Raytracing::DEFAULT_HEAP_MA;
	blasDesc.CustomPool = rt.blasPool.get();

	desc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
	DX::ThrowIfFailed(rt.allocator->CreateResource(&blasDesc, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, blasBuffer.put(), IID_NULL, NULL));

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = blasBuffer->GetResource()->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.SourceAccelerationStructureData = 0,
		.ScratchAccelerationStructureData = blasScratchBuffer->GetResource()->GetGPUVirtualAddress()
	};

	commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// Register frame that BLAS was created
	blasBuildFrame = rt.frameIndex;

	const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(blasBuffer->GetResource());
	commandList->ResourceBarrier(1, &asBarrier);
}

bool Model::UpdateBLAS(ID3D12GraphicsCommandList4* commandList)
{
	const bool update = flags.any(Flags::BLASUpdate); 
	const bool rebuild = flags.any(Flags::BLASRebuild);

	if (!update && !rebuild)
		return false;

	if (!BLASBuildExecuted())
		return false;

	if (!BLASUpdateExecuted())
		return false;
	
	if (update && shapeflags.none(Shape::Flags::Skinned,Shape::Flags::Dynamic)) {
		logger::critical("[RT] Model::UpdateBLAS - Only Skinned and Dynamic geometry should get the 'BLASUpdate' flag - [0x{:08X}]", reinterpret_cast<uintptr_t>(this));

		flags.reset(Flags::BLASUpdate);

		if (!rebuild)
			return false;
	}

	geometryDescs.clear();
	geometryDescs.reserve(shapes.size());

	for (auto& shape : shapes) {
		if (rebuild) {
			if (shape->IsPendingHidden())
				continue;
		} else {
			if (shape->IsHidden())
				continue;		
		}

		geometryDescs.push_back(shape->GeometryDesc());
	}

	if (geometryDescs.empty()) {
		logger::warn("[RT] Model::UpdateBLAS - Empty Geometry Descs");
		return false;
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = UpdateFlags(rebuild),
		.NumDescs = static_cast<uint>(geometryDescs.size()),
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = geometryDescs.data()
	};

	/*D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	globals::features::raytracing.d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	if (prebuildInfo.ResultDataMaxSizeInBytes > blasBuffer->GetResource()->GetDesc().Width) {
		logger::critical("[RT] ResultDataMaxSizeInBytes greater than current resource size.");
	}

	auto scratchWidth = blasScratchBuffer->GetResource()->GetDesc().Width;

	if (prebuildInfo.ScratchDataSizeInBytes > scratchWidth) {
		logger::critical("[RT] ScratchDataSizeInBytes greater than current scratch resource size.");
	}

	if (prebuildInfo.UpdateScratchDataSizeInBytes > scratchWidth) {
		logger::critical("[RT] UpdateScratchDataSizeInBytes greater than current scratch resource size.");
	}*/

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = blasBuffer->GetResource()->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.SourceAccelerationStructureData = rebuild ? 0 : blasBuffer->GetResource()->GetGPUVirtualAddress(),
		.ScratchAccelerationStructureData = blasScratchBuffer->GetResource()->GetGPUVirtualAddress()
	};

	commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	if (rebuild)
		for (auto& shape : shapes) {
			shape->UpdateState();
		}

	flags.reset(Flags::BLASUpdate, Flags::BLASRebuild);

	// Register frame that BLAS was updated
	blasUpdateFrame = globals::features::raytracing.frameIndex;
	return true;
}