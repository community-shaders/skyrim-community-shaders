#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "Features/Raytracing.h"

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Types.h"

#include "Features/Raytracing/Core/Shape.h"

#include "Raytracing/Includes/Types/Material.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Vertex.hlsli"

struct Model
{
	eastl::vector<eastl::unique_ptr<Shape>> shapes;

	winrt::com_ptr<D3D12MA::Allocation> blasBuffer = nullptr;
	winrt::com_ptr<D3D12MA::Allocation> blasScratchBuffer = nullptr;

	Model(eastl::vector<eastl::unique_ptr<Shape>>& shapes) :
		shapes(eastl::move(shapes))
	{
		for (auto& shape : this->shapes) {
			flags |= shape->flags;
			shaderTypes |= shape->material.shaderType;
			features |= static_cast<int>(shape->material.Feature);
		}
	}

	Shape::Flags GetFlags() const
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

	bool IsRenderUseValid() const
	{
		for (auto& shape : shapes) {
			if (shape->geometry->GetFlags().any(RE::NiAVObject::Flag::kRenderUse))
				return true;
		}

		return false;
	}

	void AddRef()
	{
		refCount.fetch_add(1, eastl::memory_order_relaxed);
	}

	// Returns refCount
	int Release()
	{
		return refCount.fetch_sub(1, eastl::memory_order_acq_rel) - 1;
	}

private:
	Shape::Flags flags = Shape::Flags::None;
	uint32_t shaderTypes = RE::BSShader::Type::None;
	int features = static_cast<int>(RE::BSShaderMaterial::Feature::kNone);
	eastl::atomic<int> refCount{ 0 };
};
