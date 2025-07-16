## What's Changed

### 🚀 Features

-   **Flatrim (SE/AE):**
    -   Sky Sync ([#1073](https://github.com/doodlum/skyrim-community-shaders/pull/1073) by @sicsix)
    -   Terrain Variation ([#1064](https://github.com/doodlum/skyrim-community-shaders/pull/1064) by @davo0411)
    -   Interior Sun Shadows ([#1076](https://github.com/doodlum/skyrim-community-shaders/pull/1076) by @sicsix)
    -   Hair Specular ([#1056](https://github.com/doodlum/skyrim-community-shaders/pull/1056), [#1203](https://github.com/doodlum/skyrim-community-shaders/pull/1203) by @jiayev)
    -   Diffuse Image Based Lighting (IBL) ([#1054](https://github.com/doodlum/skyrim-community-shaders/pull/1054) by @jiayev)
    -   Extended Translucency ([#678](https://github.com/doodlum/skyrim-community-shaders/pull/678) by @ArcEarth)
    -   Weather Picker ([#1167](https://github.com/doodlum/skyrim-community-shaders/pull/1167) by @alandtse)
    -   Performance Overlay ([#1070](https://github.com/doodlum/skyrim-community-shaders/pull/1070), [#1123](https://github.com/doodlum/skyrim-community-shaders/pull/1123) by @davo0411, @soda3000)
    -   Frame Generation improvements ([#1125](https://github.com/doodlum/skyrim-community-shaders/pull/1125) by @doodlum)
    -   Vanilla Character Light strength slider ([#1065](https://github.com/doodlum/skyrim-community-shaders/pull/1065) by @jiayev)
    -   Raindrop ripples on water ([#577](https://github.com/doodlum/skyrim-community-shaders/pull/577) by @TheRiverwoodModder)
    -   UI/UX: Feature summary tooltips, enhanced unloaded UI, icon support, subheading organization, improved menu layout ([#1134](https://github.com/doodlum/skyrim-community-shaders/pull/1134), [#1107](https://github.com/doodlum/skyrim-community-shaders/pull/1107), [#1155](https://github.com/doodlum/skyrim-community-shaders/pull/1155), [#1178](https://github.com/doodlum/skyrim-community-shaders/pull/1178), [#1190](https://github.com/doodlum/skyrim-community-shaders/pull/1190))
-   **VR:**
    -   Volumetric Lighting movement fix ([#1057](https://github.com/doodlum/skyrim-community-shaders/pull/1057) by @sicsix)
    -   VR-specific shader validation and compatibility improvements

### 🐛 Fixes

-   **Terrain Variation:**
    -   Fixed parallax shadows and mipmapping ([#1124](https://github.com/doodlum/skyrim-community-shaders/pull/1124) by @davo0411)
    -   Added CPU-side padding ([#1080](https://github.com/doodlum/skyrim-community-shaders/pull/1080) by @Pentalimbed)
-   **Hair Specular:**
    -   Enhanced visuals and default settings ([#1203](https://github.com/doodlum/skyrim-community-shaders/pull/1203) by @jiayev)
-   **Light Limit Fix:**
    -   Fixed shadowed lights being ignored ([#1194](https://github.com/doodlum/skyrim-community-shaders/pull/1194) by @jiayev)
    -   Particle light issue with ISL enabled ([#1059](https://github.com/doodlum/skyrim-community-shaders/pull/1059) by @sicsix)
-   **PBR/TruePBR:**
    -   Reinstate PBR texture checks ([#1041](https://github.com/doodlum/skyrim-community-shaders/pull/1041) by @sicsix)
    -   Support for season swaps ([#1099](https://github.com/doodlum/skyrim-community-shaders/pull/1099) by @hakasapl)
    -   Nullptr check for land in TESObjectLAND_SetupMaterial ([#1197](https://github.com/doodlum/skyrim-community-shaders/pull/1197) by @hakasapl)
-   **Volumetric Lighting:**
    -   VR movement fix ([#1057](https://github.com/doodlum/skyrim-community-shaders/pull/1057) by @sicsix)
    -   Single-pass performance improvement ([#1209](https://github.com/doodlum/skyrim-community-shaders/pull/1209) by @sicsix)
-   **UI/UX:**
    -   Improved menu width/layout, missing/pending feature distinction, hotkey handling, and message for no settings ([#1117](https://github.com/doodlum/skyrim-community-shaders/pull/1117), [#1188](https://github.com/doodlum/skyrim-community-shaders/pull/1188), [#1118](https://github.com/doodlum/skyrim-community-shaders/pull/1118), [#1204](https://github.com/doodlum/skyrim-community-shaders/pull/1204) by @davo0411, @soda3000)
    -   Resolve alt-tab and shift-tab bugs ([#1196](https://github.com/doodlum/skyrim-community-shaders/issues/1196))
-   **General/Obsolete Features:**
    -   Fix detection of deleted obsolete features ([#1157](https://github.com/doodlum/skyrim-community-shaders/issues/1157))
-   **Engine/General:**
    -   Light flags bitcasting ([#1032](https://github.com/doodlum/skyrim-community-shaders/pull/1032) by @sicsix)
    -   Clamp weights to avoid line artifacts ([#1042](https://github.com/doodlum/skyrim-community-shaders/pull/1042) by @ThePagi)
    -   Human profile judging when skin alpha is not fully 1 ([#1053](https://github.com/doodlum/skyrim-community-shaders/pull/1053) by @jiayev)
    -   Renderdoc CTD fix ([#1058](https://github.com/doodlum/skyrim-community-shaders/pull/1058) by @alandtse)
    -   Shadowmap cascade culling engine bug ([#1061](https://github.com/doodlum/skyrim-community-shaders/pull/1061) by @sicsix)
    -   Various null/edge case and crash fixes ([#1177](https://github.com/doodlum/skyrim-community-shaders/pull/1177), [#1201](https://github.com/doodlum/skyrim-community-shaders/pull/1201), [#1205](https://github.com/doodlum/skyrim-community-shaders/pull/1205))

### ⚡️ Performance

-   Volumetric Lighting: single-pass dispatch ([#1209](https://github.com/doodlum/skyrim-community-shaders/pull/1209) by @sicsix)
-   Light Limit Fix: improved scaling with many lights ([#1185](https://github.com/doodlum/skyrim-community-shaders/pull/1185) by @doodlum)
-   Fast random float generation ([#1158](https://github.com/doodlum/skyrim-community-shaders/pull/1158) by @sicsix)

### 🛠 Refactor

-   Hotkey handling, PBR direct lighting, IBL naming, menu layout ([#1118](https://github.com/doodlum/skyrim-community-shaders/pull/1118), [#1213](https://github.com/doodlum/skyrim-community-shaders/pull/1213), [#1084](https://github.com/doodlum/skyrim-community-shaders/pull/1084), [#1178](https://github.com/doodlum/skyrim-community-shaders/pull/1178))

### 🏗 Build/CI

-   Shader compilation PR test and artifact posting ([#1078](https://github.com/doodlum/skyrim-community-shaders/pull/1078))
-   Feature audit checks ([#1193](https://github.com/doodlum/skyrim-community-shaders/pull/1193))
-   Improved caching, submodule handling, build triggers ([#1081](https://github.com/doodlum/skyrim-community-shaders/pull/1081), [#1172](https://github.com/doodlum/skyrim-community-shaders/pull/1172), [#1140](https://github.com/doodlum/skyrim-community-shaders/pull/1140))
-   HLSL validation and build reliability ([#1145](https://github.com/doodlum/skyrim-community-shaders/pull/1145), [#1148](https://github.com/doodlum/skyrim-community-shaders/pull/1148))
-   Pre-commit formatting and automation ([#1106](https://github.com/doodlum/skyrim-community-shaders/pull/1106), [#1116](https://github.com/doodlum/skyrim-community-shaders/pull/1116))

### 📝 Docs

-   Feature descriptions, mod links, tooltips, badges ([#1198](https://github.com/doodlum/skyrim-community-shaders/pull/1198), [#1111](https://github.com/doodlum/skyrim-community-shaders/pull/1111), [#1127](https://github.com/doodlum/skyrim-community-shaders/pull/1127))

### 🙌 New Contributors

-   @hakasapl ([#1030](https://github.com/doodlum/skyrim-community-shaders/pull/1030))
-   @soda3000 ([#1121](https://github.com/doodlum/skyrim-community-shaders/pull/1121))
-   @onymic ([#1177](https://github.com/doodlum/skyrim-community-shaders/pull/1177))
-   @ArcEarth ([#678](https://github.com/doodlum/skyrim-community-shaders/pull/678))

### 📊 Release Stats & Contributors

-   **Number of PRs merged:** ~90
-   **Number of contributors:** 12
-   **Features added:** 12
-   **Bugs fixed:** 18
-   **Time since last release:** 3 months, 5 days (2025-03-24 to 2025-06-29)
-   **Code changes:** 193 files changed, +74,107 lines added, -2,198 lines deleted

#### ⭐ Top Feature Contributor

-   **@sicsix:** For delivering Sky Sync and Interior Sun Shadows - two major new features that enhance the visual experience

#### 🐞 Top Fixes Contributor

-   **@sicsix:** For critical engine and lighting fixes, including Sky Sync, Interior Sun Shadows, and VR/Volumetric Lighting

#### ⚡ Top Performance Contributor

-   **@sicsix:** For Volumetric Lighting single-pass dispatch and fast random float generation that improve real-time performance

#### 🏆 MVP of the Release

**@sicsix**

For leading across all three categories: delivering major new features (Sky Sync, Interior Sun Shadows), critical engine and lighting fixes, and significant performance improvements. Their work has had the broadest impact on both visual quality and technical stability this release cycle.

---

**[Full Changelog](https://github.com/doodlum/skyrim-community-shaders/compare/v1.2.1...v1.3.0-rc0)**
