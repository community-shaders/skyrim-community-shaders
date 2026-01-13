# Interior vs Exterior Settings Management

## Overview

This document explores different approaches for managing per-weather feature settings that differ between interior and exterior environments. This is important for features like IBL, lighting, and effects that need different intensities or behaviors indoors vs outdoors.

## Use Cases

- **Image-Based Lighting (IBL)**: Strong exterior lighting during sunny weather should be reduced indoors
- **Screen Space Effects**: Full-strength exterior effects may be too intense in small interior spaces
- **Weather-Dependent Effects**: Rain/storm settings for exterior shouldn't fully apply to covered interiors
- **Performance**: Different quality settings for interior vs exterior environments

---

## Option 1: Nested Interior Overrides (Recommended)

### UI Structure

```
[Feature Name (e.g., IBL)]
  [Button: Using Weather-Specific Settings ✓]
  
  Exterior Settings (always shown when weather overrides enabled)
  ├─ DiffuseIBLScale: [slider: 5.0]
  ├─ IBLSaturation: [slider: 1.2]
  └─ ...
  
  Interior Overrides
  ├─ [Button: Override for Interiors ✓/✗]
  └─ (If enabled:)
      ├─ DiffuseIBLScale: [slider: 0.3] + "Reset to Exterior"
      ├─ IBLSaturation: [grayed: 1.2] (inherited from exterior)
      └─ ...
```

### JSON Structure

```json
{
  "IBL": {
    "__enabled": true,
    "DiffuseIBLScale": 5.0,
    "IBLSaturation": 1.2,
    "__interior_overrides": {
      "__enabled": true,
      "DiffuseIBLScale": 0.3
      // Other values inherit from exterior
    }
  }
}
```

### Implementation Overview

**1. UI Changes (WeatherWidget.cpp):**
- After exterior sliders, add collapsible "Interior Overrides" section
- Second toggle button for `__interior_overrides.__enabled`
- Show sliders with exterior value in tooltip when not overridden
- "Use Exterior Value" context menu option for individual settings

**2. WeatherManager Changes:**
- Add `IsPlayerInInterior()` check using Skyrim's player location data
- When loading settings, detect if player is indoors
- If interior and overrides enabled, merge `__interior_overrides` over base values
- Priority: Interior override > Exterior weather > Global feature settings

**3. Variable Registration (no changes needed):**
- Same variables work for both contexts
- Registry doesn't need to know about interior/exterior distinction

### Pros

- ✅ Intuitive hierarchy (global → weather → exterior → interior)
- ✅ Only override what differs (efficient storage)
- ✅ Easy to see what's different between interior/exterior
- ✅ Follows existing `__enabled` pattern
- ✅ Supports per-variable granularity
- ✅ Authors can disable all interior overrides with one toggle
- ✅ Values inherit naturally (set once, override selectively)

### Cons

- ❌ Adds UI complexity with nested sections
- ❌ Requires interior/exterior detection in WeatherManager
- ❌ More clicks to set up initially

---

## Option 2: Side-by-Side Columns

### UI Structure

```
[Feature Name]
  [Button: Using Weather-Specific Settings]
  [Button: Enable Interior Differences]
  
  Setting Name          | Exterior  | Interior
  ─────────────────────────────────────────
  DiffuseIBLScale      | [5.0]     | [0.3]
  IBLSaturation        | [1.2]     | [1.2] (grayed if same)
  DALCAmount           | [0.5]     | [0.5]
```

### JSON Structure

```json
{
  "IBL": {
    "__enabled": true,
    "exterior": {
      "DiffuseIBLScale": 5.0,
      "IBLSaturation": 1.2
    },
    "interior": {
      "DiffuseIBLScale": 0.3,
      "IBLSaturation": 1.2
    }
  }
}
```

### Pros

- ✅ Easy to compare values at a glance
- ✅ Compact vertical space
- ✅ Clear what differs between interior/exterior
- ✅ No nested sections

### Cons

- ❌ Wide UI (may not fit in editor panel)
- ❌ Redundant storage (must set all values for both)
- ❌ Less intuitive for "most settings are the same" use case
- ❌ Harder to implement with ImGui's layout system

---

## Option 3: Interior Multipliers

### UI Structure

```
[Feature Name]
  [Button: Using Weather-Specific Settings]
  
  DiffuseIBLScale: [5.0]  [✓ Interior Mult: 0.1x]
  IBLSaturation:   [1.2]  [✗ Interior Mult: 1.0x]
  DALCAmount:      [0.5]  [✓ Interior Mult: 0.8x]
```

### JSON Structure

```json
{
  "IBL": {
    "__enabled": true,
    "DiffuseIBLScale": 5.0,
    "DiffuseIBLScale_InteriorMult": 0.1,  // Effective value: 0.5 indoors
    "IBLSaturation": 1.2,
    "IBLSaturation_InteriorMult": 1.0,    // No change indoors
    "DALCAmount": 0.5,
    "DALCAmount_InteriorMult": 0.8        // Effective value: 0.4 indoors
  }
}
```

### Pros

- ✅ Compact UI (one line per setting)
- ✅ Easy to understand relationship (0.5x = half strength)
- ✅ Simple implementation
- ✅ No nested structures

### Cons

- ❌ Less flexible (can't set completely independent values)
- ❌ Multipliers may not make sense for all value types (colors, toggles)
- ❌ Can't disable specific settings for interiors
- ❌ Awkward for settings where interior should be higher than exterior

---

## Option 4: Separate Interior Toggle at Top Level

### UI Structure

```
Weather Editor Tabs:
┌─────────────────────────────────┐
│ [Exterior] | [Interior]         │
└─────────────────────────────────┘

Exterior Tab:
  [IBL Feature]
    ├─ [Using Weather-Specific Settings ✓]
    ├─ DiffuseIBLScale: [5.0]
    └─ ...
  
  [ScreenSpaceGI Feature]
    └─ ...

Interior Tab:
  [IBL Feature]
    ├─ [○ Use Exterior Settings | ● Override]
    ├─ DiffuseIBLScale: [0.3]
    └─ ...
  
  [ScreenSpaceGI Feature]
    └─ [● Use Exterior Settings]
```

### JSON Structure

```json
{
  "exterior": {
    "IBL": {
      "__enabled": true,
      "DiffuseIBLScale": 5.0
    }
  },
  "interior": {
    "IBL": {
      "__enabled": true,
      "DiffuseIBLScale": 0.3
    },
    "ScreenSpaceGI": {
      "__use_exterior": true
    }
  }
}
```

### Pros

- ✅ Clean separation of concerns
- ✅ Entire set of interior settings in one place
- ✅ Can bulk "use exterior for all features"
- ✅ Tab-based approach is familiar

### Cons

- ❌ Must switch tabs to compare values
- ❌ More clicking to set up
- ❌ Harder to see what's overridden at a glance
- ❌ Duplicates feature tree structure

---

## Recommendation: Option 1 (Nested Interior Overrides)

### Why This Approach?

1. **Natural Hierarchy**: Follows the existing pattern of global → weather-specific → interior-specific
2. **Efficient**: Only store what differs from exterior settings
3. **Discoverable**: UI clearly shows when interior overrides are active
4. **Flexible**: Can override individual settings or all of them
5. **Extensible**: Could add "dungeon overrides" or "underwater overrides" using same pattern

### Implementation Plan

#### Phase 1: Core Infrastructure
1. Add interior detection utility function
2. Extend WeatherManager to handle `__interior_overrides` JSON key
3. Update LoadSettingsFromWeather to merge interior overrides when appropriate

#### Phase 2: UI
1. Add collapsible "Interior Overrides" section in DrawFeatureSettings
2. Add toggle button for `__interior_overrides.__enabled`
3. Implement control rendering with inheritance indicators
4. Add "Copy from Exterior" and "Reset to Exterior" context menu options

#### Phase 3: Polish
1. Add visual indicators showing which values differ from exterior
2. Add bulk operations (copy all, clear all interior overrides)
3. Update documentation with interior override examples

### Key Design Decisions

**Q: Should interior overrides be per-weather or global across all weathers?**
A: Per-weather. Different weathers may need different interior adjustments (e.g., storm vs clear sky).

**Q: Should there be a "copy all exterior to interior" bulk action?**
A: Yes, as a context menu option on the "Interior Overrides" header.

**Q: How to handle weather transitions when entering/exiting buildings?**
A: Apply interior/exterior settings immediately on cell change, respecting ongoing weather transitions. The lerp factor remains the same, but the "to" values switch between interior/exterior variants.

**Q: What about features that shouldn't have interior/exterior differences?**
A: The system is opt-in. If a feature doesn't need interior overrides, authors simply don't enable them. The toggle button is always available but defaults to off.

### Example Workflow

1. Author opens weather editor for "Clear" weather
2. Navigates to Features tab → IBL
3. Enables "Using Weather-Specific Settings"
4. Sets DiffuseIBLScale to 5.0 (strong outdoor lighting)
5. Clicks "Interior Overrides" section to expand
6. Clicks "Override for Interiors" toggle button
7. Sets DiffuseIBLScale to 0.8 (reduced indoor lighting)
8. Other IBL settings (saturation, etc.) inherit from exterior automatically
9. Saves weather → JSON contains both exterior and interior values
10. Player enters an interior during clear weather → IBL smoothly adjusts to 0.8

---

## Future Enhancements

### Conditional Overrides
- **Dungeon Interiors**: Different settings for caves/dungeons vs buildings
- **Underwater**: Special handling for underwater environments
- **Location-Specific**: Override for specific interior cells (Dragonsreach vs Bannered Mare)

### Visual Indicators
- Color-code sliders: Green = exterior only, Blue = has interior override
- Show diff values in tooltips: "Exterior: 5.0 → Interior: 0.8"
- Icons indicating inheritance status

### Bulk Operations
- "Copy All Exterior to Interior"
- "Clear All Interior Overrides"
- "Invert Interior/Exterior" (swap values)
- "Apply Interior Multiplier to All" (global 0.5x reduction)

### Testing Tools
- "Preview Interior" toggle in editor (shows what values would be with interior overrides)
- "Toggle Interior/Exterior" hotkey for quick testing
- Log window showing which override is active

---

## Conclusion

Nested interior overrides (Option 1) provides the best balance of flexibility, usability, and efficiency for managing interior vs exterior feature settings in the weather system. It follows existing patterns, minimizes UI complexity, and supports gradual adoption by feature authors.
