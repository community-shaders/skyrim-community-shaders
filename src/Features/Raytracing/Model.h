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

#include <shared_mutex>

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

	// Returns refCount
	int Release()
	{
		//std::lock_guard lock{ releaseMutex };

		refCount--;
		return refCount;
	}

private:
	Flags flags = Flags::None;
	uint32_t shaderTypes = RE::BSShader::Type::None;
	int features = static_cast<int>(RE::BSShaderMaterial::Feature::kNone);
	int refCount = 0;

	//std::shared_mutex releaseMutex;
};
