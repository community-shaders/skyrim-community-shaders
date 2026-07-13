#include "Serialize.h"

namespace
{
	/** @brief Read exactly N floats from a JSON array into out; leaves out untouched (default kept)
	 *  when the value isn't an N-element numeric array. Prevents nlohmann's array conversion from
	 *  throwing out_of_range/type_error on hand-edited or older-format config values. */
	template <size_t N>
	bool ReadFloatArray(const nlohmann::json& j, std::array<float, N>& out)
	{
		if (!j.is_array() || j.size() < N)
			return false;
		for (size_t i = 0; i < N; ++i) {
			if (!j[i].is_number())
				return false;
			out[i] = j[i].get<float>();
		}
		return true;
	}
}

namespace nlohmann
{
	void to_json(json& j, const float2& v)
	{
		j = json{ v.x, v.y };
	}

	void from_json(const json& j, float2& v)
	{
		std::array<float, 2> temp;
		if (ReadFloatArray(j, temp))
			v = { temp[0], temp[1] };
	}

	void to_json(json& j, const float3& v)
	{
		j = json{ v.x, v.y, v.z };
	}

	void from_json(const json& j, float3& v)
	{
		std::array<float, 3> temp;
		if (ReadFloatArray(j, temp))
			v = { temp[0], temp[1], temp[2] };
	}

	void to_json(json& j, const float4& v)
	{
		j = json{ v.x, v.y, v.z, v.w };
	}

	void from_json(const json& j, float4& v)
	{
		std::array<float, 4> temp;
		if (ReadFloatArray(j, temp))
			v = { temp[0], temp[1], temp[2], temp[3] };
	}

	void to_json(json& j, const ImVec2& v)
	{
		j = json{ v.x, v.y };
	}

	void from_json(const json& j, ImVec2& v)
	{
		std::array<float, 2> temp;
		if (ReadFloatArray(j, temp))
			v = { temp[0], temp[1] };
	}

	void to_json(json& j, const ImVec4& v)
	{
		j = json{ v.x, v.y, v.z, v.w };
	}

	void from_json(const json& j, ImVec4& v)
	{
		std::array<float, 4> temp;
		if (ReadFloatArray(j, temp))
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
		std::array<float, 8> temp;
		if (ReadFloatArray(j, temp))
			fog = { temp[0],
				temp[1],
				temp[2],
				temp[3],
				temp[4],
				temp[5],
				temp[6],
				temp[7] };
	}
}