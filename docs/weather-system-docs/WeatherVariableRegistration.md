# Weather Variable Registration System

## Overview

The weather variable registration system provides a centralized way for features to support per-weather settings. Features register their variables once during initialization, and the weather system automatically handles serialization, interpolation, and state management.

## Architecture

### Core Components

**WeatherVariableRegistry.h** contains:

- **`IWeatherVariable`**: Base interface for all weather variables
- **`WeatherVariable<T>`**: Templated variable with type-safe serialization and interpolation
- **`FloatVariable`, `Float3Variable`, `Float4Variable`**: Specialized types with range support
- **`FeatureWeatherRegistry`**: Manages all variables for a single feature
- **`GlobalWeatherRegistry`**: Singleton coordinating all features

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

#### Step 3: Implementation Complete

The system now automatically:
- Saves/loads weather-specific settings to JSON
- Interpolates variables during weather transitions
- Appears in the weather editor UI
- Handles default values and missing dataanced Usage

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
    WeatherEditorID_FormID.json
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
- Registry uses `std::shared_ptr` for variable lifetime
- Variables store raw pointers to feature data (safe as features outlive registry)
- No copying - variables are modified in-place

### Thread Safety
Current implementation is single-threaded (main game thread). Variables are accessed and modified on the same thread that updates weather.

### JSON Serialization
Uses nlohmann::json for type conversion. Built-in support for:
- Primitive types (float, int, bool)
- float2, float3, float4 (see `Utils/Serialize.h`)
- Custom types require NLOHMANN_DEFINE_TYPE_* macros

### Error Handling
- Missing JSON keys use default values
- Type mismatches caught by json exceptions
- Invalid weather files logged but don't crashherVariables::FloatVariable>(
            "intensity", "Intensity", "Effect intensity",
            &settings.intensity, 1.0f, 0.0f, 2.0f
        ));
    }

    // That's it! No save/load/update code needed for weather variables
};
```

## Architecture Benefits

### Separation of Concerns
- **Features**: Focus on rendering logic and effect implementation
- **Weather System**: Handles persistence, interpolation, and state management
- **UI Layer**: Automatically discovers registered variables for editor display

### Future Enhancements
The centralized registry enables:
- Weather template inheritance (parent weather settings override children)
- Automatic UI generation for weather variable editing
- Bulk operations (reset all weathers to defaults, copy settings, etc.)
- Variable validation and constraints
- Change tracking and undo/redo support

### Performance
- Variables are directly modified in place (no copying)
- Interpolation only happens during weather transitions
- Registration is one-time during feature initialization
