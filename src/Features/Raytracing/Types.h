#pragma once

#include <directxpackedvector.h>

#define FMT_STRUCT(name, ...)                                                        \
	template <>                                                                      \
	struct fmt::formatter<name>                                                      \
	{                                                                                \
		constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); } \
		template <typename FormatContext>                                            \
		auto format(const name& s, FormatContext& ctx) const                         \
		{                                                                            \
			return fmt::format_to(ctx.out(), "{}", s.to_string());                   \
		}                                                                            \
	};

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

	half& operator+=(const half& rhs)
	{
		v = DirectX::PackedVector::XMConvertFloatToHalf(float(*this) + float(rhs));
		return *this;
	}

	half& operator-=(const half& rhs)
	{
		v = DirectX::PackedVector::XMConvertFloatToHalf(float(*this) - float(rhs));
		return *this;
	}

	half& operator*=(const half& rhs)
	{
		v = DirectX::PackedVector::XMConvertFloatToHalf(float(*this) * float(rhs));
		return *this;
	}

	half& operator/=(const half& rhs)
	{
		v = DirectX::PackedVector::XMConvertFloatToHalf(float(*this) / float(rhs));
		return *this;
	}
};
static_assert(sizeof(half) == 2);

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

	std::string to_string() const
	{
		return "[" + std::to_string(x) + ", " + std::to_string(y) + "]";
	}
};
static_assert(sizeof(half2) == 4);

FMT_STRUCT(half2, s.x, s.y)

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

	half3& operator+=(const half3& rhs)
	{
		x += rhs.x;
		y += rhs.y;
		z += rhs.z;
		return *this;
	}

	std::string to_string() const
	{
		return "[" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + "]";
	}
};
static_assert(sizeof(half3) == 6);

FMT_STRUCT(half3, s.x, s.y, s.z)

struct half4
{
	half x;
	half y;
	half z;
	half w;

	half4() = default;

	constexpr half4(half _x, half _y, half _z, half _w) :
		x(_x), y(_y), z(_z), w(_w) {}

	half4(float _x, float _y, float _z, float _w) :
		x(_x), y(_y), z(_z), w(_w) {}

	half4(const float4& v) :
		x(v.x), y(v.y), z(v.z), w(v.w) {}

	operator float4() const
	{
		return float4(
			static_cast<float>(x),
			static_cast<float>(y),
			static_cast<float>(z),
			static_cast<float>(w));
	}

	std::string to_string() const
	{
		return "[" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ", " + std::to_string(w) + "]";
	}
};
static_assert(sizeof(half4) == 8);

FMT_STRUCT(half4, s.x, s.y, s.z, s.w)

struct uint2
{
	uint x;
	uint y;

	bool operator==(const uint2&) const = default;
	bool operator!=(const uint2&) const = default;
};
static_assert(sizeof(uint2) == 8);

struct uint3
{
	uint x;
	uint y;
	uint z;
};
static_assert(sizeof(uint3) == 12);

struct uint4
{
	uint x;
	uint y;
	uint z;
	uint w;
};
static_assert(sizeof(uint4) == 16);

typedef half4 float16_t4;

struct AABB
{
	float3 center;
	float3 size;
	float3 extents;

	static AABB FromMinMax(float3 min, float3 max)
	{
		const float3 size = max - min;
		return AABB((min + max) * 0.5f, size, size * 0.5f);
	}
};