# Settings Layering Architecture Proposal

## Problem
Current override system causes user customizations to be overwritten by overrides on each startup, or override effects get "baked in" to user settings.

## Solution 1: Three-Layer Settings System

### Architecture
```
Final Settings = Base + Overrides + User Customizations
```

1. **Base Settings** (`CommunityShaders.json`) - Default feature configurations
2. **Override Settings** (`Overrides/*.json`) - Mod-provided overrides 
3. **User Settings** (`CommunityShaders.user.json`) - User customizations

### Implementation Strategy

#### A. Enhanced SettingsOverrideManager
```cpp
class SettingsLayerManager {
    struct LayeredSettings {
        json baseSettings;      // From CommunityShaders.json
        json overrideSettings;  // Merged from all override files
        json userSettings;      // From CommunityShaders.user.json
        json finalSettings;     // Computed final result
    };
    
    // Apply layers: base -> overrides -> user
    json ComputeFinalSettings(const std::string& featureName);
    
    // Save only user changes (delta from base+overrides)
    void SaveUserCustomizations(const std::string& featureName, const json& settings);
    
    // Check if setting was modified by user vs override
    bool IsUserModified(const std::string& featureName, const std::string& settingPath);
};
```

#### B. Delta Tracking
- Track which settings were changed by user vs overrides
- Only save user-specific changes to `CommunityShaders.user.json`
- Preserve override effects while allowing user customization

#### C. UI Indicators
- Visual indicators in settings UI showing:
  - 🔧 Settings modified by overrides (with mod name tooltip)
  - ✏️ Settings customized by user
  - 🔄 Reset buttons to remove user customizations

### Advantages
- Clean separation of concerns
- User changes persist across override updates
- Clear visibility of what's overridden vs customized
- Backwards compatible

### Disadvantages
- More complex file management
- Requires UI changes to show layer sources
- Migration needed for existing configs

---

## Solution 2: Override Exclusion Lists

### Architecture
Allow overrides to specify which settings should be "user-customizable" vs "locked"

```json
{
  "MaxZenith": 2.0,
  "MinDiffuseVisibility": 0.15,
  "_metadata": {
    "modName": "Enhanced Skylighting",
    "lockSettings": ["MaxZenith"],        // User cannot change these
    "allowUserOverride": ["MinDiffuseVisibility"]  // User can customize these
  }
}
```

### Implementation
```cpp
class SettingsOverrideManager {
    struct OverrideInfo {
        // ... existing fields ...
        std::vector<std::string> lockedSettings;
        std::vector<std::string> userOverridableSettings;
    };
    
    bool IsSettingLocked(const std::string& featureName, const std::string& settingPath);
    void ApplyUserSettings(json& settings, const json& userCustomizations);
};
```

### Advantages
- Simpler implementation
- Mod authors control what users can customize
- Clear user feedback on locked settings

### Disadvantages
- Less flexible for users
- Requires override file updates
- UI complexity for locked settings

---

## Solution 3: Priority-Based Merging

### Architecture
Settings have priority levels, with user settings always taking precedence

```cpp
enum class SettingPriority {
    Default = 0,
    Override = 10,
    UserCustomization = 20
};

struct PrioritizedSetting {
    json value;
    SettingPriority priority;
    std::string source;  // "default", "ModName", "user"
};
```

### Implementation
- Each setting tracks its source and priority
- User changes always override everything else
- Overrides only apply to non-user-modified settings

### Advantages
- Simple priority rules
- User always in control
- Clear source tracking

### Disadvantages
- Memory overhead for tracking
- Complex merge logic
- Potential for user to unknowingly override important mod settings

---

## Recommended Approach: Solution 1 (Three-Layer System)

### Implementation Plan

1. **Create SettingsLayerManager** alongside existing SettingsOverrideManager
2. **Separate config files**:
   - Keep `CommunityShaders.json` for base settings
   - Add `CommunityShaders.user.json` for user customizations
   - Keep override files in `Overrides/` directory

3. **Modified load/save flow**:
   ```cpp
   // Loading
   json baseSettings = LoadBaseConfig();
   json overrideSettings = ApplyAllOverrides(baseSettings);
   json userSettings = LoadUserConfig();
   json finalSettings = MergeUserCustomizations(overrideSettings, userSettings);
   
   // Saving (only save user delta)
   json currentSettings = GetCurrentFeatureSettings();
   json userDelta = ComputeUserDelta(overrideSettings, currentSettings);
   SaveUserConfig(userDelta);
   ```

4. **UI enhancements**:
   - Setting tooltips showing source (override mod name or "default")
   - Reset buttons to clear user customizations
   - Visual indicators for overridden vs customized settings

5. **Migration strategy**:
   - First run: migrate existing config to user customizations
   - Detect and preserve user changes from current mixed config

### File Structure Example
```
Data/SKSE/Plugins/CommunityShaders/
├── CommunityShaders.json          # Base settings (shipped with mod)
├── CommunityShaders.user.json     # User customizations only
└── Overrides/
    ├── EnhancedLighting_Skylighting.json
    └── PerformanceMod_Global.json
```

This approach provides the cleanest separation while preserving user agency and mod compatibility.
