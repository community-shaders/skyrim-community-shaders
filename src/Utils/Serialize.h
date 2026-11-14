#pragma once
namespace nlohmann
{
	/** @brief Serialize a float2 to JSON as a two-element array. */
	void to_json(json& j, const float2& v);
	/** @brief Deserialize a float2 from a JSON two-element array. */
	void from_json(const json& j, float2& v);
	/** @brief Serialize a float3 to JSON as a three-element array. */
	void to_json(json& j, const float3& v);
	/** @brief Deserialize a float3 from a JSON three-element array. */
	void from_json(const json& j, float3& v);
	/** @brief Serialize a float4 to JSON as a four-element array. */
	void to_json(json& j, const float4& v);
	/** @brief Deserialize a float4 from a JSON four-element array. */
	void from_json(const json& j, float4& v);

	/** @brief Serialize an ImVec2 to JSON as a two-element array. */
	void to_json(json& j, const ImVec2& v);
	/** @brief Deserialize an ImVec2 from a JSON two-element array. */
	void from_json(const json& j, ImVec2& v);
	/** @brief Serialize an ImVec4 to JSON as a four-element array. */
	void to_json(json& j, const ImVec4& v);
	/** @brief Deserialize an ImVec4 from a JSON four-element array. */
	void from_json(const json& j, ImVec4& v);

	/** @brief Serialize an RE::NiColor to JSON. */
	void to_json(json&, const RE::NiColor&);
	/** @brief Deserialize an RE::NiColor from JSON. */
	void from_json(const json&, RE::NiColor&);

	/** @brief Serialize Skyrim weather fog data to JSON. */
	void to_json(json& j, const RE::TESWeather::FogData& fog);
	/** @brief Deserialize Skyrim weather fog data from JSON. */
	void from_json(const json& j, RE::TESWeather::FogData& fog);
};
namespace Util
{
	/**
	 * @brief Clamps a loaded setting, replacing NaN or infinity with a fallback first.
	 * @param value The value read from JSON.
	 * @param fallback The value used when value is not finite, usually the setting's default.
	 * @param min Lower bound.
	 * @param max Upper bound.
	 * @return The finite value clamped to [min, max].
	 */
	inline float ClampFinite(float value, float fallback, float min, float max) noexcept
	{
		return std::clamp(std::isfinite(value) ? value : fallback, min, max);
	}
}
