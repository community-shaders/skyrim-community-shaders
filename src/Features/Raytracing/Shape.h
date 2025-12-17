#pragma once

#include "PCH.h"

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Utils.h"
#include "Features/Raytracing/Allocator.h"

#include <d3d12.h>
#include <winrt/base.h>

#include "Features/Raytracing/Types.h"

#include "Raytracing/Includes/Types/Vertex.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Material.hlsli"

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
		half4 BaseColor;
		half4 EffectColor;
		half4 TexCoordOffsetScale;

		half roughness;

		eastl::shared_ptr<Allocation> BaseTexture;
		eastl::shared_ptr<Allocation> NormalTexture;
		eastl::shared_ptr<Allocation> EffectTexture;
		eastl::shared_ptr<Allocation> RMAOSTexture;

		RE::BSShader::Type ShaderType;
		stl::enumeration<PBRShaderFlags, uint16_t> PBRShaderFlags;

		MaterialData GetData() {
			return MaterialData(
				BaseColor, EffectColor, 
				TexCoordOffsetScale,
				roughness,
				BaseTexture->GetIndex(), 
				NormalTexture->GetIndex(), 
				EffectTexture->GetIndex(), 
				RMAOSTexture->GetIndex(), 
				static_cast<uint16_t>(ShaderType),
				PBRShaderFlags.underlying());
		}
	};

	// The position of this meshes SRV in the register stack
	eastl::unique_ptr<Allocation, AllocationDeleter> registerIndex;

	uint vertexCount = 0;
	uint triangleCount = 0;
	RE::BSGraphics::Vertex::Flags vertexFlags;

	// Reference to original geometry
	RE::BSGeometry* geometry = nullptr;

	eastl::vector<float4> dynamicPosition;
	eastl::vector<Vertex> vertices;
	eastl::vector<Skinning> skinning;
	eastl::vector<Triangle> triangles;

	eastl::unique_ptr<DX12::StructuredBufferUpload<float4>> dynamicPositionBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Vertex>> vertexBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Skinning>> skinningBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUpload<Triangle>> triangleBuffer = nullptr;

	Material material;

	Flags flags = Flags::None;

	Shape(Allocation* registerIndex, Flags flags = Flags::None) :
		registerIndex({ registerIndex, AllocationDeleter() }), flags(flags) {}

	Shape(Allocation* registerIndex, RE::BSGeometry* geometry, Flags flags = Flags::None) :
		registerIndex({ registerIndex, AllocationDeleter() }), geometry(geometry), flags(flags) {}

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

	void CalculateNTB(bool normals);

	// For PBR shader flags we need to copy exactly what TruePBR does 
	static stl::enumeration<PBRShaderFlags, uint16_t> GetPBRShaderFlags(const BSLightingShaderMaterialPBR* pbrMaterial);
};