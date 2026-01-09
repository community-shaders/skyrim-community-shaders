#include "Shape.h"
#include "Features/Raytracing.h"
#include "Features/Raytracing/Heap.h"
#include "TruePBR.h"
#include "TruePBR/BSLightingShaderMaterialPBR.h"
#include "TruePBR/BSLightingShaderMaterialPBRLandscape.h"

using GIHeap = Raytracing::GIHeap;
using SkinningHeap = Raytracing::SkinningHeap;

static std::uint32_t GetVertexSize(RE::BSGraphics::Vertex::Flags flags, bool land)
{
	using RE::BSGraphics::Vertex;

	std::uint32_t vertexSize = 0;

	if (flags & Vertex::VF_VERTEX) {
		vertexSize += sizeof(float) * 4;
	}
	if (flags & Vertex::VF_UV) {
		vertexSize += sizeof(std::uint16_t) * 2;
	}
	if (flags & Vertex::VF_UV_2) {
		vertexSize += sizeof(std::uint16_t) * 2;
	}
	if (flags & Vertex::VF_NORMAL) {
		vertexSize += sizeof(std::uint16_t) * 2;
		if (flags & Vertex::VF_TANGENT) {
			vertexSize += sizeof(std::uint16_t) * 2;
		}
	}
	if (flags & Vertex::VF_COLORS) {
		vertexSize += sizeof(std::uint8_t) * 4;
	}
	if (flags & Vertex::VF_SKINNED) {
		vertexSize += sizeof(std::uint16_t) * 4 + sizeof(std::uint8_t) * 4;
	}
	if (flags & Vertex::VF_EYEDATA) {
		vertexSize += sizeof(float);
	}
	if (flags & Vertex::VF_LANDDATA) {
		vertexSize += sizeof(uint32_t) * 2;
	}

	return vertexSize;
}

static std::string PrintVertexFlags(uint16_t value)
{
	using RE::BSGraphics::Vertex;

	std::string result;
	if (value & Vertex::Flags::VF_VERTEX)
		result += "VF_VERTEX ";

	if (value & Vertex::Flags::VF_UV)
		result += "VF_UV ";

	if (value & Vertex::Flags::VF_UV_2)
		result += "VF_UV_2 ";

	if (value & Vertex::Flags::VF_NORMAL)
		result += "VF_NORMAL ";

	if (value & Vertex::Flags::VF_TANGENT)
		result += "VF_TANGENT ";

	if (value & Vertex::Flags::VF_COLORS)
		result += "VF_COLORS ";

	if (value & Vertex::Flags::VF_SKINNED)
		result += "VF_SKINNED ";

	if (value & Vertex::Flags::VF_LANDDATA)
		result += "VF_LANDDATA ";

	if (value & Vertex::Flags::VF_EYEDATA)
		result += "VF_EYEDATA ";

	if (value & Vertex::Flags::VF_INSTANCEDATA)
		result += "VF_INSTANCEDATA ";

	if (value & Vertex::Flags::VF_FULLPREC)
		result += "VF_FULLPREC ";

	return result;
}

void Shape::BuildMesh(RE::BSGraphics::TriShape* rendererData, const std::uint32_t& vertexCountIn, const std::uint16_t& triangleCountIn, const std::uint16_t& bonesPerVertex, const float4x4& transform)
{
	auto vertexDesc = rendererData->vertexDesc;

	vertexFlags = vertexDesc.GetFlags();

	bool hasNormal = vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL;
	bool hasBitangent = vertexFlags & RE::BSGraphics::Vertex::VF_TANGENT;

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

		auto vertexSize = GetVertexSize(vertexFlags, flags & Flags::Landscape);

		bool hasPosition = vertexFlags & RE::BSGraphics::Vertex::VF_VERTEX;

		uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
		uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
		uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
		uint32_t tangOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_BINORMAL);
		uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);
		uint32_t skinOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
		uint32_t landOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_LANDDATA);

		uint32_t boneIDOffset = sizeof(uint16_t) * bonesPerVertex;

		eastl::vector<half> weights(bonesPerVertex);
		eastl::vector<uint8_t> boneIds(bonesPerVertex);

		for (uint16_t i = 0; i < vertexCountIn; i++) {
			uint8_t* vtx = rendererData->rawVertexData + i * vertexSize;

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

				if (hasBitangent) {
					uint32_t bitangentData;
					std::memcpy(&bitangentData, vtx + tangOffset, sizeof(uint32_t));
					auto bitangentUnpacked = UnpackByte4(bitangentData);

					vertexData.Bitangent = Normalize(float3::TransformNormal({ bitangentUnpacked.x, bitangentUnpacked.y, bitangentUnpacked.z }, transform));

					float3 tangent = { pos.w, normalUnpacked.w, bitangentUnpacked.w };
					vertexData.Tangent = hasPosition ? Normalize(float3::TransformNormal(tangent, transform)) : tangent;
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

			if (vertexFlags & RE::BSGraphics::Vertex::VF_LANDDATA) {
				std::memcpy(&vertexData.LandBlend0, vtx + landOffset, sizeof(uint32_t));
				std::memcpy(&vertexData.LandBlend1, vtx + landOffset + sizeof(uint32_t), sizeof(uint32_t));
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
		// Landscape contains no triangles, so we build them ourselves
		if (flags & Flags::Landscape && !rendererData->rawIndexData) {
			triangles.reserve(triangleCountIn);

			constexpr uint16_t GRID_SIZE = 16;
			constexpr uint16_t VERTICES = GRID_SIZE + 1;

			for (uint16_t y = 0; y < GRID_SIZE; y++) {
				for (uint16_t x = 0; x < GRID_SIZE; x++) {
					uint16_t v0 = y * VERTICES + x;
					uint16_t v1 = v0 + 1;
					uint16_t v2 = v0 + VERTICES;
					uint16_t v3 = v2 + 1;

					if (v0 >= vertexCount || v1 >= vertexCount || v2 >= vertexCount)
						logger::critical("[RT] Quad {} {} vertex overflow: [{}, {}, {}]", x, y, v0, v1, v2);

					// First triangle
					triangles.emplace_back(v0, v1, v2);

					// Second triangle
					triangles.emplace_back(v1, v3, v2);
				}
			}
		} else {
			triangles.resize(triangleCountIn);

			const auto indexCount = triangleCountIn * 3;

			eastl::vector<uint16_t> indices(indexCount);
			std::memcpy(indices.data(), rendererData->rawIndexData, sizeof(uint16_t) * indexCount);

			for (uint16_t t = 0; t < triangleCountIn; ++t) {
				uint16_t i = t * 3u;

				uint16_t v0 = indices[i];
				uint16_t v1 = indices[i + 1u];
				uint16_t v2 = indices[i + 2u];

				if (v0 >= vertexCount || v1 >= vertexCount || v2 >= vertexCount)
					logger::critical("[RT] Triangle {} vertex overflow: [{}, {}, {}]", t, v0, v1, v2);

				triangles[t] = Triangle(v0, v1, v2);
			}
		}

		triangleCount = triangleCountIn;
	}

	if (!hasNormal || !hasBitangent) {
		CalculateVectors(!hasNormal);
	}
}

static eastl::shared_ptr<Allocation> TextureRegister(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<Allocation> defaultTexture)
{
	if (!niPointer || !niPointer->rendererTexture)
		return defaultTexture;

	return globals::features::raytracing.GetTextureRegister(niPointer->rendererTexture->texture, defaultTexture);
}

void Shape::BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, [[maybe_unused]] const char* name)
{
	auto& rt = globals::features::raytracing;
	auto& whiteTexture = rt.defaultWhiteTexture->allocation;
	auto& normalTexture = rt.defaultNormalTexture->allocation;
	auto& blackTexture = rt.defaultBlackTexture->allocation;
	auto& rmaosTexture = rt.defaultRMAOSTexture->allocation;

	using State = RE::BSGeometry::States;
	using Feature = RE::BSShaderMaterial::Feature;
	using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;

	eastl::array<half4, 2> colors = {
		float4(1.0f, 1.0f, 1.0f, 1.0f),
		float4(0.0f, 0.0f, 0.0f, 0.0f)
	};

	eastl::array<half, 3> scalars;
	scalars.fill(0.0f);

	eastl::array<half4, 2> texCoordOffsetScales = {
		float4(0.0f, 0.0f, 1.0f, 1.0f),
		float4(0.0f, 0.0f, 1.0f, 1.0f)
	};

	eastl::array<eastl::shared_ptr<Allocation>, 20> textures;
	textures.fill(blackTexture);

	RE::BSShader::Type shaderType = RE::BSShader::Type::None;
	REX::EnumSet<EShaderPropertyFlag, std::uint64_t> shaderFlags;
	RE::BSShaderMaterial::Feature feature = RE::BSShaderMaterial::Feature::kNone;
	stl::enumeration<PBRShaderFlags, uint32_t> pbrFlags;

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
			if (auto shaderProp = netimmerse_cast<RE::BSShaderProperty*>(effect)) {
				shaderFlags = shaderProp->flags.get();
			}

			if (auto* lightingShaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(effect)) {
				shaderType = RE::BSShader::Type::Lighting;

				logger::debug("[RT] BuildMaterial - [Effect] BSLightingShaderProperty Flags: {}", GetFlagsString<EShaderPropertyFlag>(lightingShaderProp->flags.underlying()));

				// This is always nullptr :(
				if (auto& effectData = lightingShaderProp->effectData) {
					logger::info("[RT] BuildMaterial - Effect - Alpha: {}, Z Test Func: {}", effectData->alpha, magic_enum::enum_name(effectData->zTestFunc));
				}

				colors[1] = {
					lightingShaderProp->emissiveColor->red,
					lightingShaderProp->emissiveColor->green,
					lightingShaderProp->emissiveColor->blue,
					lightingShaderProp->emissiveMult
				};

				if (auto shaderMaterial = lightingShaderProp->material) {
					feature = shaderMaterial->GetFeature();

					for (size_t i = 0; i < 2; i++) {
						texCoordOffsetScales[i] = {
							shaderMaterial->texCoordOffset[i].x, shaderMaterial->texCoordOffset[i].y,
							shaderMaterial->texCoordScale[i].x, shaderMaterial->texCoordScale[i].y
						};
					}

					// Landscape
					if (const auto* lightingBaseMaterialLand = skyrim_cast<RE::BSLightingShaderMaterialLandscape*>(shaderMaterial)) {
						textures[0] = TextureRegister(lightingBaseMaterialLand->diffuseTexture, whiteTexture);
						textures[Material::MAX_PBRLAND_TEXTURES] = TextureRegister(lightingBaseMaterialLand->normalTexture, whiteTexture);

						for (uint i = 0; i < std::min(lightingBaseMaterialLand->numLandscapeTextures, Material::MAX_LAND_TEXTURES); i++) {
							textures[i + 1] = TextureRegister(lightingBaseMaterialLand->landscapeDiffuseTexture[i], whiteTexture);
							textures[Material::MAX_PBRLAND_TEXTURES + i + 1] = TextureRegister(lightingBaseMaterialLand->landscapeNormalTexture[i], normalTexture);
						}

						textures[Material::MAX_PBRLAND_TEXTURES * 3] = TextureRegister(lightingBaseMaterialLand->terrainOverlayTexture, blackTexture);
						textures[Material::MAX_PBRLAND_TEXTURES * 3 + 1] = TextureRegister(lightingBaseMaterialLand->terrainNoiseTexture, blackTexture);
					} else if (typeid(*shaderMaterial) == typeid(BSLightingShaderMaterialPBRLandscape)) {
						const auto* lightingPBRMaterialLand = static_cast<BSLightingShaderMaterialPBRLandscape*>(shaderMaterial);

						for (uint i = 0; i < std::min(lightingPBRMaterialLand->numLandscapeTextures, Material::MAX_PBRLAND_TEXTURES); i++) {
							textures[i] = TextureRegister(lightingPBRMaterialLand->landscapeBaseColorTextures[i], whiteTexture);
							textures[Material::MAX_PBRLAND_TEXTURES + i] = TextureRegister(lightingPBRMaterialLand->landscapeNormalTextures[i], normalTexture);
							textures[Material::MAX_PBRLAND_TEXTURES * 2 + i] = TextureRegister(lightingPBRMaterialLand->landscapeRMAOSTextures[i], rmaosTexture);
						}

						textures[Material::MAX_PBRLAND_TEXTURES * 3] = TextureRegister(lightingPBRMaterialLand->terrainOverlayTexture, blackTexture);
						textures[Material::MAX_PBRLAND_TEXTURES * 3 + 1] = TextureRegister(lightingPBRMaterialLand->terrainNoiseTexture, blackTexture);
					} else if (typeid(*shaderMaterial) == typeid(BSLightingShaderMaterialPBR)) {
						// TrueBR - Tried to check for 'lightingShaderProp->flags.any(EShaderPropertyFlag::kMenuScreen)'
						// but it did not work at all, skyrim_cast is not safe and will cast even if not PBR material (no RTTI?)

						const auto* lightingPBRMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

						// Does this work?
						textures[0] = TextureRegister(lightingPBRMaterial->diffuseTexture, whiteTexture);
						textures[1] = TextureRegister(lightingPBRMaterial->normalTexture, normalTexture);

						textures[2] = TextureRegister(lightingPBRMaterial->emissiveTexture, blackTexture);
						textures[3] = TextureRegister(lightingPBRMaterial->rmaosTexture, rmaosTexture);

						scalars[0] = lightingPBRMaterial->GetRoughnessScale();
						scalars[1] = lightingPBRMaterial->GetSpecularLevel();

						pbrFlags = GetPBRShaderFlags(lightingPBRMaterial);

						// Enforce TruePBR flag
						shaderFlags.set(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);
					} else {
						// Roughness Scale
						scalars[0] = 1.0f;

						// Specular Level
						scalars[1] = 0.04f;

						// Vanilla Materials
						if (const auto* lightingBaseMaterial = skyrim_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial)) {
							if (shaderFlags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular)) {
								textures[0] = TextureRegister(lightingBaseMaterial->diffuseTexture, whiteTexture);
								textures[1] = TextureRegister(lightingBaseMaterial->normalTexture, normalTexture);
								textures[3] = TextureRegister(lightingBaseMaterial->specularBackLightingTexture, blackTexture);

								colors[1] = {
									lightingBaseMaterial->specularColor.red,
									lightingBaseMaterial->specularColor.green,
									lightingBaseMaterial->specularColor.blue,
									lightingBaseMaterial->specularColorScale
								};

								scalars[0] = ShininessToRoughness(lightingBaseMaterial->specularPower);
							}
						}

						// Envmap
						if (feature == Feature::kEnvironmentMap || feature == Feature::kEye) {
							if (const auto* lightingEnvmapMaterial = skyrim_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial)) {
								textures[4] = TextureRegister(lightingEnvmapMaterial->envTexture, blackTexture);
								textures[5] = TextureRegister(lightingEnvmapMaterial->envMaskTexture, blackTexture);
							}
						}

						// Glow
						if (feature == Feature::kGlowMap) {
							if (const auto* lightingGlowMaterial = skyrim_cast<RE::BSLightingShaderMaterialGlowmap*>(shaderMaterial)) {
								textures[2] = TextureRegister(lightingGlowMaterial->glowTexture, blackTexture);
							}
						}

						// Hair
						if (feature == Feature::kHairTint) {
							if (const auto* lightingHairTintMaterial = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderMaterial)) {
								colors[0] = float4(lightingHairTintMaterial->tintColor.red, lightingHairTintMaterial->tintColor.green, lightingHairTintMaterial->tintColor.blue, 1.0f);
							}
						}
					}
				} else {
					logger::warn("[RT] BuildMaterial - BSShaderMaterial is nullptr");
				}
			}

			if (auto effectShaderProp = netimmerse_cast<RE::BSEffectShaderProperty*>(effect)) {
				shaderType = RE::BSShader::Type::Effect;

				logger::debug("[RT] BuildMaterial - BSEffectShaderProperty: {}", name);
				logger::debug("[RT] BuildMaterial - Flags: {}", GetFlagsString<RE::BSShaderProperty::EShaderPropertyFlag>(effectShaderProp->flags.underlying()));

				if (auto effectMaterial = skyrim_cast<RE::BSEffectShaderMaterial*>(effectShaderProp->material)) {
					colors[1] = {
						effectMaterial->baseColor.red,
						effectMaterial->baseColor.green,
						effectMaterial->baseColor.blue,
						effectMaterial->baseColorScale
					};

					textures[0] = TextureRegister(effectMaterial->sourceTexture, blackTexture);
					textures[2] = TextureRegister(effectMaterial->greyscaleTexture, blackTexture);
				}
			}
		}
	}

	material = Material(
		shaderFlags,
		shaderType,
		feature,
		pbrFlags,
		colors,
		scalars,
		texCoordOffsetScales,
		textures);
}

stl::enumeration<PBRShaderFlags, uint32_t> Shape::GetPBRShaderFlags(const BSLightingShaderMaterialPBR* pbrMaterial)
{
	auto graphicsState = globals::game::graphicsState;

	stl::enumeration<PBRShaderFlags, uint32_t> pbrFlags;

	if (pbrMaterial->pbrFlags.any(PBRFlags::TwoLayer)) {
		pbrFlags.set(PBRShaderFlags::TwoLayer);
		if (pbrMaterial->pbrFlags.any(PBRFlags::InterlayerParallax)) {
			pbrFlags.set(PBRShaderFlags::InterlayerParallax);
		}
		if (pbrMaterial->pbrFlags.any(PBRFlags::CoatNormal)) {
			pbrFlags.set(PBRShaderFlags::CoatNormal);
		}
		if (pbrMaterial->pbrFlags.any(PBRFlags::ColoredCoat)) {
			pbrFlags.set(PBRShaderFlags::ColoredCoat);
		}
	} else if (pbrMaterial->pbrFlags.any(PBRFlags::HairMarschner)) {
		pbrFlags.set(PBRShaderFlags::HairMarschner);
	} else {
		if (pbrMaterial->pbrFlags.any(PBRFlags::Subsurface)) {
			pbrFlags.set(PBRShaderFlags::Subsurface);
		}
		if (pbrMaterial->pbrFlags.any(PBRFlags::Fuzz)) {
			pbrFlags.set(PBRShaderFlags::Fuzz);
		} else {
			if (pbrMaterial->GetGlintParameters().enabled) {
				pbrFlags.set(PBRShaderFlags::Glint);
			}

			// This is slimmed down because we don't have access to lightingFlags
			if (pbrMaterial->GetProjectedMaterialGlintParameters().enabled) {
				pbrFlags.set(PBRShaderFlags::ProjectedGlint);
			}
		}
	}

	const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != graphicsState->GetRuntimeData().defaultTextureBlack;
	if (hasEmissive) {
		pbrFlags.set(PBRShaderFlags::HasEmissive);
	}

	const bool hasDisplacement = pbrMaterial->displacementTexture != nullptr && pbrMaterial->displacementTexture != graphicsState->GetRuntimeData().defaultTextureBlack;
	if (hasDisplacement) {
		pbrFlags.set(PBRShaderFlags::HasDisplacement);
	}

	const bool hasFeaturesTexture0 = pbrMaterial->featuresTexture0 != nullptr && pbrMaterial->featuresTexture0 != graphicsState->GetRuntimeData().defaultTextureWhite;
	if (hasFeaturesTexture0) {
		pbrFlags.set(PBRShaderFlags::HasFeaturesTexture0);
	}

	const bool hasFeaturesTexture1 = pbrMaterial->featuresTexture1 != nullptr && pbrMaterial->featuresTexture1 != graphicsState->GetRuntimeData().defaultTextureWhite;
	if (hasFeaturesTexture1) {
		pbrFlags.set(PBRShaderFlags::HasFeaturesTexture1);
	}

	return pbrFlags;
}

void Shape::CreateBuffers(const std::wstring& name)
{
	auto& rt = globals::features::raytracing;
	auto* device = rt.d3d12Device.get();
	auto* commandList = rt.commandList.get();

	auto* skinningHeap = rt.skinningHeap.get();
	auto* giHeap = rt.giHeap.get();

	auto* materialBuffer = rt.materialBuffer.get();

	D3D12MA::ALLOCATION_DESC allocDesc = { .HeapType = D3D12_HEAP_TYPE_DEFAULT };

	D3D12MA::ALLOCATION_DESC uploadAllocDesc = { .HeapType = D3D12_HEAP_TYPE_UPLOAD };
	uploadAllocDesc.CustomPool = rt.uploadPool.get();

	auto allocator = rt.allocator.get();

	std::lock_guard lock{ rt.renderMutex };

	// Dynamic
	if (flags & Flags::Dynamic) {
		// Not really a buffer but we need to initialize it somewhere
		dynamicPosition.resize(vertexCount);

		allocDesc.CustomPool = rt.dynamicVertexPool.get();
		dynamicPositionBuffer = eastl::make_unique<DX12::StructuredBufferUploadMA<float4>>(device, allocator, allocDesc, uploadAllocDesc, vertexCount);

		dynamicPositionBuffer->CreateSRV(skinningHeap->CPUHandle(SkinningHeap::Slot::DynamicVertices, allocation->GetIndex()));
	}

	// Vertices
	{
		bool hasUAV = (flags & Flags::Dynamic) || (flags & Flags::Skinned);

		allocDesc.CustomPool = rt.vertexPool.get();
		vertexBuffer = eastl::make_unique<DX12::StructuredBufferUploadMA<Vertex>>(device, allocator, allocDesc, uploadAllocDesc, vertexCount, hasUAV);

		vertexBuffer->UpdateList(vertices.data(), vertexCount);
		DX::ThrowIfFailed(vertexBuffer->resource->SetName(std::format(L"Vertex Buffer [{}] - {}", allocation->GetIndex(), name).c_str()));

		if (vertexCount != vertices.size())
			logger::error("[RT] Shape::CreateBuffers - VertexCount: {}, Vertices Size: {}", vertexCount, vertices.size());

		vertexBuffer->Upload(commandList);

		vertexBuffer->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		// UAV
		if (hasUAV) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = vertexCount;
			uavDesc.Buffer.StructureByteStride = sizeof(Vertex);
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

			device->CreateUnorderedAccessView(vertexBuffer->resource.get(), nullptr, &uavDesc, skinningHeap->CPUHandle(SkinningHeap::Slot::Output, allocation->GetIndex()));
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

			device->CreateShaderResourceView(vertexBuffer->resource.get(), &vbDesc, giHeap->CPUHandle(GIHeap::Slot::Vertices, allocation->GetIndex()));
		}
	}

	// Skinning
	if (flags & Flags::Skinned) {
		allocDesc.CustomPool = rt.skinningPool.get();
		skinningBuffer = eastl::make_unique<DX12::StructuredBufferUploadMA<Skinning>>(device, allocator, allocDesc, uploadAllocDesc, vertexCount);

		skinningBuffer->UpdateList(skinning.data(), vertexCount);
		DX::ThrowIfFailed(skinningBuffer->resource->SetName(std::format(L"Skinning Buffer [{}] - {}", allocation->GetIndex(), name).c_str()));

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

			device->CreateShaderResourceView(skinningBuffer->resource.get(), &vbDesc, skinningHeap->CPUHandle(SkinningHeap::Slot::SkinningData, allocation->GetIndex()));
		}
	}

	// Triangles
	{
		allocDesc.CustomPool = rt.trianglePool.get();
		triangleBuffer = eastl::make_unique<DX12::StructuredBufferUploadMA<Triangle>>(device, allocator, allocDesc, uploadAllocDesc, triangleCount);

		triangleBuffer->UpdateList(triangles.data(), triangles.size());
		DX::ThrowIfFailed(triangleBuffer->resource->SetName(std::format(L"Triangle Buffer [{}] - {}", allocation->GetIndex(), name).c_str()));

		triangleBuffer->Upload(commandList);

		triangleBuffer->TransitionBarrier(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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

			device->CreateShaderResourceView(triangleBuffer->resource.get(), &ibDesc, giHeap->CPUHandle(GIHeap::Slot::Triangles, allocation->GetIndex()));
		}
	}

	// Material
	auto materialData = material.GetData();
	materialBuffer->UpdateAt(&materialData, allocation->GetIndex());
}

void Shape::CalculateVectors(bool calculateNormal)
{
	eastl::vector<float3> normals;

	if (calculateNormal)
		normals.resize(vertexCount);

	eastl::vector<float3> tangents(vertexCount);
	eastl::vector<float3> bitangents(vertexCount);

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

		// Optionaly compute normals
		if (calculateNormal) {
			float3 faceNormal = edge1.Cross(edge2);

			normals[t.x] += faceNormal;
			normals[t.y] += faceNormal;
			normals[t.z] += faceNormal;
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
		tangents[t.x] += tangent;
		tangents[t.y] += tangent;
		tangents[t.z] += tangent;

		bitangents[t.x] += bitangent;
		bitangents[t.y] += bitangent;
		bitangents[t.z] += bitangent;
	}

	// Normalize and orthogonalize
	for (size_t i = 0; i < vertexCount; i++) {
		auto& v = vertices[i];

		float3 n = calculateNormal ? normals[i] : float3(v.Normal);
		float3 t = tangents[i];
		float3 b = bitangents[i];

		n.Normalize();

		// Handle missing tangents (planar / degenerate UVs)
		if (t.Length() < 1e-6f) {
			float3 up = (fabs(n.z) < 0.999f) ? float3(0, 0, 1) : float3(0, 1, 0);
			t = up.Cross(n);
		} else {
			// Gram-Schmidt orthogonalization
			t = t - n * n.Dot(t);
		}

		t.Normalize();

		float handedness = (n.Cross(t).Dot(b) < 0.0f) ? -1.0f : 1.0f;

		// Recompute bitangent
		b = n.Cross(t) * handedness;
		b.Normalize();

		v.Normal = n;
		v.Tangent = t;
		v.Bitangent = b;
	}
}