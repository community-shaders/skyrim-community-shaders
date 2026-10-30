# Unified Presets

Drop packs under this folder as `Presets/<PackId>/`.

Metadata (`<PackId>.json` or `preset.json`) is **optional**. A classic ENB folder dropped here works as-is.

## Supported layouts

### Classic ENB drop (no metadata)

Same shape as Nexus ENB presets (e.g. NAT.ENB):

```
Presets/NAT.ENB - .../
  enbseries.ini
  enbseries/enbeffect.fx
  enblocal.ini          (optional)
  ReadMe.txt            (optional, used as description)
  Data/                 (optional game assets)
  01_LOW .../           (optional quality variants)
```

### Nested Effects11 (with or without metadata)

```
Presets/MyPack/
  MyPack.json           (optional metadata)
  effects11/            (enbseries.ini + enbseries/)
  logo.png / cover.png / gallery/
```

### CS Presets pack

Scene Manager **Export preset** writes a pack in this layout; it can sit beside `effects11/` in the same pack:

```
Presets/MyPack/
  MyPack.json                        (metadata, merged on re-export)
  InteriorOnly/MyPack_<Feature>.json
  TimeOfDay/<Period>/MyPack_<Feature>.json
  Weather/<spid>/[<Period>/]MyPack_<Feature>.json
  Locations/<formKey>/[<Period>/]MyPack_<Feature>.json
  logo.png / cover.png / gallery/
```

Only the **active** pack's scene files load. Applying another pack swaps the Scene Manager layer to it; a pack without scene files clears it. Your own Scene Manager edits stay in `SceneSettings/SceneManager.json` and always apply on top.

Export writes `version` (`MAJOR.MINOR.PATCH`), `type` (`CS` or `E11`, the pipeline the preset targets) and the transition controls `periodTransitionHours`, `locationTransitionSeconds`, `weatherTransitionScale` and `transitionEase` into the metadata alongside the display fields below. Each applies while the pack is active, unless the user has set their own on the Scene Manager page. Choosing **E11 Preset** also saves the active Effects 11 layout into `effects11/` (`enbseries.ini` + `enbseries/`) and records `effects11.path` in the manifest.

### Baseline pack (feature defaults)

A `Baseline/` folder holds feature overwrites: the same setting trees a feature saves to `SettingsUser.json`, one file per feature, named `<FeatureShortName>.json` (or `<Anything>_<FeatureShortName>.json`). Use it to ship better defaults with a modpack, for example LOD Blending values that depend on the textures. It can sit beside `effects11/` and scene files in the same pack, or stand alone with `"type": "Baseline"`.

```
Presets/MyModpack/
  MyModpack.json                     (metadata, "type": "Baseline")
  Baseline/LODBlending.json          ({ "_metadata": { "description": "..." }, "<setting>": value, ... })
  logo.png / cover.png / gallery/
```

- Baseline sits **beneath** the Scene Manager and is independent of the active pack: applying a Baseline-only pack never replaces the active Effects 11 / CS pack, and several Baseline packs can be enabled at once (the one applied last wins conflicts).
- It is not limited by the Scene Manager black/whitelists. Any setting a feature saves to `SettingsUser.json` can be set. Only per-feature files are read, so Menu, Advanced (compiler threads, log level, ...) and other global keys cannot be shipped this way; files that name an unknown feature, an unknown setting key or a value of the wrong type are skipped and reported under Feature Issues.
- Enabled Baseline packs are remembered in the `Presets` section of `SettingsUser.json` and re-applied every launch, so the values win over saved settings until the user changes a setting themselves (their change is kept as a delta; a change back to the pack's own value is not, so removing the pack later takes that setting with it). **Remove Baseline** stops applying the pack right away: the settings it set fall back to the layers beneath, and the user's own changes stay.
- Per-file `_metadata` holds `version` and `description`; `"enabled": false` keeps the file from loading.
- Files left in `CommunityShaders/Overrides/` by older versions are no longer read; move them into a pack's `Baseline/` folder.
- Author one from **Export Overwrite** (feature header or Scene Manager): tick _Save as Baseline preset pack_ and the selected settings are written to `Presets/<Mod Name>/Baseline/<Feature>.json` with a starter manifest.

## Discovery

Packs are found under both the Data (VFS) path and the Community Shaders mod's own folder, so packs from other MO2 mods and packs created mid-session both show up. Folders starting with `_` or `.` are ignored.

## Metadata (optional, Presets packs)

When present, `<PackId>.json` can set name, author, version, description, tags, artwork paths (`logo` / `cover` / `screenshots`, relative to the pack folder), and backend paths (`effects11.path`, `backends`). `type` (`CS`, `E11` or `Baseline`) sets which group the pack is listed under; without it the group is inferred from the payloads found. Applying always loads every payload present. Without metadata the UI uses the folder name, ReadMe text, and inferred E11/CS Presets content and artwork.

`sceneControl` (feature short name to `true`) hands features Effects 11 replaces back to Community Shaders. It is the pack's default: the user's own choice for the pack is kept in `SettingsUser.json`, and the manifest is never rewritten for it.
