# Scene Manager Authoring UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replicate every feature's real `DrawSettings()` inside the Scene Manager's weather tab, scene panel, and location windows, with a per-control override toggle that writes to the scene context the panel is editing.

**Architecture:** A Detours-based interceptor wraps ten ImGui widget entry points. It is armed only for the duration of a replica's `DrawSettings()` call, so the normal feature menu is untouched. While armed, each intercepted widget resolves its value address through the generated catalog's `ControlResolver`, looks up (or creates) the matching scene entry for the panel's `SceneContextId`, draws a small gutter toggle to its right, and commits edits through a new context-keyed façade on `SceneSettingsManager`. Unresolved controls tail-call the original untouched.

**Tech Stack:** C++23, Dear ImGui 1.92.6 (vcpkg static lib), Microsoft Detours (already linked), CommonLibSSE-NG, nlohmann_json, Python 3 build-time catalog generator + pytest.

**User decisions (already made):**
- "We can't edit the feature files, because that adds extra maintenance to anybody developing features." Zero feature-side changes.
- "it needs to be very recognizable, losing stuff like conditional visibility and custom widgets is a no-no." The replica runs the feature's real `DrawSettings()`.
- Per-control toggles anchored to the right of each control, using real `ImGui::BeginDisabled`. Fake-disabled was explicitly rejected: "yes, that would be bad."
- The toggle serves A/B testing **and** persists an author's decision that an override is disabled without losing its value. The existing `SettingEntry::paused` flag already carries this.
- When paused, the control displays the **stored override value**.
- Same system applies identically to `DrawSceneManagerPanel()` and `DrawLocationWindows()`.
- Interception approach chosen: "Detour ImGui functions (Recommended)."
- Right-click menu on the control is the home for per-entry actions (revert, delete, copy).
- Time-of-day editing forces the game clock; locations are simulated only.
- Preset export beyond `SceneManager.json` is deferred: "sounds good to me, defer but keep in mind."
- Coloured "winning/losing" sliders are deferred, but the binding layer must keep the flexibility.

---

## Deviation from the approved spec (read before Task 1)

The spec's section 2 says "the detour set must cover every distinct `sourceWidget` backing a scene-controllable entry" and derives 13 functions by counting call sites in `src/Features`. That derivation is superseded.

`sourceWidget` in `SceneSettingsCatalog::SettingMetadata` is a **logical editor kind, not an ImGui function name**. Scanning the 299 scene-controllable entries in `build/ALL/generated/FeatureSceneSettingsAdapters.generated.cpp` gives:

| sourceWidget | count | reached through |
|---|---|---|
| `SliderFloat` | 164 | `ImGui::SliderFloat` |
| `Checkbox` | 72 | `ImGui::Checkbox` |
| `ColorEdit3` | 18 | `ImGui::ColorEdit3` |
| `ColorEdit4` | 16 | `ImGui::ColorEdit4` |
| `SliderInt` | 12 | `ImGui::SliderInt` |
| `Combo` | 9 | `ImGui::Combo` (2 overloads) **and** 3-arg `ImGui::RadioButton` |
| `SliderScalar` | 3 | `ImGui::SliderScalar` |
| `PercentageSlider` | 2 | `Util::PercentageSlider` → `ImGui::SliderFloat` **binding a stack temporary** |
| `SliderFloat2` | 2 | `ImGui::SliderFloat2` |
| `SliderAngle` | 1 | `ImGui::SliderAngle` |

So the real detour set is **10 ImGui functions / 11 entry points** (`Combo` has two used overloads), reached through a `sourceWidget → entry points` mapping rather than a name-for-name list. Task 1 encodes that mapping in the generator so a new widget kind fails the build instead of silently losing coverage.

`DragFloat` and `DragInt` (in the spec's list) have zero call sites in `src/Features` and are not detoured.

**Known gap, accepted for v1:** five of the 299 scene-controllable entries hand ImGui a stack temporary instead of their settings member, so `FindSettingForControl` misses and they draw with **no gutter** (unbindable, not greyed):

- ScreenSpaceGI `DepthDisocclusion` and `GISaturation`, through `Util::PercentageSlider` (`src/Utils/UI.cpp:390`), which scales into a local before calling `SliderFloat`.
- `IBL::DALCMode` (`src/Features/IBL.cpp:77-79`), `Upscaling::streamlineLogLevel` (`src/Features/Upscaling.cpp:450-452`), and `WetnessEffects::climatePreset` (`src/Features/WetnessEffects.cpp:408-415`), whose combos build a local `int` only because `ImGui::Combo` requires `int*` while the members are `uint` or an enum.

Both groups are cheap to close later, additively, with no feature-file edits:

- `Util::PercentageSlider` is shared infrastructure and already holds the real member address as its `data` parameter. One line inside it can pass that address to the interceptor before the inner `SliderFloat`, which also covers any future `Util::` wrapper that binds a temporary.
- The combos need the generator to emit a `{label i18n key -> setting}` map for the proxies it already detects (`unwrap_combo_proxy`, `is_proxy_write`). Everything the write needs is in the catalog: these entries are `EditorSemantic::Choice` with `NumericTransform::Identity` and a real `ChoiceMetadata[]` (DALCMode's runs `{0, "Luminance Ratio"}` through `{3, "DALC + Sky (Directional)"}`), so index maps to stored value via `choices[index].value`.

Note for whoever closes this: a *blind* label fallback across all 299 entries is still the wrong shape, because it would guess at controls the generator never classified. The fix above is a narrow, generator-emitted map covering only the handful of controls already known to be proxies.

**Second, smaller deviation:** `ImGuiItemFlags_MixedValue` lives in `imgui_internal.h` and, per its own comment, "Currently only supported by Checkbox()". The mixed-across-periods state therefore uses `MixedValue` for checkboxes and a distinct gutter tint for every other widget kind. See Task 5.

---

## File Structure

| File | Responsibility |
|---|---|
| `cmake/generate_scene_settings_catalog.py` | Adds the `sourceWidget → entry point` map, the coverage gate, and emits `kRequiredInterceptorEntryPoints` |
| `tests/test_scene_settings_catalog_generator.py` | Pytest for the new gate |
| `src/SceneSettingsManager.h/.cpp` | Adds `SceneContextType::Interior` and the `SceneContextId`-keyed entry façade |
| `src/CSEditor/SceneWidgetInterceptor.h/.cpp` | Detour install/uninstall, the arm scope, and the 11 detour bodies. Plumbing only. |
| `src/CSEditor/SceneWidgetBinding.h/.cpp` | Binding resolution, the three per-widget states, the gutter, the right-click menu, and commits. Policy only. |
| `src/CSEditor/SceneFeatureReplica.h/.cpp` | Draws one feature's real `DrawSettings()` inside an armed scope, with the failure banner |
| `src/CSEditor/SceneSettingsUI.h/.cpp` | Host wiring for all three panels |
| `src/CSEditor/Weather/WeatherWidget.cpp` | Passes the weather FormID into the tab |
| `src/Menu.cpp` | Calls `SceneWidgetInterceptor::Install()` once |
| `package/SKSE/Plugins/CommunityShaders/Translations/en.json` | Regenerated |

New `.cpp` files under `src/` are globbed by `cmake/AddCXXFiles.cmake` with `CONFIGURE_DEPENDS`, so no `CMakeLists.txt` edit is needed for them.

---

### Task 1: Catalog gate for interceptor coverage

**Goal:** The build fails if a feature introduces a widget kind the interceptor does not cover, and the generated header tells the interceptor which ImGui entry points it must install.

**Files:**
- Modify: `cmake/generate_scene_settings_catalog.py` (`validate_entries` at :4865, `write_catalog`, `main` at :4947)
- Modify: `tests/test_scene_settings_catalog_generator.py`

**Acceptance Criteria:**
- [ ] `SOURCE_WIDGET_ENTRY_POINTS` maps all 10 observed `sourceWidget` values to the ImGui entry-point names that can produce them.
- [ ] `validate_entries` raises `ValueError` naming the offending widget when a scene-controllable entry has a `sourceWidget` absent from the map.
- [ ] The generated header declares `std::span<const std::string_view> GetRequiredInterceptorEntryPoints()`, whose contents are the sorted union of entry points required by the scene-controllable entries actually present.
- [ ] `PercentageSlider` maps to `SliderFloat` (it is a `Util::` helper over it), so it does not trip the gate.
- [ ] Regenerating the catalog against the current tree still emits 299 scene-controllable entries and passes the existing floors.

**Verify:** `python -m pytest tests/test_scene_settings_catalog_generator.py -v` → all pass, including the two new tests.

**Steps:**

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_scene_settings_catalog_generator.py`:

```python
    def test_unmapped_source_widget_fails_validation(self):
        entries = [{
            "feature": "Foo", "path": "", "key": "Bar",
            "flags": "SceneSettingsCatalog::SettingFlag::SceneControllable",
            "editorSemantic": "Scalar", "sourceWidget": "KnobWidget",
        }]
        with self.assertRaises(ValueError) as caught:
            GENERATOR.validate_entries(entries, 1, 1, 1)
        self.assertIn("KnobWidget", str(caught.exception))

    def test_percentage_slider_maps_onto_slider_float(self):
        self.assertEqual(
            GENERATOR.SOURCE_WIDGET_ENTRY_POINTS["PercentageSlider"], ("SliderFloat",))
        entries = [{
            "feature": "Foo", "path": "", "key": "Bar",
            "flags": "SceneSettingsCatalog::SettingFlag::SceneControllable",
            "editorSemantic": "Scalar", "sourceWidget": "PercentageSlider",
        }]
        GENERATOR.validate_entries(entries, 1, 1, 1)
        self.assertEqual(GENERATOR.required_entry_points(entries), ["SliderFloat"])
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python -m pytest tests/test_scene_settings_catalog_generator.py -k "source_widget or percentage_slider" -v`
Expected: FAIL with `AttributeError: module 'scene_catalog_generator' has no attribute 'SOURCE_WIDGET_ENTRY_POINTS'`.

- [ ] **Step 3: Add the mapping and the helper**

Insert above `def validate_entries(` in `cmake/generate_scene_settings_catalog.py`:

```python
# Logical editor kind -> the ImGui entry points that can produce it. `sourceWidget` names an
# editor kind, not an ImGui function: Combo also covers 3-arg RadioButton groups, and
# PercentageSlider is Util::PercentageSlider over SliderFloat.
SOURCE_WIDGET_ENTRY_POINTS = {
    "Checkbox": ("Checkbox",),
    "ColorEdit3": ("ColorEdit3",),
    "ColorEdit4": ("ColorEdit4",),
    "Combo": ("Combo", "RadioButton"),
    "PercentageSlider": ("SliderFloat",),
    "SliderAngle": ("SliderAngle",),
    "SliderFloat": ("SliderFloat",),
    "SliderFloat2": ("SliderFloat2",),
    "SliderInt": ("SliderInt",),
    "SliderScalar": ("SliderScalar",),
}


def required_entry_points(entries: list[dict[str, object]]) -> list[str]:
    """Sorted union of ImGui entry points the interceptor must cover for these entries."""
    points: set[str] = set()
    for entry in entries:
        if "SceneSettingsCatalog::SettingFlag::SceneControllable" not in entry.get("flags", ""):
            continue
        points.update(SOURCE_WIDGET_ENTRY_POINTS.get(entry.get("sourceWidget", ""), ()))
    return sorted(points)
```

- [ ] **Step 4: Enforce the map in `validate_entries`**

Inside the existing `for entry in entries:` loop in `validate_entries`, after the `semantic` check (around :4903), add:

```python
        if allowed:
            widget = entry.get("sourceWidget", "")
            if widget not in SOURCE_WIDGET_ENTRY_POINTS:
                errors.append(
                    f"scene-controllable {identity} uses widget '{widget}' with no interceptor "
                    f"entry point; add it to SOURCE_WIDGET_ENTRY_POINTS and to "
                    f"SceneWidgetInterceptor's detour table")
```

- [ ] **Step 5: Emit the required entry points**

In `write_catalog`, add to the generated header's declarations (next to `GetVirtualAggregateControls`):

```cpp
	/// ImGui entry points SceneWidgetInterceptor must detour to cover every scene-controllable control.
	std::span<const std::string_view> GetRequiredInterceptorEntryPoints();
```

and to the generated source:

```python
    entry_points = required_entry_points(entries)
    lines.append("namespace {")
    lines.append("\tconstexpr std::string_view kRequiredInterceptorEntryPoints[] = {")
    for point in entry_points:
        lines.append(f'\t\t"{point}",')
    lines.append("\t};")
    lines.append("}")
    lines.append("")
    lines.append("std::span<const std::string_view> "
                 "SceneSettingsCatalog::GetRequiredInterceptorEntryPoints()")
    lines.append("{")
    lines.append("\treturn kRequiredInterceptorEntryPoints;")
    lines.append("}")
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `python -m pytest tests/test_scene_settings_catalog_generator.py -v`
Expected: PASS, no regressions.

- [ ] **Step 7: Regenerate against the live tree and confirm the floors hold**

Run:
```bash
python cmake/generate_scene_settings_catalog.py --source-dir . --out-dir build/ALL/generated \
  --min-entries 250 --min-controllable 290 --min-controllable-features 28
```
Expected: `Generated 326 scene setting catalog entries`, and `build/ALL/generated/SceneSettingsCatalog.generated.cpp` contains `kRequiredInterceptorEntryPoints` with exactly: `Checkbox`, `ColorEdit3`, `ColorEdit4`, `Combo`, `RadioButton`, `SliderAngle`, `SliderFloat`, `SliderFloat2`, `SliderInt`, `SliderScalar`.

```json:metadata
{"files": ["cmake/generate_scene_settings_catalog.py", "tests/test_scene_settings_catalog_generator.py"], "verifyCommand": "python -m pytest tests/test_scene_settings_catalog_generator.py -v", "acceptanceCriteria": ["SOURCE_WIDGET_ENTRY_POINTS covers all 10 observed sourceWidget values", "validate_entries raises ValueError naming an unmapped widget", "generated header declares GetRequiredInterceptorEntryPoints", "PercentageSlider maps to SliderFloat", "regeneration still emits 299 scene-controllable entries"], "modelTier": "standard"}
```

---

### Task 2: `SceneContextId` façade on `SceneSettingsManager`

**Goal:** One set of entry operations keyed on `SceneContextId`, so the interceptor never branches on interior vs time-of-day vs weather vs location.

**Files:**
- Modify: `src/SceneSettingsManager.h:526-543` (add `SceneContextType::Interior`), and add the façade declarations after the copy block at `:603`
- Modify: `src/SceneSettingsManager.cpp` (add definitions next to `GetCopyContextEntries` at `:5272`)

**Acceptance Criteria:**
- [ ] `SceneContextType::Interior` exists and `IsValidSceneContext` accepts it with `period == Count`, `weatherId == 0`, empty `locationFormKey`.
- [ ] `GetCopyContextEntries` returns `&GetEntries(SceneType::InteriorOnly)` for an Interior context; the copy switches at `:5229/:5248/:5277/:5539/:5723` all have `default:` arms, so nothing else needs touching. Interior contexts are simply never offered as copy sources or destinations in v1.
- [ ] `FindContextUserEntry` returns the index of the `EntrySource::User` entry matching feature/path/key **and** the context's period, or `nullopt`.
- [ ] `FindContextUserEntryPerPeriod` returns a `kPeriodCount`-sized array for TimeOfDay and Weather contexts, and an array with only index 0 populated for Interior and Location.
- [ ] `AddContextSetting` captures the feature's current base value, matching the underlying `AddSetting`/`AddWeatherSetting`/`AddLocationSetting` behaviour, and returns the new entry's index.
- [ ] `UpdateContextEntryValues`, `RemoveContextSetting`, `TogglePauseContextEntry`, `RevertContextEntryToDefault` all dispatch to the existing per-type implementations.
- [ ] Every façade call on an invalid context is a no-op (`IsValidSceneContext` guard) and never throws.

**Verify:** `python -m pytest tests/test_scene_settings_policy.py -v` → passes (guards against catalog/policy drift), and the plugin compiles: `./BuildDevFast.bat`.

**Steps:**

- [ ] **Step 1: Add the Interior context type**

In `src/SceneSettingsManager.h`, change the enum at `:526`:

```cpp
	/// Kind of scene context participating in a copy or authoring operation.
	enum class SceneContextType : std::uint8_t
	{
		Interior,
		TimeOfDay,
		Weather,
		Location,
	};
```

Leave `SceneContextId::type`'s default at `SceneContextType::TimeOfDay` so existing copy call sites are unchanged.

- [ ] **Step 2: Declare the façade**

In `src/SceneSettingsManager.h`, immediately after the `CopySettings` declaration at `:603`:

```cpp
	// --- Context-Keyed Entry Access (Scene Manager authoring UI) ---

	/// Index of the user entry for one setting in one context, or nullopt when none exists.
	std::optional<size_t> FindContextUserEntry(const SceneContextId& context,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey) const;

	/// Per-period user entry indices for one setting. Contexts without periods populate index 0 only.
	std::array<std::optional<size_t>, kPeriodCount> FindContextUserEntryPerPeriod(
		const SceneContextId& context, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey) const;

	/// Add a user entry capturing the feature's current base value. Returns its index.
	std::optional<size_t> AddContextSetting(const SceneContextId& context,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, bool deferSave = false);

	void UpdateContextEntryValues(const SceneContextId& context,
		std::span<const EntryValueUpdate> updates, bool deferSave = false);
	void RemoveContextSetting(const SceneContextId& context, size_t index);
	void TogglePauseContextEntry(const SceneContextId& context, size_t index);
	void RevertContextEntryToDefault(const SceneContextId& context, size_t index);

	/// Entries stored for one context, unfiltered by period. Empty when the context holds none.
	std::span<const SettingEntry> GetContextEntries(const SceneContextId& context) const;
```

- [ ] **Step 3: Accept Interior in the context validators**

In `src/SceneSettingsManager.cpp`, add to `IsValidSceneContext` (`:5228`) before the `TimeOfDay` case:

```cpp
	case SceneContextType::Interior:
		return context.period == TimeOfDayPeriod::Count && context.weatherId == 0 &&
		       context.locationFormKey.empty() && context.locationType == LocationTargetType::Location;
```

and to `IsSameSceneContext` (`:5247`):

```cpp
	case SceneContextType::Interior:
		return true;
```

and to `GetCopyContextEntries` (`:5277`):

```cpp
	case SceneContextType::Interior:
		return &GetEntries(SceneType::InteriorOnly);
```

- [ ] **Step 4: Implement the façade**

Add to `src/SceneSettingsManager.cpp` after `GetCopyContextEntries`:

```cpp
namespace
{
	/// The stored SceneType behind a non-weather, non-location context.
	SceneSettingsManager::SceneType ContextSceneType(SceneSettingsManager::SceneContextType type)
	{
		return type == SceneSettingsManager::SceneContextType::Interior ?
		           SceneSettingsManager::SceneType::InteriorOnly :
		           SceneSettingsManager::SceneType::TimeOfDay;
	}
}

std::span<const SceneSettingsManager::SettingEntry> SceneSettingsManager::GetContextEntries(
	const SceneContextId& context) const
{
	const auto* entries = GetCopyContextEntries(context);
	return entries ? std::span{ *entries } : std::span<const SettingEntry>{};
}

std::optional<size_t> SceneSettingsManager::FindContextUserEntry(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey) const
{
	if (!IsValidSceneContext(context))
		return std::nullopt;
	const auto entries = GetContextEntries(context);
	for (size_t index = 0; index < entries.size(); ++index) {
		const auto& entry = entries[index];
		if (entry.source == EntrySource::User && entry.featureShortName == featureShortName &&
			entry.settingPath == settingPath && entry.settingKey == settingKey &&
			entry.period == context.period)
			return index;
	}
	return std::nullopt;
}

std::array<std::optional<size_t>, SceneSettingsManager::kPeriodCount>
SceneSettingsManager::FindContextUserEntryPerPeriod(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey) const
{
	std::array<std::optional<size_t>, kPeriodCount> indices{};
	if (!IsValidSceneContext(context))
		return indices;

	const bool periodic = context.type == SceneContextType::TimeOfDay ||
	                      context.type == SceneContextType::Weather;
	const auto entries = GetContextEntries(context);
	for (size_t index = 0; index < entries.size(); ++index) {
		const auto& entry = entries[index];
		if (entry.source != EntrySource::User || entry.featureShortName != featureShortName ||
			entry.settingPath != settingPath || entry.settingKey != settingKey)
			continue;
		const auto slot = periodic ? static_cast<int>(entry.period) : 0;
		if (slot >= 0 && slot < kPeriodCount)
			indices[static_cast<size_t>(slot)] = index;
	}
	return indices;
}

std::optional<size_t> SceneSettingsManager::AddContextSetting(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey, bool deferSave)
{
	if (!IsValidSceneContext(context))
		return std::nullopt;

	bool added = false;
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay: {
		// AddSetting takes the value; the others capture it themselves.
		const auto value = GetFeatureSettingValue(featureShortName, settingPath, settingKey);
		if (value.is_null())
			return std::nullopt;
		added = AddSetting(ContextSceneType(context.type), featureShortName, settingPath, settingKey,
			value, context.period, deferSave);
		break;
	}
	case SceneContextType::Weather:
		added = AddWeatherSetting(context.weatherId, featureShortName, settingPath, settingKey,
			context.period, deferSave);
		break;
	case SceneContextType::Location: {
		const auto& config = GetLocationConfig(context.locationType, context.locationFormKey);
		added = AddLocationSetting(context.locationType, context.locationFormKey, config.name,
			config.cocCode, featureShortName, settingPath, settingKey, deferSave);
		break;
	}
	default:
		return std::nullopt;
	}

	if (!added)
		return std::nullopt;
	return FindContextUserEntry(context, featureShortName, settingPath, settingKey);
}

void SceneSettingsManager::UpdateContextEntryValues(const SceneContextId& context,
	std::span<const EntryValueUpdate> updates, bool deferSave)
{
	if (!IsValidSceneContext(context))
		return;
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		UpdateEntryValues(ContextSceneType(context.type), updates, deferSave);
		break;
	case SceneContextType::Weather:
		UpdateWeatherEntryValues(context.weatherId, updates, deferSave);
		break;
	case SceneContextType::Location:
		UpdateLocationEntryValues(context.locationType, context.locationFormKey, updates, deferSave);
		break;
	default:
		break;
	}
}

void SceneSettingsManager::RemoveContextSetting(const SceneContextId& context, size_t index)
{
	if (!IsValidSceneContext(context))
		return;
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		RemoveSetting(ContextSceneType(context.type), index);
		break;
	case SceneContextType::Weather:
		RemoveWeatherSetting(context.weatherId, index);
		break;
	case SceneContextType::Location:
		RemoveLocationSetting(context.locationType, context.locationFormKey, index);
		break;
	default:
		break;
	}
}

void SceneSettingsManager::TogglePauseContextEntry(const SceneContextId& context, size_t index)
{
	if (!IsValidSceneContext(context))
		return;
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		TogglePauseEntry(ContextSceneType(context.type), index);
		break;
	case SceneContextType::Weather:
		TogglePauseWeatherEntry(context.weatherId, index);
		break;
	case SceneContextType::Location:
		TogglePauseLocationEntry(context.locationType, context.locationFormKey, index);
		break;
	default:
		break;
	}
}

void SceneSettingsManager::RevertContextEntryToDefault(const SceneContextId& context, size_t index)
{
	if (!IsValidSceneContext(context))
		return;
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		RevertEntryToDefault(ContextSceneType(context.type), index);
		break;
	case SceneContextType::Weather:
		RevertWeatherEntryToDefault(context.weatherId, index);
		break;
	case SceneContextType::Location:
		RevertLocationEntryToDefault(context.locationType, context.locationFormKey, index);
		break;
	default:
		break;
	}
}
```

- [ ] **Step 5: Add the missing include**

`src/SceneSettingsManager.h` uses `std::array` in the new declaration. Confirm `<array>` is included at the top; add it if not.

- [ ] **Step 6: Verify**

Run: `python -m pytest tests/test_scene_settings_policy.py -v` → PASS.
Run: `./BuildDevFast.bat` → compiles clean, no new warnings in `SceneSettingsManager.cpp`.

```json:metadata
{"files": ["src/SceneSettingsManager.h", "src/SceneSettingsManager.cpp"], "verifyCommand": "python -m pytest tests/test_scene_settings_policy.py -v", "acceptanceCriteria": ["SceneContextType::Interior added and validated", "GetCopyContextEntries handles Interior", "FindContextUserEntry matches feature/path/key/period", "FindContextUserEntryPerPeriod fills index 0 for aperiodic contexts", "AddContextSetting returns the new index", "all façade calls no-op on invalid contexts"], "modelTier": "standard"}
```

---

### Task 3: `SceneWidgetInterceptor` detour plumbing

**Goal:** Eleven ImGui entry points are detoured once at startup; while unarmed each detour tail-calls the original, and an arm scope carries the feature and `SceneContextId` for the replica being drawn.

**Files:**
- Create: `src/CSEditor/SceneWidgetInterceptor.h`
- Create: `src/CSEditor/SceneWidgetInterceptor.cpp`
- Modify: `src/Menu.cpp:645` (`Menu::Init`)

**Acceptance Criteria:**
- [ ] `Install()` is idempotent, runs on the render thread from `Menu::Init()` before the first ImGui frame, and returns `false` with a populated `GetInstallError()` if any `DetourAttach` fails.
- [ ] On partial failure every already-attached detour is rolled back inside the same transaction, so the process is never left half-hooked.
- [ ] `Install()` asserts that every name in `SceneSettingsCatalog::GetRequiredInterceptorEntryPoints()` appears in the local detour table.
- [ ] With no `Scope` alive, every detour is a direct call to the original with unchanged arguments.
- [ ] `Scope` is non-copyable, nests safely (inner scope replaces the context and restores it on destruction), and is exception-safe.
- [ ] Both used `ImGui::Combo` overloads (array form and `\0`-separated form) and the 3-arg `ImGui::RadioButton` are detoured; the ambiguity is resolved with explicit `static_cast` of the function pointers.

**Verify:** `./BuildDevFast.bat` compiles, then launch the game and confirm `CommunityShaders.log` contains `SceneWidgetInterceptor: installed 11 entry points` and the normal feature menu renders and edits exactly as before.

**Steps:**

- [ ] **Step 1: Write the header**

Create `src/CSEditor/SceneWidgetInterceptor.h`:

```cpp
#pragma once

#include <string_view>

#include "SceneSettingsManager.h"

class Feature;

/// Intercepts ImGui widget calls so a Scene Manager replica of a feature's DrawSettings can bind
/// each control to a scene entry. Detours are installed once and stay inert until a Scope is alive.
namespace SceneWidgetInterceptor
{
	/// What the armed replica is editing. Copied into the interceptor for the scope's lifetime.
	struct Context
	{
		Feature* feature = nullptr;
		SceneSettingsManager::SceneContextId contextId;
		/// False writes every edit to all six periods, which is what "no time of day" means.
		bool perPeriod = true;
	};

	/** @brief Installs the detours. Idempotent; call from the render thread before the first frame. */
	bool Install();

	bool IsInstalled();

	/// Empty while healthy; otherwise names the entry point whose attach failed.
	std::string_view GetInstallError();

	/// Arms interception for the duration of one replica's DrawSettings call.
	class Scope
	{
	public:
		explicit Scope(const Context& context);
		~Scope();

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		Context previous;
		bool previousArmed;
	};

	/// The armed context, or nullptr when unarmed. For SceneWidgetBinding only.
	const Context* GetArmedContext();
}
```

- [ ] **Step 2: Write the detour table and install**

Create `src/CSEditor/SceneWidgetInterceptor.cpp`:

```cpp
#include "SceneWidgetInterceptor.h"

#include <detours/detours.h>
#include <imgui.h>

#include "SceneSettingsCatalog.generated.h"
#include "SceneWidgetBinding.h"

namespace
{
	using namespace SceneWidgetInterceptor;

	bool installed = false;
	std::string installError;

	bool armed = false;
	Context armedContext;

	// --- Originals ---
	auto* RealSliderFloat = &ImGui::SliderFloat;
	auto* RealSliderFloat2 = &ImGui::SliderFloat2;
	auto* RealSliderInt = &ImGui::SliderInt;
	auto* RealSliderScalar = &ImGui::SliderScalar;
	auto* RealSliderAngle = &ImGui::SliderAngle;
	auto* RealCheckbox = &ImGui::Checkbox;
	auto* RealColorEdit3 = &ImGui::ColorEdit3;
	auto* RealColorEdit4 = &ImGui::ColorEdit4;
	// Both Combo overloads are used in src/Features, and RadioButton is overloaded too, so the
	// address of each has to be disambiguated by its exact signature.
	auto* RealComboArray = static_cast<bool (*)(const char*, int*, const char* const[], int, int)>(
		&ImGui::Combo);
	auto* RealComboZeroSeparated = static_cast<bool (*)(const char*, int*, const char*, int)>(
		&ImGui::Combo);
	auto* RealRadioButton = static_cast<bool (*)(const char*, int*, int)>(&ImGui::RadioButton);
}
```

- [ ] **Step 3: Write the detour bodies**

Each body has the same shape: bail out to the original when unarmed, otherwise let `SceneWidgetBinding` wrap the call. `SceneWidgetBinding::Guard` is defined in Task 4; its contract is fixed here.

Append to `src/CSEditor/SceneWidgetInterceptor.cpp`:

```cpp
namespace
{
	bool DetouredSliderFloat(const char* label, float* v, float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!armed)
			return RealSliderFloat(label, v, vMin, vMax, format, flags);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Float(v));
		return guard.Finish(RealSliderFloat(label, guard.Float(), vMin, vMax, format, flags));
	}

	bool DetouredSliderFloat2(const char* label, float v[2], float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!armed)
			return RealSliderFloat2(label, v, vMin, vMax, format, flags);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::FloatVector(v, 2));
		return guard.Finish(RealSliderFloat2(label, guard.Float(), vMin, vMax, format, flags));
	}

	bool DetouredSliderInt(const char* label, int* v, int vMin, int vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!armed)
			return RealSliderInt(label, v, vMin, vMax, format, flags);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Int(v));
		return guard.Finish(RealSliderInt(label, guard.Int(), vMin, vMax, format, flags));
	}

	bool DetouredSliderScalar(const char* label, ImGuiDataType dataType, void* data,
		const void* min, const void* max, const char* format, ImGuiSliderFlags flags)
	{
		if (!armed)
			return RealSliderScalar(label, dataType, data, min, max, format, flags);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Scalar(data, dataType));
		return guard.Finish(
			RealSliderScalar(label, dataType, guard.Raw(), min, max, format, flags));
	}

	bool DetouredSliderAngle(const char* label, float* radians, float degreesMin, float degreesMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!armed)
			return RealSliderAngle(label, radians, degreesMin, degreesMax, format, flags);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Float(radians));
		return guard.Finish(
			RealSliderAngle(label, guard.Float(), degreesMin, degreesMax, format, flags));
	}

	bool DetouredCheckbox(const char* label, bool* v)
	{
		if (!armed)
			return RealCheckbox(label, v);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Bool(v));
		return guard.Finish(RealCheckbox(label, guard.Bool()));
	}

	bool DetouredColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags)
	{
		if (!armed)
			return RealColorEdit3(label, col, flags);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::FloatVector(col, 3));
		return guard.Finish(RealColorEdit3(label, guard.Float(), flags));
	}

	bool DetouredColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags)
	{
		if (!armed)
			return RealColorEdit4(label, col, flags);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::FloatVector(col, 4));
		return guard.Finish(RealColorEdit4(label, guard.Float(), flags));
	}

	bool DetouredComboArray(const char* label, int* current, const char* const items[],
		int itemCount, int popupMaxHeight)
	{
		if (!armed)
			return RealComboArray(label, current, items, itemCount, popupMaxHeight);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Int(current));
		return guard.Finish(
			RealComboArray(label, guard.Int(), items, itemCount, popupMaxHeight));
	}

	bool DetouredComboZeroSeparated(const char* label, int* current, const char* items,
		int popupMaxHeight)
	{
		if (!armed)
			return RealComboZeroSeparated(label, current, items, popupMaxHeight);
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Int(current));
		return guard.Finish(RealComboZeroSeparated(label, guard.Int(), items, popupMaxHeight));
	}

	bool DetouredRadioButton(const char* label, int* v, int buttonValue)
	{
		if (!armed)
			return RealRadioButton(label, v, buttonValue);
		// A radio group is several calls against one address; only the first owns the gutter.
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Int(v),
			SceneWidgetBinding::GutterPolicy::GroupMember);
		return guard.Finish(RealRadioButton(label, guard.Int(), buttonValue));
	}
}
```

- [ ] **Step 4: Write `Install`, `Scope`, and the accessors**

Append to `src/CSEditor/SceneWidgetInterceptor.cpp`:

```cpp
namespace
{
	/// One detourable entry point, named so the catalog's coverage list can be checked against it.
	struct DetourEntry
	{
		std::string_view name;
		PVOID* original;
		PVOID replacement;
	};

	DetourEntry GetDetourTable(size_t index)
	{
		static DetourEntry table[] = {
			{ "SliderFloat", reinterpret_cast<PVOID*>(&RealSliderFloat), &DetouredSliderFloat },
			{ "SliderFloat2", reinterpret_cast<PVOID*>(&RealSliderFloat2), &DetouredSliderFloat2 },
			{ "SliderInt", reinterpret_cast<PVOID*>(&RealSliderInt), &DetouredSliderInt },
			{ "SliderScalar", reinterpret_cast<PVOID*>(&RealSliderScalar), &DetouredSliderScalar },
			{ "SliderAngle", reinterpret_cast<PVOID*>(&RealSliderAngle), &DetouredSliderAngle },
			{ "Checkbox", reinterpret_cast<PVOID*>(&RealCheckbox), &DetouredCheckbox },
			{ "ColorEdit3", reinterpret_cast<PVOID*>(&RealColorEdit3), &DetouredColorEdit3 },
			{ "ColorEdit4", reinterpret_cast<PVOID*>(&RealColorEdit4), &DetouredColorEdit4 },
			{ "Combo", reinterpret_cast<PVOID*>(&RealComboArray), &DetouredComboArray },
			{ "Combo", reinterpret_cast<PVOID*>(&RealComboZeroSeparated), &DetouredComboZeroSeparated },
			{ "RadioButton", reinterpret_cast<PVOID*>(&RealRadioButton), &DetouredRadioButton },
		};
		return table[index];
	}

	constexpr size_t kDetourCount = 11;

	/// Every widget kind the catalog needs must have a detour, or scene authoring silently loses it.
	bool VerifyCoverage()
	{
		for (const auto required : SceneSettingsCatalog::GetRequiredInterceptorEntryPoints()) {
			bool found = false;
			for (size_t index = 0; index < kDetourCount && !found; ++index)
				found = GetDetourTable(index).name == required;
			if (!found) {
				installError = std::string{ required };
				return false;
			}
		}
		return true;
	}
}

bool SceneWidgetInterceptor::Install()
{
	if (installed)
		return true;
	if (!VerifyCoverage()) {
		logger::error("SceneWidgetInterceptor: no detour for required entry point '{}'; "
					  "scene authoring is off for this session",
			installError);
		return false;
	}

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	for (size_t index = 0; index < kDetourCount; ++index) {
		const auto entry = GetDetourTable(index);
		if (const auto result = DetourAttach(entry.original, entry.replacement); result != NO_ERROR) {
			installError = std::string{ entry.name };
			// Abort rolls back every attach in this transaction, so nothing is left half-hooked.
			DetourTransactionAbort();
			logger::error("SceneWidgetInterceptor: DetourAttach failed for ImGui::{} ({}); "
						  "scene authoring is off for this session",
				installError, result);
			return false;
		}
	}
	if (const auto result = DetourTransactionCommit(); result != NO_ERROR) {
		installError = "DetourTransactionCommit";
		logger::error("SceneWidgetInterceptor: DetourTransactionCommit failed ({}); "
					  "scene authoring is off for this session",
			result);
		return false;
	}

	installed = true;
	logger::info("SceneWidgetInterceptor: installed {} entry points", kDetourCount);
	return true;
}

bool SceneWidgetInterceptor::IsInstalled()
{
	return installed;
}

std::string_view SceneWidgetInterceptor::GetInstallError()
{
	return installError;
}

SceneWidgetInterceptor::Scope::Scope(const Context& context) :
	previous(armedContext), previousArmed(armed)
{
	armedContext = context;
	armed = installed && context.feature != nullptr;
}

SceneWidgetInterceptor::Scope::~Scope()
{
	armedContext = previous;
	armed = previousArmed;
}

const SceneWidgetInterceptor::Context* SceneWidgetInterceptor::GetArmedContext()
{
	return armed ? &armedContext : nullptr;
}
```

- [ ] **Step 5: Install from `Menu::Init`**

In `src/Menu.cpp`, add the include near the other CSEditor includes:

```cpp
#include "CSEditor/SceneWidgetInterceptor.h"
```

and at the end of `Menu::Init()` (`:645`), after the ImGui context is created:

```cpp
	// The detours must land before any feature draws, and this is the first render-thread point
	// where that is guaranteed. A failure is reported by the Scene Manager panels, not here.
	SceneWidgetInterceptor::Install();
```

- [ ] **Step 6: Verify**

Run: `./BuildDevFast.bat` → compiles.
Launch the game, open the Community Shaders menu, and confirm:
- `CommunityShaders.log` contains `SceneWidgetInterceptor: installed 11 entry points`.
- The normal feature menu (Settings → any feature) draws identically and every slider, checkbox, colour picker, and combo still edits its value.

```json:metadata
{"files": ["src/CSEditor/SceneWidgetInterceptor.h", "src/CSEditor/SceneWidgetInterceptor.cpp", "src/Menu.cpp"], "verifyCommand": "./BuildDevFast.bat", "acceptanceCriteria": ["Install is idempotent and rolls back on partial failure", "Install asserts coverage against GetRequiredInterceptorEntryPoints", "unarmed detours tail-call the original unchanged", "Scope is non-copyable and nests safely", "both Combo overloads and 3-arg RadioButton are detoured with explicit casts", "log line confirms 11 entry points installed"], "modelTier": "frontier"}
```

---

### Task 4: `SceneWidgetBinding` — the three per-widget states

**Goal:** Each intercepted control resolves to a scene entry, draws its gutter toggle, and commits edits to the right context, in the three states the spec defines: Absent, Active, Paused.

**Files:**
- Create: `src/CSEditor/SceneWidgetBinding.h`
- Create: `src/CSEditor/SceneWidgetBinding.cpp`

**Acceptance Criteria:**
- [ ] **Absent** (no entry): real pointer, live, no `BeginDisabled`. Gutter drawn unchecked. An edit creates the entry.
- [ ] **Active** (entry exists, not paused): real pointer, live. The live value already *is* the override because the scene layer applied it. Gutter drawn checked. An edit updates the entry's value.
- [ ] **Paused** (entry exists, paused): `ImGui::BeginDisabled()` around the real call, and the control is bound to a **temporary holding the stored override value**, so the greyed control shows what is stored, not what is live. No write ever reaches the feature member.
- [ ] Pointer resolution goes through `SceneSettingsCatalog::FindSettingForControl(feature, address)`. A miss means no gutter and a plain tail-call, leaving that control indistinguishable from the normal menu.
- [ ] Entry creation restores the pre-call value into the feature member **before** calling `AddContextSetting`, so the manager's `EnsureBaselines` snapshots the base and not the just-edited value. The pre-call value is captured unconditionally so no early return can skip the restore.
- [ ] While `ImGui::IsItemActive()` (mid-drag) commits use `deferSave = true`; on `ImGui::IsItemDeactivatedAfterEdit()` one `deferSave = false` commit lands the file write.
- [ ] Unchecking the gutter calls `TogglePauseContextEntry`; re-checking a paused entry unpauses it rather than creating a duplicate.
- [ ] The gutter is anchored on the same line to the right of the control, using the existing theme spacing, with no `ImGui::SameLine` drift when the control is inside a table cell.
- [ ] The binding layer exposes the resolved entry (index, paused, value, catalog metadata) to callers, so the deferred "winning/losing" colouring can be added without reshaping it.

**Verify:** `./BuildDevFast.bat`, then in-game open the weather widget → Scene Manager tab → pick IBL, drag a slider, and confirm via `communityshaders.feature get ImageBasedLighting` (DevBench bridge) that the base value is unchanged while `SceneManager.json` gained the entry.

**Steps:**

- [ ] **Step 1: Write the header**

Create `src/CSEditor/SceneWidgetBinding.h`:

```cpp
#pragma once

#include <cstdint>
#include <optional>

#include <imgui.h>

#include "SceneSettingsCatalog.generated.h"
#include "SceneSettingsManager.h"

/// Binds one intercepted ImGui control to a scene entry and owns its gutter toggle.
namespace SceneWidgetBinding
{
	/// The caller's storage, erased to the primitive kinds the catalog can persist.
	struct Value
	{
		enum class Kind : std::uint8_t
		{
			Bool,
			Int,
			Float,
			FloatVector,
			Scalar
		};

		Kind kind = Kind::Float;
		void* data = nullptr;
		std::uint8_t componentCount = 1;
		ImGuiDataType scalarType = ImGuiDataType_COUNT;

		static Value Bool(bool* data) { return { Kind::Bool, data, 1, ImGuiDataType_COUNT }; }
		static Value Int(int* data) { return { Kind::Int, data, 1, ImGuiDataType_COUNT }; }
		static Value Float(float* data) { return { Kind::Float, data, 1, ImGuiDataType_COUNT }; }
		static Value FloatVector(float* data, std::uint8_t count)
		{
			return { Kind::FloatVector, data, count, ImGuiDataType_COUNT };
		}
		static Value Scalar(void* data, ImGuiDataType type)
		{
			return { Kind::Scalar, data, 1, type };
		}
	};

	/// Whether this call owns the gutter. A radio group draws it on the first call per address.
	enum class GutterPolicy : std::uint8_t
	{
		Owner,
		GroupMember
	};

	/// How the bound control resolved this frame. Kept public so the deferred winning/losing
	/// colouring can read it without reshaping the guard.
	enum class State : std::uint8_t
	{
		Unbound,  // resolver miss: behaves exactly like the normal menu
		Absent,   // scene-controllable, no entry yet
		Active,   // entry exists and applies
		Paused    // entry exists and is held back
	};

	/// Wraps one intercepted widget call for the duration of that call.
	class Guard
	{
	public:
		Guard(const char* label, const Value& value, GutterPolicy policy = GutterPolicy::Owner);
		~Guard();

		Guard(const Guard&) = delete;
		Guard& operator=(const Guard&) = delete;

		/// Pointer the real ImGui call should bind: the caller's storage, or the paused holding value.
		bool* Bool() const;
		int* Int() const;
		float* Float() const;
		void* Raw() const;

		/// Closes the disabled scope, commits any edit, and draws the gutter. Returns what the
		/// intercepted function should return: never true while paused.
		bool Finish(bool changed);

		State GetState() const { return state; }

	private:
		void Commit();
		void DrawGutter();

		const char* label;
		Value value;
		GutterPolicy policy;
		State state = State::Unbound;

		const SceneSettingsCatalog::SettingMetadata* metadata = nullptr;
		SceneSettingsManager::SceneContextId contextId;
		std::optional<size_t> entryIndex;

		/// Captured unconditionally so entry creation can restore the base before snapshotting.
		float preCallFloat[4]{};
		int preCallInt = 0;
		bool preCallBool = false;
		std::uint64_t preCallScalar = 0;

		/// Storage the paused control is bound to, so no write reaches the feature member.
		float holdingFloat[4]{};
		int holdingInt = 0;
		bool holdingBool = false;
		std::uint64_t holdingScalar = 0;

		bool disabledOpened = false;
	};
}
```

- [ ] **Step 2: Resolve the binding in the constructor**

Create `src/CSEditor/SceneWidgetBinding.cpp` starting with:

```cpp
#include "SceneWidgetBinding.h"

#include <cstring>

#include "Feature.h"
#include "Menu.h"
#include "SceneWidgetInterceptor.h"
#include "Utils/UI.h"

namespace
{
	/// Gutter is a compact square so it reads as an annotation on the control, not a second control.
	constexpr float kGutterSize = 14.0f;

	/// Copies the caller's storage into a scratch buffer of the matching kind.
	void CopyOut(const SceneWidgetBinding::Value& value, void* destination)
	{
		using Kind = SceneWidgetBinding::Value::Kind;
		switch (value.kind) {
		case Kind::Bool:
			std::memcpy(destination, value.data, sizeof(bool));
			break;
		case Kind::Int:
			std::memcpy(destination, value.data, sizeof(int));
			break;
		case Kind::Float:
		case Kind::FloatVector:
			std::memcpy(destination, value.data, sizeof(float) * value.componentCount);
			break;
		case Kind::Scalar:
			std::memcpy(destination, value.data, ImGui::DataTypeGetInfo(value.scalarType)->Size);
			break;
		}
	}

	void CopyIn(const SceneWidgetBinding::Value& value, const void* source)
	{
		using Kind = SceneWidgetBinding::Value::Kind;
		switch (value.kind) {
		case Kind::Bool:
			std::memcpy(value.data, source, sizeof(bool));
			break;
		case Kind::Int:
			std::memcpy(value.data, source, sizeof(int));
			break;
		case Kind::Float:
		case Kind::FloatVector:
			std::memcpy(value.data, source, sizeof(float) * value.componentCount);
			break;
		case Kind::Scalar:
			std::memcpy(value.data, source, ImGui::DataTypeGetInfo(value.scalarType)->Size);
			break;
		}
	}
}

SceneWidgetBinding::Guard::Guard(const char* a_label, const Value& a_value, GutterPolicy a_policy) :
	label(a_label), value(a_value), policy(a_policy)
{
	// Captured before anything can return early, so entry creation always has the base to restore.
	CopyOut(value, PreCallStorage());

	const auto* context = SceneWidgetInterceptor::GetArmedContext();
	if (!context)
		return;

	metadata = SceneSettingsCatalog::FindSettingForControl(context->feature, value.data);
	if (!metadata || !SceneSettingsCatalog::IsSceneControllable(*metadata))
		return;

	contextId = context->contextId;
	auto* manager = SceneSettingsManager::GetSingleton();
	entryIndex = manager->FindContextUserEntry(contextId, std::string{ metadata->featureShortName },
		SplitSettingPath(metadata->settingPath), std::string{ metadata->settingKey });

	if (!entryIndex) {
		state = State::Absent;
		return;
	}

	const auto entries = manager->GetContextEntries(contextId);
	const auto& entry = entries[*entryIndex];
	state = entry.paused ? State::Paused : State::Active;
	if (state == State::Paused) {
		// The greyed control shows what is stored, not what the scene is currently running.
		StoreHoldingValue(entry.value);
		ImGui::BeginDisabled();
		disabledOpened = true;
	}
}
```

`PreCallStorage()`, `StoreHoldingValue()`, and `SplitSettingPath()` are small private helpers: `PreCallStorage` returns the address of the matching `preCall*` member for `value.kind`, `StoreHoldingValue` converts the entry's `json` into the matching `holding*` member (respecting `metadata->displayScale` and `metadata->numericTransform` via `SceneSettingsManager::GetNumericDisplayValue`), and `SplitSettingPath` splits the catalog's `settingPath` on `'/'` into the `std::vector<std::string>` the manager APIs take. Declare them as file-local statics plus two private members on `Guard`.

- [ ] **Step 3: Implement the pointer accessors**

```cpp
bool* SceneWidgetBinding::Guard::Bool() const
{
	return state == State::Paused ? const_cast<bool*>(&holdingBool) : static_cast<bool*>(value.data);
}

int* SceneWidgetBinding::Guard::Int() const
{
	return state == State::Paused ? const_cast<int*>(&holdingInt) : static_cast<int*>(value.data);
}

float* SceneWidgetBinding::Guard::Float() const
{
	return state == State::Paused ? const_cast<float*>(holdingFloat) : static_cast<float*>(value.data);
}

void* SceneWidgetBinding::Guard::Raw() const
{
	return state == State::Paused ? const_cast<std::uint64_t*>(&holdingScalar) : value.data;
}
```

- [ ] **Step 4: Implement `Finish`**

```cpp
bool SceneWidgetBinding::Guard::Finish(bool changed)
{
	if (disabledOpened) {
		ImGui::EndDisabled();
		disabledOpened = false;
	}

	if (state == State::Unbound)
		return changed;

	// Read the drag state before the gutter's own item replaces the current item.
	const bool dragging = ImGui::IsItemActive();
	const bool settled = ImGui::IsItemDeactivatedAfterEdit();

	if (changed && state != State::Paused) {
		commitDeferred = dragging && !settled;
		Commit();
	} else if (settled && state == State::Active) {
		// One non-deferred pass so the debounced save always lands on release.
		commitDeferred = false;
		Commit();
	}

	if (policy == GutterPolicy::Owner)
		DrawGutter();

	// A paused control must never report a change: nothing behind it moved.
	return state == State::Paused ? false : changed;
}
```

Add `bool commitDeferred = false;` to the private members.

- [ ] **Step 5: Implement `Commit`**

```cpp
void SceneWidgetBinding::Guard::Commit()
{
	auto* manager = SceneSettingsManager::GetSingleton();
	const auto featureShortName = std::string{ metadata->featureShortName };
	const auto settingPath = SplitSettingPath(metadata->settingPath);
	const auto settingKey = std::string{ metadata->settingKey };

	if (state == State::Absent) {
		// The base value must be back in the member before the manager snapshots its baseline,
		// or the edit itself is recorded as the feature's default.
		const auto edited = ReadEditedValue();
		CopyIn(value, PreCallStorage());
		entryIndex = manager->AddContextSetting(contextId, featureShortName, settingPath, settingKey, true);
		CopyIn(value, &edited);
		if (!entryIndex) {
			state = State::Unbound;
			return;
		}
		state = State::Active;
	}

	const auto updates = BuildEntryValueUpdates();
	manager->UpdateContextEntryValues(contextId, updates, commitDeferred);
}
```

`ReadEditedValue()` copies the caller's post-call storage into a local scratch of the same layout as `PreCallStorage()`. `BuildEntryValueUpdates()` returns a `std::vector<SceneSettingsManager::EntryValueUpdate>` holding `{ *entryIndex, json }` for the single-period case; Task 5 extends it to the aggregate and all-periods cases.

- [ ] **Step 6: Implement `DrawGutter`**

```cpp
void SceneWidgetBinding::Guard::DrawGutter()
{
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

	bool enabled = state == State::Active;
	ImGui::PushID(label);
	if (Util::FeatureToggle("##SceneOverride", &enabled,
			ImVec2(kGutterSize * Util::GetUIScale(), kGutterSize * Util::GetUIScale()))) {
		auto* manager = SceneSettingsManager::GetSingleton();
		if (entryIndex) {
			manager->TogglePauseContextEntry(contextId, *entryIndex);
		} else if (enabled) {
			// Checking an absent control captures the current value as the override.
			manager->AddContextSetting(contextId, std::string{ metadata->featureShortName },
				SplitSettingPath(metadata->settingPath), std::string{ metadata->settingKey });
		}
	}
	Util::AddTooltip(state == State::Absent ?
			T(TKEY("scene_override_absent"), "No override here. Edit the control, or tick to capture the current value.") :
		state == State::Paused ?
			T(TKEY("scene_override_paused"), "Override stored but held back. Tick to apply it.") :
			T(TKEY("scene_override_active"), "Override applies here. Untick to hold it back without losing the value."));
	ImGui::PopID();
}
```

Add `#define I18N_KEY_PREFIX "cs_editor."` after the includes and `#undef I18N_KEY_PREFIX` at the end of the file, matching `SceneSettingsUI.cpp:15`.

- [ ] **Step 7: Implement the destructor defensively**

```cpp
SceneWidgetBinding::Guard::~Guard()
{
	// Finish is always called on the happy path; this only closes a scope an exception skipped.
	if (disabledOpened)
		ImGui::EndDisabled();
}
```

- [ ] **Step 8: Verify**

Run: `./BuildDevFast.bat` → compiles.
In-game: open a weather widget → Scene Manager tab, pick a feature, drag one slider.
- The gutter appears checked after the drag.
- `SceneManager.json` gains one weather entry whose `originalValue` matches the feature's pre-edit value.
- `communityshaders.feature get ImageBasedLighting` (DevBench) reports the feature's base value unchanged after closing the panel.
- Unticking the gutter greys the slider and it shows the stored override value; the scene reverts to the base.

```json:metadata
{"files": ["src/CSEditor/SceneWidgetBinding.h", "src/CSEditor/SceneWidgetBinding.cpp"], "verifyCommand": "./BuildDevFast.bat", "acceptanceCriteria": ["Absent/Active/Paused states behave as specified", "Paused uses real BeginDisabled and binds a temporary holding the stored value", "resolver miss draws no gutter and tail-calls", "pre-call value restored before AddContextSetting so baselines are correct", "deferSave true mid-drag, false on IsItemDeactivatedAfterEdit", "unchecking calls TogglePauseContextEntry rather than deleting", "state is publicly readable for the deferred winning/losing colouring"], "modelTier": "frontier"}
```

---

### Task 5: Aggregates, radio groups, all-periods writes, and the right-click menu

**Goal:** Multi-component controls, radio groups, and the "time of day off" flat mode all behave correctly, and each bound control carries the per-entry actions the user asked for.

**Files:**
- Modify: `src/CSEditor/SceneWidgetBinding.h`
- Modify: `src/CSEditor/SceneWidgetBinding.cpp`

**Acceptance Criteria:**
- [ ] A `ColorEdit3`/`ColorEdit4`/`SliderFloat2` control resolves to the aggregate's first component and drives all of its siblings, read from `metadata->aggregateStart` and `metadata->aggregateCount`. One gutter governs the whole aggregate; ticking or unticking it applies to every component in one `UpdateContextEntryValues` call.
- [ ] A radio group draws its gutter exactly once. The first call for a given value address in a frame is the owner; later calls with `GutterPolicy::GroupMember` draw none. The owner is tracked in a per-frame set keyed on the address, cleared when the frame's `ImGui::GetFrameCount()` changes.
- [ ] With `Context::perPeriod == false`, one edit writes the same value to all six periods: `FindContextUserEntryPerPeriod` gives the existing indices, missing periods are created with `AddContextSetting` (deferred), and every index is written in one `UpdateContextEntryValues` call.
- [ ] With `perPeriod == false` and the six periods holding different values, the control renders **mixed**: `ImGuiItemFlags_MixedValue` for `Checkbox` (the only widget ImGui supports it on), and a distinct gutter tint (`Menu::GetTheme().StatusPalette.Warning`) plus a tooltip for every other widget kind.
- [ ] Right-clicking a bound control opens a popup with: Revert to original (`RevertContextEntryToDefault`), Delete override (`RemoveContextSetting`), and the entry's stored value shown as text. The popup is suppressed for `State::Unbound` and `State::Absent`.
- [ ] The popup never opens on a control the user is dragging (`ImGui::IsItemActive()` guard).

**Verify:** `./BuildDevFast.bat`, then in-game confirm on the weather tab: (a) an IBL colour picker creates three entries with one gutter; (b) with Time of Day off, editing one slider writes six entries; (c) turning Time of Day on after that shows the same value on every period; (d) right-click → Revert restores the original.

**Steps:**

- [ ] **Step 1: Extend the guard for aggregates**

Add to `Guard`'s private section in `SceneWidgetBinding.h`:

```cpp
	/// Entry indices this control owns: one per aggregate component, per period.
	std::vector<size_t> ownedEntries;
	/// True when the periods this control spans do not agree on a value.
	bool mixedAcrossPeriods = false;
```

In the constructor, after resolving `metadata`, replace the single `FindContextUserEntry` call with a resolution over the aggregate's components:

```cpp
	// A colour or vector control is several catalog entries behind one widget, so the gutter and
	// every commit have to move all of them together.
	componentCount = metadata->aggregateCount > 0 ? metadata->aggregateCount : 1;
	componentStart = metadata->aggregateStart > 0 ? metadata->aggregateStart : 0;
```

and resolve each component's entry through `SceneSettingsCatalog::FindSetting(featureShortName, settingPath, componentKey)`, collecting indices into `ownedEntries`. The state is `Absent` when `ownedEntries` is empty, `Paused` when every owned entry is paused, and `Active` otherwise (a partially paused aggregate reads as Active and ticking normalises it).

- [ ] **Step 2: Track radio-group ownership**

Add to the anonymous namespace in `SceneWidgetBinding.cpp`:

```cpp
	/// Radio groups are several calls against one address, so only the first per frame gets a gutter.
	int gutterFrame = -1;
	std::set<const void*> gutterOwners;

	bool ClaimGutter(const void* address)
	{
		if (const auto frame = ImGui::GetFrameCount(); frame != gutterFrame) {
			gutterFrame = frame;
			gutterOwners.clear();
		}
		return gutterOwners.insert(address).second;
	}
```

and change the gutter branch in `Finish`:

```cpp
	if (policy == GutterPolicy::Owner || ClaimGutter(value.data))
		DrawGutter();
```

- [ ] **Step 3: Implement the all-periods write**

Replace `BuildEntryValueUpdates()` with a version that fans out over periods when the armed context is flat:

```cpp
std::vector<SceneSettingsManager::EntryValueUpdate> SceneWidgetBinding::Guard::BuildEntryValueUpdates()
{
	std::vector<SceneSettingsManager::EntryValueUpdate> updates;
	updates.reserve(ownedEntries.size());
	const auto edited = ReadEditedJsonComponents();
	for (size_t component = 0; component < edited.size() && component < ownedEntries.size(); ++component)
		updates.push_back({ ownedEntries[component], edited[component] });
	return updates;
}
```

and in `Commit`, when `SceneWidgetInterceptor::GetArmedContext()->perPeriod` is false, iterate the six periods before building the updates:

```cpp
	// "Time of day off" is a global override for the context, which the data model expresses as
	// the same value in all six periods.
	if (!armed->perPeriod) {
		for (int period = 0; period < SceneSettingsManager::kPeriodCount; ++period) {
			auto periodContext = contextId;
			periodContext.period = static_cast<SceneSettingsManager::TimeOfDayPeriod>(period);
			EnsureEntriesFor(periodContext);
		}
	}
```

`EnsureEntriesFor` creates any missing entry with `AddContextSetting(..., /*deferSave=*/true)` and appends its index to `ownedEntries`.

- [ ] **Step 4: Detect and render the mixed state**

In the constructor, when the context is flat and periodic, compare the per-period entry values:

```cpp
	if (!armed->perPeriod) {
		const auto perPeriod = manager->FindContextUserEntryPerPeriod(
			contextId, featureShortName, settingPath, settingKey);
		const auto entries = manager->GetContextEntries(contextId);
		std::optional<json> first;
		for (const auto index : perPeriod) {
			if (!index)
				continue;
			if (!first)
				first = entries[*index].value;
			else if (entries[*index].value != *first)
				mixedAcrossPeriods = true;
		}
	}
```

In the constructor, when `mixedAcrossPeriods` and the widget is a checkbox, push the native flag; `ImGuiItemFlags_MixedValue` lives in `imgui_internal.h` and, per its own comment, is only honoured by `Checkbox`:

```cpp
	if (mixedAcrossPeriods && value.kind == Value::Kind::Bool) {
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
		mixedFlagPushed = true;
	}
```

with the matching `ImGui::PopItemFlag()` at the top of `Finish`. For every other kind the mixed signal is carried by the gutter: in `DrawGutter`, wrap the toggle in `ImGui::PushStyleColor(ImGuiCol_CheckMark, Menu::GetSingleton()->GetTheme().StatusPalette.Warning)` and append the mixed tooltip line. Add `#include <imgui_internal.h>` to `SceneWidgetBinding.cpp`.

- [ ] **Step 5: Add the right-click menu**

Insert into `Finish`, immediately before the gutter branch:

```cpp
	// A drag must not be interrupted by the popup, and an absent override has nothing to act on.
	if (!dragging && (state == State::Active || state == State::Paused))
		DrawContextMenu();
```

and implement:

```cpp
void SceneWidgetBinding::Guard::DrawContextMenu()
{
	ImGui::PushID(label);
	if (ImGui::BeginPopupContextItem("##SceneOverrideMenu")) {
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto entries = manager->GetContextEntries(contextId);
		if (!ownedEntries.empty() && ownedEntries.front() < entries.size())
			Util::Text::Disabled("%s", entries[ownedEntries.front()].value.dump().c_str());
		ImGui::Separator();

		if (ImGui::MenuItem(T(TKEY("scene_override_revert"), "Revert to original")))
			for (const auto index : ownedEntries)
				manager->RevertContextEntryToDefault(contextId, index);

		if (ImGui::MenuItem(T(TKEY("scene_override_delete"), "Delete override"))) {
			// Descending, so each removal cannot shift an index still to be removed.
			auto descending = ownedEntries;
			std::ranges::sort(descending, std::greater{});
			for (const auto index : descending)
				manager->RemoveContextSetting(contextId, index);
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}
```

- [ ] **Step 6: Verify**

Run: `./BuildDevFast.bat` → compiles.
In-game, on the weather Scene Manager tab:
- Edit an IBL colour: `SceneManager.json` gains three entries and the control shows one gutter.
- With Time of Day off, edit a slider: six entries appear, one per period.
- Turn Time of Day on: every period shows that same value, no mixed tint.
- Edit one period, turn Time of Day off: the gutter shows the warning tint and the mixed tooltip.
- Right-click → Revert to original: the slider returns to the pre-override value.

```json:metadata
{"files": ["src/CSEditor/SceneWidgetBinding.h", "src/CSEditor/SceneWidgetBinding.cpp"], "verifyCommand": "./BuildDevFast.bat", "acceptanceCriteria": ["aggregates drive all components from one gutter", "radio groups draw exactly one gutter per frame per address", "flat mode writes all six periods in one update", "mixed uses ImGuiItemFlags_MixedValue for Checkbox and a gutter tint elsewhere", "right-click menu offers revert and delete and shows the stored value", "popup suppressed while dragging and for Unbound/Absent"], "modelTier": "frontier"}
```

---

### Task 6: `SceneFeatureReplica` and the install-failure banner

**Goal:** One call draws a feature's real `DrawSettings()` bound to a scene context, or an error-coloured banner when the detours did not install.

**Files:**
- Create: `src/CSEditor/SceneFeatureReplica.h`
- Create: `src/CSEditor/SceneFeatureReplica.cpp`

**Acceptance Criteria:**
- [ ] `Draw(featureShortName, contextId, perPeriod)` resolves the feature via `Feature::FindFeatureByShortName`, opens a `SceneWidgetInterceptor::Scope`, and calls `feature->DrawSettings()`.
- [ ] Conditional visibility, custom widgets, tabs, and layout inside `DrawSettings()` are untouched, because the real function runs.
- [ ] The replica holds **no** `SceneLayerGuard`. It must read the live, scene-applied values so an Active control shows the override.
- [ ] When `SceneWidgetInterceptor::IsInstalled()` is false, the replica draws `Util::Text::WrappedError` naming the failed entry point and stating scene authoring is off for the session, and does not call `DrawSettings()`.
- [ ] A `logger::error` line is emitted **once per session**, not per frame, guarded by a file-local flag.
- [ ] An unknown or unloaded feature draws `Util::Text::WrappedDisabled` and returns without touching the interceptor.
- [ ] An exception thrown from a feature's `DrawSettings()` cannot leave the interceptor armed: the `Scope` destructor restores the previous state.

**Verify:** `./BuildDevFast.bat`, then in-game the weather Scene Manager tab shows the real IBL settings UI rather than the "not yet available" placeholder.

**Steps:**

- [ ] **Step 1: Write the header**

Create `src/CSEditor/SceneFeatureReplica.h`:

```cpp
#pragma once

#include <string>

#include "SceneSettingsManager.h"

/// Draws a feature's real settings UI bound to one scene context.
namespace SceneFeatureReplica
{
	/**
	 * @brief Draws one feature's DrawSettings with every control bound to the given scene context.
	 * @param featureShortName Feature to replicate, as returned by Feature::GetShortName().
	 * @param contextId Scene context every edit is written to.
	 * @param perPeriod False writes each edit to all six periods, which is the flat/global mode.
	 */
	void Draw(const std::string& featureShortName,
		const SceneSettingsManager::SceneContextId& contextId, bool perPeriod);
}
```

- [ ] **Step 2: Write the implementation**

Create `src/CSEditor/SceneFeatureReplica.cpp`:

```cpp
#include "SceneFeatureReplica.h"

#include "../I18n/I18n.h"
#include "Feature.h"
#include "SceneWidgetInterceptor.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	/// The failure is a session-wide fact, so it is logged once rather than every frame it is shown.
	bool loggedInstallFailure = false;

	void DrawInstallFailure()
	{
		const auto failed = SceneWidgetInterceptor::GetInstallError();
		if (!loggedInstallFailure) {
			loggedInstallFailure = true;
			logger::error("Scene Manager authoring is unavailable: ImGui interception failed at '{}'",
				failed.empty() ? "install" : failed);
		}
		Util::Text::WrappedError(
			T(TKEY("scene_interception_failed"),
				"Scene Manager authoring is unavailable: ImGui interception failed at '%s'. "
				"Scene settings cannot be edited for the rest of this session. Restart the game, "
				"and report this if it persists."),
			failed.empty() ? "install" : std::string{ failed }.c_str());
	}
}

void SceneFeatureReplica::Draw(const std::string& featureShortName,
	const SceneSettingsManager::SceneContextId& contextId, bool perPeriod)
{
	if (!SceneWidgetInterceptor::IsInstalled()) {
		DrawInstallFailure();
		return;
	}

	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature || !feature->loaded) {
		Util::Text::WrappedDisabled("%s",
			T(TKEY("scene_feature_unloaded"), "This feature is not loaded, so it has nothing to edit."));
		return;
	}

	// No SceneLayerGuard here: the replica must show the live, scene-applied values so an active
	// override reads back as the value it applies.
	const SceneWidgetInterceptor::Scope scope({ feature, contextId, perPeriod });
	feature->DrawSettings();
}

#undef I18N_KEY_PREFIX
```

- [ ] **Step 3: Verify**

Run: `./BuildDevFast.bat` → compiles.
In-game: the weather Scene Manager tab draws the selected feature's real settings UI. Confirm a feature with conditional sections (ScreenSpaceGI) still hides and shows them exactly as in the normal menu.

```json:metadata
{"files": ["src/CSEditor/SceneFeatureReplica.h", "src/CSEditor/SceneFeatureReplica.cpp"], "verifyCommand": "./BuildDevFast.bat", "acceptanceCriteria": ["Draw opens a Scope and calls the real DrawSettings", "no SceneLayerGuard is held", "install failure draws WrappedError naming the failed entry point", "logger::error emitted once per session", "unknown or unloaded feature draws WrappedDisabled", "Scope destructor disarms on exception"], "modelTier": "standard"}
```

---

### Task 7: Weather tab wiring

**Goal:** The weather widget's Scene Manager tab hosts the replica for the selected feature, bound to that weather and the active period.

**Files:**
- Modify: `src/CSEditor/SceneSettingsUI.h:8`
- Modify: `src/CSEditor/SceneSettingsUI.cpp:255-297, 495-501`
- Modify: `src/CSEditor/Weather/WeatherWidget.cpp:408`

**Acceptance Criteria:**
- [ ] `DrawWeatherSceneTab(RE::FormID weatherId)` replaces the no-argument form, and `WeatherWidget` passes the weather it is editing.
- [ ] `DrawPanel` gains a `SceneContextId` and a feature short name, and calls `SceneFeatureReplica::Draw` instead of the `WrappedDisabled` placeholder at `:265-266`.
- [ ] `DrawFeatureLayout` threads the context through to `DrawPanel`; its existing signature keeps `transitionableOnly` and `withPeriodBar` unchanged.
- [ ] The context is `{ .type = SceneContextType::Weather, .period = <active period from DrawPeriodBar>, .weatherId = weatherId }`.
- [ ] `perPeriod` is the tab's `timeOfDayEnabled` state, so turning Time of Day off makes edits global for that weather, exactly as the user described.
- [ ] With Time of Day on, the period bar's selected period is the one written; with it off, the period passed is still a valid one (`DrawPeriodBar` already returns the live period) so `IsValidSceneContext` holds.
- [ ] `weatherId == 0` draws `Util::Text::WrappedDisabled` rather than building an invalid context.

**Verify:** `./BuildDevFast.bat`, then in-game open a weather widget → Scene Manager tab, pick a feature, edit a slider at Dawn, switch to Night, and confirm the slider reads the base value at Night while Dawn keeps the override.

**Steps:**

- [ ] **Step 1: Change the public signature**

In `src/CSEditor/SceneSettingsUI.h`:

```cpp
	/** @brief Draws the Scene Manager tab body inside a weather widget. */
	void DrawWeatherSceneTab(RE::FormID weatherId);
```

- [ ] **Step 2: Thread the context through `DrawPanel`**

Replace `DrawPanel` in `src/CSEditor/SceneSettingsUI.cpp:255`:

```cpp
	/// Period bar, intro, and the replicated feature UI bound to one scene context.
	void DrawPanel(const char* intro, const std::string& selectedFeature,
		const SceneSettingsManager::SceneContextId& baseContext, bool withPeriodBar = true,
		bool withInteriorToggle = false)
	{
		auto context = baseContext;
		if (withPeriodBar) {
			context.period = static_cast<TimeOfDayPeriod>(DrawPeriodBar(withInteriorToggle));
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
		ImGui::TextWrapped("%s", intro);
		ImGui::Spacing();

		if (selectedFeature.empty())
			return;
		SceneFeatureReplica::Draw(selectedFeature, context, timeOfDayEnabled);
	}
```

- [ ] **Step 3: Thread it through `DrawFeatureLayout`**

Change the signature at `:271` and the `DrawPanel` call at `:293`:

```cpp
	void DrawFeatureLayout(std::string& selectedFeature, bool transitionableOnly, const char* intro,
		bool withPeriodBar, const SceneSettingsManager::SceneContextId& baseContext)
```

```cpp
		ImGui::TableSetColumnIndex(1);
		if (ImGui::BeginChild("##SceneFeatureBody"))
			DrawPanel(intro, selectedFeature, baseContext, withPeriodBar);
		ImGui::EndChild();
```

- [ ] **Step 4: Build the weather context**

Replace `DrawWeatherSceneTab` at `:495`:

```cpp
void SceneSettingsUI::DrawWeatherSceneTab(RE::FormID weatherId)
{
	panelVisible = true;

	if (weatherId == 0) {
		Util::Text::WrappedDisabled("%s",
			T(TKEY("scene_weather_unresolved"), "This weather has no form, so it cannot hold overrides."));
		return;
	}

	const SceneSettingsManager::SceneContextId context{
		.type = SceneSettingsManager::SceneContextType::Weather,
		.weatherId = weatherId,
	};
	DrawFeatureLayout(weatherSelectedFeature, true,
		T(TKEY("scene_manager_weather_intro"), "Settings overridden while this weather is active."),
		true, context);
}
```

- [ ] **Step 5: Add the include and pass the FormID**

Add `#include "SceneFeatureReplica.h"` to `src/CSEditor/SceneSettingsUI.cpp`'s include block.

In `src/CSEditor/Weather/WeatherWidget.cpp:408`, pass the widget's form:

```cpp
			SceneSettingsUI::DrawWeatherSceneTab(formID);
```

Use whichever member `WeatherWidget` already holds for its edited form; read the surrounding code at `:340-400` to pick the right one rather than introducing a new lookup.

- [ ] **Step 6: Verify**

Run: `./BuildDevFast.bat` → compiles.
In-game:
- Open a weather widget → Scene Manager tab → the feature list and the real settings UI both render.
- Edit a slider at Dawn, click Night on the period bar: the slider shows the base value, the gutter is unchecked.
- Click Dawn again: the override value and a checked gutter come back.

```json:metadata
{"files": ["src/CSEditor/SceneSettingsUI.h", "src/CSEditor/SceneSettingsUI.cpp", "src/CSEditor/Weather/WeatherWidget.cpp"], "verifyCommand": "./BuildDevFast.bat", "acceptanceCriteria": ["DrawWeatherSceneTab takes a FormID and WeatherWidget passes it", "DrawPanel calls SceneFeatureReplica::Draw instead of the placeholder", "context is Weather + active period + weatherId", "perPeriod follows timeOfDayEnabled", "weatherId 0 draws WrappedDisabled"], "modelTier": "standard"}
```

---

### Task 8: Scene Manager panel wiring (interior and time of day)

**Goal:** The CS Editor's Scene Manager panel hosts the replica, bound to the interior layer or the time-of-day layer depending on its toggles.

**Files:**
- Modify: `src/CSEditor/SceneSettingsUI.cpp:503-508`

**Acceptance Criteria:**
- [ ] `DrawSceneManagerPanel` builds `SceneContextType::Interior` when `interiorEnabled` is true and the player is indoors (the existing `SyncSceneToggles()` result), and `SceneContextType::TimeOfDay` otherwise.
- [ ] The Interior context carries `period == TimeOfDayPeriod::Count`, which is what `IsValidSceneContext` requires.
- [ ] The panel draws through `DrawPanel` with `withPeriodBar = true, withInteriorToggle = true`, keeping the existing period bar and interior checkbox behaviour byte-for-byte.
- [ ] The panel uses `panelSelectedFeature`, which `DrawSceneManagerCategoryFeatures()` at `:510` already populates, so the split list/body layout is unchanged.
- [ ] With Interior active, `perPeriod` is forced false: interior settings have no periods.

**Verify:** `./BuildDevFast.bat`, then in-game go indoors, open the CS Editor → Scene Manager, tick Interior, edit a slider, and confirm `SceneManager.json` gains an `InteriorOnly` entry.

**Steps:**

- [ ] **Step 1: Build the context**

Replace `DrawSceneManagerPanel` at `:503`:

```cpp
void SceneSettingsUI::DrawSceneManagerPanel()
{
	panelVisible = true;

	// The interior layer takes the panel over indoors, and it has no periods.
	const bool interior = SyncSceneToggles() && interiorEnabled;
	const SceneSettingsManager::SceneContextId context{
		.type = interior ? SceneSettingsManager::SceneContextType::Interior :
						   SceneSettingsManager::SceneContextType::TimeOfDay,
		.period = interior ? TimeOfDayPeriod::Count : TimeOfDayPeriod::Dawn,
	};

	DrawPanel(T(TKEY("scene_manager_panel_intro"), "Settings overridden by interior and time of day."),
		panelSelectedFeature, context, true, true);
}
```

`DrawPanel` overwrites `context.period` from the period bar whenever `withPeriodBar` is true, and `DrawPeriodBar` already returns the live period while the bar is disabled indoors. Guard that overwrite so an Interior context keeps `Count`:

```cpp
		if (withPeriodBar) {
			const auto period = static_cast<TimeOfDayPeriod>(DrawPeriodBar(withInteriorToggle));
			// The interior layer is aperiodic, so the bar informs the view but not the context.
			if (context.type != SceneSettingsManager::SceneContextType::Interior)
				context.period = period;
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
```

and force `perPeriod` false for aperiodic contexts:

```cpp
		const bool perPeriod = timeOfDayEnabled &&
		                       context.type != SceneSettingsManager::SceneContextType::Interior &&
		                       context.type != SceneSettingsManager::SceneContextType::Location;
		SceneFeatureReplica::Draw(selectedFeature, context, perPeriod);
```

- [ ] **Step 2: Verify**

Run: `./BuildDevFast.bat` → compiles.
In-game, indoors: CS Editor → Scene Manager → Interior is auto-ticked, the period bar is disabled with the existing "Time of day editing is unavailable indoors" notice, and editing a slider writes one `InteriorOnly` entry to `SceneManager.json`.
Outdoors with Time of Day on: editing writes a `TimeOfDay` entry for the selected period only.

```json:metadata
{"files": ["src/CSEditor/SceneSettingsUI.cpp"], "verifyCommand": "./BuildDevFast.bat", "acceptanceCriteria": ["Interior context built when indoors and interiorEnabled", "Interior context keeps period == Count", "period bar and interior checkbox behaviour unchanged", "panelSelectedFeature drives the body", "perPeriod forced false for aperiodic contexts"], "modelTier": "standard"}
```

---

### Task 9: Location window wiring

**Goal:** Each open location window hosts the replica bound to that location target, with no period bar.

**Files:**
- Modify: `src/CSEditor/SceneSettingsUI.cpp:545-571`

**Acceptance Criteria:**
- [ ] Each window builds `{ .type = SceneContextType::Location, .locationType = window.target.type, .locationFormKey = window.target.formKey }` with `period == TimeOfDayPeriod::Count`.
- [ ] `DrawFeatureLayout` is called with `withPeriodBar = false`, preserving the existing "location settings are flat" comment and behaviour.
- [ ] `perPeriod` is false, so a location edit writes exactly one entry.
- [ ] Several location windows can be open at once, each with its own `selectedFeature`, and editing in one does not disturb another. The `Scope` is opened and closed inside each window's draw, so contexts never leak across windows.
- [ ] Locations are simulated, not forced: the window does not move the player or the clock, matching the recorded decision.

**Verify:** `./BuildDevFast.bat`, then in-game open two location windows from the Locations browser, edit a different feature in each, and confirm `SceneManager.json` has one entry per location under the right form key.

**Steps:**

- [ ] **Step 1: Build the per-window context**

In `DrawLocationWindows` at `:562`, replace the `DrawFeatureLayout` call:

```cpp
		if (visible) {
			// Location settings are flat, so the window carries no period bar and no time selection.
			const SceneSettingsManager::SceneContextId context{
				.type = SceneSettingsManager::SceneContextType::Location,
				.period = TimeOfDayPeriod::Count,
				.locationType = window.target.type,
				.locationFormKey = window.target.formKey,
			};
			DrawFeatureLayout(window.selectedFeature, false,
				T(TKEY("scene_manager_location_intro"),
					"Settings overridden while the player is in this location."),
				false, context);
		}
```

- [ ] **Step 2: Verify**

Run: `./BuildDevFast.bat` → compiles.
In-game: open two locations from the browser, select a different feature in each, edit a slider in each.
- `SceneManager.json` holds one location entry per target under the matching form key.
- Neither window's selection or gutter state changes when the other is edited.
- Neither window pauses time or moves the player.

```json:metadata
{"files": ["src/CSEditor/SceneSettingsUI.cpp"], "verifyCommand": "./BuildDevFast.bat", "acceptanceCriteria": ["Location context carries type, formKey, and period Count", "withPeriodBar stays false", "perPeriod false so one entry per edit", "multiple windows stay independent", "no player or clock forcing"], "modelTier": "mechanical"}
```

---

### Task 10: Regenerate translations and pass the i18n CI checks

**Goal:** Every new user-visible string is extracted into `en.json` with the project's key convention, and the i18n CI workflow passes.

**Files:**
- Modify: `package/SKSE/Plugins/CommunityShaders/Translations/en.json`
- Possibly modify: `package/SKSE/Plugins/CommunityShaders/Translations/*.json` (ordering only)

**Acceptance Criteria:**
- [ ] `python tools/extract-i18n.py --check` exits 0.
- [ ] `python tools/extract-i18n.py --orphans` reports no orphans. The removed `cs_editor.scene_manager_unavailable` key must be gone from `en.json`.
- [ ] `python tools/sort-i18n.py --check` exits 0.
- [ ] Every new key uses the `cs_editor.` prefix already declared in `SceneSettingsUI.cpp:15`, and the new prefixes in `SceneWidgetBinding.cpp` and `SceneFeatureReplica.cpp` match it.
- [ ] Format specifiers are preserved: `scene_interception_failed` keeps its `%s`.

**Verify:** `python tools/extract-i18n.py --check && python tools/extract-i18n.py --orphans && python tools/sort-i18n.py --check` → all exit 0.

**Steps:**

- [ ] **Step 1: Extract**

Run: `python tools/extract-i18n.py --write`

- [ ] **Step 2: Reorder the non-English files**

Run: `python tools/sort-i18n.py --write`

- [ ] **Step 3: Confirm the removed key is gone**

Run: `python tools/extract-i18n.py --orphans`
Expected: no output listing `cs_editor.scene_manager_unavailable`. If it is still listed, remove it from `en.json` by hand and re-run.

- [ ] **Step 4: Run the full CI check set**

Run each separately:
```bash
python tools/extract-i18n.py --check
python tools/extract-i18n.py --orphans
python tools/sort-i18n.py --check
```
Expected: all exit 0.

```json:metadata
{"files": ["package/SKSE/Plugins/CommunityShaders/Translations/en.json"], "verifyCommand": "python tools/extract-i18n.py --check", "acceptanceCriteria": ["extract-i18n --check exits 0", "no orphaned keys including the removed scene_manager_unavailable", "sort-i18n --check exits 0", "all new keys use the cs_editor. prefix", "%s preserved in scene_interception_failed"], "modelTier": "mechanical"}
```

---

### Task 11: Commit the full implementation

**Goal:** One commit carrying the whole Scene Manager authoring UI.

**Files:**
- All files touched by Tasks 1-10.

**Acceptance Criteria:**
- [ ] `detect_changes({scope: "compare", base_ref: "dev"})` reports only the symbols this plan touches.
- [ ] The commit message follows the conventional-commit format with a 50-character title and 72-character body wrap.
- [ ] The type is `feat` (this adds a user-facing capability), so semantic-release cuts a minor bump.
- [ ] The working tree is clean afterwards, and nothing in `.git/info/exclude` was staged.

**Verify:** `git status --short` → empty; `git log -1 --stat` → shows the expected file set.

**Steps:**

- [ ] **Step 1: Run the pre-commit scope check**

Run the GitNexus `detect_changes` tool with `{"scope": "compare", "base_ref": "dev"}` and confirm the affected symbols are limited to `SceneSettingsManager`, the new `src/CSEditor/Scene*` units, `SceneSettingsUI`, `WeatherWidget::DrawWindow`, `Menu::Init`, and the generator.

- [ ] **Step 2: Stage and commit**

```bash
git add src/ cmake/generate_scene_settings_catalog.py tests/ docs/superpowers/plans/ package/SKSE/Plugins/CommunityShaders/Translations/
git commit -m "$(cat <<'EOF'
feat(scenemanager): replicate feature menus in scene tabs

Detour ten ImGui widget entry points and arm them only around a
replica of a feature's real DrawSettings, so the Scene Manager's
weather tab, scene panel, and location windows all present the
exact feature UI with a per-control override toggle. Feature files
are untouched.

Each bound control resolves through the generated catalog's control
resolver and reads as absent, active, or paused; a paused control is
really disabled and shows the stored override value. A build gate
now fails when a feature introduces a widget kind the interceptor
does not cover.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Verify**

Run: `git status --short` → empty.
Run: `git log -1 --stat` → the file list matches Tasks 1-10 and nothing else.

```json:metadata
{"files": [], "verifyCommand": "git status --short", "acceptanceCriteria": ["detect_changes reports only expected symbols", "conventional commit format with 50/72 wrapping", "feat type for a minor bump", "clean working tree afterwards"], "modelTier": "mechanical", "blockedBy": ["Task 1", "Task 2", "Task 3", "Task 4", "Task 5", "Task 6", "Task 7", "Task 8", "Task 9", "Task 10"]}
```

---

## Self-review notes

**Spec coverage.** Each of the spec's five components maps to a task: `SceneWidgetInterceptor` → Task 3, `SceneFeatureReplica` → Task 6, the `SceneContextId`-keyed façade → Task 2, the catalog generator changes → Task 1, host wiring → Tasks 7-9. The three per-widget states are Task 4; the louder error banner is Task 6; capture ordering is Task 4 Step 5. The spec's deferred items (preset export, winning/losing colouring) stay deferred, with Task 4's public `GetState()` preserving the flexibility the user asked for.

**Type consistency.** `SceneWidgetBinding::Guard`'s accessors (`Bool`/`Int`/`Float`/`Raw`) are used with the same names in Task 3's detour bodies. `SceneWidgetInterceptor::Context` uses `perPeriod` in Tasks 3, 4, 5, 6, 7, 8, 9. The façade names (`FindContextUserEntry`, `FindContextUserEntryPerPeriod`, `AddContextSetting`, `UpdateContextEntryValues`, `RemoveContextSetting`, `TogglePauseContextEntry`, `RevertContextEntryToDefault`, `GetContextEntries`) are declared in Task 2 and used unchanged in Tasks 4 and 5.

**Known gaps.**
- The 5 temporary-bound controls listed at the top of this plan draw no gutter. Closing them is additive and needs no feature-file edits: one line inside `Util::PercentageSlider`, plus a generator-emitted label map for the three combo proxies.
- Interior contexts are not offered as copy sources or destinations; the copy switches reject them through their `default:` arms.
- `CaptureExternalFeatureChanges` stays callerless: the replica commits through the façade rather than relying on post-hoc diffing.
