#pragma once

#include <directxpackedvector.h>

struct half
{
	DirectX::PackedVector::HALF v;

	half() = default;
	half(const half&) = default;
	half& operator=(const half&) = default;

	half(const float& fv)
	{
		v = DirectX::PackedVector::XMConvertFloatToHalf(fv);
	}

	operator float() const
	{
		return DirectX::PackedVector::XMConvertHalfToFloat(v);
	}
};

struct half2
{
	half x;
	half y;

	half2() = default;

	constexpr half2(half _x, half _y) :
		x(_x), y(_y) {}

	half2(float _x, float _y) :
		x(_x), y(_y) {}

	half2(const float2& v) :
		x(v.x), y(v.y) {}

	operator float2() const
	{
		return float2(
			static_cast<float>(x),
			static_cast<float>(y));
	}
};
static_assert(sizeof(half2) == 4);

struct half3
{
	half x;
	half y;
	half z;

	half3() = default;

	constexpr half3(half _x, half _y, half _z) :
		x(_x), y(_y), z(_z) {}

	half3(float _x, float _y, float _z) :
		x(_x), y(_y), z(_z) {}

	half3(const float3& v) :
		x(v.x), y(v.y), z(v.z) {}

	operator float3() const
	{
		return float3(
			static_cast<float>(x),
			static_cast<float>(y),
			static_cast<float>(z));
	}
};
static_assert(sizeof(half3) == 6);