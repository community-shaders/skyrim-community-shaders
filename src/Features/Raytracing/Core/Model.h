#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "State.h"

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Types.h"

#include "Features/Raytracing/Core/Shape.h"

#include "Raytracing/Includes/Types/Material.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Vertex.hlsli"

struct Model
{
	enum Flags {
		BLASUpdate = 1 << 0,
		BLASRebuild	= 1 << 1	
	};

	eastl::vector<eastl::unique_ptr<Shape>> shapes;

	winrt::com_ptr<D3D12MA::Allocation> blasBuffer = nullptr;
	winrt::com_ptr<D3D12MA::Allocation> blasScratchBuffer = nullptr;

	Flags flags;

	Model(eastl::vector<eastl::unique_ptr<Shape>>& shapes) :
		shapes(eastl::move(shapes))
	{
		for (auto& shape : this->shapes) {
			shapeflags |= shape->flags;
			shaderTypes |= shape->material.shaderType;
			features |= static_cast<int>(shape->material.Feature);
			shaderFlags.set(shape->material.shaderFlags.get());
		}
	}

	static std::string KeySuffix(RE::NiAVObject* root)
	{
		return std::format("_{:08X}", reinterpret_cast<uintptr_t>(root));
	}

	Shape::Flags GetFlags() const
	{
		return shapeflags;
	}

	uint32_t GetShaderTypes() const
	{
		return shaderTypes;
	}

	auto GetFeatures() const
	{
		return features;
	}


	auto GetShaderFlags() const
	{
		return shaderFlags;
	}

	bool ShouldQueueMSNConversion() const
	{
		for (auto& shape : shapes) {
			if (shape->material.shaderFlags.any(RE::BSShaderProperty::EShaderPropertyFlag::kModelSpaceNormals))
				return true;
		}

		return false;
	}

	void ConvertMSN();

	bool BLASBuildExecuted() const;

	bool BLASUpdateExecuted() const;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags() const
	{
		if ((shapeflags & Shape::Flags::Skinned) || (shapeflags & Shape::Flags::Dynamic))
			return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;

		return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	}


	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS UpdateFlags(bool rebuild) const
	{
		if (rebuild)
			return BuildFlags();

		return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	}

	void BuildBLAS(ID3D12GraphicsCommandList4* commandList);

	bool UpdateBLAS(ID3D12GraphicsCommandList4* commandList);

	bool HideShape([[maybe_unused]]Shape* shape) const
	{
		return BLASBuildExecuted() && shape->IsHidden();
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
	Shape::Flags shapeflags = Shape::Flags::None;
	uint32_t shaderTypes = RE::BSShader::Type::None;
	int features = static_cast<int>(RE::BSShaderMaterial::Feature::kNone);
	REX::EnumSet<RE::BSShaderProperty::EShaderPropertyFlag, std::uint64_t> shaderFlags;
	uint64_t blasBuildFrame;
	uint64_t blasUpdateFrame;
	eastl::atomic<int> refCount{ 0 };
};
