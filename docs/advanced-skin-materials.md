# Advanced Skin material system

For creation steps, see the artist guide ([English](advanced-skin-artist-guide.md), [中文](advanced-skin-artist-guide.zh-CN.md)). This document describes the material contract and implementation boundaries.

## Why a material belongs to a texture

Skyrim assembles a character's skin from pieces that do not line up with meshes or actors:

| Surface                 | Mesh comes from                                                                 | Skin textures come from                                              |
| ----------------------- | ------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| Naked body, hands, feet | The race's (or NPC's) skin armor and its armor addon                            | The addon's skin texture set, otherwise the mesh                     |
| Skin inside an outfit   | The outfit's own mesh, shared by every race the addon supports                  | The wearer's skin textures, swapped in by the engine                 |
| NPC head                | That NPC's pre-built FaceGen mesh                                               | The race or head part's face texture set                             |
| Player head             | Built at runtime from the race's head parts; beast and human heads differ in UV | The race's face texture set and the character-creation choices       |
| Per-actor replacements  | Unchanged                                                                       | Plugins such as RaceMenu's NiOverride can replace a shape's textures |

A mesh is therefore shared by many characters and races, and one character wears many meshes. An actor is not stable either: the player can change race, and with it the head mesh and its UV layout. The one thing every path agrees on is **the diffuse texture that ends up bound to the shape**. It is also what skin authors make and ship, and what painted inputs such as a roughness map or a detail mask are aligned to.

So the diffuse texture is the material's identity:

- Bare arms in a cuirass show the wearer's body texture, so they get the same material as the naked body.
- A Khajiit and a Nord in the same armor mesh show different textures, so each gets its own material.
- When the player changes race, the head shows a different texture and switches to that texture's material. Data painted for another UV layout cannot be applied by mistake.

## Layers

Each layer is sparse and overrides only the values it sets.

1. **Common settings** — the feature's settings panel. Applies to all compatible skin.
2. **Texture material** — a file beside the diffuse texture. Shared by everything that shows the texture.
3. **Character, all skin** — a character file's numeric values for every skin texture the character shows.
4. **Character, one texture** — a character file's values and textures while the character shows one particular texture.

A compatible surface uses the native FaceGen (4) or FaceGenRGBTint (5) lighting shader without environment mapping, multilayer parallax, decals or menu rendering. No mesh, record or plugin change is needed to make a surface eligible.

## Material key and file

The **material key** is the diffuse texture bound to the shape when it is drawn, normalized to a lower-case `textures/...dds` path with forward slashes. It is read from the bound texture's name, and from slot 0 of the shape's texture set when the bound texture has no usable name. A surface whose diffuse cannot be identified as a DDS file keeps the common settings.

The material file has the diffuse texture's name with `.csskin.json` in place of `.dds`:

```
textures/actors/character/female/femalebody_1.dds
textures/actors/character/female/femalebody_1.csskin.json
```

It is read through the game's resource system, so it works as a loose file or inside a BSA and follows the same mod priority rules as the texture. Each key is read once and cached, including the fact that no file exists; **Reload skin resources** clears the cache.

```json
{
    "format": "CommunityShaders.SkinMaterial",
    "version": 1,
    "parameters": {
        "Roughness": 0.62,
        "Fuzz": 0.8,
        "DetailEnabled": true
    },
    "textures": {
        "controls": "textures/myskin/femalebody_rfaos.dds",
        "detail": "textures/myskin/pores_n.dds"
    },
    "authoredFor": { "size": 22369748, "hash": "9c1f0b6a2d3e4f50" }
}
```

- `parameters` and `textures` are optional and sparse. Omitted values inherit the common settings.
- Two-state parameters are `true` or `false`.
- Texture inputs are `controls`, `wet` and `detail`, given as `textures/...dds` resource paths.
- `//` and `/* */` comments are accepted. Unknown top-level members are ignored.
- `authoredFor` is optional. It records the size and content hash of the diffuse the material was made for. The Skin Editor compares it when the material is selected and shows a warning on a mismatch; the material still applies. It is not checked during play, because that would require reading every diffuse in full.

A file is applied as a whole or not at all. A syntax error, an unknown parameter or texture name, a value outside its range, or an invalid texture path makes the file invalid: it is ignored, the common settings apply, and the reason is written to the log and shown in the editor.

### Parameters

| Name                              | Factory value | Range     |
| --------------------------------- | ------------: | --------- |
| Roughness                         |           0.7 | 0.0–1.0   |
| SecondaryRoughness                |          0.35 | 0.0–1.0   |
| SpecularTextureMultiplier         |           1.0 | 0.0–10.0  |
| SecondarySpecularStrength         |          0.15 | 0.0–1.0   |
| Reflectance                       |        0.0278 | 0.0–0.1   |
| BaseColorMultiplier               |           1.0 | 0.0–2.0   |
| PhysicalMainRoughnessMultiplier   |           1.3 | 0.0–2.0   |
| PhysicalSecondRoughnessMultiplier |          0.75 | 0.0–2.0   |
| PhysicalSpecularStrength          |           1.0 | 0.0–2.0   |
| ExtraEdgeRoughness                |          0.25 | 0.0–1.0   |
| Fuzz                              |           1.0 | 0.0–2.0   |
| FuzzRoughness                     |          0.35 | 0.1–1.0   |
| FuzzF0                            |         0.045 | 0.0–0.5   |
| TransmissionEnabled               |          true | bool      |
| Transmission                      |           0.1 | 0.0–1.0   |
| TransmissionDepth                 |           0.2 | 0.0–1.0   |
| SSSAmount                         |           1.0 | 0.0–1.0   |
| DetailEnabled                     |          true | bool      |
| DetailStrength                    |          0.25 | -2.0–2.0  |
| DetailTiling                      |          10.0 | 0.1–100.0 |
| BodyTilingMultiplier              |           2.0 | 0.1–5.0   |
| WetResponse                       |           1.0 | 0.0–1.0   |

Factory values are the defaults of the common settings, not values baked into a material.

## Texture contract

All extension channels are sampled as linear data, including files tagged sRGB.

| Input      | Purpose              | Channels                                                        |
| ---------- | -------------------- | --------------------------------------------------------------- |
| `controls` | Skin controls, RFAOS | R roughness, G fuzz multiplier, B ambient occlusion, A specular |
| `wet`      | Wet surface          | RGB tangent normal, A wet response mask                         |
| `detail`   | Detail               | RGB tangent normal, A main-UV region strength                   |

The native textures keep their native meaning and are never replaced by this feature. The native skin texture (mesh slot 2) drives transmission thickness: `thickness = 1 - saturate(R)`, so white is thin.

Without RFAOS, the two lobes use their independent roughness values and the original specular-map multiplier adjusts the first lobe. With RFAOS, R is multiplied separately by the two physical roughness multipliers. Extra Edge Roughness applies to both, and the result is clamped to 0.02–1. Secondary Specular Strength controls the lobe mix. Without RFAOS, F0 uses Reflectance; with RFAOS it uses `0.08 * A * PhysicalSpecularStrength`. Missing wet input uses a flat normal and a white mask. AO affects indirect lighting.

Extension textures use an independent 2D DDS loader. RGBA8 and BC1/2/3/7 are supported, with BC7 or uncompressed RGBA8 preferred for masks. BC5 UNORM is supported only for detail: positive Z is reconstructed and the mask is 1. Signed BC5, arrays, volumes and cubemaps are rejected. A broken explicit texture disables that input rather than substituting another one.

Detail RGB samples the transformed main UV multiplied by detail tiling, with a wrap sampler. Alpha samples the transformed main UV separately, so mask regions do not move when tiling changes. Tangent-space detail is composed using RNM; model-space base normals use a surface basis derived from world position and main UV. An empty detail input uses the global detail texture. Body tiling applies outside the head.

Post-process SSS continues to receive the scalar `baseColor.a * SSSAmount` through the existing render target and keeps the existing SSS feature and profile. Transmission is a separate direct-light term.

## Character files

Per-character adjustments are ordinary files, one per character, in:

```
Data/SKSE/Plugins/CommunityShaders/AdvancedSkin/Characters/*.json
```

```json
{
    "format": "CommunityShaders.SkinCharacter",
    "version": 1,
    "target": "0xA2C8E~Skyrim.esm",
    "name": "Lydia",
    "all": {
        "parameters": { "Fuzz": 0.6 }
    },
    "materials": {
        "textures/actors/character/female/femalehead.dds": {
            "parameters": { "Roughness": 0.55 },
            "textures": { "detail": "textures/myfollower/lydia_face_detail.dds" }
        }
    }
}
```

- `target` is `"player"` or an NPC record written as `<form ID>~<plugin>`. The form ID is the record's own ID within its plugin; a load-order prefix and `0x` are accepted and ignored, so the file does not depend on load order. The plugin name is matched without regard to case.
- `name` is informational. The file name does not matter either; the editor writes `Player.json` or `<name> <form ID> <plugin>.json`.
- `all` holds numeric values for every skin texture of the character. Textures are rejected there: no texture is valid for every UV layout.
- `materials` is keyed by material key. Each entry may set values and replace the three extension textures; an empty texture path means "explicitly none" (neutral input, or the global detail texture).
- Comments are accepted. A file is used as a whole or not at all, with the same checks as a material file, plus: the target's plugin must be installed. Ignored files and the reasons are logged and listed in the editor.
- Two files targeting the same character: the one whose file name sorts last wins, and the other is reported.

The files apply in every save, and are read again by **Reload skin resources**, so they can be edited outside the game, shared, or shipped by a follower mod. The player file applies to whichever character you are playing; keep different looks as copies and swap them in, or use **Load from another character file...** in the editor.

An NPC file applies to every actor that uses the record. Actors generated from a leveled list use the record their appearance comes from. An actor created at runtime from a record that is itself generated has no stable identity and cannot be customized.

When the character no longer shows a texture listed under `materials` — for example after a race change — that entry is inactive, and applies again when the texture returns. It is never applied to a different texture.

Limits: 1 MiB per file, 256 texture entries per character.

## Editor

**CS Editor → Skin Editor** has three workspaces.

The page follows the Scene Manager's layout:

- **Character bar**: **Player** and **Console selection** chips, the character's name and its record. The page header holds the texture tools and **Reload skin resources**.
- **Texture column**: every skin texture the character shows, grouped by head, body, hands, feet and other, with where each appears, including outfits and first person. A dot gives the material file's state; markers flag this character's adjustments and unsaved edits. Textures with stored character adjustments that are not shown now are listed separately. The column filters and resizes.
- **Texture panel**: the texture's name, path, material file badge and warnings; a breadcrumb **Common › Texture material › Whole character › Character on this texture** whose highlighted chip is the layer being edited, with a toolbar (undo, redo, save, revert, copy and share, clear layer, wetness preview); the destination of a save; and the values by category.
- **Rows**: each value leads with a gutter box, ticked when the edited layer sets the value. It is green when that value is the one shown, and amber when a higher layer of the selected character sets it too. Editing a value sets it on the layer; untick, or right-click and choose **Inherit again**, to inherit. The tooltip names the layer the value comes from.
- **Texture tools**: a separate window for channel packing, whose result can be assigned to the layer being edited.

Saving the texture material writes its file. Saving either character layer writes the character's file; with no adjustments left, it keeps an empty override so older files for the same character do not become active again. Errors and confirmations appear as editor notifications.

While the editor is open, a texture material draft previews on everything that shows the texture, on any character, and a character draft previews on that character. That is exactly what saving will affect. Both drafts are retained while switching layers; choosing another texture or character asks before discarding unsaved edits. Undo and redo cover both drafts.

### Paths in a mod manager

Under Mod Organizer and similar tools the game sees a virtual Data folder assembled from many mod folders. A path on disk such as `D:\MO2\mods\My Skin\textures\body_rfaos.dds` therefore means nothing to the game, and would break as soon as the file moved into a BSA or another mod. So the editor separates two things:

- **References to resources** — every texture in a material or character file, the global detail texture, the material whose values are copied — are game resource paths (`textures/...`). They are chosen in an in-game browser that lists the Data folder as the game sees it, including all virtual files. Archived files are not listed, but their resource path can be typed in.
- **Where new files are written** — material files and textures built by the texture tools — is the one place a disk location is needed. It is a single setting, the output mod folder, chosen once with a system folder dialog (`Data` by default, which Mod Organizer redirects to Overwrite). A resource path is then appended to it. If the chosen folder is a `textures` folder or below, the mod folder above it is used.

The system file dialog is used only for files that are not game resources: source images for the texture tools, and copies of character files written for sharing.

A file created inside a mod folder while the game runs may not be visible through the mod manager's virtual Data folder until the next launch. The editor remembers every file it writes this session and reads it from where it was written when the game cannot find it, so previews keep working. **Reload skin resources** re-reads through the game's resource system and therefore shows which file actually wins.

## Code organization

- `Material/` — parameters, layering, the material file format, and the per-texture file lookup.
- `Runtime/` — surface identification, extension textures, rendering and wetness.
- `Persistence/` — character files.
- `Editor/` — file publication, texture tools and UI.

The feature installs two `BSLightingShader` virtual hooks and no detours. SE, AE and VR share one code path.

Skin UI and errors use the `feature.skin.*` translation namespace with inline English defaults.

## Validation boundary

This implementation builds, but has not been run in the game. Before release, verify at least:

1. Naked body versus a cuirass with bare arms: the arms keep the body's material.
2. A human and a beast race in the same armor mesh: each uses its own texture's material.
3. Changing the player's race in RaceMenu: the head switches material, entries for the old texture go inactive, and nothing is drawn with another layout's masks.
4. Character files: player and NPC targets, a leveled-list actor, a plugin that is not installed, two files for one character, and editing a file outside the game followed by **Reload skin resources**.
5. A texture replaced per actor by another plugin: the key follows the replacement.
6. First-person hands and body.
7. NPC FaceGen heads and the player's runtime-built head both resolve to the expected diffuse path.
8. A material file inside a BSA, and mod-manager priority between two mods shipping the same file.
9. Invalid files, missing extension textures, and **Reload skin resources**.
10. VR.

Items 5, 7 and 8 rest on assumptions about engine behaviour that could not be checked statically: that the bound diffuse texture's name is its resource path, and that the resource system serves a `.json` file under `textures` from an archive.
