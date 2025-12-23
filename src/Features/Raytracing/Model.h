#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "Features/Raytracing.h"

#include "Features/Raytracing/Types.h"

#include "Raytracing/Includes/Types/Vertex.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Material.hlsli"

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Shape.h"
#include "Features/Raytracing/Utils.h"

struct Model
{
	eastl::vector<eastl::unique_ptr<Shape>> shapes;

	winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;
	winrt::com_ptr<ID3D12Resource> blasScratchBuffer = nullptr;

	Model() = default;

	Model(eastl::vector<eastl::unique_ptr<Shape>>& shapes) :
		shapes(eastl::move(shapes))
	{
		for (auto& shape : this->shapes) {
			flags |= shape->flags;
			shaderTypes |= shape->material.ShaderType;
			features |= static_cast<int>(shape->material.Feature);
		}
	}

	Flags GetFlags() const
	{
		return flags;
	}

	uint32_t GetShaderTypes() const
	{
		return shaderTypes;
	}

	auto GetFeatures() const
	{
		return features;
	}

	void AddRef()
	{
		refCount++;
	}

	// Returns true if refCount reaches zero
	bool Release()
	{
		refCount--;
		return refCount <= 0;
	}

private:
	Flags flags = Flags::None;
	uint32_t shaderTypes = RE::BSShader::Type::None;
	int features = static_cast<int>(RE::BSShaderMaterial::Feature::kNone);
	uint refCount;
};
