#pragma once

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

struct SceneBlend
{
	nlohmann::json states = nlohmann::json::array();

	static bool IsValid(const nlohmann::json& value)
	{
		if (!value.is_object() || value.size() != 1 || !value.contains("states") || !value["states"].is_array())
			return false;
		double total = 0.0;
		for (const auto& state : value["states"]) {
			if (!state.is_object() || !state.contains("value") || !state["value"].is_object() ||
				!state.contains("weight") || !state["weight"].is_number())
				return false;
			const auto weight = state["weight"].get<double>();
			if (!std::isfinite(weight) || weight < 0.0)
				return false;
			total += weight;
		}
		return total <= 1.00001;
	}

	static bool IsBlendable(const nlohmann::json& value)
	{
		return (value.is_number_float() && std::isfinite(value.get<float>())) || IsValid(value);
	}

	static nlohmann::json EmptyLike(const nlohmann::json& value)
	{
		return value.is_number() ? nlohmann::json(0.0f) : nlohmann::json{ { "states", nlohmann::json::array() } };
	}

	static void Accumulate(nlohmann::json& result, const nlohmann::json& value, float weight)
	{
		if (weight <= 0.f)
			return;
		if (result.is_number()) {
			result = result.get<float>() + value.get<float>() * weight;
			return;
		}
		for (const auto& state : value["states"]) {
			const float contribution = state["weight"].get<float>() * weight;
			if (contribution <= 0.f)
				continue;
			auto& states = result["states"];
			auto found = std::find_if(states.begin(), states.end(), [&](const auto& existing) { return existing["value"] == state["value"]; });
			if (found == states.end())
				states.push_back({ { "value", state["value"] }, { "weight", contribution } });
			else
				(*found)["weight"] = (*found)["weight"].template get<float>() + contribution;
		}
	}

	static nlohmann::json Interpolate(const nlohmann::json& from, const nlohmann::json& to, float weight)
	{
		weight = std::clamp(weight, 0.f, 1.f);
		if (weight == 0.f || from == to)
			return from;
		if (weight == 1.f)
			return to;
		if (from.is_number())
			return from.get<float>() + (to.get<float>() - from.get<float>()) * weight;
		auto result = EmptyLike(from);
		Accumulate(result, from, 1.f - weight);
		Accumulate(result, to, weight);
		return result;
	}
};

inline void to_json(nlohmann::json& value, const SceneBlend& blend)
{
	value = { { "states", blend.states } };
}

inline void from_json(const nlohmann::json& value, SceneBlend& blend)
{
	blend.states = SceneBlend::IsValid(value) ? value["states"] : nlohmann::json::array();
}
