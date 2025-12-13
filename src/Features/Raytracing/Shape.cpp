#include "Shape.h"
#include "Features/Raytracing.h"
#include "Features/Raytracing/Heap.h"
#include "TruePBR.h"
#include "TruePBR/BSLightingShaderMaterialPBR.h"

using GIHeap = Raytracing::GIHeap;
using SkinningHeap = Raytracing::SkinningHeap;

void Shape::BuildMesh(RE::BSGraphics::TriShape* rendererData, const std::uint32_t& vertexCountIn, const std::uint16_t& triangleCountIn, const std::uint16_t& bonesPerVertex, const float4x4& transform)
{
	bool hasNormal = vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL;
	bool hasTangent = vertexFlags & RE::BSGraphics::Vertex::VF_TANGENT;

	// Vertices
	{
		bool dynamic = flags & Flags::Dynamic;
		bool skinned = flags & Flags::Skinned;

		if (dynamic) {
			dynamicPosition.resize(vertexCountIn);

			auto* pDynamicTriShape = skyrim_cast<RE::BSDynamicTriShape*>(geometry);

			if (pDynamicTriShape) {
				const auto& dynTriShapeRuntime = pDynamicTriShape->GetDynamicTrishapeRuntimeData();
				std::memcpy(dynamicPosition.data(), dynTriShapeRuntime.dynamicData, dynTriShapeRuntime.dataSize);
			}
		}

		vertices.resize(vertexCountIn);

		if (skinned)
			skinning.resize(vertexCountIn);

		auto vertexDesc = rendererData->vertexDesc;

		vertexFlags = vertexDesc.GetFlags();
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
			} else if (dynamic) {
				pos = dynamicPosition[i];
			}

			if (hasPosition || dynamic) {
				vertexData.Position = float3::Transform({ pos.x, pos.y, pos.z }, transform);
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
				std::memcpy(&vertexData.Texcoord0, vtx + uvOffset, sizeof(half2));
			}

			if (hasNormal) {
				uint32_t normalData;
				std::memcpy(&normalData, vtx + normOffset, sizeof(uint32_t));
				auto normalUnpacked = UnpackByte4(normalData);

				vertexData.Normal = Normalize(float3::TransformNormal({ normalUnpacked.x, normalUnpacked.y, normalUnpacked.z }, transform));

				if (hasTangent) {
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
			} else {
				vertexData.Color.packed = PackUByte4({ 1.0f, 1.0f, 1.0f, 1.0f });
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

	if (!hasNormal || !hasTangent) {
		CalculateNTB(!hasNormal);
	}
}

void Shape::BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, [[maybe_unused]] const char* name)
{
	using State = RE::BSGeometry::States;
	using Feature = RE::BSShaderMaterial::Feature;
	using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;

	auto& rt = globals::features::raytracing;

	//Feature feature = Feature::kNone;
	float4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	float4 effectColor = { 0.0f, 0.0f, 0.0f, 1.0f };

	float4 texCoordOffsetScale = { 0.0f, 0.0f, 1.0f, 1.0f };
	float4 texCoord1OffsetScale = { 0.0f, 0.0f, 1.0f, 1.0f };

	half roughness = 1.0f;

	int effectType = 0;

	RE::BSShader::Type shaderType = RE::BSShader::Type::None;

	ID3D11Texture2D* baseTexture = nullptr;
	ID3D11Texture2D* normalTexture = nullptr;
	ID3D11Texture2D* effectTexture = nullptr;
	ID3D11Texture2D* rmaosTexture = nullptr;

	{
		auto* property = geometryRuntimeData.properties[State::kProperty].get();

		if (property && property->GetType() == RE::NiProperty::Type::kAlpha) {
			flags |= Flags::Alpha;
		}

		if (property; auto* lightingShaderProp = netimmerse_cast<RE::BSLightingShaderProperty*>(property)) {
			logger::info("[RT] BuildMaterial - [Prop] BSLightingShaderProperty Flags: {}", GetFlagsString<EShaderPropertyFlag>(lightingShaderProp->flags.underlying()));

			if (auto& effectData = lightingShaderProp->effectData) {
				logger::debug("[RT] BuildMaterial - Effect - Alpha: {}, Z Test Func: {}", effectData->alpha, magic_enum::enum_name(effectData->zTestFunc));
			}
		}

		logger::trace("[RT] BuildMaterial {}", name);

		auto* effect = geometryRuntimeData.properties[State::kEffect].get();

		logger::trace("[RT] BuildMaterial - Effect RTTI: {}", effect->GetRTTI()->GetName());
		
		if (effect) {
			if (auto* lightingShaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(effect)) {
				logger::debug("[RT] BuildMaterial - [Effect] BSLightingShaderProperty Flags: {}", GetFlagsString<EShaderPropertyFlag>(lightingShaderProp->flags.underlying()));

				// This is always nullptr :(
				if (auto& effectData = lightingShaderProp->effectData) {
					logger::info("[RT] BuildMaterial - Effect - Alpha: {}, Z Test Func: {}", effectData->alpha, magic_enum::enum_name(effectData->zTestFunc));
				}

				shaderType = RE::BSShader::Type::Lighting;

				effectColor = {
					lightingShaderProp->emissiveColor->red,
					lightingShaderProp->emissiveColor->green,
					lightingShaderProp->emissiveColor->blue,
					lightingShaderProp->emissiveMult
				};

				logger::debug("[RT] BuildMaterial - BSLightingShaderProperty Alpha: {}", lightingShaderProp->alpha);

				if (auto shaderMaterial = lightingShaderProp->material) {
					//logger::info("[RT] BuildMaterial - Feature: {}, Type: {}", magic_enum::enum_name(shaderMaterial->GetFeature()), magic_enum::enum_name(shaderMaterial->GetType()));

					texCoordOffsetScale = {
						shaderMaterial->texCoordOffset[0].x, shaderMaterial->texCoordOffset[0].y,
						shaderMaterial->texCoordScale[0].x, shaderMaterial->texCoordScale[0].y
					};

					//texCoord1OffsetScale = {
					//	material->texCoordOffset[1].x, material->texCoordOffset[1].y,
					//	material->texCoordScale[1].x, material->texCoordScale[1].y
					//};

					// Using static_cast so we still get diffuse and normal for PBR materials
					if (const auto* lightingBaseMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial)) {
						logger::debug("[RT] BuildMaterial - BSLightingShaderMaterialBase Alpha: {}", lightingBaseMaterial->materialAlpha);

						baseTexture = TryGetTexture(lightingBaseMaterial->diffuseTexture);
						normalTexture = TryGetTexture(lightingBaseMaterial->normalTexture);
					}

					// TrueBR - Tried to check for 'lightingShaderProp->flags.any(TruePBR::PBRFlag)' 
					// where 'TruePBR::PBRFlag = RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen' but it did not work at all
					// skyrim_cast also doesn't work (no RTTI?)
					if (typeid(*shaderMaterial) == typeid(BSLightingShaderMaterialPBR)) {
						const auto* lightingPBRMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

						logger::debug("[RT] BuildMaterial - BSLightingShaderMaterialPBR: [0x{:8X}]", reinterpret_cast<uintptr_t>(lightingPBRMaterial->diffuseTexture.get()));

						effectTexture = TryGetTexture(lightingPBRMaterial->emissiveTexture);
						rmaosTexture = TryGetTexture(lightingPBRMaterial->rmaosTexture);

						roughness = lightingPBRMaterial->GetRoughnessScale();
					}

					// Glow
					if (shaderMaterial->GetFeature() == Feature::kGlowMap) {
						if (const auto* lightingGlowMaterial = skyrim_cast<RE::BSLightingShaderMaterialGlowmap*>(shaderMaterial)) {
							effectTexture = TryGetTexture(lightingGlowMaterial->glowTexture);
						}
					}

					// Hair
					if (shaderMaterial->GetFeature() == Feature::kHairTint) {
						if (const auto* lightingHairTintMaterial = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderMaterial)) {
							baseColor *= float4(lightingHairTintMaterial->tintColor.red, lightingHairTintMaterial->tintColor.green, lightingHairTintMaterial->tintColor.blue, 1.0f);
						}
					}
				} else {
					logger::warn("[RT] BuildMaterial - BSShaderMaterial is nullptr");
				}
			} else {
				logger::warn("[RT] BuildMaterial - BSLightingShaderProperty is nullptr");
			}

			if (auto effectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(effect)) {
				shaderType = RE::BSShader::Type::Effect;

				if (auto shaderMaterial = effectShader->material) {
					if (auto effectMaterial = skyrim_cast<RE::BSEffectShaderMaterial*>(shaderMaterial)) {
						effectType = 1;
						effectColor = { effectMaterial->baseColor.red, effectMaterial->baseColor.green, effectMaterial->baseColor.blue, effectMaterial->baseColorScale };

						baseTexture = TryGetTexture(effectMaterial->sourceTexture);
						effectTexture = TryGetTexture(effectMaterial->greyscaleTexture);
					}
				}
			}
		} else {
			logger::warn("[RT] BuildMaterial - Effect is nullptr");
		}
	}

	if (baseTexture == nullptr)
		logger::warn("[RT] BuildMaterial {} - Base texture is nullptr", name);

	auto& defaultWhiteIndex = rt.defaultWhiteTexture->allocation;
	auto& defaultNormalIndex = rt.defaultNormalTexture->allocation;
	auto& defaultBlackIndex = rt.defaultBlackTexture->allocation;
	auto& defaultRMAOSIndex = rt.defaultRMAOSTexture->allocation;

	auto baseTexReg = rt.GetTextureRegister(baseTexture, defaultWhiteIndex);
	auto normalTexReg = rt.GetTextureRegister(normalTexture, defaultNormalIndex);
	auto effectTexReg = rt.GetTextureRegister(effectTexture, defaultBlackIndex);
	auto rmaosTexReg = rt.GetTextureRegister(rmaosTexture, defaultRMAOSIndex);

	if (baseTexture && baseTexReg->GetIndex() == defaultWhiteIndex->GetIndex())
		logger::warn("[RT] BuildMaterial {} - Base texture [0x{:8X}] not shared", name, reinterpret_cast<uintptr_t>(baseTexture));

	if (normalTexture && normalTexReg->GetIndex() == defaultNormalIndex->GetIndex())
		logger::warn("[RT] BuildMaterial {} - Normal texture [0x{:8X}] not shared", name, reinterpret_cast<uintptr_t>(normalTexture));

	if (effectTexture && effectTexReg->GetIndex() == defaultBlackIndex->GetIndex())
		logger::warn("[RT] BuildMaterial {} - Effect texture [0x{:8X}] not shared", name, reinterpret_cast<uintptr_t>(effectTexture));

	if (rmaosTexture && rmaosTexReg->GetIndex() == defaultRMAOSIndex->GetIndex())
		logger::warn("[RT] BuildMaterial {} - RMAOS texture [0x{:8X}] not shared", name, reinterpret_cast<uintptr_t>(rmaosTexture));

	material = Material(
		baseColor,
		effectColor,
		texCoordOffsetScale,
		roughness,
		baseTexReg,
		normalTexReg,
		effectTexReg,
		rmaosTexReg,
		shaderType);
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
		dynamicPositionBuffer->CreateSRV(skinningHeap->CPUHandle(SkinningHeap::Slot::DynamicVertices, registerIndex->GetIndex()));
	}

	// Vertices
	{
		bool hasUAV = (flags & Flags::Dynamic) || (flags & Flags::Skinned);

		vertexBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Vertex>>(device, vertexCount, hasUAV);

		vertexBuffer->UpdateList(vertices.data(), vertexCount);
		DX::ThrowIfFailed(vertexBuffer->resource->SetName(std::format(L"Vertex Buffer [{}] - {}", registerIndex->GetIndex(), name).c_str()));

		if (vertexCount != vertices.size())
			logger::error("[RT] Shape::CreateBuffers - VertexCount: {}, Vertices Size: {}", vertexCount, vertices.size());

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

			device->CreateUnorderedAccessView(vertexBuffer->resource.get(), nullptr, &uavDesc, skinningHeap->CPUHandle(SkinningHeap::Slot::Output, registerIndex->GetIndex()));
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

			device->CreateShaderResourceView(vertexBuffer->resource.get(), &vbDesc, giHeap->CPUHandle(GIHeap::Slot::Vertices, registerIndex->GetIndex()));
		}
	}

	// Skinning
	if (flags & Flags::Skinned) {
		skinningBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Skinning>>(device, vertexCount);

		skinningBuffer->UpdateList(skinning.data(), vertexCount);
		DX::ThrowIfFailed(skinningBuffer->resource->SetName(std::format(L"Skinning Buffer [{}] - {}", registerIndex->GetIndex(), name).c_str()));

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

			device->CreateShaderResourceView(skinningBuffer->resource.get(), &vbDesc, skinningHeap->CPUHandle(SkinningHeap::Slot::SkinningData, registerIndex->GetIndex()));
		}
	}

	// Triangles
	{
		triangleBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Triangle>>(device, triangleCount);

		triangleBuffer->UpdateList(triangles.data(), triangles.size());
		DX::ThrowIfFailed(triangleBuffer->resource->SetName(std::format(L"Triangle Buffer [{}] - {}", registerIndex->GetIndex(), name).c_str()));

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

			device->CreateShaderResourceView(triangleBuffer->resource.get(), &ibDesc, giHeap->CPUHandle(GIHeap::Slot::Triangles, registerIndex->GetIndex()));
		}
	}

	// Material
	auto materialData = material.GetData();
	materialBuffer->UpdateAt(&materialData, registerIndex->GetIndex());
}

void Shape::CalculateNTB(bool normals)
{
	// Loop over triangles
	for (auto& t : triangles) {
		Vertex& v0 = vertices[t.x];
		Vertex& v1 = vertices[t.y];
		Vertex& v2 = vertices[t.z];

		float3 pos0 = v0.Position;
		float3 pos1 = v1.Position;
		float3 pos2 = v2.Position;

		half2 uv0 = v0.Texcoord0;
		half2 uv1 = v1.Texcoord0;
		half2 uv2 = v2.Texcoord0;

		float3 edge1 = pos1 - pos0;
		float3 edge2 = pos2 - pos0;

		// Optional: compute normals
		if (normals) {
			float3 faceNormal = edge1.Cross(edge2);
			faceNormal.Normalize();
			v0.Normal = v1.Normal = v2.Normal = faceNormal;
		}

		// Compute UV deltas
		float2 deltaUV1 = uv1 - uv0;
		float2 deltaUV2 = uv2 - uv0;

		float f = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
		if (f == 0.0f)
			f = 1.0f;
		f = 1.0f / f;

		// Compute tangent / bitangent
		half3 tangent = half3(f * (deltaUV2.y * edge1 - deltaUV1.y * edge2));
		half3 bitangent = half3(f * (-deltaUV2.x * edge1 + deltaUV1.x * edge2));

		// Accumulate per-vertex
		v0.Tangent += tangent;
		v1.Tangent += tangent;
		v2.Tangent += tangent;

		v0.Bitangent += bitangent;
		v1.Bitangent += bitangent;
		v2.Bitangent += bitangent;
	}

	// Normalize and orthogonalize
	for (auto& v : vertices) {
		float3 n = v.Normal;
		float3 t = float3(v.Tangent.x, v.Tangent.y, v.Tangent.z);
		float3 b = float3(v.Bitangent.x, v.Bitangent.y, v.Bitangent.z);

		// Gram-Schmidt orthogonalization
		t = t - n * n.Dot(t);
		t.Normalize();

		// Recompute bitangent for right-handed tangent space
		b = n.Cross(t);
		b.Normalize();

		v.Tangent = half3(t);
		v.Bitangent = half3(b);
	}
}