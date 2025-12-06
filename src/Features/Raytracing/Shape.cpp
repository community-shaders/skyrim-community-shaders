#include "Shape.h"
#include "Features/Raytracing.h"
#include "Features/Raytracing/Heap.h"

using GIHeap = Raytracing::GIHeap;
using SkinningHeap = Raytracing::SkinningHeap;

void Shape::BuildMesh(RE::BSGraphics::TriShape* rendererData, const std::uint32_t& vertexCountIn, const std::uint16_t& triangleCountIn, const std::uint16_t& bonesPerVertex, const float4x4& transform)
{
	// Vertices
	{
		bool skinned = bonesPerVertex > 0;

		vertices.resize(vertexCountIn);

		if (skinned)
			skinning.resize(vertexCountIn);

		auto vertexDesc = rendererData->vertexDesc;

		auto vertexFlags = vertexDesc.GetFlags();
		uint32_t stride = vertexDesc.GetSize();

		bool hasPosition = vertexFlags & RE::BSGraphics::Vertex::VF_VERTEX;

		uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
		uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
		uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
		uint32_t tangOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_BINORMAL);
		uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);
		uint32_t skinOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);

		uint32_t boneIDOffset = sizeof(uint16_t) * bonesPerVertex;

		eastl::vector<half> weights(bonesPerVertex);
		eastl::vector<uint8_t> boneIds(bonesPerVertex);

		for (uint16_t i = 0; i < vertexCountIn; i++) {
			uint8_t* vtx = rendererData->rawVertexData + i * stride;

			Vertex vertexData{};

			float4 pos;

			if (hasPosition) {
				std::memcpy(&pos, vtx + posOffset, sizeof(float4));

				vertexData.Position = float3::Transform({ pos.x, pos.y, pos.z }, transform);
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
				std::memcpy(&vertexData.Texcoord0, vtx + uvOffset, sizeof(half2));
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL) {
				uint32_t normalData;
				std::memcpy(&normalData, vtx + normOffset, sizeof(uint32_t));
				auto normalUnpacked = UnpackByte4(normalData);

				vertexData.Normal = Normalize(float3::TransformNormal({ normalUnpacked.x, normalUnpacked.y, normalUnpacked.z }, transform));

				if (vertexFlags & RE::BSGraphics::Vertex::VF_TANGENT) {
					uint32_t tangentData;
					std::memcpy(&tangentData, vtx + tangOffset, sizeof(uint32_t));
					auto tangentUnpacked = UnpackByte4(tangentData);

					vertexData.Tangent = Normalize(float3::TransformNormal({ tangentUnpacked.x, tangentUnpacked.y, tangentUnpacked.z }, transform));

					float3 bitangent = { pos.w, normalUnpacked.w, tangentUnpacked.w };
					vertexData.Bitangent = hasPosition ? Normalize(float3::TransformNormal(bitangent, transform)) : bitangent;
				}
			}

			if (skinned) {
				if (vertexFlags & RE::BSGraphics::Vertex::VF_SKINNED) {
					std::memcpy(weights.data(), vtx + skinOffset, sizeof(half) * bonesPerVertex);
					std::memcpy(boneIds.data(), vtx + skinOffset + boneIDOffset, sizeof(uint8_t) * bonesPerVertex);

				} else {
					weights.clear();
					weights.resize(bonesPerVertex);

					boneIds.clear();
					boneIds.resize(bonesPerVertex);
				}
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_COLORS) {
				std::memcpy(&vertexData.Color, vtx + colorOffset, sizeof(uint32_t));
			}

			vertices[i] = vertexData;

			if (skinned)
				skinning[i] = Skinning(weights, boneIds);
		}

		vertexCount = vertexCountIn;
	}

	// Triangles
	{
		triangles.resize(triangleCountIn);

		eastl::vector<uint16_t> indices(triangleCountIn * 3);
		std::memcpy(indices.data(), rendererData->rawIndexData, sizeof(uint16_t) * triangleCountIn * 3);

		for (uint16_t t = 0; t < triangleCountIn; ++t) {
			uint16_t i = t * 3u;

			uint16_t v0 = indices[i];
			uint16_t v1 = indices[i + 1u];
			uint16_t v2 = indices[i + 2u];

			if (v0 > vertexCount || v1 > vertexCount || v2 > vertexCount)
				logger::critical("[RT] Triangle {} vertice overflow: [{}, {}, {}]", t, v0, v1, v2);

			triangles[t] = Triangle(v0, v1, v2);
		}

		triangleCount = triangleCountIn;
	}
}

void Shape::BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, [[maybe_unused]] const char* name)
{
	using State = RE::BSGeometry::States;
	using Feature = RE::BSShaderMaterial::Feature;
	using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;

	auto& rt = globals::features::raytracing;

	//Feature feature = Feature::kNone;
	float4 baseColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float4 effectColor = { 0.0f, 0.0f, 0.0f, 1.0f };

	float4 texCoordOffsetScale = { 0.0f, 0.0f, 1.0f, 1.0f };
	float4 texCoord1OffsetScale = { 0.0f, 0.0f, 1.0f, 1.0f };

	uint16_t baseTextureRegister = 0;
	uint16_t effectTextureRegister = 0;
	uint16_t rmaosTextureRegister = 0;
	int effectType = 0;

	RE::BSShader::Type shaderType = RE::BSShader::Type::None;

	ID3D11Texture2D* baseTexture = nullptr;
	ID3D11Texture2D* effectTexture = nullptr;
	//ID3D11Texture2D* rmaosTexture = nullptr; // Useful for path tracing, not much for GI

	{
		auto* property = geometryRuntimeData.properties[State::kProperty].get();

		if (property && property->GetType() == RE::NiProperty::Type::kAlpha) {
			flags |= Flags::Alpha;
		}

		if (property; auto lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(property)) {
			if (auto& effectData = lightingShader->effectData) {
				logger::info("[RT] CreateMaterial - Effect - Alpha: {}, Z Test Func: {}", effectData->alpha, magic_enum::enum_name(effectData->zTestFunc));
			}
		}

		auto* effect = geometryRuntimeData.properties[State::kEffect].get();

		if (effect) {
			auto lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect);

			logger::debug("[RT] CreateMaterial - BSLightingShaderProperty Flags: {}", GetFlags<EShaderPropertyFlag>(lightingShader->flags.underlying()));

			if (lightingShader) {
				// This is always nullptr :(
				if (auto& effectData = lightingShader->effectData) {
					logger::info("[RT] CreateMaterial - Effect - Alpha: {}, Z Test Func: {}", effectData->alpha, magic_enum::enum_name(effectData->zTestFunc));
				}

				shaderType = RE::BSShader::Type::Lighting;

				effectColor = {
					lightingShader->emissiveColor->red,
					lightingShader->emissiveColor->green,
					lightingShader->emissiveColor->blue,
					lightingShader->emissiveMult
				};

				logger::debug("[RT] CreateMaterial - BSLightingShaderProperty Alpha: {}", lightingShader->alpha);

				if (auto shaderMaterial = lightingShader->material) {
					texCoordOffsetScale = {
						shaderMaterial->texCoordOffset[0].x, shaderMaterial->texCoordOffset[0].y,
						shaderMaterial->texCoordScale[0].x, shaderMaterial->texCoordScale[0].y
					};

					/*texCoord1OffsetScale = {
							material->texCoordOffset[1].x, material->texCoordOffset[1].y,
							material->texCoordScale[1].x, material->texCoordScale[1].y
						};*/

					const auto* lightingBaseMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial);

					if (lightingBaseMaterial) {
						baseTexture = TryGetTexture(lightingBaseMaterial->diffuseTexture);

						logger::debug("[RT] CreateMaterial - BSLightingShaderMaterialBase Alpha: {}", lightingBaseMaterial->materialAlpha);

						if (shaderMaterial->GetFeature() == Feature::kGlowMap) {
							const auto* lightingGlowMaterial = static_cast<RE::BSLightingShaderMaterialGlowmap*>(shaderMaterial);

							if (lightingGlowMaterial) {
								effectTexture = TryGetTexture(lightingGlowMaterial->glowTexture);
							}
						}
					}
				}
			}

			auto effectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(effect);

			if (effectShader) {
				shaderType = RE::BSShader::Type::Effect;

				if (auto shaderMaterial = effectShader->material) {
					auto effectMaterial = static_cast<RE::BSEffectShaderMaterial*>(shaderMaterial);

					if (effectMaterial) {
						effectType = 1;
						effectColor = { effectMaterial->baseColor.red, effectMaterial->baseColor.green, effectMaterial->baseColor.blue, effectMaterial->baseColorScale };

						baseTexture = TryGetTexture(effectMaterial->sourceTexture);
						effectTexture = TryGetTexture(effectMaterial->greyscaleTexture);
					}
				}
			}
		}
	}

	if (baseTexture == nullptr)
		logger::warn("[RT] CreateMaterial {} - Base texture is nullptr", name);

	baseTextureRegister = rt.GetTextureRegister(baseTexture, true);
	effectTextureRegister = rt.GetTextureRegister(effectTexture, false);

	if (baseTexture && baseTextureRegister == 0)
		logger::warn("[RT] CreateMaterial {} - Base texture [0x{:8X}] not shared", name, reinterpret_cast<uintptr_t>(baseTexture));

	material = Material(
		baseColor,
		effectColor,
		texCoordOffsetScale,
		baseTextureRegister,
		effectTextureRegister,
		rmaosTextureRegister,
		static_cast<uint16_t>(shaderType));
}

void Shape::CreateBuffers(const std::wstring& name)
{
	auto& rt = globals::features::raytracing;
	auto* device = rt.d3d12Device.get();
	auto* commandList = rt.commandList.get();

	auto* skinningHeap = rt.skinningHeap.get();
	auto* giHeap = rt.giHeap.get();

	auto* materialBuffer = rt.materialBuffer.get();

	// Dynamic
	if (flags & Flags::Dynamic) {
		// Not really a buffer but we need to initialize it somewhere
		dynamicPosition.resize(vertexCount);

		dynamicPositionBuffer = eastl::make_unique<DX12::StructuredBufferUpload<float4>>(device, vertexCount);
		dynamicPositionBuffer->CreateSRV(skinningHeap->CPUHandle(SkinningHeap::Slot::DynamicVertices, registerIndex));
	}

	// Vertices
	{
		bool hasUAV = (flags & Flags::Dynamic) || (flags & Flags::Skinned);

		vertexBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Vertex>>(device, vertexCount, hasUAV);

		vertexBuffer->UpdateList(vertices.data(), vertexCount);
		DX::ThrowIfFailed(vertexBuffer->resource->SetName(std::format(L"Vertex Buffer [{}] - {}", registerIndex, name).c_str()));

		vertexBuffer->Upload(commandList);

		// UAV
		if (hasUAV) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = vertexCount;
			uavDesc.Buffer.StructureByteStride = sizeof(Vertex);
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

			device->CreateUnorderedAccessView(vertexBuffer->resource.get(), nullptr, &uavDesc, skinningHeap->CPUHandle(SkinningHeap::Slot::Output, registerIndex));
		}

		// SRV
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC vbDesc = {};
			vbDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			vbDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			vbDesc.Format = DXGI_FORMAT_UNKNOWN;
			vbDesc.Buffer.FirstElement = 0;
			vbDesc.Buffer.NumElements = vertexCount;
			vbDesc.Buffer.StructureByteStride = sizeof(Vertex);
			vbDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			device->CreateShaderResourceView(vertexBuffer->resource.get(), &vbDesc, giHeap->CPUHandle(GIHeap::Slot::Vertices, registerIndex));
		}
	}

	// Skinning
	if (flags & Flags::Skinned) {
		skinningBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Skinning>>(device, vertexCount);

		skinningBuffer->UpdateList(skinning.data(), vertexCount);
		DX::ThrowIfFailed(skinningBuffer->resource->SetName(std::format(L"Skinning Buffer [{}] - {}", registerIndex, name).c_str()));

		skinningBuffer->Upload(commandList);

		// SRV
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC vbDesc = {};
			vbDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			vbDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			vbDesc.Format = DXGI_FORMAT_UNKNOWN;
			vbDesc.Buffer.FirstElement = 0;
			vbDesc.Buffer.NumElements = vertexCount;
			vbDesc.Buffer.StructureByteStride = sizeof(Skinning);
			vbDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			device->CreateShaderResourceView(skinningBuffer->resource.get(), &vbDesc, skinningHeap->CPUHandle(SkinningHeap::Slot::Skinning, registerIndex));
		}
	}

	// Triangles
	{
		triangleBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Triangle>>(device, triangleCount);

		triangleBuffer->UpdateList(triangles.data(), triangles.size());
		DX::ThrowIfFailed(triangleBuffer->resource->SetName(std::format(L"Triangle Buffer [{}] - {}", registerIndex, name).c_str()));

		triangleBuffer->Upload(commandList);

		// SRV
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC ibDesc = {};
			ibDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			ibDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			ibDesc.Format = DXGI_FORMAT_UNKNOWN;
			ibDesc.Buffer.FirstElement = 0;
			ibDesc.Buffer.NumElements = triangleCount;
			ibDesc.Buffer.StructureByteStride = sizeof(Triangle);
			ibDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			device->CreateShaderResourceView(triangleBuffer->resource.get(), &ibDesc, giHeap->CPUHandle(GIHeap::Slot::Triangles, registerIndex));
		}
	}

	// Material
	materialBuffer->Update(&material, sizeof(Material), sizeof(Material) * registerIndex);
}