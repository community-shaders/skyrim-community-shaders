# Scene Settings Manager

> Contextual, automatic setting overrides for Community Shaders — no feature code changes required.

## Table of Contents

-   [What Is the Scene Settings Manager?](#what-is-the-scene-settings-manager)
-   [How Settings Flow (Priority Order)](#how-settings-flow-priority-order)
-   [Design Philosophy: Zero Coupling](#design-philosophy-zero-coupling)
-   [Interior Only Settings](#interior-only-settings)
-   [Time of Day Settings](#time-of-day-settings)
-   [UI Guide](#ui-guide)
-   [For Mod Authors: Overwrite Files](#for-mod-authors-overwrite-files)
-   [For Developers: Adding Features to the Whitelist](#for-developers-adding-features-to-the-whitelist)
-   [The Whitelist and Why Features Don't Opt In](#the-whitelist-and-why-features-dont-opt-in)
-   [Comparison: Scene Settings Manager vs Settings Override Manager](#comparison-scene-settings-manager-vs-settings-override-manager)
-   [FAQ](#faq)

---

## What Is the Scene Settings Manager?

The Scene Settings Manager lets you automatically adjust Community Shaders feature settings based on **where you are** and **what time it is**. It has two modes:

-   **Interior Only** — Override settings when you enter an interior cell. Values revert automatically when you leave.
-   **Time of Day** — Smoothly blend settings across six time-of-day periods (Dawn, Sunrise, Day, Sunset, Dusk, Night) while you're in an exterior cell.

Both modes work entirely through the existing `SaveSettings`/`LoadSettings` JSON interface that every feature already has. Features don't need to do anything special — the Scene Settings Manager reads their current values, patches in overrides, and writes them back. The feature never knows the difference.

The two modes are **mutually exclusive by context** — Interior Only is active in interiors, Time of Day is active in exteriors. You can have entries for both; the system automatically activates the correct one based on where you are. It's impossible for both to be active simultaneously.

---

## How Settings Flow (Priority Order)

Settings in Community Shaders follow a layered override system. Each layer can modify values from the layer below it. Later layers win.

The feature's settings on its settings page act as the **master settings** — they are the source of truth that the Scene Settings Manager builds from. When the Scene Settings Manager activates (on cell transition or per-frame TOD blending), it reads the feature's current values via `SaveSettings()` and stores them as the **baseline**. All scene overrides are then applied on top of that baseline. When scene settings deactivate (leaving an interior, or TOD reverting), the baseline is restored — putting the feature back to exactly where its master settings had it.

```
┌────────────────────────────────────────────────────┐
│               Scene Settings Manager               │  ← Highest priority (runtime, contextual)
│  ┌─────────────────────┐ ┌───────────────────────┐ │
│  │   Interior Only     │ │    Time of Day        │ │
│  │   (overwrite files  │ │    (overwrite files   │ │
│  │    + user settings) │ │     + user settings)  │ │
│  └─────────────────────┘ └───────────────────────┘ │
├────────────────────────────────────────────────────┤
│             Settings Override Manager              │  ← Applied at boot (mod author JSON files)
│  ┌─────────────────────┐ ┌───────────────────────┐ │
│  │  Mod Override Files │ │   User Override Files │ │
│  │  (Overrides/*.json) │ │   (Overrides/User/)   │ │
│  └─────────────────────┘ └───────────────────────┘ │
├────────────────────────────────────────────────────┤
│          User Settings (In-Game CS Menu)           │  ← Runtime (saved to SettingsUser.json)
│  ┌───────────────────────────────────────────────┐ │
│  │ Slider, checkbox, and input changes made by   │ │
│  │ the user through the CS in-game menu          │ │
│  └───────────────────────────────────────────────┘ │
├────────────────────────────────────────────────────┤
│             Feature Default Settings               │  ← Lowest priority (hardcoded + INI)
│  ┌─────────────────────┐ ┌───────────────────────┐ │
│  │  Hardcoded Defaults │ │    Feature INI File   │ │
│  │  (C++ source code)  │ │  (loaded at boot)     │ │
│  └─────────────────────┘ └───────────────────────┘ │
└────────────────────────────────────────────────────┘
```

### Layer Details

| Layer                         | When Applied                                                                                                                                       | Persists?                                                                                 | Who Creates It                                       |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| **Feature Defaults**          | At boot, baked into the feature code and loaded from the feature's INI file                                                                        | Always present                                                                            | Feature developers                                   |
| **User Settings**             | At runtime, whenever the user changes a setting through the in-game CS menu. Saved to `SettingsUser.json`.                                         | Yes (saved to disk on change)                                                             | Users (in-game UI sliders, checkboxes, etc.)         |
| **Settings Override Manager** | At boot, after defaults are loaded. Mod author JSON files in `Overrides/` folder merge on top of defaults. User `.user` files sit on top of those. | Yes (files on disk)                                                                       | Mod authors and users                                |
| **Scene Settings Manager**    | At runtime, contextually. Interior Only applies on cell transitions. Time of Day blends continuously in exteriors.                                 | User settings saved to disk. Overwrite files on disk. Values revert when context changes. | Mod authors (overwrite files) and users (in-game UI) |

### Flow in Practice

Here's what happens to a single setting — say, `ScreenSpaceGI.Intensity`:

1. **Boot**: Feature loads its default (e.g., `1.0` from INI).
2. **Boot**: If a Settings Override Manager file sets `Intensity: 0.8`, the feature now uses `0.8`.
3. **User tweaks**: You open the CS menu and drag the Intensity slider to `0.9`. This is saved to `SettingsUser.json` and becomes the active value.
4. **Gameplay (exterior)**: If a Time of Day entry sets `Intensity` to `0.5` at Night and `1.2` at Day, the Scene Settings Manager saves the current baseline (`0.9`), then blends between period values using the current game hour. Uncovered periods fall back to the saved baseline.
5. **Gameplay (enter interior)**: Time of Day deactivates. If an Interior Only entry sets `Intensity` to `0.3`, the current exterior value is saved and the interior override applies.
6. **Gameplay (exit interior)**: The interior override reverts to the saved exterior value. Time of Day reactivates in the exterior.

Within the Scene Settings Manager itself, **Overwrite files** (from mod authors) take priority over **User settings** (from the in-game UI). Both are visible and manageable in the same panel. This is a last-write-wins system applied in order: user entries first, then overwrites on top.

---

## Design Philosophy: Zero Coupling

The Scene Settings Manager's most important design principle is **zero coupling to feature code**.

### How It Works Under the Hood

Every feature in Community Shaders already implements two methods:

```cpp
virtual void SaveSettings(json&) {}   // Serialize current settings to JSON
virtual void LoadSettings(json&) {}   // Deserialize settings from JSON
```

The Scene Settings Manager exploits this existing interface:

1. **Read**: Call `feature->SaveSettings(json)` to get the feature's current state as a JSON blob.
2. **Patch**: Modify specific keys in that JSON (the overrides).
3. **Write**: Call `feature->LoadSettings(json)` to push the modified settings back.

That's it. The feature's own serialization code handles all type conversion, validation, and clamping. The Scene Settings Manager never touches feature internals — it only operates on the JSON interface that already exists for saving settings to disk.

```
┌──────────────┐       ┌─────────────────────┐       ┌──────────────┐
│   Feature    │──────►│  Scene Settings Mgr │──────►│   Feature    │
│ SaveSettings │ JSON  │  (patch overrides)  │ JSON  │ LoadSettings │
└──────────────┘       └─────────────────────┘       └──────────────┘
```

### Why This Matters

-   **No feature code changes needed.** A feature gets Scene Settings Manager support by being added to a whitelist — a single line in a static list. The feature itself is unmodified.
-   **Forward-compatible.** Features that don't exist yet will work with the Scene Settings Manager the moment they're added to the whitelist. If someone is developing a new feature that hasn't been merged yet, it can still be whitelisted in advance.
-   **Any JSON-serializable setting works.** Floats get smoothly blended between time-of-day periods (integers, booleans, and strings are rejected from TOD — only continuous float sliders can transition). For Interior Only, all setting types are supported. If a feature adds new settings, they're automatically available — no registration step needed.
-   **Round-trip verification.** After applying an override, the manager reads the value back and logs a warning if the feature clamped it. This catches range violations without requiring the Scene Settings Manager to know anything about valid ranges.

### Contrast with Tighter Coupling

To appreciate the zero-coupling approach, consider what a tightly-coupled system would look like:

-   Features would need to **register** each controllable variable with name, type, range, and interpolation function.
-   Adding a new setting to scene control would require **code changes in the feature**.
-   Type-specific interpolation logic would need to be **duplicated or centralized** for every variable type.

The Scene Settings Manager avoids all of this. It treats features as black boxes with a JSON interface. This means:

-   A mod author can create overwrite files targeting any setting that appears in a feature's JSON — even settings the Scene Settings Manager developers have never heard of.
-   The system scales to any number of features and settings without increasing complexity.

---

## Interior Only Settings

### How It Works

Interior Only settings activate when you enter an interior cell and deactivate when you leave.

**Detection**: The system listens for Skyrim's `MenuOpenCloseEvent`. When the Loading Menu closes, it checks the current cell's sky mode. If `sky->mode != kFull`, you're in an interior.

**Lifecycle**:

```
┌────────────┐    Loading Menu    ┌──────────────────┐
│  Exterior  │───── closes ──────►│  Check sky mode  │
│  (normal)  │                    │  sky->mode?      │
└────────────┘                    └────────┬─────────┘
                                           │
                              ┌────────────┴────────────┐
                              │                         │
                         kFull (exterior)          !kFull (interior)
                              │                         │
                    ┌─────────▼──────────┐   ┌──────────▼──────────┐
                    │ Revert interior    │   │ Save exterior vals  │
                    │ settings if active │   │ Apply overrides     │
                    │ Activate TOD       │   │ Deactivate TOD      │
                    └────────────────────┘   └─────────────────────┘
```

**What "save and restore" means:**

1. Before applying interior overrides, the manager calls `SaveSettings()` on each affected feature and stores **only the keys it's about to override** (a partial baseline).
2. Interior overrides are applied via `LoadSettings()`.
3. When you exit to an exterior, the saved baseline values are written back — restoring the feature to its pre-interior state.

This partial-save approach means features keep any settings you changed in-game (via the CS menu) that aren't part of the interior override. Only the specific overridden keys revert.

User settings (entries you add through the in-game UI) are persisted automatically to `SceneSettings/InteriorOnly.json`. They survive game restarts — you don't need to save them

### Example

Say you have Interior Only overrides for:

-   `ScreenSpaceGI.EnableGI` → `false` (disable GI in interiors)
-   `SubsurfaceScattering.Intensity` → `0.2` (reduce SSS indoors)

When you enter Dragonsreach:

1. Current values of `EnableGI` and `Intensity` are saved.
2. `EnableGI` is set to `false`, `Intensity` to `0.2`.
3. You play through the interior with these settings active.

When you exit to Whiterun:

1. `EnableGI` reverts to its saved value (e.g., `true`).
2. `Intensity` reverts to its saved value (e.g., `0.5`).
3. Time of Day reactivates (if you have TOD entries).

---

## Time of Day Settings

### How It Works

Time of Day (TOD) settings smoothly blend feature values across six periods while you're in an exterior cell.

**Periods and Hour Boundaries**:

| Period  | Hours         | Description                           |
| ------- | ------------- | ------------------------------------- |
| Dawn    | 4:00 – 6:00   | Pre-sunrise golden hour               |
| Sunrise | 6:00 – 8:00   | Sun coming up                         |
| Day     | 8:00 – 17:00  | Full daylight                         |
| Sunset  | 17:00 – 19:00 | Sun going down                        |
| Dusk    | 19:00 – 21:00 | Post-sunset blue hour                 |
| Night   | 21:00 – 4:00  | Full darkness (wraps around midnight) |

**Blending**: At the boundary between two periods, values blend over a 30-minute (0.5 game-hour) transition zone. Outside the transition zone, the current period's value is used at full weight.

User settings for Time of Day are persisted automatically to `SceneSettings/TimeOfDay.json` and survive game restarts.

**Float values** are linearly interpolated between periods based on these factors. If a setting isn't defined for a particular period, the saved baseline value is used for that period's weight — so the blend always sums to the correct total.

**Only float settings are allowed** in Time of Day. Integers, booleans, and strings cannot be smoothly interpolated between periods and are rejected — both from the UI dialog and from overwrite files. If an overwrite file contains a non-float setting, it is skipped with a log warning.

### Performance Optimizations

The blending runs every frame, so the system includes several optimizations:

-   **Hour throttle**: The blend only recalculates when the game hour has changed by more than 0.001 (about 0.36 real-time seconds at default timescale). This skips 98%+ of per-frame work.
-   **Epsilon cache**: For each float value, the last-applied result is cached. If the new result differs by less than 0.001, the `LoadSettings()` call is skipped entirely.
-   **Batch updates**: All dirty keys for a single feature are collected and applied in a single `LoadSettings()` call, rather than calling it once per key.

### Example

Say you set `CloudShadows.Opacity`:

-   Dawn: `0.3`
-   Day: `0.8`
-   Sunset: `0.5`
-   Night: `0.1`

(Sunrise and Dusk are left undefined — they'll fall back to the baseline.)

At 5:30 (mid-Dawn, 30 min before Sunrise transition):

-   Dawn factor = 1.0, result = `0.3`

At 5:45 (Dawn→Sunrise transition starts, 15 min left):

-   Dawn factor ≈ 0.5, Sunrise factor ≈ 0.5
-   Sunrise has no override → uses baseline (say `0.6`)
-   Result = 0.5 × 0.3 + 0.5 × 0.6 = `0.45`

At 12:00 (mid-Day):

-   Day factor = 1.0, result = `0.8`

---

## UI Guide

The Scene Settings Manager is accessed through the **Weather Editor** window (`F8` by default). It adds two categories to the objects window sidebar: **Interior Only** and **Time of Day**.

### Accessing the Scene Settings Panels

```
┌─────────────────────────────────────────────────────────────────┐
│  Weather Editor                                             [X] │
├──────────────────┬──────────────────────────────────────────────┤
│  Categories      │                                              │
│ ───────────────  │                  (Panel content              │
│  Weather         │                   depends on                 │
│  ImageSpace      │                   selected category)         │
│  Lighting Templ. │                                              │
│  Cell Lighting   │                                              │
│  Vol. Lighting   │                                              │
│  Shader Particle │                                              │
│  Lens Flare      │                                              │
│  Visual Effect   │                                              │
│ ───────────────  │                                              │
│▸ Interior Only   │  ◄── Scene Settings Manager categories       │
│▸ Time of Day     │                                              │
│                  │                                              │
└──────────────────┴──────────────────────────────────────────────┘
```

Select **Interior Only** or **Time of Day** from the left sidebar to open the corresponding panel.

---

### Interior Only Panel (UI)

```
┌───────────────────────────────────────────────────────────────┐
│  Interior Only Settings                                  [+]  │
│  ──────────────────────────────────────────────────────────   │
│                                                               │
│  Overwrite Files              [Pause All] [Delete All]        │
│  ─────────────────────────────────────────────────────        │
│  ▼ Screen Space GI:                                           │
│      EnableGI           [V]                    [●] [X]        │
│      AmbientIntensity   [0.500___]             [●] [X]        │
│  · · · · · · · · · · · · · · · · · · · · · ·                  │
│  ▼ Subsurface Scattering:                                     │
│      Intensity          [0.200___]             [●] [X]        │
│                                                               │
│  User Settings                [Pause All] [Delete All]        │
│  ─────────────────────────────────────────────────────        │
│  ▼ Linear Lighting:                                           │
│      GammaCorrection    [2.200___]             [●] [X]        │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

The **[+]** button is **right-aligned** on the header line.

Clicking it opens the **Add Feature Settings** dialog:

```
┌───────────────────────────────────────┐
│  Add Feature Settings            [X]  │
│  ┌──────────────────────────────────┐ │
│  │ Select Feature...             ▼  │ │
│  └──────────────────────────────────┘ │
│  ──────────────────────────────────── │
│  [Select All] [Select None]           │
│                                       │
│  ┌──────────────────────────────────┐ │
│  │ [✓] EnableGI                     │ │
│  │ [ ] AmbientIntensity             │ │
│  │ [ ] IndirectLightingStrength     │ │
│  │ [ ] MaxDistance                  │ │
│  │ [ ] NumSteps                     │ │
│  │ ...  (scrollable)                │ │
│  └──────────────────────────────────┘ │
│                                       │
│  [         Add (1)                 ]  │
└───────────────────────────────────────┘
```

**Elements:**

| Element                      | Description                                                                                                                                                                                                      |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **[+] button**               | Opens the Add Feature Settings dialog to select a feature and its settings.                                                                                                                                      |
| **Feature dropdown**         | Lists whitelisted features. Selecting one populates the setting checkbox list below.                                                                                                                             |
| **Select All / Select None** | Bulk-select or clear all checkboxes in the settings list.                                                                                                                                                        |
| **Settings checkbox list**   | Scrollable list of JSON keys from the feature's `SaveSettings()`. For Time of Day, only float keys are shown (integers, booleans, and strings are excluded). Already-added settings appear checked and disabled. |
| **Add button**               | Adds all checked settings with their current values. Shows the count of selected settings. Closes the dialog on success.                                                                                         |
| **Overwrite Files section**  | Entries loaded from mod author JSON files. Values are read-only (greyed out) — mod authors set them. You can pause or delete individual entries or all at once.                                                  |
| **User Settings section**    | Entries you added through the UI. Values are editable.                                                                                                                                                           |
| **Value editor**             | Checkbox for booleans, number input for floats/integers.                                                                                                                                                         |
| **[●] toggle**               | Pause/resume individual entries. Paused entries are ignored without being deleted.                                                                                                                               |
| **[X] button**               | Delete the entry. For overwrites, this deletes the file from disk (with confirmation).                                                                                                                           |
| **Pause All / Delete All**   | Bulk controls per section.                                                                                                                                                                                       |

**Entries are grouped by feature** with collapsible tree nodes, sorted alphabetically. Light separators appear between feature groups for visual clarity.

---

### Time of Day Panel (UI)

A row of named add buttons sits below the header, one per period plus an "Add All" shortcut:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  Time of Day Settings (Exterior Only)  [Day 12.0h]                          │
│  ────────────────────────────────────────────────────────────────────────── │
│  [Add Dawn][Add Sunrise][Add Day][Add Sunset][Add Dusk][Add Night][Add All] │
│  ────────────────────────────────────────────────────────────────────────── │
│                                                                             │
│  Overwrite Files              [Pause All] [Delete All]                      │
│  ┌─────────┬────────┬────────┬────────┬────────┬──────┬───────┐             │
│  │Setting  │ Dawn   │Sunrise │  Day   │ Sunset │ Dusk │ Night │             │
│  │         │ [●][X] │ [●][X] │ [●][X] │ [●][X] │[●][X]│[●][X] │             │
│  ├─────────┼────────┼────────┼────────┼────────┼──────┼───────┤             │
│  │CloudShadows:                                               │             │
│  │ Opacity │ 0.300  │  --    │ 0.800  │ 0.500  │  --  │ 0.100 │             │
│  │         │ [●][X] │        │ [●][X] │ [●][X] │      │[●][X] │             │
│  └─────────┴────────┴────────┴────────┴────────┴──────┴───────┘             │
│                                                                             │
│  User Settings                [Pause All] [Delete All]                      │
│  ┌─────────┬────────┬────────┬────────┬────────┬──────┬───────┐             │
│  │Setting  │ Dawn   │Sunrise │  Day   │ Sunset │ Dusk │ Night │             │
│  │         │ [●][X] │ [●][X] │ [●][X] │ [●][X] │[●][X]│[●][X] │             │
│  ├─────────┼────────┼────────┼────────┼────────┼──────┼───────┤             │
│  │Skylighting:                                                │             │
│  │ MixAmt  │ 0.400  │ 0.600  │ 0.800  │ 0.600  │0.400 │ 0.200 │             │
│  │         │ [●][X] │ [●][X] │ [●][X] │ [●][X] │[●][X]│[●][X] │             │
│  └─────────┴────────┴────────┴────────┴────────┴──────┴───────┘             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

Clicking an **Add** button opens the **Add Feature Settings** dialog (see above). **Only float settings appear** in the TOD dialog — integers, booleans, and strings cannot be smoothly transitioned between periods and are excluded. Overwrite files containing non-float TOD settings are also rejected at load time with a log warning.

**Elements:**

| Element                  | Description                                                                                                                                                                                                                         |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Header**               | Shows the current period and game hour (e.g., `[Day 12.0h]`).                                                                                                                                                                       |
| **Add buttons**          | An inline row of small buttons — one per period ("Add Dawn" through "Add Night") plus "Add All". Each opens a dialog scoped to that period; "Add All" populates all 6 periods at once.                                             |
| **Header controls**      | Each period column header includes a toggle [●] (pause/unpause all entries in that period) and [X] (delete all entries in that period) below the period name.                                                                       |
| **Period columns**       | One column per period. The active period column is highlighted; inactive periods are dimmed. `--` means no override for that period (falls back to baseline).                                                                       |
| **Row-level controls**   | Each setting row has a toggle (pause all periods) and delete (remove all periods) button in the Setting column.                                                                                                                     |
| **Per-cell controls**    | Each individual period cell has its own value editor, pause toggle, and delete button.                                                                                                                                              |
| **Setting filter**       | The add dialog only shows float settings. Integers, booleans, and strings are excluded since they cannot be smoothly interpolated between periods. Overwrite files are also validated — non-float TOD entries are rejected at load. |

---

### Feature Settings Page (Scene Toggle)

When scene settings are actively controlling a feature, its settings page in the main CS menu shows a toggle:

```
┌──────────────────────────────────────────────────────────────────┐
│  Screen Space GI                            v2.1.0    [●] [▼]    │
│  High quality ambient occlusion and indirect lighting.           │
│  ────────────────────────────────────────────────────            │
│                                                                  │
│  [●] Scene Specific Settings                                     │
│  ────────────────────────────                                    │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐   │
│  │  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   │   │
│  │  ░░░ (All settings greyed out while scene settings  ░░░   │   │
│  │  ░░░  are active.) ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   │   │
│  │  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   │   │
│  └───────────────────────────────────────────────────────────┘   │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

**Behaviour:**

-   The **Scene Specific Settings** toggle appears only when scene entries exist for this feature (active or paused).
-   When **active** (toggle on, green): All feature settings are **disabled** (greyed out). The Scene Settings Manager is controlling values.
-   When **paused** (toggle off): Scene settings stop applying. Feature settings become editable again. This is per-feature — it doesn't affect other features.
-   The **"Apply Override" button** (from the Settings Override Manager) is also disabled while scene settings are active, to prevent conflicting writes.

**Why all settings are greyed out, not just the overridden ones:**

The entire feature settings page is disabled because the scene settings toggle is drawn at the top of the feature page, before any individual settings are rendered. Since ImGui draws top-to-bottom, disabling at the page level greys out everything below it — there's no per-setting knowledge at that point in the draw call. This is functionally fine because the Scene Settings Manager only modifies the specific keys it has entries for; all other settings stay at their current values. Pausing all scene settings to edit a non-overridden setting is the natural workflow — if you're tweaking settings manually, you'd want scene overwrites paused anyway to see your changes without interference. And if you need a setting to change contextually while scene settings are active, you'd be adding it as a scene setting entry rather than editing it on the feature page.

---

## For Mod Authors: Overwrite Files

Mod authors can ship pre-configured scene settings as JSON files. These appear in the **Overwrite Files** section of the UI, separate from user settings. Users can pause or delete them, but can't edit their values.

### Directory Structure

```
Data/
└── SKSE/
    └── Plugins/
        └── CommunityShaders/
            └── SceneSettings/
                ├── InteriorOnly.json         ← User settings (auto-saved)
                ├── TimeOfDay.json            ← User settings (auto-saved)
                ├── InteriorOnly/             ← Overwrite files directory
                │   ├── MyModPack_ScreenSpaceGI_EnableGI.json
                │   ├── MyModPack_SubsurfaceScattering_Intensity.json
                │   └── AnotherMod_LinearLighting_GammaCorrection.json
                └── TimeOfDay/                ← Overwrite files directory
                    ├── Dawn/                 ← Per-period subdirectories
                    │   ├── MyModPack_CloudShadows_Opacity.json
                    │   └── MyModPack_Skylighting_MixAmount.json
                    ├── Sunrise/
                    │   └── MyModPack_CloudShadows_Opacity.json
                    ├── Day/
                    │   └── MyModPack_CloudShadows_Opacity.json
                    ├── Sunset/
                    │   └── MyModPack_CloudShadows_Opacity.json
                    ├── Dusk/
                    │   └── MyModPack_Skylighting_MixAmount.json
                    └── Night/
                        ├── MyModPack_CloudShadows_Opacity.json
                        └── MyModPack_Skylighting_MixAmount.json
```

### Interior Only Overwrites

Place JSON files in `CommunityShaders/SceneSettings/InteriorOnly/`.

**File format:**

```json
{
    "_feature": "ScreenSpaceGI",
    "EnableGI": false
}
```

**Rules:**

-   Each file must contain **exactly one setting** (one non-metadata key).
-   The `_feature` field identifies the target feature. If omitted, the system tries to infer it from the filename (the part after the last underscore must match a feature short name).
-   Keys starting with `_` are treated as metadata and ignored when extracting the setting.

### Time of Day Overwrites

Place JSON files in `CommunityShaders/SceneSettings/TimeOfDay/{PeriodName}/`.

Each period has its own subdirectory: `Dawn/`, `Sunrise/`, `Day/`, `Sunset/`, `Dusk/`, `Night/`.

The file format is the same as Interior Only:

```json
{
    "_feature": "CloudShadows",
    "Opacity": 0.3
}
```

To set `Opacity` across multiple periods, create the same-named file in each period's directory with different values:

```
TimeOfDay/Dawn/MyMod_CloudShadows_Opacity.json     → {"_feature": "CloudShadows", "Opacity": 0.3}
TimeOfDay/Day/MyMod_CloudShadows_Opacity.json      → {"_feature": "CloudShadows", "Opacity": 0.8}
TimeOfDay/Sunset/MyMod_CloudShadows_Opacity.json   → {"_feature": "CloudShadows", "Opacity": 0.5}
TimeOfDay/Night/MyMod_CloudShadows_Opacity.json    → {"_feature": "CloudShadows", "Opacity": 0.1}
```

Periods without a file fall back to the feature's baseline value during blending.

### Overwrite File Format

| Field                            | Required?            | Description                                                                                               |
| -------------------------------- | -------------------- | --------------------------------------------------------------------------------------------------------- |
| `_feature`                       | Recommended          | The feature's short name (e.g., `"ScreenSpaceGI"`, `"CloudShadows"`). If omitted, inferred from filename. |
| `{settingKey}`                   | Required (exactly 1) | The JSON key matching the feature's `SaveSettings()` output, with the desired override value.             |
| `_*` (any key starting with `_`) | Optional             | Metadata fields, ignored by the system. Use for comments, authorship, etc.                                |

**Example with metadata:**

```json
{
    "_feature": "Skylighting",
    "_author": "MyModPack",
    "_description": "Reduce skylighting at night for darker evenings",
    "_version": "1.0",
    "MixAmount": 0.2
}
```

### Best Practices for Mod Authors

1. **One setting per file.** The system enforces this — files with multiple non-metadata keys are skipped. This makes overrides granular: users can delete one specific override without losing others.

2. **Use descriptive filenames.** The naming convention `{ModName}_{FeatureName}_{SettingKey}.json` is recommended. The system can infer the feature name from the part after the last underscore if `_feature` is missing.

3. **Test with the UI.** After installing your overwrite files, open the Weather Editor and check the Interior Only or Time of Day panel. Your entries should appear in the "Overwrite Files" section. If they don't, check the CS log for warnings.

4. **Don't override everything.** Only override settings that genuinely need to change for your visual goal. Leave others at baseline so users' personal settings are respected.

5. **Ship files through your mod manager.** Overwrite files are just JSON in a folder — they can be installed and uninstalled via any mod manager (MO2, Vortex) like any other loose file.

6. **You can't target non-whitelisted features.** The file will be loaded but the feature won't pass the whitelist filter, so it will be silently skipped with a log warning. This is intentional — some features aren't safe to hot-swap.

### Conflict Resolution

If two mods ship overwrite files for the same feature, setting, and period, the **first one loaded wins** (alphabetical order by filename). Duplicate entries are silently skipped. The second mod's file won't appear in the UI. Use distinctive mod name prefixes in filenames to avoid conflicts.

If both an overwrite file and a user setting exist for the same feature and key, the **overwrite wins**. User settings are applied first, then overwrites layer on top (last-write-wins). The value editor for that user entry is greyed out in the UI to indicate it's being overridden.

### Troubleshooting: Overwrite File Not Appearing

If your file doesn't show up in the Overwrite Files section of the UI, check:

1. The file is in the correct directory (`SceneSettings/InteriorOnly/` or `SceneSettings/TimeOfDay/{Period}/`).
2. The file has a `.json` extension.
3. The file contains exactly one non-metadata key (keys starting with `_` are metadata).
4. The `_feature` field (or inferred feature name from the filename) matches a loaded, whitelisted feature.
5. You restarted Skyrim after adding the file.
6. Check `CommunityShaders.log` for warnings about skipped or malformed files.

---

## For Developers: Adding Features to the Whitelist

Adding Scene Settings Manager support for a feature is trivial — it requires **no changes to the feature itself**.

### Steps

1. Open `src/SceneSettingsManager.cpp`.
2. Find the appropriate whitelist:
    - `GetInteriorRelevantFeatureNames()` for interior support.
    - `GetExteriorRelevantFeatureNames()` for time-of-day support.
3. Add the feature's short name to the `unordered_set`:

```cpp
std::vector<std::string> SceneSettingsManager::GetExteriorRelevantFeatureNames()
{
    static const std::unordered_set<std::string> whitelist = {
        "CloudShadows",
        "ExponentialHeightFog",
        "GrassLighting",
        "YourNewFeature",   // ← add here
    };
    return FilterFeatureNames(whitelist);
}
```

### Requirements

Before whitelisting a feature, verify:

1. **`LoadSettings()` is safe to call at runtime.** It should not trigger shader recompilation, buffer reallocation, or other expensive operations. If it does, the feature may only be safe for the Interior whitelist (called once per cell transition) and not the Exterior/TOD whitelist (called per frame).

2. **Settings are JSON-serializable.** The feature's `SaveSettings()` and `LoadSettings()` should produce and consume a flat JSON object. Nested objects are not supported by the Scene Settings Manager's per-key override system.

3. **The feature does not crash when unknown keys are present.** `LoadSettings()` receives the full JSON blob with the patched key — it should gracefully ignore keys it doesn't recognize.

That's it. No interface to implement, no registration call to add. The Scene Settings Manager picks up the feature automatically through `Feature::FindFeatureByShortName()` and interacts with it purely through the SaveSettings/LoadSettings JSON round-trip.

---

## The Whitelist and Why Features Don't Opt In

### How the Whitelist Works

Not every feature is exposed to the Scene Settings Manager. Features must be present in one of two static whitelists:

**Interior-Relevant Features** — settings that make sense to override in interiors:

```
ScreenSpaceGI          SubsurfaceScattering   LinearLighting
ImageBasedLighting     PostProcessing         ScreenSpacePointLightShadows
ScreenSpaceRayTracing  VanillaFresnel
```

**Exterior/Time-of-Day-Relevant Features** — settings that make sense to vary across the day:

```
CloudShadows           ExponentialHeightFog   GrassLighting
ImageBasedLighting     LinearLighting         Skylighting
SubsurfaceScattering   TerrainShadows         WetnessEffects
```

### Why Not Make It Opt-In Per Feature?

You might wonder: why not have each feature declare `bool SupportsSceneSettings()` or register itself? There are several reasons:

1. **Zero feature code changes.** The whole point is that features never need to know the Scene Settings Manager exists. An opt-in system would require every feature to add a method, defeating the decoupled design.

2. **Centralized safety control.** Some features can't safely have their settings hot-swapped at runtime. For example, `ScreenSpaceGI` is excluded from the exterior/TOD whitelist because its `LoadSettings()` triggers synchronous recompilation of 6 compute shaders — causing massive lag if called every frame during time-of-day blending. A centralized whitelist lets the maintainers exclude problematic features without touching feature code.

3. **Easy to extend.** Adding a new feature to the whitelist is a single-line diff. There's no API to implement, no registration call to add, no interface to satisfy. When a new feature is developed — even one that hasn't been merged yet — it can be added to the whitelist in the same PR or in a follow-up.

4. **Even whitelist changes are less work than a coupled system.** In the unlikely event that the whitelist needs updating (a feature is added, removed, or moved between lists), the change is a single line in one file — `SceneSettingsManager.cpp`. In a coupled system where features opt themselves in, the same change would require editing the feature's own code, which is strictly more work. Any change you'd need to make to the whitelist is a change you'd _also_ need to make in a coupled system — except in the coupled version, you'd be editing the feature itself, dealing with its build dependencies, and potentially breaking its tests. The whitelist approach is always equal or less effort.

### Features That Aren't Whitelisted (and Why)

Some features are intentionally missing:

-   **ScreenSpaceGI** (exterior whitelist): `LoadSettings()` recompiles 6 compute shaders synchronously. Fine for interior transitions (happens once), but not for per-frame TOD blending. It _is_ on the interior whitelist since interior overrides only apply once on cell transition.
-   **VolumetricLighting, LightLimitFix**: Heavy GPU features where hot-swapping settings could cause transient artifacts or require buffer reallocation.
-   **TerrainBlending, TerrainVariation**: Terrain features that work at a mesh level and don't benefit from per-frame setting changes.

As features are improved and their `LoadSettings()` paths become cheaper, they can be promoted to the whitelist with a single-line change. Even in this scenario, the whitelist approach is less work than a coupled system — a coupled system would require the same decision about whether to enable scene settings support, but the change would live inside the feature's own code rather than in a centralized, reviewable list.

---

## Comparison: Scene Settings Manager vs Settings Override Manager

Community Shaders has two systems that modify feature settings after boot. They serve different purposes and operate at different layers.

|                                 | Scene Settings Manager                                       | Settings Override Manager                                 |
| ------------------------------- | ------------------------------------------------------------ | --------------------------------------------------------- |
| **Purpose**                     | Context-dependent overrides (interior/exterior, time of day) | Permanent baseline overrides (mod author presets)         |
| **When applied**                | At runtime, on cell transitions and per-frame (TOD)          | At boot, when features first load settings                |
| **Reverts?**                    | Yes — automatically when context changes                     | No — permanent until manually reset                       |
| **Feature coupling**            | Zero — uses SaveSettings/LoadSettings JSON round-trip        | Zero — merges JSON on top of defaults at boot             |
| **Granularity**                 | Per-setting, per-context (interior, per-period)              | Per-setting, per-feature, or global                       |
| **User editing**                | In-game UI (Weather Editor panels)                           | "Apply Override" button per feature                       |
| **Mod author format**           | One setting per JSON file, in SceneSettings/ subfolders      | Multi-setting JSON files in Overrides/ folder             |
| **File naming**                 | `{ModName}_{FeatureName}_{SettingKey}.json`                  | `{ModName}_{FeatureName}.json` or `{ModName}_Global.json` |
| **Priority**                    | Higher — applies on top of everything else                   | Lower — applies at boot, overwritten by scene settings    |
| **Blending**                    | Yes — float values smoothly interpolate between TOD periods  | No — values are merged, not blended                       |
| **Interior/Exterior awareness** | Yes — core feature                                           | No — applies everywhere                                   |

The Settings Override Manager establishes the **baseline** that the Scene Settings Manager saves and modifies. They're complementary:

-   A mod author might use the **Override Manager** to set `CloudShadows.Opacity` to `0.6` as their recommended default.
-   They might then use the **Scene Settings Manager** to set `Opacity` to `0.3` at Night and `0.9` at Day.
-   The saved baseline would be `0.6` (from the Override Manager), and TOD would blend between `0.3`, `0.9`, and the baseline for uncovered periods.

---

## FAQ

### General

**Q: Do I need to restart Skyrim after adding overwrite files?**
A: Yes. Overwrite files are discovered once during initialization. Changes to overwrite files on disk require a restart.

**Q: Can I use both Interior Only and Time of Day at the same time?**
A: They're mutually exclusive by context. Interior Only applies in interiors; Time of Day applies in exteriors. You can have entries for both — the system activates the correct one based on where you are.

**Q: What happens if I'm in an interior and exterior settings are active?**
A: This can't happen. The system detects cell type and automatically deactivates the wrong mode. If Interior Only is active, Time of Day is always off (and vice versa).

**Q: Do user settings persist between game sessions?**
A: Yes. User settings are saved to `SceneSettings/InteriorOnly.json` and `SceneSettings/TimeOfDay.json` automatically whenever you add, remove, or modify entries.

### Settings & Values

**Q: A setting I want to override doesn't appear in the dropdown. Why?**
A: The feature may not be on the whitelist, or the setting may have a type that isn't exposed through `SaveSettings()`. For Time of Day, only float settings appear — integers, booleans, and strings are excluded because they cannot be smoothly transitioned between periods. This applies to both the UI dialog and overwrite files. Check the whitelist in `SceneSettingsManager.cpp`.

**Q: Can I set a value outside the feature's normal range?**
A: You can enter any value, but the feature will clamp it to its valid range during `LoadSettings()`. The Scene Settings Manager logs a warning when this happens. Check the in-game log if your override seems to have no effect.

**Q: My overwrite file isn't showing up in the Overwrite Files section.**
A: Check:

1. The file is in the correct directory (`SceneSettings/InteriorOnly/` or `SceneSettings/TimeOfDay/{Period}/`).
2. The file has a `.json` extension.
3. The file contains exactly one non-metadata key.
4. The `_feature` field (or inferred feature name) matches a loaded, whitelisted feature.
5. You restarted Skyrim after adding the file.
6. Check `CommunityShaders.log` for warnings about skipped files.

**Q: I have an overwrite and a user setting for the same feature+key. Which wins?**
A: The overwrite wins. User settings are applied first, then overwrites overwrite them (last-write-wins). The user value editor is greyed out for settings that have an active overwrite.

### Performance

**Q: Does the Scene Settings Manager affect performance?**
A: The overhead is negligible. Time of Day blending runs per-frame but uses hour throttling (skips recalculation unless the game clock advanced enough) and epsilon caching (skips `LoadSettings()` calls if values haven't meaningfully changed). Interior Only overrides are applied once on cell transition.

**Q: Why is ScreenSpaceGI excluded from the Time of Day whitelist?**
A: Its `LoadSettings()` triggers synchronous recompilation of 6 compute shaders. This is fine for a one-time interior transition, but would cause massive lag if called every frame during TOD blending. It's on the Interior whitelist for this reason.

### Mod Authoring

**Q: Can I ship a single file that overrides multiple settings?**
A: No. Each overwrite file must contain exactly one non-metadata setting. This is by design — it lets users delete individual overrides without losing the rest. Create one file per setting.

**Q: What if two mods ship overwrite files for the same feature+setting+period?**
A: The first one loaded wins (alphabetical order by filename). Duplicate entries are silently skipped. The second mod's file will not appear. Use distinctive mod prefixes in filenames to avoid conflicts.

**Q: Can I target features that aren't on the whitelist?**
A: No. The overwrite file will be loaded but the feature won't be found in the whitelist filter, so it will be skipped with a log warning. The whitelist is intentional — some features aren't safe to hot-swap.

### Development

**Q: I'm developing a new feature. When should I add it to the whitelist?**
A: Once your feature's `LoadSettings()` is safe to call at runtime without expensive side effects (shader recompilation, buffer reallocation). You can add it to the whitelist in the same PR as the feature, or in a follow-up. No other code changes are needed.

**Q: Does the Scene Settings Manager work with VR?**
A: Yes. It uses the same SaveSettings/LoadSettings interface, which is VR-agnostic. The cell detection uses `RE::Sky::Mode` which works across all Skyrim variants.

**Q: How do I test my overwrite files during development?**
A: Place them in the appropriate directory, start Skyrim, and open the Weather Editor. Your entries should appear in the Overwrite Files section. Check the log for any warnings about skipped or malformed files.
