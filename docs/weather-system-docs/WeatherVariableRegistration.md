# Weather Variable Registration System

## Overview

The weather variable registration system provides a centralized way for features to support per-weather settings. Features register their variables once during initialization, and the weather system automatically handles serialization, interpolation, and state management.

## Architecture

### Core Components

**WeatherVariableRegistry.h** contains:

-   **`IWeatherVariable`**: Base interface for all weather variables
-   **`WeatherVariable<T>`**: Templated variable with type-safe serialization and interpolation
-   **`FloatVariable`, `Float3Variable`, `Float4Variable`**: Specialized types with range support
-   **`FeatureWeatherRegistry`**: Manages all variables for a single feature
-   **`GlobalWeatherRegistry`**: Singleton coordinating all features

### Data Flow

```
Feature Registration → Global Registry → Weather Manager
         ↓                    ↓                ↓
  RegisterWeatherVariables  Tracks Support  Detects Changes
         ↓                    ↓                ↓
   Variable Metadata      Per-Feature      Loads JSON
                          Registry            ↓
                                         Interpolates
                                              ↓
                                      Updates Variables
```

## Usage Guide

### Implementing Weather Support in Features

#### Step 1: Define Your Settings Structure

In your feature class, override `RegisterWeatherVariables()`:

```cpp
class MyFeature : public Feature
{
    struct Settings
    {
        float intensity = 1.0f;
        float3 color = { 1.0f, 1.0f, 1.0f };
        bool enabled = true;
    } settings;

    void RegisterWeatherVariables() override
    {
    // ... rest of feature implementation
};
```

#### Step 2: Override RegisterWeatherVariables()

```cpp
void MyFeature::RegisterWeatherVariables() override
{
    auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
        ->GetOrCreateFeatureRegistry(GetShortName());

    // Register a float with range constraints
    registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
        "Intensity",                    // JSON key
        "Effect Intensity",             // Display name
        "Controls the strength",        // Tooltip
        &settings.intensity,            // Pointer to variable
        1.0f,                          // Default value
        0.0f, 2.0f                     // Min/max range
    ));

    // Register a float3 (color or vector)
    registry->RegisterVariable(std::make_shared<WeatherVariables::Float3Variable>(
        "Color",
        "Effect Color",
        "RGB color values",
        &settings.color,
        float3{ 1.0f, 1.0f, 1.0f }
    ));

    // Register bool with custom interpolation
    registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
        "Enabled",
        "Enable Effect",
        "Toggle the effect",
        &settings.enabled,
        true,
        [](const bool& from, const bool& to, float factor) {
            return factor > 0.5f ? to : from;  // Switch at midpoint
        }
    ));
}
```

#### Step 3: Update DrawSettings() to Use Weather-Aware UI Controls

For weather-controlled settings, use the `Util::WeatherUI` helpers instead of direct ImGui calls. This automatically greys out controls when a per-weather override is active and displays tooltips:

```cpp
void MyFeature::DrawSettings()
{
    // Weather-aware slider (will be disabled if current weather overrides it)
    Util::WeatherUI::SliderFloat("Effect Intensity", this, "Intensity",
        &settings.intensity, 0.0f, 2.0f, "%.2f");

    // Weather-aware color picker
    Util::WeatherUI::ColorEdit3("Effect Color", this, "Color",
        (float*)&settings.color);

    // Regular checkbox (not weather-controlled in this example)
    ImGui::Checkbox("Enable Effect", (bool*)&settings.enabled);
}
```

**Available Weather-Aware Helpers:**

-   `Util::WeatherUI::SliderFloat()` - Float slider with min/max
-   `Util::WeatherUI::Checkbox()` - Boolean checkbox
-   `Util::WeatherUI::ColorEdit3()` - RGB color picker
-   `Util::WeatherUI::ColorEdit4()` - RGBA color picker with alpha

**Why Use These?**

-   Automatically detects if the current weather has overridden the setting
-   Disables and greys out the control to show it's weather-controlled
-   Shows tooltip: "Weather Override Active - This setting is controlled by the current weather (WeatherName)"
-   Prevents confusion when editing global settings that are overridden by weather

#### Step 4: Implementation Complete

The system now automatically:

-   Saves/loads weather-specific settings to JSON
-   Interpolates variables during weather transitions
-   Appears in the CS Editor UI with per-weather toggle buttons
-   Handles default values and missing data
-   Shows weather-controlled status in feature settings UI

### Custom Variable Types

Create custom weather variable types for complex data:

```cpp
class CustomTypeVariable : public WeatherVariables::WeatherVariable<MyCustomType>
{
public:
    CustomTypeVariable(const std::string& name, MyCustomType* valuePtr, MyCustomType defaultValue) :
        WeatherVariable<MyCustomType>(name, name, "", valuePtr, defaultValue,
            [](const MyCustomType& from, const MyCustomType& to, float factor) {
                // Custom interpolation logic
                MyCustomType result;
                result.field1 = std::lerp(from.field1, to.field1, factor);
                result.field2 = from.field2; // No lerp for this field
                return result;
            })
    {
    }
};
```

### Array and Vector Types

The weather system supports `std::array` and `std::vector` for complex data structures:

#### Using ArrayVariable for Fixed-Size Arrays

```cpp
void RegisterWeatherVariables() override
{
    auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
        ->GetOrCreateFeatureRegistry(GetShortName());

    // Array of primitive types (floats)
    registry->RegisterVariable(std::make_shared<WeatherVariables::ArrayVariable<float, 8>>(
        "weights",
        "Weight Values",
        "Array of weight coefficients",
        &settings.weights,
        std::array<float, 8>{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }
        // elementLerpFunc optional for float types - uses default std::lerp
    ));

    // Array of complex types with custom interpolation
    registry->RegisterVariable(std::make_shared<WeatherVariables::ArrayVariable<ColorProfile, 8>>(
        "profiles",
        "Color Profiles",
        "Array of color profile configurations",
        &settings.profiles,
        defaultProfiles,
        [](const ColorProfile& from, const ColorProfile& to, float factor) {
            ColorProfile result;
            result.hue = std::lerp(from.hue, to.hue, factor);
            result.saturation = std::lerp(from.saturation, to.saturation, factor);
            result.brightness = std::lerp(from.brightness, to.brightness, factor);
            // interpolate other fields...
            return result;
        }
    ));
}
```

#### Using VectorVariable for Dynamic Arrays

```cpp
void RegisterWeatherVariables() override
{
    auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
        ->GetOrCreateFeatureRegistry(GetShortName());

    // Vector of floats with default interpolation
    registry->RegisterVariable(std::make_shared<WeatherVariables::VectorVariable<float>>(
        "coefficients",
        "Dynamic Coefficients",
        "Variable-length coefficient array",
        &settings.coefficients,
        std::vector<float>{ 1.0f, 0.5f, 0.25f }
    ));

    // Vector of complex types with custom interpolation
    registry->RegisterVariable(std::make_shared<WeatherVariables::VectorVariable<LightConfig>>(
        "lights",
        "Light Configurations",
        "Dynamic array of light settings",
        &settings.lights,
        defaultLights,
        [](const LightConfig& from, const LightConfig& to, float factor) {
            LightConfig result;
            result.intensity = std::lerp(from.intensity, to.intensity, factor);
            result.color = float3{
                std::lerp(from.color.x, to.color.x, factor),
                std::lerp(from.color.y, to.color.y, factor),
                std::lerp(from.color.z, to.color.z, factor)
            };
            return result;
        }
    ));
}
```

**Notes on Vector Interpolation:**

-   When vectors have different sizes, interpolation uses the maximum size
-   Missing elements from shorter vectors are default-initialized (T{})
-   This allows smooth transitions when adding/removing elements between weathers

## System Integration

### How Weather Manager Uses the Registry

The weather manager detects and updates features automatically:

```cpp
// Detection
if (globalRegistry->HasWeatherSupport(featureName)) {
    // Feature has registered variables
}

// Loading
json currWeatherSettings, nextWeatherSettings;
LoadSettingsFromWeather(weather, featureName, currWeatherSettings);
LoadSettingsFromWeather(lastWeather, featureName, nextWeatherSettings);

// Interpolation during weather transitions
globalRegistry->UpdateFeatureFromWeathers(
    featureName,
    currWeatherSettings,
    nextWeatherSettings,
    lerpFactor  // 0.0 to 1.0
);
```

### File Structure

Weather-specific settings are stored in:

```
Data/SKSE/Plugins/CommunityShaders/Weathers/
    WeatherFormEditorID_FormID.json
```

Each file contains settings for all features:

```json
{
    "FeatureName1": {
        "Intensity": 1.5,
        "Color": [1.0, 0.8, 0.6]
    },
    "FeatureName2": {
        "Enabled": true
    }
}
```

## Advanced Patterns

### Conditional Registration

Register variables based on feature state:

```cpp
void RegisterWeatherVariables() override
{
    auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
        ->GetOrCreateFeatureRegistry(GetShortName());

    // Core variables
    registry->RegisterVariable(std::make_shared<FloatVariable>(
        "intensity", "Intensity", "Main intensity",
        &settings.intensity, 1.0f, 0.0f, 2.0f
    ));

    // Advanced variables (conditional)
    if (settings.enableAdvancedMode) {
        registry->RegisterVariable(std::make_shared<Float3Variable>(
            "advancedColor", "Advanced Color", "Color tuning",
            &settings.advancedColor, float3{ 1.0f, 1.0f, 1.0f }
        ));
    }
}
```

### Runtime Queries

Access registered variables for debugging or dynamic UI:

```cpp
auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
    ->GetFeatureRegistry("MyFeature");

if (registry) {
    for (const auto& var : registry->GetVariables()) {
        logger::info("{}: {}", var->GetDisplayName(), var->GetName());
    }
}
```

## Implementation Notes

### Memory Management

-   Registry uses `std::shared_ptr` for variable lifetime
-   Variables store raw pointers to feature data (safe as features outlive registry)
-   No copying - variables are modified in-place

### Thread Safety

Current implementation is single-threaded (main game thread). Variables are accessed and modified on the same thread that updates weather.

### JSON Serialization

Uses nlohmann::json for type conversion. Built-in support for:

-   Primitive types (float, int, bool)
-   float2, float3, float4 (see `Utils/Serialize.h`)
-   **std::array<T, N>** - Fixed-size arrays serialized as JSON arrays
-   **std::vector<T>** - Dynamic arrays serialized as JSON arrays
-   Custom types require NLOHMANN*DEFINE_TYPE*\* macros or custom serialization functions

Example JSON with array/vector types:

```json
{
    "MyFeature": {
        "intensity": 1.5,
        "weights": [1.0, 0.8, 0.6, 0.4, 0.2, 0.1, 0.05, 0.025],
        "coefficients": [1.0, 0.5, 0.25],
        "profiles": [
            { "hue": 0.5, "saturation": 1.0, "brightness": 1.0 },
            { "hue": 0.7, "saturation": 0.8, "brightness": 0.9 }
        ]
    }
}
```

### Error Handling

-   Missing JSON keys use default values
-   Type mismatches caught by json exceptions
-   Invalid weather files are logged but do not crash the system

## Architecture Benefits

### Separation of Concerns

-   **Features**: Focus on rendering logic and effect implementation
-   **Weather System**: Handles persistence, interpolation, and state management
-   **UI Layer**: Automatically discovers registered variables for editor display

### Future Enhancements

The centralized registry enables:

-   Weather template inheritance (parent weather settings override children)
-   Automatic UI generation for weather variable editing
-   Bulk operations (reset all weathers to defaults, copy settings, etc.)
-   Variable validation and constraints
-   Change tracking and undo/redo support

### Performance

-   Variables are directly modified in place (no copying)
-   Interpolation only happens during weather transitions
-   Registration is one-time during feature initialization

```

```
