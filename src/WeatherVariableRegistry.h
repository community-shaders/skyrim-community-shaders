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
	/** @brief Abstract interface for a weather-controllable variable that can be interpolated, serialized, and reset. */
	class IWeatherVariable
	{
	public:
		virtual ~IWeatherVariable() = default;

		/**
		 * @brief Interpolates this variable between two weather override values.
		 * @param from JSON value for the outgoing weather (null if no override).
		 * @param to JSON value for the incoming weather (null if no override).
		 * @param factor Interpolation factor in [0, 1].
		 */
		virtual void Lerp(const json& from, const json& to, float factor) = 0;

		/**
		 * @brief Serializes the current value into a JSON object.
		 * @param j JSON object to write this variable's value into, keyed by name.
		 */
		virtual void SaveToJson(json& j) const = 0;

		/**
		 * @brief Deserializes this variable's value from a JSON object.
		 * @param j JSON object to read from (key must match this variable's name).
		 */
		virtual void LoadFromJson(const json& j) = 0;

		/** @brief Resets this variable to its compile-time default value. */
		virtual void SetToDefault() = 0;

		/** @brief Returns the internal key name used for JSON serialization. */
		virtual std::string GetName() const = 0;

		/** @brief Returns the human-readable label shown in the weather editor UI. */
		virtual std::string GetDisplayName() const = 0;

		/** @brief Returns the tooltip text describing this variable. */
		virtual std::string GetTooltip() const = 0;

		/** @brief Snapshots the current in-memory value as the baseline user setting. */
		virtual void CaptureUserSettingsValue() = 0;

		/** @brief Restores this variable to the previously captured user setting value. */
		virtual void SetToUserSettings() = 0;

		/**
		 * @brief Begins a weather transition, caching the starting value for smooth interpolation.
		 * @param fromOverride JSON value for the outgoing weather override (null to use current value).
		 */
		virtual void BeginTransition(const json& fromOverride) = 0;

		/** @brief Ends the current weather transition, clearing the cached start value. */
		virtual void EndTransition() = 0;

		/** @brief Returns true if this variable is currently mid-transition between two weather states. */
		virtual bool IsInTransition() const = 0;
	};

	/**
	 * @brief Type-safe weather variable that wraps a pointer to a live setting value.
	 *
	 * Manages interpolation between weather overrides, JSON serialization,
	 * user-settings capture/restore, and transition state for a single typed value.
	 *
	 * @tparam T The value type (float, float3, float4, std::array, std::vector, etc.).
	 */
	template <typename T>
	class WeatherVariable : public IWeatherVariable
	{
	public:
		/**
		 * @brief Constructs a weather variable bound to a live value pointer.
		 * @param name Internal key name for JSON serialization.
		 * @param displayName Human-readable label for the weather editor UI.
		 * @param tooltip Descriptive text shown on hover.
		 * @param valuePtr Pointer to the live setting value that will be modified during interpolation.
		 * @param defaultValue Compile-time default used by SetToDefault().
		 * @param lerpFunc Optional custom interpolation function; defaults to std::lerp for floating-point types.
		 */
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

		/** @brief Interpolates between two weather JSON overrides, falling back to user settings when an override is absent. */
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

		/** @brief Caches the starting value for a new weather transition from the given override. */
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

		/** @brief Ends the current weather transition. */
		void EndTransition() override
		{
			inTransition = false;
		}

		/** @brief Returns true if a weather transition is in progress. */
		bool IsInTransition() const override { return inTransition; }

		/** @brief Serializes the current value to JSON keyed by this variable's name. */
		void SaveToJson(json& j) const override
		{
			if (valuePtr) {
				j[name] = *valuePtr;
			}
		}

		/** @brief Loads this variable's value from JSON, falling back to default on type errors. */
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

		/** @brief Resets the live value to the compile-time default. */
		void SetToDefault() override
		{
			if (valuePtr) {
				*valuePtr = defaultValue;
			}
		}

		/** @brief Snapshots the current live value as the user's baseline setting. */
		void CaptureUserSettingsValue() override
		{
			if (valuePtr) {
				userSettingsValue = *valuePtr;
			}
		}

		/** @brief Restores the live value to the previously captured user setting. */
		void SetToUserSettings() override
		{
			if (valuePtr) {
				*valuePtr = userSettingsValue;
			}
		}

		/** @brief Returns the internal key name for JSON serialization. */
		std::string GetName() const override { return name; }
		/** @brief Returns the human-readable display label. */
		std::string GetDisplayName() const override { return displayName; }
		/** @brief Returns the tooltip description. */
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

	/** @brief Weather variable specialization for scalar floats with min/max bounds for UI sliders. */
	class FloatVariable : public WeatherVariable<float>
	{
	public:
		/**
		 * @brief Constructs a bounded float weather variable.
		 * @param name Internal key name for JSON serialization.
		 * @param displayName Human-readable label for the weather editor UI.
		 * @param tooltip Descriptive text shown on hover.
		 * @param valuePtr Pointer to the live float value.
		 * @param defaultValue Compile-time default.
		 * @param minValue Minimum allowed value for UI sliders.
		 * @param maxValue Maximum allowed value for UI sliders.
		 */
		FloatVariable(const std::string& name, const std::string& displayName, const std::string& tooltip,
			float* valuePtr, float defaultValue, float minValue = 0.0f, float maxValue = 1.0f) :
			WeatherVariable<float>(name, displayName, tooltip, valuePtr, defaultValue),
			minValue(minValue), maxValue(maxValue)
		{
		}

		/** @brief Returns the minimum allowed value. */
		float GetMin() const { return minValue; }
		/** @brief Returns the maximum allowed value. */
		float GetMax() const { return maxValue; }

	private:
		float minValue;
		float maxValue;
	};

	/** @brief Weather variable specialization for float3 vectors with per-component linear interpolation. */
	class Float3Variable : public WeatherVariable<float3>
	{
	public:
		/**
		 * @brief Constructs a float3 weather variable with built-in per-component lerp.
		 * @param name Internal key name for JSON serialization.
		 * @param displayName Human-readable label for the weather editor UI.
		 * @param tooltip Descriptive text shown on hover.
		 * @param valuePtr Pointer to the live float3 value.
		 * @param defaultValue Compile-time default.
		 */
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

	/** @brief Weather variable specialization for float4 vectors with per-component linear interpolation. */
	class Float4Variable : public WeatherVariable<float4>
	{
	public:
		/**
		 * @brief Constructs a float4 weather variable with built-in per-component lerp.
		 * @param name Internal key name for JSON serialization.
		 * @param displayName Human-readable label for the weather editor UI.
		 * @param tooltip Descriptive text shown on hover.
		 * @param valuePtr Pointer to the live float4 value.
		 * @param defaultValue Compile-time default.
		 */
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

	/**
	 * @brief Weather variable specialization for fixed-size std::array types.
	 *
	 * Interpolation is performed element-wise using a caller-supplied per-element lerp function.
	 *
	 * @tparam T Element type.
	 * @tparam N Array size.
	 */
	template <typename T, size_t N>
	class ArrayVariable : public WeatherVariable<std::array<T, N>>
	{
	public:
		/**
		 * @brief Constructs an array weather variable with a custom per-element interpolation function.
		 * @param name Internal key name for JSON serialization.
		 * @param displayName Human-readable label for the weather editor UI.
		 * @param tooltip Descriptive text shown on hover.
		 * @param valuePtr Pointer to the live std::array value.
		 * @param defaultValue Compile-time default array.
		 * @param elementLerpFunc Function that interpolates a single element from/to by factor.
		 */
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

		/** @brief Helper constructor for floating-point element types that uses std::lerp as the default interpolation. */
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

	/**
	 * @brief Weather variable specialization for dynamic std::vector types.
	 *
	 * Interpolation is performed element-wise up to the maximum size of the two vectors.
	 * Missing elements are treated as default-constructed values.
	 *
	 * @tparam T Element type.
	 */
	template <typename T>
	class VectorVariable : public WeatherVariable<std::vector<T>>
	{
	public:
		/**
		 * @brief Constructs a vector weather variable with a custom per-element interpolation function.
		 * @param name Internal key name for JSON serialization.
		 * @param displayName Human-readable label for the weather editor UI.
		 * @param tooltip Descriptive text shown on hover.
		 * @param valuePtr Pointer to the live std::vector value.
		 * @param defaultValue Compile-time default vector.
		 * @param elementLerpFunc Function that interpolates a single element from/to by factor.
		 */
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

		/** @brief Helper constructor for floating-point element types that uses std::lerp as the default interpolation. */
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

	/** @brief Per-feature collection of weather variables with batch operations for interpolation, serialization, and transitions. */
	class FeatureWeatherRegistry
	{
	public:
		/**
		 * @brief Registers a weather variable with this feature's registry.
		 * @tparam VarType Concrete type deriving from IWeatherVariable.
		 * @param var Shared pointer to the variable to register.
		 */
		template <typename VarType, typename = std::enable_if_t<std::is_base_of_v<IWeatherVariable, VarType>>>
		void RegisterVariable(std::shared_ptr<VarType> var)
		{
			variables.push_back(std::static_pointer_cast<IWeatherVariable>(var));
		}

		/** @brief Removes all registered variables from this feature's registry. */
		void ClearVariables()
		{
			variables.clear();
		}

		/**
		 * @brief Interpolates all registered variables between two weather JSON objects.
		 * @param from JSON settings for the outgoing weather.
		 * @param to JSON settings for the incoming weather.
		 * @param factor Interpolation factor in [0, 1].
		 */
		void LerpAllVariables(const json& from, const json& to, float factor)
		{
			for (auto& var : variables) {
				auto [fromVar, toVar] = ExtractVarJson(var->GetName(), from, to);
				var->Lerp(fromVar, toVar, factor);
			}
		}

		/**
		 * @brief Serializes all registered variables into a single JSON object.
		 * @param j JSON object to populate with variable key/value pairs.
		 */
		void SaveAllToJson(json& j) const
		{
			for (const auto& var : variables) {
				var->SaveToJson(j);
			}
		}

		/**
		 * @brief Deserializes all registered variables from a JSON object.
		 * @param j JSON object containing variable key/value pairs.
		 */
		void LoadAllFromJson(const json& j)
		{
			for (auto& var : variables) {
				var->LoadFromJson(j);
			}
		}

		/** @brief Resets all registered variables to their compile-time defaults. */
		void SetAllToDefaults()
		{
			for (auto& var : variables) {
				var->SetToDefault();
			}
		}

		/** @brief Snapshots the current live values of all variables as user-settings baselines. */
		void CaptureAllUserSettingsValues()
		{
			for (auto& var : variables) {
				var->CaptureUserSettingsValue();
			}
		}

		/**
		 * @brief Begins a weather transition for all variables, caching starting values.
		 * @param fromWeatherSettings JSON settings for the outgoing weather.
		 */
		void BeginTransition(const json& fromWeatherSettings)
		{
			for (auto& var : variables) {
				auto [fromVar, _] = ExtractVarJson(var->GetName(), fromWeatherSettings, json{});
				var->BeginTransition(fromVar);
			}
		}

		/** @brief Ends the weather transition for all variables. */
		void EndTransition()
		{
			for (auto& var : variables) {
				var->EndTransition();
			}
		}

		/**
		 * @brief Checks whether a specific variable is currently mid-transition.
		 * @param settingName Internal key name of the variable to query.
		 * @return True if the named variable is in transition; false if not found or not transitioning.
		 */
		bool IsVariableInTransition(const std::string& settingName) const
		{
			for (const auto& var : variables) {
				if (var->GetName() == settingName)
					return var->IsInTransition();
			}
			return false;
		}

		/** @brief Returns a read-only reference to all registered weather variables. */
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

	/**
	 * @brief Global singleton mapping feature names to their per-feature weather variable registries.
	 *
	 * Coordinates weather-driven variable updates across all features, managing
	 * transitions, pausing, and per-feature JSON serialization.
	 */
	class GlobalWeatherRegistry
	{
	public:
		/** @brief Returns the global singleton instance. */
		static GlobalWeatherRegistry* GetSingleton()
		{
			static GlobalWeatherRegistry singleton;
			return &singleton;
		}

		/**
		 * @brief Returns the weather registry for a feature, creating it if it does not exist.
		 *
		 * If the registry already exists, its variables are cleared to support settings reload.
		 *
		 * @param featureName Short name of the feature.
		 * @return Pointer to the feature's weather registry.
		 */
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

		/**
		 * @brief Returns the weather registry for a feature, or nullptr if none exists.
		 * @param featureName Short name of the feature.
		 * @return Pointer to the feature's registry, or nullptr.
		 */
		FeatureWeatherRegistry* GetFeatureRegistry(const std::string& featureName)
		{
			auto it = featureRegistries.find(featureName);
			return it != featureRegistries.end() ? it->second.get() : nullptr;
		}

		/**
		 * @brief Checks whether a feature has registered any weather variables.
		 * @param featureName Short name of the feature.
		 * @return True if the feature has a weather registry.
		 */
		bool HasWeatherSupport(const std::string& featureName) const
		{
			return featureRegistries.find(featureName) != featureRegistries.end();
		}

		/**
		 * @brief Interpolates a feature's weather variables between two weather states.
		 *
		 * No-op if the feature is paused or has no registered weather variables.
		 *
		 * @param featureName Short name of the feature.
		 * @param currWeather JSON settings for the outgoing weather.
		 * @param nextWeather JSON settings for the incoming weather.
		 * @param lerpFactor Interpolation factor in [0, 1].
		 */
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

		/**
		 * @brief Checks whether weather updates are paused for a feature.
		 * @param featureName Short name of the feature.
		 * @return True if the feature's weather updates are paused.
		 */
		bool IsFeaturePaused(const std::string& featureName)
		{
			auto it = pausedFeatures.find(featureName);
			if (it != pausedFeatures.end()) {
				return it->second;
			}
			return false;
		}

		/**
		 * @brief Pauses or resumes weather-driven updates for a feature.
		 * @param featureName Short name of the feature.
		 * @param paused True to pause, false to resume.
		 */
		void SetFeaturePaused(const std::string& featureName, bool paused)
		{
			pausedFeatures[featureName] = paused;
		}

		/**
		 * @brief Serializes a feature's weather variables to JSON.
		 * @param featureName Short name of the feature.
		 * @param j JSON object to populate.
		 */
		void SaveFeatureToJson(const std::string& featureName, json& j) const
		{
			auto it = featureRegistries.find(featureName);
			if (it != featureRegistries.end()) {
				it->second->SaveAllToJson(j);
			}
		}

		/**
		 * @brief Deserializes a feature's weather variables from JSON.
		 * @param featureName Short name of the feature.
		 * @param j JSON object containing variable key/value pairs.
		 */
		void LoadFeatureFromJson(const std::string& featureName, const json& j)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->LoadAllFromJson(j);
			}
		}

		/**
		 * @brief Snapshots a feature's current variable values as user-settings baselines.
		 * @param featureName Short name of the feature.
		 */
		void CaptureFeatureUserSettings(const std::string& featureName)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->CaptureAllUserSettingsValues();
			}
		}

		/**
		 * @brief Begins a weather transition for a feature, caching starting values.
		 * @param featureName Short name of the feature.
		 * @param fromWeatherSettings JSON settings for the outgoing weather.
		 */
		void BeginFeatureTransition(const std::string& featureName, const json& fromWeatherSettings)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->BeginTransition(fromWeatherSettings);
			}
		}

		/**
		 * @brief Ends the weather transition for a feature.
		 * @param featureName Short name of the feature.
		 */
		void EndFeatureTransition(const std::string& featureName)
		{
			auto* registry = GetFeatureRegistry(featureName);
			if (registry) {
				registry->EndTransition();
			}
		}

		/**
		 * @brief Checks whether a specific variable in a feature is currently mid-transition.
		 * @param featureName Short name of the feature.
		 * @param settingName Internal key name of the variable.
		 * @return True if the variable is in transition.
		 */
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
