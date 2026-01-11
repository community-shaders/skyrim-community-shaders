#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using json = nlohmann::json;

// Weather variable system - features register variables which are automatically handled by weather system
namespace WeatherVariables
{
	// Base class for weather-controllable variables
	class IWeatherVariable
	{
	public:
		virtual ~IWeatherVariable() = default;
		virtual void Lerp(const json& from, const json& to, float factor) = 0;
		virtual void SaveToJson(json& j) const = 0;
		virtual void LoadFromJson(const json& j) = 0;
		virtual void SetToDefault() = 0;
		virtual std::string GetName() const = 0;
		virtual std::string GetDisplayName() const = 0;
		virtual std::string GetTooltip() const = 0;
	};

	// Templated weather variable for type safety
	template <typename T>
	class WeatherVariable : public IWeatherVariable
	{
	public:
		WeatherVariable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			T* valuePtr, T defaultValue,
			std::function<T(const T&, const T&, float)> lerpFunc = nullptr) :
			name(name),
			displayName(displayName), tooltip(tooltip), valuePtr(valuePtr), defaultValue(defaultValue), lerpFunc(lerpFunc)
		{
			if (!lerpFunc) {
				// Default lerp for float types
				if constexpr (std::is_floating_point_v<T>) {
					this->lerpFunc = [](const T& from, const T& to, float factor) {
						return static_cast<T>(std::lerp(from, to, factor));
					};
				}
			}
		}

		void Lerp(const json& from, const json& to, float factor) override
		{
			if (!valuePtr || !lerpFunc)
				return;

			T fromVal = defaultValue;
			T toVal = defaultValue;

			if (!from.is_null()) {
				try {
					fromVal = from.get<T>();
				} catch (const nlohmann::json::type_error& e) {
					logger::debug("Type error in Lerp 'from' for {}: {}", name, e.what());
					fromVal = defaultValue;
				}
			}

			if (!to.is_null()) {
				try {
					toVal = to.get<T>();
				} catch (const nlohmann::json::type_error& e) {
					logger::debug("Type error in Lerp 'to' for {}: {}", name, e.what());
					toVal = defaultValue;
				}
			}

			*valuePtr = lerpFunc(fromVal, toVal, factor);
		}

		void SaveToJson(json& j) const override
		{
			if (valuePtr) {
				j[name] = *valuePtr;
			}
		}

		void LoadFromJson(const json& j) override
		{
			if (valuePtr && j.contains(name)) {
				try {
					*valuePtr = j[name].get<T>();
				} catch (const nlohmann::json::type_error& e) {
					logger::debug("Type error in LoadFromJson for {}: {}", name, e.what());
					*valuePtr = defaultValue;
				}
			}
		}

		void SetToDefault() override
		{
			if (valuePtr) {
				*valuePtr = defaultValue;
			}
		}

		std::string GetName() const override { return name; }
		std::string GetDisplayName() const override { return displayName; }
		std::string GetTooltip() const override { return tooltip; }

	private:
		std::string name;
		std::string displayName;
		std::string tooltip;
		T* valuePtr;
		T defaultValue;
		std::function<T(const T&, const T&, float)> lerpFunc;
	};

	// Specialized weather variables for common types
	class FloatVariable : public WeatherVariable<float>
	{
	public:
		FloatVariable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			float* valuePtr, float defaultValue, float minValue = 0.0f, float maxValue = 1.0f) :
			WeatherVariable<float>(name, displayName, tooltip, valuePtr, defaultValue),
			minValue(minValue), maxValue(maxValue)
		{
		}

		float GetMin() const { return minValue; }
		float GetMax() const { return maxValue; }

	private:
		float minValue;
		float maxValue;
	};

	class Float3Variable : public WeatherVariable<float3>
	{
	public:
		Float3Variable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			float3* valuePtr, float3 defaultValue) :
			WeatherVariable<float3>(name, displayName, tooltip, valuePtr, defaultValue,
				[](const float3& from, const float3& to, float factor) {
					return float3{
						std::lerp(from.x, to.x, factor),
						std::lerp(from.y, to.y, factor),
						std::lerp(from.z, to.z, factor)
					};
				})
		{
		}
	};

	class Float4Variable : public WeatherVariable<float4>
	{
	public:
		Float4Variable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			float4* valuePtr, float4 defaultValue) :
			WeatherVariable<float4>(name, displayName, tooltip, valuePtr, defaultValue,
				[](const float4& from, const float4& to, float factor) {
					return float4{
						std::lerp(from.x, to.x, factor),
						std::lerp(from.y, to.y, factor),
						std::lerp(from.z, to.z, factor),
						std::lerp(from.w, to.w, factor)
					};
				})
		{
		}
	};

	// Registry for a feature's weather variables
	class FeatureWeatherRegistry
	{
	public:
		template <typename VarType, typename = std::enable_if_t<std::is_base_of_v<IWeatherVariable, VarType>>>
		void RegisterVariable(std::shared_ptr<VarType> var)
		{
			variables.push_back(std::static_pointer_cast<IWeatherVariable>(var));
		}

		void LerpAllVariables(const json& from, const json& to, float factor)
		{
			for (auto& var : variables) {
				json fromVar = from.is_null() || !from.contains(var->GetName()) ? json{} : from[var->GetName()];
				json toVar = to.is_null() || !to.contains(var->GetName()) ? json{} : to[var->GetName()];
				var->Lerp(fromVar, toVar, factor);
			}
		}

		void SaveAllToJson(json& j) const
		{
			for (const auto& var : variables) {
				var->SaveToJson(j);
			}
		}

		void LoadAllFromJson(const json& j)
		{
			for (auto& var : variables) {
				var->LoadFromJson(j);
			}
		}

		void SetAllToDefaults()
		{
			for (auto& var : variables) {
				var->SetToDefault();
			}
		}

		const std::vector<std::shared_ptr<IWeatherVariable>>& GetVariables() const { return variables; }

	private:
		std::vector<std::shared_ptr<IWeatherVariable>> variables;
	};

	// Global registry mapping feature names to their weather variables
	class GlobalWeatherRegistry
	{
	public:
		static GlobalWeatherRegistry* GetSingleton()
		{
			static GlobalWeatherRegistry singleton;
			return &singleton;
		}

		FeatureWeatherRegistry* GetOrCreateFeatureRegistry(const std::string& featureName)
		{
			auto it = featureRegistries.find(featureName);
			if (it == featureRegistries.end()) {
				featureRegistries[featureName] = std::make_unique<FeatureWeatherRegistry>();
			}
			return featureRegistries[featureName].get();
		}

		FeatureWeatherRegistry* GetFeatureRegistry(const std::string& featureName)
		{
			auto it = featureRegistries.find(featureName);
			return it != featureRegistries.end() ? it->second.get() : nullptr;
		}

		bool HasWeatherSupport(const std::string& featureName) const
		{
			return featureRegistries.find(featureName) != featureRegistries.end();
		}

		void UpdateFeatureFromWeathers(const std::string& featureName, const json& currWeather, const json& nextWeather, float lerpFactor)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->LerpAllVariables(currWeather, nextWeather, lerpFactor);
			}
		}

		void SaveFeatureToJson(const std::string& featureName, json& j) const
		{
			auto it = featureRegistries.find(featureName);
			if (it != featureRegistries.end()) {
				it->second->SaveAllToJson(j);
			}
		}

		void LoadFeatureFromJson(const std::string& featureName, const json& j)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->LoadAllFromJson(j);
			}
		}

	private:
		GlobalWeatherRegistry() = default;
		~GlobalWeatherRegistry() = default;
		GlobalWeatherRegistry(const GlobalWeatherRegistry&) = delete;
		GlobalWeatherRegistry& operator=(const GlobalWeatherRegistry&) = delete;

		std::map<std::string, std::unique_ptr<FeatureWeatherRegistry>> featureRegistries;
	};
}
