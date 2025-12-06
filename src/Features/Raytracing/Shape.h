#pragma once

#include "PCH.h"

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Utils.h"

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
	// The position of this meshes SRV in the register stack
	uint16_t registerIndex;

	uint vertexCount = 0;
	uint triangleCount = 0;

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

	winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;
	Material material;
	eastl::vector<RE::BSTriShape*> instances;

	Flags flags = Flags::None;

	Shape(uint16_t registerIndex, Flags flags = Flags::None) :
		registerIndex(registerIndex), flags(flags) {}

	Shape(uint16_t registerIndex, RE::BSGeometry* geometry, Flags flags = Flags::None) :
		registerIndex(registerIndex), geometry(geometry), flags(flags) {}

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
};