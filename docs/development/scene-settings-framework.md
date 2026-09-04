# Scene Settings Framework

Catalog-backed system that overrides feature settings by **interior**, **time of day**, **weather**, and
**location**, blending between them as the game state changes.

**Provenance:** ported from `Dlizzio/open-shaders` branch `feat/scene-manager` (upstream range
`06eaa584a..1119234f9`), squashed into `feat(scene-manager): port scene settings framework`, then
re-synced against upstream **rev4** (location regions replacing rev3's categories, instant apply across a
loading screen, apply verification, the cached apply document; rev3 brought location transitions, resolver
caching and the generic scene copy API). The port is **backend only**: upstream's authoring UI was never taken, and
Community Shaders branding was kept throughout. This fork grows its own editor in `src/CSEditor/`. See
[What was dropped](#what-was-dropped) and [Known gaps](#known-gaps) before assuming a missing piece is a
bug.

**On-disk compatibility with upstream is a hard requirement:** a `SceneManager.json` authored in
open-shaders must load in Community Shaders with every setting honored, and vice versa. Any divergence
below is behavioral, never a change to the file format.

## Files

| File | Role |
| ---- | ---- |
| `src/SceneSettingsManager.{h,cpp}` | The system. Storage, persistence, resolver, apply/restore, blending. |
| `src/SceneSettingsPolicy.h` | Hand-maintained allow/deny lists consumed by the manager. |
| `src/Features/SceneManager.{h,cpp}` | Thin `Feature` wrapper that drives the manager's lifecycle. |
| `cmake/generate_scene_settings_catalog.py` | Build-time generator; parses `src/**/*.{h,hpp,cpp,cxx}`. |
| `features/Scene Manager/` | `CORE` marker + `SceneManager.ini` (version `1-0-0`). |
| `tests/test_scene_settings_catalog_generator.py` | Generator unit tests (hermetic + catalog assertions). |
| `tests/test_scene_settings_policy.py` | Checks the policy lists against the real catalog. |

Generated into `${CMAKE_CURRENT_BINARY_DIR}/generated` (e.g. `build/ALL/generated`), never committed:

-   `SceneSettingsCatalog.generated.h` / `.cpp` — the `SceneSettingsCatalog` namespace and the catalog array.
-   `FeatureSceneSettingsAdapters.generated.cpp` — per-feature control resolvers (see
    [Known gaps](#known-gaps)).

## How the catalog is built

`CMakeLists.txt` runs the generator as a custom command before compiling, with
`--min-entries 250 --min-controllable 290 --min-controllable-features 28` as a regression gate. The two
controllable floors are the ones that matter: a parser change that stops binding a control does not
remove the entry, it silently drops its `SceneControllable` flag. Keep them just under the real numbers.
It statically parses feature sources and derives, for every persisted setting:

-   the serialized address (`serializedPath` / `serializedKey` / `serializedComponent`) used to reach the
    value inside a feature's settings JSON
-   the display address (`displayPath`, `selectorPath`) reconstructed from `DrawSettings()` brace scopes and
    tab helpers
-   the editor semantic (`Toggle` / `Numeric` / `Choice` / `Text` / `Generic`), numeric bounds, display scale,
    `Log2` transforms, and combo choice values, read from the actual ImGui call
-   i18n keys from the `T(TKEY(...), "...")` call wrapping the control
-   flags: `Persisted`, `Transitionable`, `Hidden`, `BooleanControl`, `SceneControllable`

`validate_entries()` fails the build on duplicates, contradictory visibility, choices with fewer than two
distinct values, `Log2` bounds that include zero, and incomplete aggregates (a vector exposing only some
components).

This makes the framework **feature-agnostic**: a feature exposes scene-controllable settings simply by
persisting them and drawing them with a recognized ImGui call. There is no registration API and no
per-feature code to write. This is what replaced the deleted `WeatherVariableRegistry`.

Current catalog on this fork: **325 entries**, 298 of them scene-controllable across 28 features.

## Runtime flow

1.  **Boot** — `SceneManager::SetupResources()` calls `LoadAll()` (overwrites + user settings for
    non-weather scene types). `globals::sceneSettingsManager` is wired in `globals::OnInit()`.
2.  **Data loaded** — `SceneManager::DataLoaded()` calls `OnDataLoaded()` (weather/location data needs
    `TESDataHandler` for SPID resolution) and registers `MenuOpenCloseEventHandler`, which flags a cell
    transition when the loading menu closes.
3.  **Per frame** — `State::Draw()` calls `SceneManager::Update()` → `SceneSettingsManager::Update()` →
    `ResolveAndApply()`. The resolver early-outs unless something it depends on moved: interior flag,
    location/cell FormID, game hour (`kHourUpdateThreshold`), or weather pair + lerp.

### How values are applied

There is no per-setting writer and no pointer patching. `ApplyCatalogSceneSettings()`:

1.  take the feature's `featureApplyDocuments` entry, seeding it from `feature.SaveSettings(json)` on first use
2.  resolve every catalog address up front and reject the whole batch on a type mismatch, before mutating
3.  overwrite the primitives, clamping each into its control's range (see [Numeric bounds](#numeric-bounds))
4.  `feature.LoadSettings(json)` to push the whole block back
5.  on exception, roll the originals back into the document and reload it

The document is **kept between applies**, so a per-frame location transition costs no `SaveSettings` and no
full copy. That is also why validation precedes mutation: a rejected update must leave nothing behind. If
even the rollback throws, the cached document is dropped so the next apply re-snapshots.

`baselineSettings` holds the pre-override value so a setting that leaves scope is restored rather than left
stuck. Failed applies back off (`kApplyRetryDelay`, 2s) and log once per signature instead of every frame.

**An accepted apply is verified.** A feature that silently clamps or discards what it was handed still
reports success, so `ScheduleApplyVerification()` records the batch and `VerifyPendingApplies()` reads it
back the following frame. A feature that did not retain the values loses its applied entries and its cached
document, and backs off like an outright failure.

**Consequence:** a feature's `SaveSettings`/`LoadSettings` pair is the contract. A setting that is not
round-trippable through them cannot be scene-controlled, no matter what its UI looks like.

### Numeric bounds

Applying a value goes JSON patch → `LoadSettings` → shader constants, which never runs the ImGui call that
would otherwise bound it. Every in-app path already produces in-range values (the interceptor replays the
feature's own clamping widget, blending lerps between two valid endpoints, a copy moves a value that was
valid at its source), so the exposure is the on-disk contract: `IsSceneSettingValueAllowed` gates the
*type* of a value, not its range, and a hand-edited or foreign `SceneManager.json` can carry anything.

`ClampCatalogNumericValue()` closes that in `ApplyCatalogSceneSettings()`, the one place any scene value
reaches a feature. It covers `EditorSemantic::Numeric` entries carrying `hasNumericBounds` (207 of the
current catalog's 325). Bounds are authored in **display** space, so the range is converted once through
`ConvertCatalogNumericDisplayToStored()` rather than round-tripping every value; both transforms are
monotonic, so the min stays the min. An integer-typed value clamps to the whole numbers inside the range,
since an integer control cannot land on a fractional bound.

**Only the applied copy is clamped.** The scene entry keeps the value it was authored with, so a document
written by another implementation survives the round trip, the same intent-preserving trade the
[location transitions](#location-transitions) make with `retainSerializedTransition`. A clamp is reported
once per address (`WarnOnceAboutClampedSceneSetting`), because an out-of-range entry re-applies every frame
its scene is active.

**Restores are never clamped.** `CatalogSceneSettingUpdate::clampToControlRange` travels per update rather
than per call, because the resolver batches scene values and baseline restores into one apply. A baseline is
the feature's own pre-override value; clamping it would rewrite user data that was already live before the
scene layer engaged. The flag is `!update.restore` in the resolver, `!transition.restoreAtEnd` for a
location transition, and `false` in `RestoreAppliedSettings()`.

### Precedence and blending

`BuildResolvedSettings()` overlays in order, later winning per setting address:

```
interior  →  time of day  →  weather  →  location (region → location → cell)
```

Within a layer, `EntrySource::Overwrite` (mod-shipped files) is overlaid first and `EntrySource::User`
overrides it, so a shipped overwrite acts as that layer's default and the user's own entry for the same
address always wins. `SettingsUser.json` remains the baseline beneath all of this.

-   **Time of day** — six periods (`Dawn`, `Sunrise`, `Day`, `Sunset`, `Dusk`, `Night`) with hour ranges in
    `kPeriodHours`; `Night` wraps midnight as `21..28`. Floats cross-fade across a `kTransitionHours` (0.5h)
    zone at each boundary. Non-float settings snap.
-   **Weather** — per-weather configs are always stored per period; floats blend across
    `Sky::currentWeatherPct` between the outgoing and incoming weather.
-   **Location**: see [Location targets](#location-targets); the chain resolves broadest to narrowest, so a
    cell entry wins over the location that contains it, which wins over the region that contains them both.
-   Writes smaller than `kBlendEpsilon` (1e-3) are skipped so blending does not spam `LoadSettings`.

**Divergence from upstream, deliberate:** upstream overlays `User` first and `Overwrite` second, so a
mod-shipped overwrite wins over the user's own entry for the same address. This fork inverts the order
everywhere (`OverlayAllEntries`, `CollectPeriodValueGroups`, `BuildEffectiveContextEntries`) so the user
wins. Both read the same files; only the winner differs. Preserve the inversion in any new overlay code.

### Location targets

A location resolves to a **chain** of targets, broadest first, built by `BuildLocationTargetChain()`:

| `LocationTargetType` | Source | Notes |
| -------------------- | ------ | ----- |
| `Region` | The `TESRegion` covering an **exterior** cell | `Sky::region` when the player is in that cell, since it knows which of the overlapping regions won; otherwise the cell's first non-null `GetRegionList()` entry. Interiors contribute no region. |
| `Location` | The `BGSLocation` chain, walked through `parentLoc` and reversed | Cycle-guarded by a visited FormID set. |
| `Cell` | The player's parent cell | Its `editorId` is the coc code. |

`GetCurrentLocationTargets()` caches the player's chain by location + cell + region FormID; the region is
part of the key because overlapping regions can change winner without the cell changing.
`ResolveLocationTargetChain(type, formKey)` answers the same question for an **arbitrary** target: it
reuses the player's chain when the target is in it, otherwise it looks the form up through
`Util::ParseSpid` / `Util::SpidToFormId` and rebuilds. A region is reached through the cells it covers, so
off-chain it resolves to a chain of itself.

The editor's Add list is the whole chain, so every link the player is standing in, the region included, is
authorable in one click. There is deliberately no picker for a target the player is *not* standing in.

Persisted under `location.regions` / `location.locations` / `location.cells` in `SceneManager.json` and
under `Locations/<form key>/` for overwrites.

### Location transitions

Location float overrides ease in and out instead of snapping, because crossing a cell boundary otherwise
pops every affected setting.

-   Duration comes from the entry's own `transitionSeconds`, falling back to the global
    `location.transitionSeconds` (`kDefaultLocationTransitionSeconds` = 5s, clamped to
    `kMaxLocationTransitionSeconds` = 300s).
-   Time comes from `globals::state->timer`, so transitions freeze with the game rather than the wall clock.
-   Easing is smoothstep (`t * t * (3 - 2t)`). `StartLocationTransitions()` retargets from the **live** eased
    value, so reversing direction mid-transition never snaps.
-   Only **walking** between exterior cells of one worldspace animates (`walkedBetweenWorldspaceCells`).
    Editing a value in place snaps to it, and so does arriving from a loading screen: `OnLoadingTransition()`
    resolves with `allowLocationTransitions = false` so the destination's values are already in place when
    the player sees it. `locationOverridesDirty` alone just reconciles the targets.
-   In-flight transitions are grouped into per-feature `LocationTransitionBatch`es so one `LoadSettings` call
    covers every animating setting of a feature. A failing batch logs once and backs off by signature.
-   `RestoreAppliedSettings()` clears all transitions first, so tearing the scene layer down cannot leave a
    half-eased value applied.

A duration set on one component of an aggregate control (a colour, a vector) applies to the whole control:
`SetLocationEntryTransitionSeconds()` expands the selection through `GetCopyGroupKey()` before validating.

**Durations are edited from the scene editor.** The global duration is a typed field in the location
page's toolbar (`ScenePageToolbar`), shown only for `SceneContextType::Location`. The per-entry
override is the third slot of each control's gutter (`SceneWidgetBinding::Guard::DrawTransitionSlot`),
shown only when the page is a location page and every catalog component the control covers carries
`SettingFlag::Transitionable`, the same predicate `FindAllowedCatalogSetting` applies when loading a
document. A gutter field left empty inherits the global; a row with no override yet shows the
inherited value inert.

Typed values are clamped to `0..kMaxLocationTransitionSeconds`, whereas a value loaded from a
document that falls outside that range is still rejected and flagged `retainSerializedTransition`.
The divergence is deliberate: a human typing sees the correction happen, while a document authored by
another implementation has an intent worth preserving through the round trip.

### Resolver caching

The resolver runs every frame, so everything it can precompute is cached and invalidated by revision
counter rather than rebuilt:

| Cache | Invalidated by | Holds |
| ----- | -------------- | ----- |
| `timeOfDayValueGroups`, `weatherValueGroups` | `sceneValueRevision` | Per-address `std::array<std::optional<float>, kPeriodCount>` period values, so blending never re-walks the entry lists. |
| `featureBaseSnapshots` | `InvalidateFeatureSnapshot()` | A feature's settings JSON with the scene layer folded back out, used as the baseline source. |
| `configuredFeatureNamesCache` | `configuredFeatureNamesRevision` | Which features have any scene entry at all. |
| `cachedLocationOverrides` | `locationOverridesDirty` | The resolved location layer, rebuilt only when the target chain or an entry moved. |
| `resolvedSettingsScratch` | reused every resolve | The resolved map itself, so the per-frame path does not reallocate. |
| `cachedLocationTargets` | location/cell/region FormID change | The player's target chain. |
| `featureApplyDocuments` | `InvalidateFeatureSnapshot()` | The settings JSON each apply mutates in place, so a transition frame never re-serializes the feature. |

**Divergence from upstream, deliberate:** upstream bumps `sceneValueRevision` at ~22 call sites and sets
`locationOverridesDirty` at ~9. This fork funnels both through `MarkSceneValuesDirty()`, called from
`BumpEntryPresentationRevision()` and `ReapplyIfActive()`. That is a strict superset of upstream's
invalidation; the cost is at most one extra period-map rebuild per user action, never per frame. Route new
mutations through those two functions instead of touching the counters directly.

Apply failures are keyed by a `size_t` signature (`CombineHash` / `HashSceneSettingValue`) so a retry is
skipped until the pending values actually change.

### SceneLayerGuard

`SceneSettingsManager::SceneLayerGuard` is an RAII suspend of the scene layer. Anything that reads or writes
a feature's *base* settings must hold one, otherwise it captures an overridden value as if it were the user's
choice. It is default-constructed (`SceneLayerGuard guard;`) and no-ops when the manager singleton does not
exist yet. Current holders: `State::Load`, `State::SaveToJson` and `State::LoadFromJson`, one internal manager
path (`GetFeatureSettingValue`), and six DevBench bridge endpoints. Add one to any new code path that
serializes feature settings.

In `State::SaveToJson` / `State::LoadFromJson` the guard is declared **before** `m_mutex` is taken, so the
resolve it triggers on destruction does not run while the lock is held.

## Generic Scene Copy API

Copies settings between any two scene contexts (a time-of-day period, a weather period, or a location
target). Driven by `ScenePageToolbar`, which puts the From/To submenus on every scene page.

A context is a `SceneContextId`: a `SceneContextType` plus whichever of `period` / `weatherId` /
`locationType` + `locationFormKey` that type uses. `IsValidSceneContext()` rejects any mixed combination,
so a malformed context can never reach the mutation path.

| Method | Const | Purpose |
| ------ | ----- | ------- |
| `GetCopySources(destination)` | yes | Every context that holds something usable, with a localized label and a compatible-setting count. Excludes the destination itself. Sorted by type, then label. |
| `GetCopyDestinations(source)` | yes | Every context the source can copy into, including pages with nothing authored yet. |
| `GetCopyCandidates(source, destination, periodScope)` | yes | Per-setting preview: display name, value, `compatible`, `conflicts`. Drives the confirmation dialog. |
| `CopySettings(source, destination, conflictPolicy)` | no | Performs the copy and returns a `CopyResult` (`copied` / `skipped` / `overwritten` / `incompatible` / `hadConflicts` / `cancelled`). |

A copy always takes the whole source context. The per-setting variant upstream carries (`CopyScope::Setting`
plus a `SettingIdentity`) was removed: this fork's toolbar is page-scoped, and a single setting is moved by
editing it on the destination page instead.

**Compatibility.** A setting is copyable when it is in the catalog, allowed for the destination's scene
type, its value passes `IsSceneSettingValueAllowed`, and the destination has no active `Overwrite` shadowing
it. Only the location layer accepts non-float settings; every other destination requires numerics.
Compatibility is **group-aware**: members of one logical control (a colour, a vector) share a
`CopyGroupKey`, and one unusable member disqualifies the whole group, so a copy can never leave half a
colour behind.

**Conflicts** are compatible settings the destination already holds as a `User` entry.
`CopyConflictPolicy::SkipExisting` leaves the whole conflicting group alone, `OverwriteExisting` replaces
the value in place (keeping the existing `originalValue`), and `Cancel` aborts the entire operation and
returns `cancelled` without touching anything.

**`Cancel` is kept without a caller, and deliberately so.** The toolbar aborts a copy by dismissing the
preview before anything is staged, not by passing `CopyConflictPolicy::Cancel`: `StartCopy` builds the
candidate list over the same `PeriodScope` the copy will use, so the modal already shows every conflict the
fan-out would hit, and dismissing it is the same observable outcome for less noise (routing the button
through the policy would fire a "0 copied, 0 overwritten" toast for an action the user just cancelled).
`Cancel` stays because "abort" has to be decided over the whole operation, not per period:
`CopySettingsAcrossPeriods` pre-checks every period before it stages anything. Removing it would bake the
fan-out's partial-application semantics into the API and leave any future non-interactive caller with no way
to ask for all-or-nothing.

**Transactionality.** Everything is validated and staged into a pending list first; the destination config
is only materialized once the copy is known to produce entries, and one `CommitSceneSettingChanges()` at the
end does a single save plus a single reapply.

**New entries get a correct `originalValue`** so revert and restore still work: from the destination's lower
layers for a location, from the time-of-day layer for a weather period, and from the feature baseline
otherwise. Copying into a location also carries a transition duration: the destination entry's own duration
wins, otherwise the source's.

## On-disk layout

Rooted at `Util::PathHelpers::GetSceneSettingsPath()` = `<CommunityShaders>/SceneSettings`.

| Path | Contents |
| ---- | ---- |
| `SceneManager.json` | All user-authored entries (interior, TOD, weather, location) in one document. |
| `InteriorOnly/`, `TimeOfDay/<Period>/` | Mod-shipped overwrite files per scene type. |
| `Weather/<SPID>/` | Per-weather overwrites, folder keyed by `Util::FormIdToSpid`. |
| `Locations/<form key>/` | Location **and** cell overwrites share one tree; the target's type comes from its form. |

`SceneManager.json` is written atomically. If the existing document is present but not a JSON object, saves
are **blocked** rather than clobbering it, and unknown fields on an entry are preserved through
`serializedTemplate` for forward compatibility.

## Policy

`src/SceneSettingsPolicy.h` is hand-maintained and pruned to features that exist in this fork:

-   `kSettingBlacklist` — settings that must never be scene-overridden, matched by catalog address prefix.
    Upstream's entries all pointed at features this fork does not have, so the list is this fork's own:
    the `ExponentialHeightFog` volumetric entries, which shape the froxel grid and its history buffers.
    Scene overrides travel through `SaveSettings` → JSON patch → `LoadSettings`, which never re-runs the
    allocation those settings size, so blending them mid-frame is not something the feature can honor.
-   `kLocationFeatureWhitelist` (4) and `kTimeOfDayFeatureWhitelist` (7) — which features those scene types
    may target.

When adding a feature to a whitelist, run `tests/test_scene_settings_policy.py`; it fails if a name is not
discovered in the generated catalog.

## Feature-facing contract

`Feature` gained two virtuals in this port (`src/Feature.h`):

| Virtual | Default | Meaning |
| ------- | ------- | ------- |
| `IsAlwaysEnabled()` | `false` | Infrastructure that cannot be disabled at boot. `State` erases it from `disabledFeatures` and refuses toggles. |
| `UsesMainSettings()` | `true` | Persists through the shared settings JSON; gates override discovery. |

`Feature::RegisterWeatherVariables()` was **removed**. Features no longer register anything; they just draw
plain ImGui controls over persisted members.

## What was dropped

Everything below exists upstream and was intentionally left out. Do not treat it as missing work unless
someone asks for the UI layer.

### Excluded upstream files

| File | Lines | What it was |
| ---- | ----- | ----------- |
| upstream `SceneSettingsUI.{h,cpp}` | ~3180 | The authoring UI: add-setting dialogs, per-scene panels, weather scene panel. This fork's `src/CSEditor/SceneSettingsUI.{h,cpp}` is unrelated in-house work that happens to share the name. |
| `src/SceneSettingsUIHooks.{h,cpp}` | ~776 | ImGui interception marking scene-controlled widgets and offering right-click capture. |
| `src/Features/SceneManagerUI.{h,cpp}` | ~34 | `SceneManager::DrawSettings()` body. |

Correspondingly, `SceneManager` here has **no** `PostPostLoad()` (upstream's called
`SceneSettingsUIHooks::Install()`), and its `DrawSettings()` is a debug view of the resolver's live state
rather than an authoring panel.

### Removed from this fork

-   `src/WeatherManager.{h,cpp}`, `src/WeatherVariableRegistry.h` and `docs/weather-system-docs/` — the old
    registry the catalog replaces.
-   `Util::WeatherUI::{IsWeatherControlled,SliderFloat,Checkbox,ColorEdit3,ColorEdit4}` — its only call sites
    were in `ExponentialHeightFog` (22) and `IBL` (7), and both are now plain `ImGui::` calls.
-   `src/CSEditor/InteriorOnlyPanel.{h,cpp}` and its EditorWindow category. `EditorWindow::LoadSettings()`
    remaps a saved `"Interior Only"` category to `"Weather"`.
-   The WeatherWidget per-feature "Features" tab and its `featureSettings` map (upstream replaced it with a
    "Scene Settings" tab that lives in the excluded UI), plus `OpenWeatherFeatureSetting()`.
-   The "Pause Weather Overrides" checkbox in `FeatureListRenderer`.
-   `SupportsVR()` on `SceneManager` — that virtual is an `alandtse` lineage concept and does not exist on
    this fork's `Feature`.
-   Upstream's `InvertedCheckbox`, `RadioButton`, `ActiveControlStorageGuard` and
    `GetActiveControlStorageAddress` UI helpers, used only by the excluded UI.

### Existing UI

-   `src/CSEditor/SceneSettingsUI.cpp` is this fork's own editor: the time-of-day period bar with its
    automatic time pause, the interior toggle, the per-feature list, and per-location windows. It uses only
    `GetCurrentGameHour` / `SetGameHour`, `GetCurrentPeriod`, `Get*RelevantFeatureNames`,
    `GetFeatureDisplayName`, and the `LocationTarget` accessors.
-   `FeatureListRenderer` shows a scene-controlled indicator and a **Scene Specific Settings** pause toggle
    per feature (`IsFeaturePaused` / `SetFeaturePaused`). The row is keyed on
    `HasAnySceneEntriesForFeature`, which answers whether the feature is authored *anywhere*, so it stays
    visible and pausable while the player is somewhere the overrides do not reach; it is marked *not active
    here* in that state. `HasActiveSettingsForFeature` answers the narrower "is it applying right now" and
    is what still gates disabling the feature's own controls and its **Apply Override** button. Its
    "Restore Defaults" button is the one path that rewrites a feature's base values while the scene layer
    is live, so it follows the restore with `CaptureExternalFeatureChanges` to re-baseline; without that the
    next resolve puts the old values back.
-   `CSEditor` flags a weather that has scene settings via `HasWeatherConfig`.
-   `src/CSEditor/SceneWidgetInterceptor.cpp` detours the ImGui calls and replays a feature's real
    `DrawSettings()` bound to a scene context, so entry authoring needs no per-scene tables:
    `SceneWidgetBinding::Guard` creates, edits, pauses and deletes entries in place.
    `GutterPolicy::GroupMember` is kept although no intercepted feature currently draws a radio group: it is
    what stops several calls against one address (`RadioButton`) from each drawing their own gutter toggle,
    and the alternative to keeping it is a latent double-gutter bug the first time a feature adds one.
-   `src/CSEditor/ScenePageToolbar.cpp` drives the [copy API](#generic-scene-copy-api) and preset export.

## Known gaps

-   **Bounds are enforced on apply, not on input.** [Numeric bounds](#numeric-bounds) clamps a value on its
    way into a feature, so nothing out of range reaches a shader constant. A value typed past the range in a
    widget that does not clamp (`clampNumericInput` records which ones) is still stored as typed; only the
    scene layer's own writes are bounded.
-   Presentation metadata that survives into the generated C++ (`sourceWidget`, `hdrColor`,
    `clampNumericInput`) is emitted but unread: the interceptor replays the feature's own widget instead of
    rebuilding one. It stays because `validate_entries` uses it, `sourceWidget` against
    `SOURCE_WIDGET_ENTRY_POINTS` and the other two to enforce colour-metadata invariants.
    `displayPath` / `selectorPath` are read, they name entries.

**Presentation-only concepts do not belong in the generated C++.** `AggregatePresentation`,
`UnifiedEditMode`, choice display names and `GetVirtualAggregateControls` were each generated without a
consumer and have been removed. Consuming any of them would mean the editor synthesizing a widget that does
not exist in the feature's own panel, which breaks the invariant the whole interceptor rests on: a scene
page shows the feature's real `DrawSettings()`, replayed. Do not re-add an emission without a caller that
justifies the second rendering path.

## Testing

```bash
python -m unittest tests.test_scene_settings_catalog_generator tests.test_scene_settings_policy
```

74 tests. Both suites run in CI via `.github/workflows/pr-python-tests.yaml`, on any PR touching `src/**`,
the generator, or `tests/**`. Run them locally after touching the generator or the policy lists.

The generator can be run standalone to inspect its output:

```bash
python cmake/generate_scene_settings_catalog.py --source-dir . --out-dir /tmp/catalog --min-entries 250 \
    --min-controllable 290 --min-controllable-features 28
```

The generator test file is upstream's with the open-shaders-coupled assertions removed (`CSUtility`,
`PostProcessing`, `LightLimitFix` contact shadows, the `ExtendedTranslucency`/`Upscaling`/`VR` regression
list, and the inverted-display IBL toggles). Do not re-add assertions naming features this fork lacks.
