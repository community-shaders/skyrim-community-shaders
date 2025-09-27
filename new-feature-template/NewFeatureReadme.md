# New Feature Development Reference

Quick reference for creating new graphics features in Community Shaders.

## File Structure

Template files to copy and customize:

-   `template/NewFeature.h` → Copy to `src/Features/YourFeature.h`
-   `template/NewFeature.cpp` → Copy to `src/Features/YourFeature.cpp`
-   `template/New Feature/` → Copy to `features/Your Feature Name/`

## Core System Registration

These are the **required** files that must be modified for your feature to appear in settings:

### 1. `src/Globals.h`

**Forward Declaration** - Add at top with other feature declarations (~lines 1-30):

struct YourFeature;


**Feature Instance Declaration** - Add in `globals::features` namespace (~lines 51-80):

extern YourFeature yourFeature;


### 2. `src/Globals.cpp`

**Feature Instance Definition** - Add in `globals::features` namespace (~lines 48-75):

YourFeature yourFeature{};


### 3. `src/Feature.cpp`

**Feature Registration** - Add to features vector in `GetFeatureList()` (~lines 200-225):

&globals::features::yourFeature,


## Template Customization

### Class Names

-   Replace all `NewFeature` → `YourFeature`
-   Replace all `"New Feature"` → `"Your Feature Name"`
-   Replace all `"NewFeature"` → `"YourFeature"`

### Metadata

-   `GetCategory()` → Choose: "Lighting", "Effects", "Rendering", "Performance", "Terrain", "Water", "Atmosphere"
-   `GetFeatureModLink()` → Update Nexus mod ID
-   `GetFeatureSummary()` → Update description and features list
-   `GetShaderDefineName()` → Your preprocessor define name
-   `HasShaderDefine()` → Which shader types use your define

### Settings

-   Update `Settings` struct with your parameters
-   Update `CbData` struct for shader constant buffer (must be 16-byte aligned)
-   Update NLOHMANN serialization macro
-   Customize `DrawSettings()` UI controls
-   Update shader compilation paths in `CompileShaders()`

### VR Support

Set `SupportsVR()` return value:

-   `return true;` - Feature works in VR
-   `return false;` - Feature disabled in VR builds

## Naming Conventions

| Component | Convention | Example |
|-----------|------------|---------|
| C++ Class | PascalCase | `YourFeature` |
| Instance Variable | camelCase | `yourFeature` |
| Display Name | Spaces | `"Your Feature Name"` |
| Short Name | PascalCase | `"YourFeature"` |
| Features Directory | Spaces | `features/Your Feature Name/` |
| Shader Directory | PascalCase | `YourFeature/` |

## Automatic Build Integration

The build system automatically handles:

-   Shader compilation and validation
-   Settings persistence (JSON serialization)
-   UI menu integration
-   Feature lifecycle management
-   Cross-platform builds (SE/AE/VR)

Build with: `./BuildRelease.bat ALL`

## Testing Checklist

-   [ ] Feature appears in settings menu
-   [ ] Settings save/load correctly
-   [ ] Shaders compile without errors
-   [ ] Feature works in-game
-   [ ] VR compatibility (if enabled)
-   [ ] No build errors