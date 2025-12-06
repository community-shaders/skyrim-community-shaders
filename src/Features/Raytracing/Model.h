#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "Features/Raytracing/Types.h"

#include "Raytracing/Includes/Types/Vertex.hlsli"
#include "Raytracing/Includes/Types/Skinning.hlsli"
#include "Raytracing/Includes/Types/Triangle.hlsli"
#include "Raytracing/Includes/Types/Material.hlsli"

#include "Features/Raytracing/Buffer.h"
#include "Features/Raytracing/Utils.h"
#include "Features/Raytracing/Shape.h"

struct Model
{
	eastl::vector<Shape> shapes;
	winrt::com_ptr<ID3D12Resource> blasBuffer = nullptr;

	Model(eastl::vector<Shape>& shapes) :
		shapes(eastl::move(shapes))
	{
		for (auto& shape : shapes) {
			flags |= shape.flags;
		}
	}

	auto GetFlags() const
	{
		return flags;
	}

	bool HasShaderType(RE::BSShader::Type shaderType) const
	{
		for (auto& shape : shapes)
			if (shape.material.ShaderType == shaderType)
				return true;

		return false;
	}

private:
	Flags flags = Flags::None;
};
