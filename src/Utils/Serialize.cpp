#include "Serialize.h"

namespace nlohmann
{
	void to_json(json& j, const float2& v)
	{
		j = json{ v.x, v.y };
	}

	void from_json(const json& j, float2& v)
	{
		std::array<float, 2> temp = j;
		v = { temp[0], temp[1] };
	}

	void to_json(json& j, const float3& v)
	{
		j = json{ v.x, v.y, v.z };
	}

	void from_json(const json& j, float3& v)
	{
		std::array<float, 3> temp = j;
		v = { temp[0], temp[1], temp[2] };
	}

	void to_json(json& j, const float4& v)
	{
		j = json{ v.x, v.y, v.z, v.w };
	}

	void from_json(const json& j, float4& v)
	{
		std::array<float, 4> temp = j;
		v = { temp[0], temp[1], temp[2], temp[3] };
	}

	void to_json(json& j, const ImVec2& v)
	{
		j = json{ v.x, v.y };
	}

	void from_json(const json& j, ImVec2& v)
	{
		std::array<float, 2> temp = j;
		v = { temp[0], temp[1] };
	}

	void to_json(json& j, const ImVec4& v)
	{
		j = json{ v.x, v.y, v.z, v.w };
	}

	void from_json(const json& j, ImVec4& v)
	{
		std::array<float, 4> temp = j;
		v = { temp[0], temp[1], temp[2], temp[3] };
	}

	void to_json(json& section, const RE::NiColor& result)
	{
		section = { result[0],
			result[1],
			result[2] };
	}

	void from_json(const json& section, RE::NiColor& result)
	{
		if (section.is_array() && section.size() == 3 &&
			section[0].is_number_float() && section[1].is_number_float() &&
			section[2].is_number_float()) {
			result[0] = section[0];
			result[1] = section[1];
			result[2] = section[2];
		}
	}

	void to_json(json& j, const RE::TESWeather::FogData& fog)
	{
		j = {
			fog.dayNear,
			fog.dayFar,
			fog.nightNear,
			fog.nightFar,
			fog.dayPower,
			fog.nightPower,
			fog.dayMax,
			fog.nightMax
		};
	}

	void from_json(const json& j, RE::TESWeather::FogData& fog)
	{
		std::array<float, 8> temp = j;
		fog = { j[0],
			j[1],
			j[2],
			j[3],
			j[4],
			j[5],
			j[6],
			j[7] };
	}
}