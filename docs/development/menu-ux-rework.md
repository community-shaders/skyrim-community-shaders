# Main Menu UX Rework: Plan

Status: planning · Owner: David Kehoe · Started 2026-10-25

This plan brings the CS Editor's newer design language (action bar, shortcuts, undo, browser widgets,
chips, page headers) back into the main Community Shaders menu. It also fixes the navigation, Home
and Settings problems found in the 2026-10-25 assessment.

All work happens on one integration ("super") branch so the whole thing can be tested together. It
is then split by area into four stacked PRs against `dev`, followed by a locale-sync PR.

---

## 1. Goals and non-goals

**Goals**

1. Clear navigation. The left pane shows a short top group (Home, Presets, CS Editor), the feature
   list, and a small bottom group (Settings, Profiling, Issues). It gets roughly 40% shorter by
   default.
2. Home becomes a status dashboard (preset, scene, shaders, attention items, quick access)
   instead of a brochure.
3. Settings sit on one page with a single sub-nav level. Today they are spread across
   General/Advanced and up to three nested tab bars.
4. Keyboard first: Ctrl+S, Ctrl+F, Ctrl+Z/Y, Ctrl+K (go to anything) and feature stepping. All are
   listed in a Help menu built from one shortcut table.
5. Search reaches individual settings, not just feature names, and works in every locale.
6. One visual vocabulary shared with the CS Editor: `BrowserUI` chips, badges, page headers, empty
   states, and the search field.

**Non-goals**

-   No changes to feature `DrawSettings()` bodies, apart from what the interceptor already does.
-   No changes to the ImGui backend, font atlas or renderer setup (CLAUDE.md rule).
-   No change to the settings file format beyond added keys. Old keys stay readable.
-   No restyling of the CS Editor itself, except extracting shared pieces.

---

## 2. Branch and PR strategy

### 2.1 Integration branch

```bash
git fetch upstream
git worktree add ../cs-menu-ux -b feat/menu-ux upstream/dev   # keeps feat/pbr-water untouched
```

-   The worktree needs its own build dir. Point `dev-cycle.ps1` at it, or add a `CMakeUserPresets.json`
    preset with a short binary dir (see the Bottlerim dev-loop notes).
-   Rebase `feat/menu-ux` onto `upstream/dev` at least weekly. `src/Menu*` is a high-churn area.

### 2.2 Commit discipline (this is what makes the split mechanical)

-   Every commit belongs to exactly one PR. Mark it with a trailer:

    ```
    feat(ui): add main menu action bar

    <body>

    Menu-PR: B
    ```

-   Order commits on the super branch A → B → C → D. Fixes found during integration testing go in as
    `git commit --fixup=<sha>` against the commit they fix, then
    `GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash upstream/dev`.
-   Do not mix areas in a commit. If a C change needs a helper, land the helper as its own commit in
    the earliest PR that can own it.
-   This plan doc is committed on the super branch with `Menu-PR: none`. It ships with PR B as a dev
    doc, or gets dropped, whichever reviewers prefer.

### 2.3 Splitting

```bash
# PR A: based on dev
git switch -c menu-ux/a-fixes upstream/dev
git cherry-pick $(git log --reverse --format=%H --grep="Menu-PR: A" upstream/dev..feat/menu-ux)
# PR B: stacked on A (retarget to dev after A merges)
git switch -c menu-ux/b-shell menu-ux/a-fixes
git cherry-pick $(git log --reverse --format=%H --grep="Menu-PR: B" upstream/dev..feat/menu-ux)
# ...C on B, D on C
```

Each split branch must build on its own (`BuildPR.bat`) and pass the i18n checks before it opens.
PRs target `dev` and use branches on `origin` (davo0411).

### 2.4 PR map and release impact

| PR | Branch | Title (conventional) | Release | Depends on |
|---|---|---|---|---|
| A | `menu-ux/a-fixes` | `fix(ui): menu search, nav and restore fixes` | patch | — |
| B | `menu-ux/b-shell` | `feat(ui): menu action bar, shortcuts and undo` | minor | A |
| C | `menu-ux/c-nav-home` | `feat(ui): menu navigation and home dashboard` | minor | B |
| D | `menu-ux/d-settings` | `feat(ui): unified settings page and go-to search` | minor | B (C for nav entry) |
| E | `menu-ux/e-i18n` | `chore(i18n): sync locales with menu rework` | none | D |

No `feat!`: the settings JSON stays backward compatible. If C grows past about 1500 changed lines,
split it into C1 (navigation) and C2 (Home).

PRs A–D change only `en.json`. Every other locale file is left alone until PR E (decision D-7).

---

## 3. PR A: correctness fixes and quick wins (`fix`)

Small, independent, and safe to ship on its own. Every item is a bug or a wording fix, so it stays a
`fix:`.

| # | Change | Where | Done when |
|---|---|---|---|
| A1 | Feature search also matches `GetDisplayName()` (localized) and the category label | [UI.cpp:1883](../../src/Utils/UI.cpp#L1883) `FeatureMatchesSearch` | In `de`, typing a translated feature name finds it |
| A2 | Canonical page IDs: `BuiltInMenu` gains an untranslated `id`. `IsCoreMenu`, `ListMenuVisitor` and `HandlePendingFeatureSelection` compare IDs. Display-name lookup stays as a fallback for existing callers | [FeatureListRenderer.cpp:40](../../src/Menu/FeatureListRenderer.cpp#L40), [:577](../../src/Menu/FeatureListRenderer.cpp#L577), [:684 PresetsPage](../../src/Menu/PresetsPageRenderer.cpp#L684), FeatureListRenderer.h | Built-in pages stay at the top in every locale; `SelectFeatureMenu("Presets")` works in `de` |
| A3 | Remove the dead `"Display"` core entry and the unused `coreMenuCount` | FeatureListRenderer.cpp | — |
| A4 | Feature Issues renders in the top group, not under the search bar | FeatureListRenderer.cpp | — |
| A5 | Restore Saved Settings asks first (`Util::ConfirmationPopup`) when there are unsaved changes | [MenuHeaderRenderer.cpp:178,263](../../src/Menu/MenuHeaderRenderer.cpp#L263) | No one-click loss of edits |
| A6 | Short header-icon tooltips ("Clear shader cache"); the long explanation moves into the confirmation dialog body | MenuHeaderRenderer.cpp, `Util::RequestClearShaderCacheConfirmation` | — |
| A7 | FAQ wording: "Display tab" → Upscaling feature; check the Keybindings path and the "Feature Issues tab" wording | [HomePageRenderer.cpp:222](../../src/Menu/HomePageRenderer.cpp#L222) | — |
| A8 | Disable at Boot list uses display names, sorted by display name | [Menu.cpp:888](../../src/Menu.cpp#L888) | — |

i18n: changed English strings keep their keys only when the meaning is unchanged. Otherwise use a
new key and drop the old one (`--orphans`).

---

## 4. PR B: header, status bar, shortcuts and undo (`feat`)

The current header design stays: logo and title on the left, icon actions on the right. There is no
File/View/Tools menu bar (decision D-1).

### B0. Shared infrastructure (refactor commits, land first inside B)

The main menu needs editor pieces that are currently tied to `EditorWindow`.

| Piece | Today | Move / change |
|---|---|---|
| `BrowserUI` widgets | [BrowserWidgets.cpp](../../src/CSEditor/Browser/BrowserWidgets.cpp) includes `EditorWindow.h` for two things: `suppressNextEditorEscape` (`:128`) and `resetLayout` (`:549`). `DrawIconStar`/`DrawIconFlag` live in EditorWindow.cpp | Replace both with a shared one-shot **Escape-consumed** flag on `Menu` (see B4) and a `resetLayout` parameter on `SplitPane`. Move the star/flag painters into BrowserWidgets.cpp. Keep the `BrowserUI` namespace (renaming would churn the editor) |
| `ShowNotification` (toasts) | `EditorWindow` member | `Util::Notifications` (queue, draw once per frame from `OverlayRenderer`). The editor delegates to it |
| `MakeFeatureUndoTarget` | anonymous in [FeatureSettingsWindow.cpp](../../src/CSEditor/FeatureSettingsWindow.cpp) | `src/Menu/FeatureUndo.{h,cpp}`, used by both surfaces |
| `UndoHistory` | owned by `EditorWindow` | Class unchanged. `Menu` owns a **separate** instance (decision D-2) |

Done when the editor behaves exactly as before (same shortcuts, Escape behavior, toasts and undo),
with no new includes from `src/Menu/*` into `EditorWindow.h`.

### B1. Header refresh (keeps the current design)

```
┌──────────────────────────────────────────────────────────────┐
│ ◆ Community Shaders 1.4     [🔍 Search…  Ctrl+K]    💾● ↺ 🗑 ? │
├────────────┬─────────────────────────────────────────────────┤
```

-   **Unchanged:** the logo and title (`CenterHeader` still applies), the docked watermark logo
    (decision D-5), and the docked/undocked icon rendering in `MenuHeaderRenderer`.
-   **Action icons** (Save, Restore, Clear Cache) keep their order. Save shows a dot while there are
    unsaved changes. Restore and Clear Cache confirm first (PR A). Tooltips are short and show the
    shortcut: "Save settings (Ctrl+S)".
-   **New `?` icon** after the action icons opens a popover:
    -   Keyboard Shortcuts (generated from the B3 table; F1 opens the popover directly)
    -   FAQ (opens a modal with the existing nine Q&As, keys unchanged)
    -   Links: Wiki · Developer Wiki · GitHub · Nexus Mods · Discord
    -   About: version/build, game runtime, GPU, D3D12 swap chain
-   **Search pill** (centered, "Search… Ctrl+K"): a button styled like a search field that opens the
    Go-to palette. It lands in **PR D** with the palette (D2); PR B leaves the space empty.
-   **Drop "Show Icon Buttons in Header"** (decision D-6). Icons are always shown, and the text-button
    fallback row under the header (wide Save / Restore / Clear Cache buttons) goes away. The
    `ShowActionIcons` key is still read from JSON and ignored. `CenterHeader`, `UseMonochromeIcons`
    and `UseMonochromeLogo` stay. If an icon texture fails to load, its button falls back to a
    font glyph (`Icons::Button`) instead of the text row.
-   The "Toggle Error Message" button row under the header moves into the status bar's failed-shader
    count (B2).
-   Former "View" options get homes: list sort stays as the icon beside the feature search; list
    collapse is the pane handle + Ctrl+B (C1); status-bar visibility and Reset Window Layout move to
    Settings › Behavior.

### B2. Status bar (replaces the footer)

```
│ ● 3 changed · Save   ⟳ Compiling 412/980   ✖ 4 failed   ! 2 issues      RTX 4090 · AE 1.6.1170 · D3D12 │
```

A thin bar at the bottom (decision D-3).

-   **Left**, each shown only when relevant:
    -   unsaved changes (click saves)
    -   shader compile progress while it runs
    -   failed-shader count (click toggles the error message)
    -   issues count (click opens the Issues page)
-   **Right**: GPU · game runtime and version · D3D12 swap chain state, as the footer shows today.
    The text is ellipsized when space is short; the left side wins.
-   `ShowFooter` becomes the status-bar toggle (same key, same default) and moves to Settings ›
    Behavior.

### B3. Shortcut system

One table drives dispatch, tooltip hints and the `?` › Keyboard Shortcuts list:

```cpp
struct MenuShortcut {
    std::string_view id;            // "menu.save"
    ImGuiKeyChord chord;            // ImGuiMod_Ctrl | ImGuiKey_S
    const char* labelKey;           // i18n key
    std::function<bool()> enabled;
    std::function<void()> run;
};
```

-   Dispatch uses `ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteFromRootWindow)`
    (public in pinned 1.92.6, imgui.h:1122). It runs only while the main menu window is focused or
    hovered, `!io.WantTextInput`, and no hotkey capture is in progress.
-   Initial set (others are reserved for D):

| Chord | Action |
|---|---|
| Ctrl+S | Save settings |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | Undo / redo (B5) |
| Ctrl+F | Focus feature search |
| Ctrl+K | Go to… palette (D3; registered in D) |
| Ctrl+PgUp / Ctrl+PgDn | Previous / next feature in the visible list |
| Ctrl+P | Pin / unpin the current feature |
| Ctrl+B | Collapse / expand feature list |
| Ctrl+1 / 2 / 3 | Home / Presets / Settings |
| Ctrl+E | Open CS Editor (when it can open) |
| F1 | Open the `?` popover (shortcuts) |
| Esc | Clear focused search → close popup → close menu (B4) |

-   **Avoid** Alt+arrows: Alt toggles ImGui's nav menu layer while keyboard nav is enabled. Also
    avoid Ctrl+Tab (ImGui window switching).
-   Conflict check: Keybindings settings flag any game-level binding that equals a menu shortcut chord.

### B4. Escape chain

Today `Menu::ProcessInputEventQueue` ([Menu.cpp:1227](../../src/Menu.cpp#L1227)) closes the menu on
Escape unless the editor or a lightbox takes it. Generalize `EditorWindow::suppressNextEditorEscape`
into a `Menu::ConsumeEscape()` one-shot. Any widget can claim the key: `SearchField` (clears text),
open popups and the palette. The CS Editor uses the same flag.

### B5. Undo / redo in the main menu

-   `Menu` owns an `UndoHistory`. Before `DrawFeatureBody`, call `Watch(MakeFeatureUndoTarget(feat))`
    for the visible feature, plus a target for the menu's own settings pages. Call `EndFrame()` at the
    end of `DrawSettings`.
-   Menu commands (Restore defaults, Use Baselines, Apply preset) go through `UndoHistory::Change` so
    they can be undone.
-   Undo/redo raises a toast ("Undid: Upscaling") through `Util::Notifications`.
-   Risk: restoring through `LoadSettings` under `SceneLayerGuard` while the baseline interceptor
    scope is live. Restore must run outside `DrawFeatureBody` (queue it for the next frame start)
    and then call `CaptureExternalFeatureChanges` + `InvalidateEditsUnderBaselines`, mirroring
    [FeatureListRenderer.cpp:1003](../../src/Menu/FeatureListRenderer.cpp#L1003).

### B6. Search field swap

`Util::DrawFeatureSearchBar` → `BrowserUI::SearchField`: magnifier, clear button, Esc clears,
Ctrl+F. `DrawFeatureSearchBar` stays as a thin wrapper until all callers have moved, then goes.

---

## 5. PR C: navigation and Home dashboard (`feat`)

### C1. Left pane structure

```
┌───────────────────────────┐
│ ⌂ Home                    │  fixed top group
│ ▣ Presets                 │
│ ✎ CS Editor            ↗  │  launcher style (external-open glyph; disabled + tooltip outside a save)
│ ! Issues (3)              │  only when there are issues
├───────────────────────────┤
│ [🔍 Search features…  ✕]  │  BrowserUI::SearchField (Ctrl+F)
│ (All)(★ 4)(● Modified 6)  │  filter chips, see C3
│ (◐ Scene)(β Pre-release)  │
│ ★ pinned rows…            │  scrolling child
│ ▸ Lighting (9)            │
│   Skylighting   [β] ●     │
│ …                         │
│ ▸ Display (4)   (end)     │
│ ▸ Utility (5)   (end)     │
├───────────────────────────┤
│ ⚙ Settings                │  fixed bottom group
│ ⏱ Profiling               │
└───────────────────────────┘
```

-   Three regions: fixed top, a scrolling feature child, and a fixed bottom (measure the bottom height
    first, then `BeginChild(..., ImVec2(0, -bottomHeight))`).
-   In C, the bottom group shows **General** and **Advanced** until D replaces them with **Settings**.
    Each PR's UI stays coherent on its own.
-   Main-menu category order moves Display and Utility to the end and collapses Utility by default.
    Implementation: a `kMainMenuOrder`, or reorder `kMenuOrder` after checking its other users
    (FeatureListPicker already skips both in the editor).
-   Collapse (Ctrl+B / View menu) hides the pane and leaves a thin handle strip that expands it on
    click. This replaces the hover-to-reveal `AutoHideFeatureList`: `true` migrates to start collapsed;
    the setting key is kept.

### C2. Feature rows via `FeatureListPicker`

Move the main list onto [FeatureListPicker](../../src/CSEditor/FeatureListPicker.cpp), extending its
`Options` so it can draw everything the main list draws today:

-   Status colors: disabled, restart-pending, failed, missing (the editor already has most of these).
-   The Modified chip (C3) also marks the row with a small dot.
-   Stage badge as `BrowserUI::InlineBadge` (α/β) instead of the text tag.
-   Scene markers: overriding (filled), configured (hollow), sketched (green). Generalize `Marker`
    into a small list of dots.
-   **No version per row.** The version stays in the feature header.
-   Right-click row menu: Pin/Unpin · Restore Defaults · Disable at Boot / Enable at Boot · Copy Name.
-   Push IDs from `GetShortName()`, not the loop index.
-   This deletes the list half of FeatureListRenderer (`ListMenuVisitor`, `PillSelectable`, the
    custom pin painter).

### C3. Filter chips

`BrowserUI::FilterChip` / `ToggleChip` rows under the search field. Chips combine with AND, and the
search text applies on top.

| Chip | Source | Notes |
|---|---|---|
| Favorites | `State::IsFeatureFavorite` | count badge |
| Modified | **new** `State::HasUserEdits(feat)`: user layer differs from defaults + enabled Baselines (decision D-8). It stays lit after saving. It's the same condition that enables Restore Defaults | cache per feature and invalidate on `InteractionEndedThisFrame`, preset apply, Baseline change and load. Never compute per frame |
| Scene | `SceneSettingsManager::HasAnySceneEntriesForFeature` | |
| Pre-release | `GetReleaseStage() != Release` | resolve the stage once per rebuild (not cached upstream) |
| Unloaded | `!loaded && IsInMenu() && !IsHiddenUnreleased()` | replaces the separate "Unloaded Features" section |

The active chip set persists in `Menu::Settings::FeatureListFilter` (bitmask; unknown bits are
ignored on load). Sort mode stays on the icon button beside the search field.

**Perf:** `BuildMenuList` currently copies and sorts the whole feature list every frame. Cache it
and rebuild only on a dirty signal (search/chip/sort change, favorite toggle, locale change, feature
load state change).

### C4. Built-in page chrome

-   `BrowserUI::PageHeader(icon, title, subtitle)` on Home, Presets, General/Advanced (later
    Settings), Profiling and Issues, so they match feature pages.
-   `BrowserUI::EmptyState` replaces "Please select an item on the left", "There are no settings
    available" and the disabled-at-boot message (with an "Enable at boot" action button).
-   Feature header (confirmed):

    ```
    Skylighting  β  v1.2.0                 ★   ⋯   Load at boot [●━]
    Description text…
    ```

    -   The boot toggle gets a visible "Load at boot" label (subtext font), so it no longer reads as
        an on/off switch for the effect.
    -   Pin stays as a direct icon.
    -   Export Overwrites, Use Baseline Values and Restore Defaults move into the `⋯` menu. Disabled
        entries keep their explanation as a tooltip (`kTooltipWhenDisabled`).
    -   The right side goes from five icon-only controls to three.

### C5. Home dashboard

A responsive card grid: 2 columns at ≥ ~60 font-widths of content, otherwise 1. Each card is a
bordered child with a `SectionLabel` title and at most 2 actions.

| Card | Content | Actions | Source |
|---|---|---|---|
| Unsaved banner (only when dirty) | "You have unsaved changes in N features" | Save · Restore… | `State::HasUnsavedChanges` |
| Preset | Active pack name, thumbnail, backend badges; Baseline count + names | Browse Presets · Disable | `UnifiedPresetCatalog::GetActivePackId`, `IsBaselineEnabled`, `PresetsPageRenderer::DrawBackendBadges` |
| Scene | "Applying Now" summary; in the main menu or with no save, a hint | Open CS Editor · Locations | `SceneLayerHeader::DrawSummary`, `EditorWindow::CanBeOpen` |
| Shaders | Compile breakdown bar, last build time, failed count | Clear Cache… · Show Errors | extract the bar from [SettingsTabRenderer.cpp:280](../../src/Menu/SettingsTabRenderer.cpp#L280) into `ShaderCacheStatusWidget` (shared with Settings) |
| Needs attention | Feature issues count, active constraints count, restart-pending features | Open Issues | `FeatureIssues`, `FeatureConstraints::GetAllActiveConstraints` |
| Quick access | Pinned features + up to 6 recently opened features | click to open | **new** `Menu::Settings::RecentFeatures` (vector of short names, capped, validated on load) |
| Community (bottom) | A **smaller Discord banner** (about half today's width cap) with a row of icon chips beside or under it: Nexus · GitHub · Wiki | — | replaces the big banner and Quick Links |

-   The welcome title and intro paragraph are removed. The version is shown once, in the header.
-   The Discord banner stays at reduced size, at the bottom (decision: "Dashboard + small Discord
    banner"). Its size comes from new `ThemeManager::Constants` (no longer the
    `DISCORD_BANNER_*` ratio).
-   FAQ moves behind the header `?` (B1), as a modal with the existing nine Q&As and keys unchanged.
-   The first-time setup dialog is unchanged.

### C6. Issues page

Merge Feature Issues (`FeatureIssues::DrawFeatureIssuesUI`) and the Active Setting Constraints table
(moved from Home) into one Issues page. Its nav entry shows only when either is non-empty, with a
count badge in the error color.

---

## 6. PR D: unified Settings page and "Go to…" (`feat`)

### D1. Settings page

One nav entry, **Settings**, replacing General and Advanced. Inside: a fixed-width sub-nav column on
the left (`Selectable` list with `SectionLabel` groups) and the content on the right. **One level
only:** no tab bars inside sections. Long sections use `SectionLabel` headings.

| Group | Section | Contents (from) |
|---|---|---|
| General | Shaders & Cache | Use Custom Shaders, Disk Cache, Skip Unchanged, Async, compiler threads, background threads, Dump Shaders, Skip Clear-Cache Dialog, `ShaderCacheStatusWidget`, Clear Cache (General›Shaders + Advanced) |
| | Keybindings | All 10 bindings + shader-block prev/next, grouped Menus & Editors / Overlays & Capture / Debug; per-row reset; conflict warnings (B3) |
| | Language | Locale combo |
| | Behavior | Tooltip delay, Require Shift to Dock, constraint-warning toggle, feature list defaults, status bar on/off, Reset Window Layout |
| Appearance | Theme | Theme picker as cards (name, description, swatch strip); Save / Save As / Delete; one save explanation (see below) |
| | Fonts | (from Interface›Fonts) |
| | Colors | (from Interface›Colors) |
| | Styling | Global scale up front; the raw ImGui sliders sit under a collapsed "Advanced styling" fold |
| | Header & Effects | Center header title, monochrome icons, monochrome logo, background blur, custom cursor |
| Advanced | Developer | Developer mode, file watcher, dump defaults, frame annotations, half precision, content store |
| | Logging | Log level, open logs, dump ini |
| | Shader Debug | Shader blocking, active shaders |
| | Testing | (as today) |
| | Disable at Boot | Searchable; display names; "Disabled only" chip; restart-required banner |

-   **Theme save model, said once:** "Theme edits are saved into the theme file with Save Theme.
    Which theme is selected is saved with Save Settings (Ctrl+S)." Remove the two contradictory
    lines in Behavior/Fonts and Themes.
-   The last section is remembered (`Menu::Settings::LastSettingsSection`, validated against known
    IDs).
-   **In-page search** at the top of the sub-nav: filters sections and highlights matching rows. It
    is driven by a small static index of the menu's own settings (`{sectionId, labelKey}`),
    declared next to each section's draw function. This is separate from the feature catalog.
-   Translation keys: keep existing `menu.settings.*` / `menu.advanced.*` keys where the string is
    unchanged. Only section titles and new strings get new keys.

### D2. "Go to…" palette (Ctrl+K)

A centered modal palette: input, ranked result list, ↑/↓, Enter, Esc. Each result shows a type
badge and a breadcrumb.

It opens with Ctrl+K or from the header's centered "Search… Ctrl+K" pill (B1), which this PR adds.
The pill shrinks to an icon-only button when the header is narrow. Commands include "Open Effects
11 Editor", "Toggle performance overlay", "Take screenshot" and "Open logs folder", since there is
no Tools menu.

| Source | Count | Result | Action |
|---|---|---|---|
| Pages and Settings sections | ~20 | "Settings › Keybindings" | navigate |
| Features | ~45 | "Lighting › Skylighting" (display + short name + category) | `SelectFeatureMenu` |
| Feature settings | 728 (generated `SceneSettingsCatalog::GetSettings()`) | "Skylighting › Diffuse › Brightness": localized via `displayNameKey`/`displayPathKeys` | navigate + reveal (D3) |
| Commands | ~12 | "Save settings", "Clear shader cache…", "Open CS Editor", "Toggle performance overlay" | run; shows the chord |

-   Ranking: case-insensitive subsequence match, with bonuses for word-start and prefix matches,
    then type priority (pages > features > settings > commands for equal scores). Build the index
    lazily; rebuild on locale change.
-   Leave out catalog entries flagged hidden/debug, and features that aren't loaded (or show them
    greyed with "(not loaded)").
-   Recent picks float to the top when the query is empty.

### D3. Reveal a setting (spike first: D3a)

Goal: after choosing a catalog setting, the feature page scrolls to the control and flashes it.

-   Mechanism: navigation sets `pendingReveal = {feature, settingPath, settingKey}`. On the next
    `DrawFeatureBody`, the baseline `SceneWidgetInterceptor::Scope` is already live. In the detoured
    widget path, when `FindSettingForControl(feature, valueAddress)` matches the pending setting,
    call `ImGui::SetScrollHereY(0.3f)` and record the item rect for a 1-second accent outline
    (foreground draw list).
-   Closed collapsing headers and tree nodes hide the control, so it is never drawn. D3a investigates
    two options:
    1. use `displayPath` segments to force-open matching `CollapsingHeader`/`TreeNode` labels
       for one frame (`SetNextItemOpen`) if the interceptor can see them;
    2. a fallback that navigates and shows a "Look under: Diffuse › Brightness" breadcrumb toast.
-   Ship D2 with fallback 2 if D3a doesn't pan out. Reveal is an enhancement, not a blocker.

### D4. (stretch) In-feature filter

A small filter field in the feature header that dims non-matching controls through the
interceptor, using the same matching as D2. Only if D3a shows the interceptor sees labels cheaply.

---

## 7. PR E: locale sync (`chore`, last)

Opens only after D's strings have settled. It touches only the eight non-English files:
`de, es, fr, ja, nl, pl, ru, zh_CN`.

1.  **Prune** keys that PRs A–D removed from `en.json`.
2.  **Carry over** values for keys that moved without changing meaning. A key renamed purely for
    reorganization keeps its existing translation.
3.  **Draft** translations for every new key. Translate values only; keep keys, placeholders
    (`{count}`), format specifiers (`%s`, `%.1f`) and anything after `##` unchanged.
4.  Run `sort-i18n.py --write`, then all the i18n checks.
5.  In the PR description, ask native speakers to review, listing new keys per locale.

Until E lands, other locales show English for new strings. That is expected, and noted in PRs B–D.

---

## 8. Cross-cutting rules (apply to every PR)

-   **i18n:** every new user-visible string goes through `T()`. New namespaces: `menu.header.*`,
    `menu.shortcuts.*`, `menu.nav.*`, `menu.home.*` (reused), `menu.issues.*`, `menu.settings.*`
    (reused), `menu.palette.*`. Each PR runs `extract-i18n.py --write`, `--check`, `--orphans`,
    `sort-i18n.py --check`. Removed English keys are deleted from `en.json`. **PRs A–D touch only
    `en.json`.** Other locales are handled in PR E (decision D-7). Before PR A, check whether any
    i18n check fails on keys left orphaned in non-English files; if one does, note it, since PR E
    clears them.
-   **Settings persistence:** new `Menu::Settings` fields (`FeatureListFilter`, `RecentFeatures`,
    `FeatureListCollapsed`, `LastSettingsSection`) are read in `Menu::Load` with type checks and
    clamping. A malformed value falls back to the default, never throws. Obsolete fields stay
    readable.
-   **Theme constants:** new sizes, alphas and paddings go into `ThemeManager::Constants`. Sizes are
    scaled by `Util::GetUIScale()`.
-   **ImGui hygiene:** RAII guards (`Util::DisableGuard`, `FontRoleGuard`, …); every
    `BeginTable`/`EndTable` paired; IDs from stable keys; check signatures in the pinned 1.92.6
    `imgui.h`; component-wise ImVec math.
-   **Doxygen** on new public methods. No TODO/FIXME.
-   **Threading:** all of this is render-thread UI. Anything that touches game state (opening the CS
    Editor, scene summary) uses the existing guarded entry points.
-   **Gamepad/keyboard nav:** header icons, the `?` popover, chips, cards and the palette must be
    reachable with ImGui nav. Test with nav enabled.

---

## 9. Testing

### 9.1 Per commit group

-   `./BuildDevFast.bat` compiles.
-   `pre-commit run --all-files`.

### 9.2 Per PR, before opening

-   `./BuildPR.bat` on the split branch on its own.
-   The four i18n checks.
-   The in-game checklist below, on Bottlerim via `dev-cycle.ps1`.
-   Before/after screenshots for the PR description (devbench `capture` with UI included, or a
    manual screenshot).

### 9.3 In-game checklist (super branch, then per split branch)

Automate where possible. devbench `menu invoke CommunityShaders {op, page}` opens pages; devbench
`input` taps go through Skyrim's input queue, which feeds `Menu::ProcessInputEvents`, so shortcuts
can be scripted. Use the imgui MCP for widget-tree assertions when it connects.

| Area | Scenarios |
|---|---|
| Locales | en, de, zh_CN: nav order, search by translated name, palette results, no clipped labels (before PR E, new strings show in English in de/zh_CN, which is expected) |
| Header | icons with a missing texture fall back to glyphs; `?` popover and FAQ modal; Center Header on/off; monochrome icons/logo; watermark when docked |
| States | title screen (no save) vs in game; CS Editor launcher disabled/enabled correctly |
| Layout | undocked, docked, 1080p, 1440p, 4K, Global Scale −1/0/+1, narrowest allowed window |
| Shortcuts | each chord with: list focused, slider active, text field focused (must not fire), hotkey capture active (must not fire), CS Editor open (must not double-fire) |
| Escape | search with text → clears; popup open → closes; palette → closes; nothing → menu closes; lightbox and editor still win |
| Undo | slider drag = 1 step; checkbox; Restore Defaults; preset apply; undo across feature switches; undo after Save; Baselines + Scene Manager overriding feature (blue values stay correct) |
| Filters | each chip with and without search; Modified updates after edit/restore/preset; favorites persist |
| Persistence | new settings survive restart; delete or garble each new key in the JSON → menu loads with defaults |
| Failures | a feature failing to load; failed shaders (status bar + Home card); constraints present |
| First run | first-time setup dialog still shows and closes with Enter/Escape/menu key |
| Gamepad | navigate header icons, `?` popover, chips, cards, palette |

---

## 10. Risks

| Risk | Mitigation |
|---|---|
| Undo interacting with the baseline interceptor, scene sketches and Baselines | Restore outside `DrawFeatureBody`; reuse the post-edit bookkeeping calls; dedicated test rows |
| Shortcut collisions with game hotkeys or ImGui nav | No Alt / Ctrl+Tab chords; focus- and text-input gating; conflict warnings in Keybindings |
| Escape regressions (the editor and lightbox have special handling) | One `ConsumeEscape` path; explicit checklist row |
| Upstream churn in `src/Menu*` | Weekly rebase; PR A ships early to shrink the diff |
| Saved dock layout breaking | Same window ID; the header keeps its structure |
| Removing the text-button header fallback leaves no actions if icon textures fail | Glyph fallback through `Icons::Button` (B1) |
| `kMenuOrder` change leaking into the editor | Separate main-menu order constant if any other user cares |
| PR C size | Split into C1/C2 (section 2.4) |
| Catalog coverage gaps for the palette | Feature-level fallback result; document in the PR |
| Users missing the removed Home content | FAQ and links one click away behind the header `?`; Discord banner kept (smaller); mention in the changelog |

---

## 11. Decisions (confirmed 2026-10-25)

| ID | Decision | Confirmed |
|---|---|---|
| D-1 | Header / action bar | **Keep the current header.** No File/View/Tools menus. Add a centered "Search… Ctrl+K" pill (lands in D) and a `?` popover (shortcuts, FAQ, links, About) |
| D-2 | Undo history | **Separate** from the CS Editor |
| D-3 | Footer | **Status bar** with actionable status on the left and GPU · runtime · D3D12 info kept on the right |
| D-4 | Profiling placement | **Bottom nav group** |
| D-5 | Logo | **Keep both** the header logo and the docked watermark as today |
| D-6 | Header theme options | **Drop "Show Icon Buttons in Header"** (and its text-button row). Keep Center Header, Monochrome Icons and Monochrome Logo |
| D-7 | Non-English translations | **Separate final PR E** once everything else is done. PRs A–D touch only `en.json` |
| D-8 | Modified chip | **Differs from defaults + enabled Baselines** (stays lit after saving) |
| D-9 | Home scope | **Full dashboard plus a smaller Discord banner**; FAQ and other links behind `?` |
| D-10 | Feature header | **Label the boot toggle "Load at boot"**; Pin stays direct; Export / Use Baselines / Restore go into a `⋯` menu |
| D-11 | Settings | **One Settings page, one sub-nav level** (General / Appearance / Advanced groups) |

---

## 12. Work tracker

Tick items as they land on `feat/menu-ux`. Put the commit SHA after each item.

**Setup**
- [x] Worktree + `feat/menu-ux` off `upstream/dev`; build dir; dev-cycle pointed at it (`Github-Fork/mux`)
- [ ] Baseline screenshots of every current page (en + de) for before/after

**PR A: fixes** (merged into `feat/menu-ux`; in-game check pending)

Rule added 2026-10-25: a PR that removes en.json keys also prunes them from the other locales (`sort-i18n.py --write --prune-extra`) so CI's sort check passes; translations still wait for PR E.

- [x] A1 localized feature search `96244a9a7`
- [x] A2 canonical page IDs `73bbd7862`
- [x] A3 remove dead "Display" entry `73bbd7862`
- [x] A4 Feature Issues placement `73bbd7862`
- [x] A5 Restore confirmation `b92136a38`
- [x] A6 short icon tooltips `b92136a38`
- [x] A7 FAQ wording `1fb24f2e2`
- [x] A8 Disable at Boot display names `266128eb5` (+ locale prune `af3418ba5`)
- [ ] Split, build, i18n, open PR A

**PR B: shell**
- [x] B0 decouple `BrowserUI` from `EditorWindow` `9a42f1dbc`
- [x] B0 `Util::Notifications` `79e6055e9`
- [x] B0 `FeatureUndo` extraction `8776579f3`
- [x] B4 `Menu::ConsumeEscape` (needed by B6) `abbb1a13b`
- [ ] B1 header: drop text-button fallback + `ShowActionIcons` UI, glyph fallback, shortcut tooltips
- [ ] B1 `?` popover (shortcuts, FAQ modal, links, About)
- [ ] B2 status bar (status left, GPU/runtime/D3D12 right)
- [ ] B3 shortcut table + dispatch + Help list
- [ ] B5 undo/redo
- [ ] B6 `SearchField` swap
- [ ] Split, build, i18n, open PR B

**PR C: navigation + Home**
- [ ] C1 three-region left pane + collapse
- [ ] C1 main-menu category order
- [ ] C2 `FeatureListPicker` extensions + switch-over
- [ ] C2 row context menu
- [ ] C3 `State::HasUserEdits` cache
- [ ] C3 filter chips + persisted filter
- [ ] C3 cached `BuildMenuList`
- [ ] C4 page headers + empty states
- [ ] C4 feature header: labelled boot toggle + overflow menu
- [ ] C5 `ShaderCacheStatusWidget` extraction
- [ ] C5 Home cards + smaller Discord banner
- [ ] C6 Issues page
- [ ] Split, build, i18n, open PR C

**PR D: settings + go to**
- [ ] D1 Settings page shell + sub-nav + remembered section
- [ ] D1 move sections (Shaders & Cache, Keybindings, Language, Behavior, Appearance ×5, Advanced ×5)
- [ ] D1 theme save wording
- [ ] D1 in-page settings search
- [ ] D2 palette index (pages, features, catalog, commands) + ranking
- [ ] D2 palette UI + Ctrl+K + header search pill
- [ ] D3a reveal spike → choose option
- [ ] D3 reveal implementation (or breadcrumb fallback)
- [ ] D4 (stretch) in-feature filter
- [ ] Split, build, i18n, open PR D

**PR E: locales**
- [ ] Prune removed keys from the 8 locales
- [ ] Carry over translations for moved keys
- [ ] Draft new-key translations
- [ ] Sort, checks, open PR E with a native-review request
