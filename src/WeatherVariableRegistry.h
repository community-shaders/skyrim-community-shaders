#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using json = nlohmann::json;

// Weather variable system - features register variables which are automatically handled by weather system
// Supports primitive types (float, float3, float4), std::array, and std::vector
//
// Usage examples:
//   - FloatVariable: Single float values with min/max bounds
//   - Float3Variable/Float4Variable: Vector types (colors, positions, etc.)
//   - ArrayVariable: Fixed-size arrays (e.g., std::array<ColorProfile, 8>)
//   - VectorVariable: Dynamic arrays (e.g., std::vector<float>)
//
// Custom element types require providing an element lerp function:
//   auto arrayVar = std::make_shared<ArrayVariable<ColorProfile, 8>>(
//       "profiles", "Color Profiles", "Array of color profiles",
//       &settings.profiles, defaultProfiles,
//       [](const ColorProfile& from, const ColorProfile& to, float factor) {
//           ColorProfile result;
//           // Implement per-field interpolation
//           return result;
//       });
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
		virtual void CaptureUserSettingsValue() = 0;
		virtual void SetToUserSettings() = 0;
		virtual void BeginTransition(const json& fromOverride) = 0;
		virtual void EndTransition() = 0;
		virtual bool IsInTransition() const = 0;
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
			displayName(displayName), tooltip(tooltip), valuePtr(valuePtr), defaultValue(defaultValue),
			userSettingsValue(valuePtr ? *valuePtr : defaultValue), lerpFunc(lerpFunc),
			inTransition(false), transitionStartValue(defaultValue)
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

			// Check if either weather actually has an override for this setting
			bool hasFromOverride = !from.is_null();
			bool hasToOverride = !to.is_null();

			// If neither weather overrides this setting, don't modify the current value
			if (!hasFromOverride && !hasToOverride) {
				return;
			}

			T fromVal;
			T toVal;

			// Use cached transition start value if in transition, otherwise parse from JSON
			if (inTransition) {
				fromVal = transitionStartValue;
			} else if (hasFromOverride) {
				try {
					fromVal = from.get<T>();
				} catch (const nlohmann::json::type_error& e) {
					logger::debug("Type error in Lerp 'from' for {}: {}", name, e.what());
					fromVal = userSettingsValue;
				}
			} else {
				fromVal = userSettingsValue;
			}

			if (hasToOverride) {
				try {
					toVal = to.get<T>();
				} catch (const nlohmann::json::type_error& e) {
					logger::debug("Type error in Lerp 'to' for {}: {}", name, e.what());
					toVal = userSettingsValue;  // Fallback to user settings on error
				}
			} else {
				toVal = userSettingsValue;
			}

			*valuePtr = lerpFunc(fromVal, toVal, factor);
		}

		void BeginTransition(const json& fromOverride) override
		{
			if (!valuePtr)
				return;

			// Capture the starting value for this transition
			if (!fromOverride.is_null()) {
				try {
					transitionStartValue = fromOverride.get<T>();
				} catch (const nlohmann::json::type_error& e) {
					logger::debug("Type error in BeginTransition for {}: {}", name, e.what());
					transitionStartValue = *valuePtr;
				}
			} else {
				transitionStartValue = *valuePtr;
			}
			inTransition = true;
		}

		void EndTransition() override
		{
			inTransition = false;
		}

		bool IsInTransition() const override { return inTransition; }

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

		void CaptureUserSettingsValue() override
		{
			if (valuePtr) {
				userSettingsValue = *valuePtr;
			}
		}

		void SetToUserSettings() override
		{
			if (valuePtr) {
				*valuePtr = userSettingsValue;
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
		T userSettingsValue;
		std::function<T(const T&, const T&, float)> lerpFunc;

		// Transition state
		bool inTransition;
		T transitionStartValue;
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

	// Specialized weather variable for std::array types
	template <typename T, size_t N>
	class ArrayVariable : public WeatherVariable<std::array<T, N>>
	{
	public:
		ArrayVariable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			std::array<T, N>* valuePtr, std::array<T, N> defaultValue,
			std::function<T(const T&, const T&, float)> elementLerpFunc) :
			WeatherVariable<std::array<T, N>>(name, displayName, tooltip, valuePtr, defaultValue,
				[elementLerpFunc](const std::array<T, N>& from, const std::array<T, N>& to, float factor) {
					std::array<T, N> result;
					for (size_t i = 0; i < N; ++i) {
						result[i] = elementLerpFunc(from[i], to[i], factor);
					}
					return result;
				})
		{
		}

		// Helper constructor for float element types with default lerp
		template <typename U = T, typename = std::enable_if_t<std::is_floating_point_v<U>>>
		ArrayVariable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			std::array<T, N>* valuePtr, std::array<T, N> defaultValue) :
			ArrayVariable(name, displayName, tooltip, valuePtr, defaultValue,
				[](const T& from, const T& to, float factor) {
					return static_cast<T>(std::lerp(from, to, factor));
				})
		{
		}
	};

	// Specialized weather variable for std::vector types
	template <typename T>
	class VectorVariable : public WeatherVariable<std::vector<T>>
	{
	public:
		VectorVariable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			std::vector<T>* valuePtr, std::vector<T> defaultValue,
			std::function<T(const T&, const T&, float)> elementLerpFunc) :
			WeatherVariable<std::vector<T>>(name, displayName, tooltip, valuePtr, defaultValue,
				[elementLerpFunc](const std::vector<T>& from, const std::vector<T>& to, float factor) {
					size_t maxSize = std::max(from.size(), to.size());
					std::vector<T> result;
					result.reserve(maxSize);

					for (size_t i = 0; i < maxSize; ++i) {
						T fromVal = (i < from.size()) ? from[i] : T{};
						T toVal = (i < to.size()) ? to[i] : T{};
						result.push_back(elementLerpFunc(fromVal, toVal, factor));
					}
					return result;
				})
		{
		}

		// Helper constructor for float element types with default lerp
		template <typename U = T, typename = std::enable_if_t<std::is_floating_point_v<U>>>
		VectorVariable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			std::vector<T>* valuePtr, std::vector<T> defaultValue) :
			VectorVariable(name, displayName, tooltip, valuePtr, defaultValue,
				[](const T& from, const T& to, float factor) {
					return static_cast<T>(std::lerp(from, to, factor));
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

		void ClearVariables()
		{
			variables.clear();
		}

		void LerpAllVariables(const json& from, const json& to, float factor)
		{
			for (auto& var : variables) {
				auto [fromVar, toVar] = ExtractVarJson(var->GetName(), from, to);
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

		void CaptureAllUserSettingsValues()
		{
			for (auto& var : variables) {
				var->CaptureUserSettingsValue();
			}
		}

		void BeginTransition(const json& fromWeatherSettings)
		{
			for (auto& var : variables) {
				auto [fromVar, _] = ExtractVarJson(var->GetName(), fromWeatherSettings, json{});
				var->BeginTransition(fromVar);
			}
		}

		void EndTransition()
		{
			for (auto& var : variables) {
				var->EndTransition();
			}
		}

		bool IsVariableInTransition(const std::string& settingName) const
		{
			for (const auto& var : variables) {
				if (var->GetName() == settingName)
					return var->IsInTransition();
			}
			return false;
		}

		const std::vector<std::shared_ptr<IWeatherVariable>>& GetVariables() const { return variables; }

	private:
		// Extract per-variable JSON from weather settings, returning null json if absent
		static std::pair<json, json> ExtractVarJson(const std::string& varName, const json& from, const json& to)
		{
			json fromVar = (!from.is_object() || !from.contains(varName)) ? json{} : from[varName];
			json toVar = (!to.is_object() || !to.contains(varName)) ? json{} : to[varName];
			return { fromVar, toVar };
		}

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
			} else {
				// Clear existing variables before re-registration (handles settings reload)
				it->second->ClearVariables();
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
			if (IsFeaturePaused(featureName)) {
				return;
			}

			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->LerpAllVariables(currWeather, nextWeather, lerpFactor);
			}
		}

		bool IsFeaturePaused(const std::string& featureName)
		{
			auto it = pausedFeatures.find(featureName);
			if (it != pausedFeatures.end()) {
				return it->second;
			}
			return false;
		}

		void SetFeaturePaused(const std::string& featureName, bool paused)
		{
			pausedFeatures[featureName] = paused;
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

		void CaptureFeatureUserSettings(const std::string& featureName)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->CaptureAllUserSettingsValues();
			}
		}

		void BeginFeatureTransition(const std::string& featureName, const json& fromWeatherSettings)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->BeginTransition(fromWeatherSettings);
			}
		}

		void EndFeatureTransition(const std::string& featureName)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->EndTransition();
			}
		}

		bool IsFeatureVariableInTransition(const std::string& featureName, const std::string& settingName)
		{
			auto* registry = GetFeatureRegistry(featureName);
			return registry && registry->IsVariableInTransition(settingName);
		}

	private:
		GlobalWeatherRegistry() = default;
		~GlobalWeatherRegistry() = default;
		GlobalWeatherRegistry(const GlobalWeatherRegistry&) = delete;
		GlobalWeatherRegistry& operator=(const GlobalWeatherRegistry&) = delete;

		std::map<std::string, std::unique_ptr<FeatureWeatherRegistry>> featureRegistries;
		std::map<std::string, bool> pausedFeatures;
	};
}
