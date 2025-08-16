# ENB Post Processing Framework

This framework provides an extensible system for loading and executing ENBSeries-compatible FX effect files.

## Architecture

### Effect (Abstract Base Class)
- Provides common functionality for all ENB effects
- Handles DirectX 11 Effect file loading and management
- Manages UI variables and technique selection
- Pure virtual methods for custom implementation:
  - `Execute()` - Main effect execution
  - `GetEffectType()` - Returns effect type identifier
  - `SetupCustomVariables()` - Initialize effect-specific variables

### EffectManager (Singleton)
- Centralized management of all Effect instances
- Factory-based effect creation and registration
- Unified execution interface for multiple effects
- ImGui integration for effect management UI

### Derived Effect Classes

#### ENBEffect
- Generic ENB effect implementation
- Handles standard ENB shader variables (ENBParam1-4)
- Suitable for basic post-processing effects

#### ENBBloom
- Specialized bloom effect implementation
- Manages bloom-specific parameters (intensity, threshold, curve, saturation, tint)
- Provides dedicated UI controls for bloom settings

## Usage Example

```cpp
// Initialize the effect system (automatically registers all known effects)
EffectManager::GetSingleton().Initialize();

// Load specific effects
auto& manager = EffectManager::GetSingleton();
manager.LoadEffect("MyBloom", "effects/enbbloom.fx");
manager.LoadEffect("MyColorGrading", "effects/enbeffect.fx");

// Execute effects in render loop
manager.ExecuteEffect("MyBloom", inputRT, swapRT, outputRT);
manager.ExecuteEffect("MyColorGrading", inputRT, swapRT, outputRT);

// Or execute all loaded effects
manager.ExecuteAllEffects(inputRT, swapRT, outputRT);
```

## Adding New Effect Types

1. Create a new class inheriting from Effect:
```cpp
class MyCustomEffect : public Effect {
public:
    virtual std::string GetEffectType() const override { return "MyCustomEffect"; }
    virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }
    virtual void Execute(...) override { /* implementation */ }
};
```

2. Add the effect to RegisterAllKnownEffects() in EffectManager.cpp:
```cpp
void EffectManager::RegisterAllKnownEffects()
{
    // Register ENBEffect
    {
        EffectEntry entry;
        entry.effect = std::make_unique<ENBEffect>();
        entry.type = entry.effect->GetEffectType();
        entry.isLoaded = false;
        entry.isEnabled = true;
        effects["enbeffect"] = std::move(entry);
    }
    
    // Register ENBBloom
    {
        EffectEntry entry;
        entry.effect = std::make_unique<ENBBloom>();
        entry.type = entry.effect->GetEffectType();
        entry.isLoaded = false;
        entry.isEnabled = true;
        effects["enbbloom"] = std::move(entry);
    }
    
    // Add your new effect here
    {
        EffectEntry entry;
        entry.effect = std::make_unique<MyCustomEffect>();
        entry.type = entry.effect->GetEffectType();
        entry.isLoaded = false;
        entry.isEnabled = true;
        effects["mycustom"] = std::move(entry);
    }
}
```

3. Load and use the effect:
```cpp
manager.LoadEffect("MyEffect", "path/to/shader.fx");
```

## Common Variables

The base Effect class provides access to common ENB variables:
- TextureColor, TextureBloom, TextureLens, TextureAdaptation, TextureAperture
- Timer, ScreenSize, AdaptiveQuality
- Weather, TimeOfDay1, TimeOfDay2
- ENightDayFactor, EInteriorFactor
- Params01, ENBParams01