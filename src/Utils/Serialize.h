#pragma once
namespace nlohmann
{
	void to_json(json& j, const float2& v);
	void from_json(const json& j, float2& v);
	void to_json(json& j, const float3& v);
	void from_json(const json& j, float3& v);
	void to_json(json& j, const float4& v);
	void from_json(const json& j, float4& v);

	void to_json(json& j, const ImVec2& v);
	void from_json(const json& j, ImVec2& v);
	void to_json(json& j, const ImVec4& v);
	void from_json(const json& j, ImVec4& v);

	void to_json(json&, const RE::NiColor&);
	void from_json(const json&, RE::NiColor&);

	void to_json(json& j, const RE::TESWeather::FogData& fog);
	void from_json(const json& j, RE::TESWeather::FogData& fog);
};