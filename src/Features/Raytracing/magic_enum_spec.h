#pragma once

#include "PCH.h"

#include "magic_enum/magic_enum.hpp"

namespace magic_enum::customize
{
	template <>
	struct enum_range<RE::NiAVObject::Flag>
	{
		static constexpr bool is_flags = true;
	};

	template <>
	struct enum_range<RE::BSXFlags::Flag>
	{
		static constexpr bool is_flags = true;
	};

	template <>
	struct enum_range<RE::BSShaderProperty::EShaderPropertyFlag>
	{
		static constexpr bool is_flags = true;
	};

	template <>
	struct enum_range<D3D11_RESOURCE_MISC_FLAG>
	{
		static constexpr bool is_flags = true;
	};

	template <>
	struct enum_range<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>
	{
		static constexpr bool is_flags = true;
	};

	template <>
	struct enum_range<Shape::Flags>
	{
		static constexpr bool is_flags = true;
	};
}