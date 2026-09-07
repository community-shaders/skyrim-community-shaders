# Design: Scene Manager Authoring UI

**Date:** 2026-08-14
**Branch:** scenemanager
**Status:** Approved

## Problem

The Scene Settings framework was ported from `Dlizzio/open-shaders` (branch `feat/scene-manager`) backend-only. Everything below the UI works: the build-time catalog (326 entries, 299 scene-controllable across 28 features), entry authoring, layered resolution, blending, location transitions, and overwrite export. What did not come across is upstream's authoring UI (`SceneSettingsUI`, `SceneSettingsUIHooks`, `SceneManagerUI`, roughly 4000 lines), which was deliberately excluded as too unfriendly to author with.

The scaffolding in [src/CSEditor/SceneSettingsUI.cpp](../../../src/CSEditor/SceneSettingsUI.cpp) draws the hosts (weather tab, scene panel, location windows), the period bar, and the feature list, then stops at a placeholder notice. There is no way to author an entry from the UI.

## Goal

Every Scene Manager host shows a **replica of the normal feature menu**. Editing a control there authors an override bound to the host's scene context (time of day, weather, location) instead of the feature's base settings. Each control gains a **gutter toggle** on its right edge that creates, pauses, and resumes its override.

Three properties are non-negotiable, all stated by the user during discovery:

1. **Agnostic and automatic.** A setting added to a feature menu appears in the Scene Manager with no extra work.
2. **No feature-file edits.** Wrapper functions that features must call are rejected: they impose maintenance on everyone writing a feature.
3. **Recognizable.** Conditional visibility, custom widgets, layout, and grouping survive intact. Rendering the UI from catalog metadata was rejected for this reason.

## Decisions (from brainstorming)

- **Replay, do not re-render.** The replica calls `feature->DrawSettings()` and intercepts the ImGui calls it makes. This is the only approach that satisfies all three properties at once.
- **Detour ImGui, not the test engine.** Binding a widget to a catalog entry needs the value pointer *and* a window before the call to push `BeginDisabled`/styles. `IMGUI_ENABLE_TEST_ENGINE` hooks fire inside `ItemAdd`, too late, and never see the pointer.
- **Real `BeginDisabled` for paused controls.** A visually greyed but still draggable control was explicitly rejected.
- **A paused entry keeps its value.** The JSON already supports this: `SettingEntry::paused` is persisted ([SceneSettingsManager.cpp:3500](../../../src/SceneSettingsManager.cpp#L3500)) and read back with a type check ([:3703](../../../src/SceneSettingsManager.cpp#L3703)), and `IsEntryActive` gates application on it ([:1783](../../../src/SceneSettingsManager.cpp#L1783)). No format change is needed.
- **Same system in all three hosts.** Weather tab, scene panel, and location windows differ only in how the context is established.

## Architecture

Five components.

### 1. `SceneWidgetInterceptor` (new)

Installs the ImGui detours once at boot and stays **inert unless armed**. While armed it holds the active `SceneContextId` and `Feature*`, and for each intercepted call performs bind, decorate, capture.

### 2. `SceneFeatureReplica` (new)

Arms the interceptor for a `(context, feature)` pair and calls `feature->DrawSettings()`. It draws no controls of its own; every control the user sees comes from the feature's own code.

### 3. `SceneContextId`-keyed façade on `SceneSettingsManager`

The existing public API is index-keyed (`RemoveSetting(SceneType, size_t)`, `TogglePauseEntry`, `RevertEntryToDefault`). The façade adds address-keyed operations over a `SceneContextId` so the UI never has to know an entry's index. It is **additive**: the index-keyed API stays, because deferred export depends on index addressability.

### 4. Catalog generator changes

[cmake/generate_scene_settings_catalog.py](../../../cmake/generate_scene_settings_catalog.py) additionally emits, per feature, the labels of **cataloged but unbindable** controls: those that write through a temporary rather than the settings member, such as IBL's `DALCMode` combo ([IBL.cpp:70-89](../../../src/Features/IBL.cpp#L70-L89)). The call itself is detoured like any other `Combo`; the miss happens at bind time, because the pointer handed to ImGui is a stack `int` that is copied back into the member afterwards. The build **fails** when a scene-controllable entry is backed by a widget the generator does not recognise, so a new widget type cannot silently become unauthorable.

### 5. Host wiring

`DrawWeatherSceneTab`, `DrawSceneManagerPanel`, and `DrawLocationWindows` each establish a context and hand it to the replica.

### Per-widget states

Keyed on the entry state for that control's address:

| State | Pointer passed to ImGui | Behaviour |
|-------|------------------------|-----------|
| **Absent** | the real member | live; an edit authors a new entry |
| **Active** | the real member | live; the value shown is the override, because the resolver has already applied it |
| **Paused** | a temporary holding the stored override | wrapped in `BeginDisabled`; shows the value being compared against, not the inherited one |

The paused case is why a temporary is needed at all: the user asked that a paused control display its stored override, and the feature's member holds the inherited value at that moment.

## Interception mechanics

**Surface.** The list is not hand-maintained. The generator already records a `sourceWidget` for every catalog entry, so the rule is: **the detour set must cover every distinct `sourceWidget` backing a scene-controllable entry**, and the build gate described below enforces exactly that. This is what keeps the surface self-maintaining, which is the same property the design promises for the UI itself.

Derived from `src/Features` today, that is **13 functions**: `SliderFloat` (167 calls), `Checkbox` (90), `SliderInt` (19), `RadioButton` (10), `ColorEdit3` (10), `Combo` (8), `ColorEdit4` (5), `SliderFloat2` (4), `SliderScalar` (3), `InputFloat` (3), `SliderAngle` (1), `InputInt` (1), `InputText` (1).

`DragFloat` and `DragInt` have zero callers in `src/Features` and are omitted. `CheckboxFlags`, `InputScalarN`, `InputTextWithHint`, and `ColorButton` do appear, but none of them backs a cataloged setting: they write to file-static UI filters, `ExtendedEffect`'s dynamic effect variables, or a local buffer. They are omitted for now and the build gate will demand them the moment that changes.

**Install** in one `DetourTransaction` at boot, taking `&(PVOID&)ImGui::SliderFloat` and friends. Detours is already a vcpkg dependency used for the D3D hooks in [src/Hooks.cpp](../../../src/Hooks.cpp). ImGui comes from vcpkg as a separate static library (`imgui::imgui`), so the plugin's `/GL /LTCG` cannot inline these calls into feature translation units: the callsites stay real calls and are detourable.

Each hook checks a single global first and tail-calls the real function when unarmed, so the normal feature menu pays a predictable-branch cost and nothing else.

**Arming** is `SceneWidgetInterceptor::Scope`, an RAII guard that asserts it never nests.

**Binding** uses the existing `FindSettingForControl(feature, valuePtr)`, whose registrations are currently dead code and described in the framework doc as "the seam the UI layer plugs into". On a miss, the label with any `##id` suffix stripped is compared against the generator's unbindable-label list for that feature: a hit renders greyed with an explanatory tooltip, a miss is a pure UI local and stays fully live. Two controls in one feature sharing a label would grey both; that is accepted, since the alternative is authoring the wrong entry.

**Decoration.** The gutter toggle is placed with `ImGui::SameLine(contentWidth - kSceneGutterWidth)`, an absolute offset because `Checkbox` ignores `ItemWidth`. The right-click menu uses `BeginPopupContextItem()` while the widget is still the last item.

**Capture ordering** is the subtle correctness point. On a `true` return from the real function:

1. Restore the pre-edit value into the feature's member.
2. Call the façade, so `AddSetting` snapshots `originalValue` as the correct baseline.
3. Call `CommitSceneSettingChanges()`, so the resolver reasserts the new value in the same frame.

The pre-call value is stored into the `Scope` **unconditionally** for every bound widget, not only on the edit path, so no early return can skip step 1.

**Aggregates.** `ColorEdit3/4` and `SliderFloat2` author, pause, and remove as a group. Membership comes from the catalog's `aggregateStart` and `aggregateCount`.

**Radio groups.** All ten `RadioButton` calls in `src/Features` use the 3-arg form and eight bind straight to a `settings.*` member, so they are ordinary bound widgets. A group is N calls sharing one value address, so the gutter toggle and the right-click menu attach to the **first** button that binds a given address in a frame, and the rest of the group draws bare. Otherwise a three-way mode selector would sprout three toggles for one entry.

**Known unreachable.** Three hand-built `BeginCombo` loops write outside any single detourable call ([CSEditor.cpp:777](../../../src/Features/CSEditor.cpp#L777), [ExtendedEffect.cpp:483](../../../src/Features/Effects11/Effects/ExtendedEffect.cpp#L483) and [:584](../../../src/Features/Effects11/Effects/ExtendedEffect.cpp#L584)). None of the three backs a cataloged setting, so nothing authorable is lost today. Should one ever gain a catalog entry, the build gate catches it.

**What the replica must not touch.** `CaptureExternalFeatureChanges` ([SceneSettingsManager.cpp:2355-2394](../../../src/SceneSettingsManager.cpp#L2355-L2394)) writes divergences into `baselineSettings`. It has no callers today. Routing replica edits through it would land them in the wrong layer.

## Context establishment per host

`SceneContextType` currently has no `Interior` enumerator. **Add one**, and let `IsValidSceneContext` accept it. This widens `GetCopySources` to offer interior as a copy source and destination, which is a welcome side effect rather than a cost.

### Weather tab

Context is `Weather` + the widget's FormID + the period bar's period. The editor's existing weather lock ([EditorWindow.cpp:1835-1986](../../../src/CSEditor/EditorWindow.cpp#L1835-L1986)) hooks every `SetWeather`/`ForceWeather` callsite and pins the weather against scripts and climate timers, so **the feature's live settings already are the resolved values** and no patching is needed. The lock is released through the draw-then-evaluate latch that `SyncTimePause` already demonstrates ([SceneSettingsUI.cpp:573](../../../src/CSEditor/SceneSettingsUI.cpp#L573)).

`DrawWeatherSceneTab()` currently takes no arguments and must gain the weather FormID.

**Flat versus TOD view.** Data is always per-period; the comment at [SceneSettingsManager.h:389-394](../../../src/SceneSettingsManager.h#L389-L394) is explicit that the flat/TOD toggle is a view preference, not a data mode. So:

- A "global override for this weather" writes the same value to all six periods.
- Flat view renders **mixed** when the six disagree: `ImGuiItemFlags_MixedValue` on the gutter checkbox, with the control showing the live period's value.
- Editing in flat view fans out to all six periods.
- Flipping between views never writes.

In TOD view the context carries the selected period. In flat view it carries `TimeOfDayPeriod::Count`, the existing sentinel, which the façade reads as "all six" and fans out over. No new encoding is introduced.

### Scene panel

Context is `Interior` when the interior toggle is on, else `TimeOfDay` + the selected period. Both already pin the game: clicking a period sets the game hour ([SceneSettingsUI.cpp:174-177](../../../src/CSEditor/SceneSettingsUI.cpp#L174-L177)), and `SyncTimePause` holds it.

### Location windows

Context is `Location` + type and form key, flat. This is the **only simulated host**: the game cannot be teleported to a location the way it can be pinned to a weather and an hour. The sequence is snapshot via `SaveSettings`, patch the resolved values into the JSON, `LoadSettings`, draw, restore. This is the same round trip the resolver itself performs per feature at 30 Hz, so the cost is already proven acceptable.

## Interaction

**Gutter toggle**, one click per transition:

| From | Click does |
|------|-----------|
| Absent | create the entry at the control's current value, active |
| Active | pause: value kept in JSON, stops applying, control switches to `BeginDisabled` showing the stored value |
| Paused | resume |

The hover tooltip names the state. It is also the natural home for the deferred winning/losing readout.

**Right-click menu** via `BeginPopupContextItem()`:

- *Remove override* → `RemoveSetting`
- *Revert to default* → `RevertEntryToDefault`
- *Copy value to all periods*, weather TOD view only: the explicit form of what flat-view editing does implicitly

Items grey out when no entry exists for the control.

**Feature-level pause.** The existing "Scene Specific Settings" toggle is `SetFeaturePaused`, which is global rather than per-context ([FeatureListRenderer.cpp:800-816](../../../src/Menu/FeatureListRenderer.cpp#L800-L816)). The replica keeps drawing and stays authorable while a feature is paused, with a banner at the top of the body stating that overrides are not applying. This mirrors the normal menu, which still shows the toggle when `sceneControlled || scenePaused`.

**The normal menu is unaffected.** The interceptor is armed only around the replica's `DrawSettings` call; the normal menu's own call runs unarmed and tail-calls the real functions. Its existing scene-controlled `BeginDisabled` wrap ([FeatureListRenderer.cpp:819-833](../../../src/Menu/FeatureListRenderer.cpp#L819-L833)) is unchanged, and the replica's per-widget `BeginDisabled` nests legally under it.

## Error handling

1. **Detour install failure at boot.** The interceptor stays permanently unarmed and the replica refuses to draw. All three hosts show an error-coloured `Util::Text::WrappedError` banner ([UI.h:924](../../../src/Utils/UI.h#L924)) naming which detour failed and stating that scene authoring is off for the session, plus one `logger::error` line. This replaces the current grey `WrappedDisabled` placeholder. Nothing else in the plugin degrades.
2. **A throwing `DrawSettings`.** `Scope` disarms in its destructor. The replica catches, logs once, and marks that feature un-replicable for the session so it does not log every frame.
3. **Nested arm.** Assert in debug. In release the inner scope is inert and widgets bind to the outer context, which is the real host, so it can never write to the wrong context.
4. **Location simulation failure.** If the `SaveSettings` snapshot fails or `LoadSettings` throws mid-patch, restore the snapshot, grey the panel with an error line, and skip the replica that frame. Patched values are never left applied.
5. **Façade rejection.** A blacklisted address, a widget value whose type disagrees with the catalog's `valueType`, or an unknown short name produces no entry, reverts the gutter, and emits one log line. The type-checked `paused` read is the precedent for this strictness.
6. **Capture ordering.** Covered by the unconditional pre-call capture described above.

## Testing

- **[tests/test_scene_settings_catalog_generator.py](../../../tests/test_scene_settings_catalog_generator.py)** gains cases for the unbindable-label emission, the widget-coverage gate failing the build when a scene-controllable entry is backed by an unrecognised widget, and `##id` label normalization. It also asserts the entry counts are unchanged: the new emission adds fields, not entries, so the CMake floors (`--min-entries 250 --min-controllable 290 --min-controllable-features 28`, [CMakeLists.txt:385-389](../../../CMakeLists.txt#L385-L389)) must still pass.
- **[tests/test_scene_settings_policy.py](../../../tests/test_scene_settings_policy.py)** gains a round-trip case proving a paused entry survives save and load with its value intact. That JSON contract is what the whole toggle rests on.
- **i18n**: `python tools/extract-i18n.py --write`, then `--check`, `--orphans`, and `python tools/sort-i18n.py --check`, per `pr-i18n.yaml`.
- **Scriptable in-game check**: `communityshaders.feature get` before and after authoring through the replica proves the edit reached the feature's `SaveSettings` blob, and that pausing removes it, without a human reading sliders ([DevBenchBridge.cpp:646-668](../../../src/Features/RemoteControl/DevBenchBridge.cpp#L646-L668)). No scene-manager bridge tool exists; adding one is out of scope.
- **Manual matrix**: three hosts × {absent, active, paused}, flat and TOD for the weather host, plus one feature with a conditionally visible control to prove the replica preserves conditional widgets.

## Deferred

**Preset export.** The manager can already export user entries into overwrite files (`ExportUserSettingsToOverwrites`, `ExportWeatherUserSettingsToOverwrites`, `ExportLocationUserSettingsToOverwrites`), all implemented with no callers. Wiring it up is deferred, with two constraints recorded now:

- The `SceneContextId` façade must stay additive on top of the index-keyed API, because export operates on entry indices.
- Export needs a multi-select entry-list surface. This design does not provide one, since removal was assigned to the right-click menu.

**Readability.** Coloured sliders showing whether an override is winning or losing in the current scene, and showing which value is currently winning. `ResolvedSettingMap` is `std::map<SettingAddress, json>` ([SceneSettingsManager.h:812](../../../src/SceneSettingsManager.h#L812)), value only, with no provenance. This does not require touching the per-frame hot path: an on-demand `GetSettingProvenance(address)` query serves the UI, which only needs one feature's worth while a panel is open.

## Known gaps left unaddressed

- `CaptureExternalFeatureChanges` remains callerless. This design routes around it rather than fixing it.
- `HasRestoreDefaults()` and parts of the catalog's presentation metadata remain unread.
