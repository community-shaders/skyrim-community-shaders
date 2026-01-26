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

		logger::info("Shapes: {}", msnShapes.size());

		// We will continuously render and blend the final result to the same texture
		for (auto* shape : msnShapes) {
			// Update Vertex Buffer
			{
				vertices.resize(shape->vertexCount);

				logger::info("Vertex Buffer: {}", shape->vertexCount);

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
				logger::info("Index Buffer: {}", shape->triangleCount);

				D3D11_MAPPED_SUBRESOURCE mapped;
				context->Map(indexBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

				memcpy(mapped.pData, shape->triangles.data(), sizeof(Triangle) * shape->triangleCount);

				context->Unmap(indexBuffer.get(), 0);
			}

			rt.normalMapConverter->Draw(vertexBuffer.get(), indexBuffer.get(), shape->triangleCount);
		}

		convertedNormalMap->converted = true;

		rt.allocationMSNormalMaps.erase(allocation);
	}
}