# CLAUDE.md

## Build

Windows-only (Visual Studio + Windows SDK). From WSL, run build commands through PowerShell:

```bash
powershell.exe -Command "./BuildRelease.bat [BUILD_PRESET]"
```

One-click wrappers (configure CMake automatically on first run):

```bash
./BuildRelease.bat   # Shipping: ALL preset, /O2 /GL /LTCG, full PDB, AIO package in dist/
./BuildDev.bat       # Optimized DLL + AIO folder in build/ALL/aio
./BuildDevFast.bat   # Fastest iteration: Ninja, /Od, incremental link, DLL only
./BuildPR.bat        # CI parity: /O2 no LTO, public-symbols PDB, AIO package
./BuildDebug.bat     # Debug-config DLL + AIO folder (ALL-DEBUG preset)
```

Generic form: `./BuildRelease.bat [BUILD_PRESET] [CONFIGURE_PRESET]` (configure preset defaults to the build preset). Presets are in `CMakePresets.json`; local overrides go in `CMakeUserPresets.json` (copy from `CMakeUserPresets.json.template`).

When asked to check that a C++ change compiles, use `./BuildDevFast.bat`.

Formatting: `pre-commit run --all-files` (clang-format, prettier, gersemi), or `cmake --build ./build/ALL --target FORMAT_CODE` for C++/HLSL only.

## Shader validation

```bash
pip install git+https://github.com/alandtse/hlslkit.git   # once
cmake --build ./build/ALL --target prepare_shaders         # required before validating

# Validate only what you changed (file or directory under build/ALL/aio/Shaders/)
hlslkit-compile --shader-dir build/ALL/aio/Shaders/<path> --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml

# GPU register conflicts across features
hlslkit-buffer-scan --features-dir features/
```

CI validates the full shader suite with `--max-warnings 0 --suppress-warnings X1519`.

When refactoring an existing shader (especially decompile-transcription shaders like
`ISTemporalAA.hlsl`), prove the change is behavior-preserving with
`pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl` (bash: `tools/verify-shader-refactor.sh`).
It compares DXBC against the base ref across HDR_OUTPUT permutations; exit 0 = identical, 2 = differs.
If the refactor legitimately reorders ops (bytecode differs, behavior shouldn't), use the runtime A/B
harness instead: capture one frame, swap just that shader, diff against the shipping baseline
(`tools/taa-renderdoc-ab.py`). Details: `docs/development/shader-workflow.md`,
`docs/development/shader-runtime-ab.md`.

## Features

-   A feature is a `Feature` subclass (`src/Feature.h`) in `src/Features/<Name>.cpp/h`, with shaders in `features/<Name>/Shaders/` and a versioned `features/<Name>/Shaders/Features/<Name>.ini`. A `CORE` marker file in `features/<Name>/` bundles it with the main mod.
-   Features reach each other through `globals::features` (`src/Globals.h`).
-   New features: start from `docs/new-feature-template/`; `NewFeatureReadme.md` there lists the required registration edits. GPU-heavy features need a user toggle.
-   Changing a feature's settings structure requires a version bump in its `.ini`.

## SE/AE runtime targeting (CommonLibSSE-NG)

The default `ALL` preset builds one binary for SE and AE. Resolve addresses and offsets per runtime with the SE/AE pair form used throughout `src/`:

```cpp
REL::RelocationID(99938, 106583).address() + REL::Relocate(0x8E, 0x84)
```

For game classes whose layout differs between SE and AE, go through CommonLib's `GetRuntimeData()` accessors instead of direct member access. Branch on `REL::Module::IsAE()` only when the logic itself differs. Reference: [Runtime Targeting](https://github.com/CharmedBaryon/CommonLibSSE-NG/wiki/Runtime-Targeting)

## Feature release stages (Alpha / Beta / Unreleased)

Declared in the feature `.ini` `[Info]` section:

```ini
[Info]
Version = 0-2-0
Beta = True
Unreleased = True
```

-   Truthy values: `true`, `1`, `yes`, `on` (case-insensitive). `Alpha` takes precedence over `Beta`.
-   `Unreleased` is independent of the stage: it only hides the feature until its `.ini` is installed, and is only ever set or cleared by hand.
-   Flag lines must start the line. `Alpha`/`Beta` are parsed by both `CMakeLists.txt` and `tools/feature_version_audit.py`; **keep these two regexes in sync**. `Unreleased` is parsed by CMake only.
-   `GetReleaseStage()` is not cached: resolve it once and pass it around.
-   `IsDisabledByDefault()` is true only for CORE features at a non-Release stage. Do not add a redundant `IsDisabledByDefault` override on a feature that already carries a stage flag.

Versioning (enforced by `tools/feature_version_audit.py`):

-   Pre-release features use `0.x`. Entering pre-release: Beta starts at `0-2-0`, Alpha at `0-1-0`.
-   `alpha -> beta` bumps the minor and resets the patch. Within a stage, normal semver inside `0.x`.
-   A breaking change (`feat!:` / `BREAKING CHANGE:`) on a pre-release feature promotes it to `1-0-0` and strips the Alpha/Beta flag (`--apply-bumps` does both). `Unreleased` is left alone; clear it by hand when the feature ships.
-   Stage transitions are exact-match enforced and may lower the version (e.g. release `1.x` -> beta `0-2-0`).

## Project conventions

-   **D3D11 resource naming**: every D3D11 resource must be named for RenderDoc. After raw `device->Create*` calls use `Util::SetResourceName(ptr, "Feature::ResourceDescription")`. Wrapper types in `src/Buffer.h` (`Texture2D`, `Buffer`, `ConstantBuffer`, ...) take the name in the constructor and name their views automatically (`"Feature::Name SRV"` / `"Feature::Name UAV"`). The single implementation is `Util::SetResourceName` in `src/Utils/D3D.cpp`; never duplicate the GUID or re-implement it inline. Forward-declare in headers and delegate.
-   UI spacing, padding and colors come from `ThemeManager::Constants` (`src/Menu/ThemeManager.h`), not hardcoded values.
-   ImGui: always pair `BeginTable()` / `EndTable()`; use RAII for style changes.
-   UI components extracted from `Menu` (`src/Menu/*Renderer`) access its private methods via callbacks rather than making them public; UI state lives in `Menu` and is passed to components as parameters.
-   Doxygen comments on public methods.
-   Minimize D3D state changes and restore state after modifying it. Respect the split between Skyrim's rendering thread and game logic thread.
-   Validate `.ini` files and user settings: malformed configs can crash Skyrim.
-   Features must disable cleanly on shader compilation failure.
-   No TODO/FIXME placeholders unless explicitly asked for planning.

## Internationalization (i18n)

All user-visible strings go through the translation system (`src/I18n/I18n.h`). Source of truth for English: `package/SKSE/Plugins/CommunityShaders/Translations/en.json`.

```cpp
ImGui::Text("%s", T("menu.faq.q10", "My new FAQ question?"));

#define I18N_KEY_PREFIX "feature.my_feature."
ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &settings.enabled);
#undef I18N_KEY_PREFIX
```

Key naming:

```
menu.<page>.<item>              Menu UI labels
menu.<page>.<item>_tooltip      Tooltip text
feature.<short_name>.<setting>  Feature settings
overlay.<type>                  Overlay messages
common.<term>                   Shared/reused text
ui.<component>                  Utility UI
weather_editor.<item>           Weather editor
```

After adding or changing strings (CI checks all of these):

```bash
python tools/extract-i18n.py --write    # regenerate en.json
python tools/extract-i18n.py --check
python tools/extract-i18n.py --orphans
python tools/sort-i18n.py --check       # if it fails: python tools/sort-i18n.py --write
```

When editing non-English files: translate values only, never keys; keep placeholders (`{count}`), format specifiers (`%s`, `%.1f`) and anything after `##` unchanged.

## Commits and branches

Conventional commits, `type(scope): description`, title ≤ 50 chars, body wrapped at 72. Types: `feat`, `fix`, `refactor`, `docs`, `style`, `test`, `chore`.

These drive semantic-release: `feat:` → minor bump, `fix:` → patch, `feat!:` / `BREAKING CHANGE:` → major; the other types release nothing on their own. Pick the type for its version impact: a refactor mislabeled `feat:` forces a minor bump.

**PRs target `dev`.** `main` is updated only by the release workflows.

Do not, without explicit user direction:

-   Force-push or rebase `main` or `dev`, or force-reset `hotfix/X.Y.x` by hand (the release workflow does that).
-   Create tags matching `v*` (semantic-release owns them).
-   Bump `VERSION` in `CMakeLists.txt` outside the release workflow.
-   PR a feature branch into `main`.
-   Run `Release: Semantic Version` on `hotfix/X.Y.x` for the current line; it fails as out of range. Use `ff_target` into `main` instead.

Release process: [Developers wiki: Patch Release Process](https://github.com/community-shaders/skyrim-community-shaders/wiki/Developers#patch-release-process-any-line).
