#pragma once

#include "PCH.h"

#include "Features/Raytracing/Allocator.h"
#include "Features/Raytracing/BufferMA.h"
#include "Features/Raytracing/Utils.h"

#include <d3d12.h>
#include <winrt/base.h>

#include "Features/Raytracing/Types.h"

#include "Raytracing/Includes/Types/Material.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Vertex.hlsli"

using namespace magic_enum::bitwise_operators;

enum Flags : uint8_t
{
	None = 0,
	Alpha = 1 << 0,
	Dynamic = 1 << 1,
	Skinned = 1 << 2
};
DEFINE_ENUM_FLAG_OPERATORS(Flags);

class Shape
{
public:
	struct Material
	{
		enum ShaderType : uint16_t
		{
			TruePBR = 0,
			Lighting = 1,
			Effect = 2,
			Grass = 3,
			Water = 4,
			BloodSplatter = 5,
			DistantTree = 6,
			Particle = 7
		};

		// We have a limited number of bits and not all types are necessary
		ShaderType GetShaderType() const
		{
			if (shaderFlags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen))
				return ShaderType::TruePBR;

			switch (shaderType) {
			case RE::BSShader::Type::Grass:
				return ShaderType::Grass;
			case RE::BSShader::Type::Water:
				return ShaderType::Water;
			case RE::BSShader::Type::BloodSplatter:
				return ShaderType::BloodSplatter;
			case RE::BSShader::Type::Effect:
				return ShaderType::Effect;
			case RE::BSShader::Type::DistantTree:
				return ShaderType::DistantTree;
			case RE::BSShader::Type::Particle:
				return ShaderType::Particle;
			default:
				return ShaderType::Lighting;
			}
		}

		enum ShaderFlags : uint16_t
		{
			None = 0,
			kSpecular = 1 << 0,
			kTempRefraction = 1 << 1,
			kVertexAlpha = 1 << 2,
			kGrayscaleToPaletteColor = 1 << 3,
			kGrayscaleToPaletteAlpha = 1 << 4,
			kFalloff = 1 << 5,
			kRefraction = 1 << 6,
			kProjectedUV = 1 << 7,
			kVertexColors = 1 << 8
		};

		ShaderFlags GetShaderFlags() const
		{
			auto shaderFlagsLocal = ShaderFlags::None;

			const auto& entries = magic_enum::enum_entries<ShaderFlags>();
			const auto& originalEntries = magic_enum::enum_entries<RE::BSShaderProperty::EShaderPropertyFlag>();

			for (const auto& [flag, name] : entries) {
				for (const auto& [originalFlag, originalName] : originalEntries) {
					if (shaderFlags.any(originalFlag) && name == originalName) {
						shaderFlagsLocal |= flag;
						break;
					}
				}
			}

			return shaderFlagsLocal;
		}

		half4 BaseColor;
		half4 EffectColor;
		half4 TexCoordOffsetScale;

		half RoughnessScale;
		half SpecularLevel;

		// Vanilla Material Colors
		half4 SpecularColor;

		eastl::shared_ptr<Allocation> BaseTexture;
		eastl::shared_ptr<Allocation> NormalTexture;
		eastl::shared_ptr<Allocation> EffectTexture;
		eastl::shared_ptr<Allocation> RMAOSTexture;

		// Vanilla Material Textures
		eastl::shared_ptr<Allocation> SpecularTexture;
		eastl::shared_ptr<Allocation> EnvTexture;
		eastl::shared_ptr<Allocation> EnvMaskTexture;

		RE::BSShader::Type shaderType;
		REX::EnumSet<RE::BSShaderProperty::EShaderPropertyFlag, std::uint64_t> shaderFlags;
		RE::BSShaderMaterial::Feature Feature;
		stl::enumeration<PBRShaderFlags, uint16_t> PBRFlags;

		MaterialData GetData()
		{
			return MaterialData(
				BaseColor, EffectColor,
				TexCoordOffsetScale,
				RoughnessScale, SpecularLevel,
				SpecularColor,
				BaseTexture->GetIndex(),
				NormalTexture->GetIndex(),
				EffectTexture->GetIndex(),
				RMAOSTexture->GetIndex(),
				SpecularTexture->GetIndex(),
				EnvTexture->GetIndex(),
				EnvMaskTexture->GetIndex(),
				GetShaderType(),
				GetShaderFlags(),
				static_cast<uint16_t>(Feature),
				PBRFlags.underlying());
		}
	};

	// The position of this meshes SRV in the register stack
	eastl::unique_ptr<Allocation, AllocationDeleter> allocation;

	uint vertexCount = 0;
	uint triangleCount = 0;
	RE::BSGraphics::Vertex::Flags vertexFlags;

	// Reference to original geometry
	RE::BSGeometry* geometry = nullptr;

	eastl::vector<float4> dynamicPosition;
	eastl::vector<Vertex> vertices;
	eastl::vector<Skinning> skinning;
	eastl::vector<Triangle> triangles;

	eastl::unique_ptr<DX12::StructuredBufferUploadMA<float4>> dynamicPositionBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUploadMA<Vertex>> vertexBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUploadMA<Skinning>> skinningBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUploadMA<Triangle>> triangleBuffer = nullptr;

	Material material;

	Flags flags = Flags::None;

	/*Shape(Allocation* allocation, Flags flags = Flags::None) :
		allocation({ allocation, AllocationDeleter() }), flags(flags) {}*/

	Shape(Allocation* allocation, RE::BSGeometry* geometry, Flags flags = Flags::None) :
		allocation({ allocation, AllocationDeleter() }), geometry(geometry), flags(flags)
	{
		//logger::info("[RT] Shape {} at Index {}", geometry->name, allocation->GetIndex());
	}

	/*~Shape() {

	};*/

	/*inline Shape Clone(uint16_t registerIndexIn, RE::BSGeometry* geometryIn) const
	{
		auto clone = Shape(registerIndexIn, geometryIn, flags);

		clone.vertexCount = vertexCount;
		clone.triangleCount = triangleCount;

		clone.vertices = vertices;
		clone.skinning = skinning;
		clone.triangles = triangles;

		clone.material = material;

		return clone;
	}*/

	void BuildMesh(RE::BSGraphics::TriShape* rendererData, const std::uint32_t& vertexCountIn, const std::uint16_t& triangleCountIn, const std::uint16_t& bonesPerVertex, const float4x4& transform);

	void BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, [[maybe_unused]] const char* name);

	void CreateBuffers(const std::wstring& name);

	void CalculateVectors(bool calculateNormal);

	// For PBR shader flags we need to copy exactly what TruePBR does
	static stl::enumeration<PBRShaderFlags, uint16_t> GetPBRShaderFlags(const BSLightingShaderMaterialPBR* pbrMaterial);
};