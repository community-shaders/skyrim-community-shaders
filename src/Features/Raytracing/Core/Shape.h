#pragma once

#include "PCH.h"

#include <d3d12.h>
#include <winrt/base.h>

#include "Features/Raytracing/Allocator.h"
#include "Features/Raytracing/BufferMA.h"
#include "Features/Raytracing/Types.h"
#include "Features/Raytracing/Utils.h"

#include "Features/Raytracing/Core/Material.h"

#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Vertex.hlsli"

class Shape
{
public:
	enum Flags : uint8_t
	{
		None = 0,
		Alpha = 1 << 0,
		Dynamic = 1 << 1,
		Skinned = 1 << 2,
		Landscape = 1 << 3
	};

	enum State : uint8_t
	{
		Hidden = 1 << 0,
		HiddenDismember = 1 << 1
	};

	// The position of this meshes SRV in the register stack
	eastl::unique_ptr<Allocation, AllocationDeleter> allocation;

	uint vertexCount = 0;
	uint triangleCount = 0;
	RE::BSGraphics::Vertex::Flags vertexFlags;

	// Reference to original geometry
	RE::BSGeometry* geometry = nullptr;

	// We could copy straight to buffer and save some (minimal) ram, but keeping a copy allows using memcmp to detect changes
	eastl::vector<float4> dynamicPosition;
	eastl::vector<Vertex> vertices;
	eastl::vector<Skinning> skinning;
	eastl::vector<Triangle> triangles;

	eastl::unique_ptr<DX12::StructuredBufferUploadMA<float4>> dynamicPositionBuffer = nullptr;

	eastl::unique_ptr<DX12::StructuredBufferUploadMA<Vertex>> vertexBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUploadMA<Vertex>> vertexCopyBuffer = nullptr;

	eastl::unique_ptr<DX12::StructuredBufferUploadMA<Skinning>> skinningBuffer = nullptr;
	eastl::unique_ptr<DX12::StructuredBufferUploadMA<Triangle>> triangleBuffer = nullptr;

	eastl::vector<float3x4> boneMatrices;

	Material material;

	Flags flags = Flags::None;

	State state;

	float boundRadius;

	Shape(Allocation* allocation, RE::BSGeometry* geometry, Flags flags = Flags::None) :
		allocation({ allocation, AllocationDeleter() }), geometry(geometry), flags(flags)
	{ }

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

	void BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, [[maybe_unused]] const char* name, RE::FormID formID);

	void CreateBuffers(const std::wstring& name);

	void CalculateVectors(bool calculateNormal);

	bool UpdateDynamicPosition();

	void UpdateUploadDynamicBuffers(ID3D12GraphicsCommandList4* commandList);

	bool UpdateSkinning();

	eastl::shared_ptr<Allocation> TextureRegister(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<Allocation> defaultTexture, bool modelSpaceNormalMap);

	//eastl::vector<float3x4> GetBoneMatrices();

	// For PBR shader flags we need to copy exactly what TruePBR does
	static stl::enumeration<PBRShaderFlags, uint32_t> GetPBRShaderFlags(const BSLightingShaderMaterialPBR* pbrMaterial);
};

DEFINE_ENUM_FLAG_OPERATORS(Shape::Flags);