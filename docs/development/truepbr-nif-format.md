# TruePBR NIF Format

This document specifies the binary representation of a standard TruePBR
`BSLightingShaderProperty` (shader feature `21`). It is intended for NIF tooling
authors and for anyone reviewing or generating TruePBR materials.

## Versioning

Two version numbers apply to TruePBR, but they have different purposes:

- The **feature version** is `2-0-0` in `TruePBR.ini`. It identifies the feature
  release and does not describe the NIF binary layout.
- The **PBR payload version** is `2`. It is stored in each PBR property block and
  identifies the layout described here.

The loader accepts only PBR payload version `2`. Earlier payload versions are
rejected rather than migrated implicitly.

## Conventions

- Integer fields are unsigned 32-bit values unless stated otherwise.
- Floating-point fields are IEEE 754 32-bit values.
- All fields use little-endian byte order.
- The PBR payload contains no structure padding.
- A reference value of `0xFFFFFFFF` means no reference.
- `N` is the number of extra-data references, and `S = 4 * N` is the size of the
  extra-data reference table in bytes.

## Property Block Layout

The following offsets are relative to the start of the
`BSLightingShaderProperty` block.

|    Offset | Type        | Description                                           |
| --------: | ----------- | ----------------------------------------------------- |
|       `0` | `uint32`    | Shader feature; must be `21`.                         |
|       `4` | `uint32`    | Name-string index, or `0xFFFFFFFF`.                   |
|       `8` | `uint32`    | Extra-data reference count `N`.                       |
|      `12` | `uint32[N]` | Extra-data references.                                |
|  `12 + S` | `uint32`    | Controller reference, or `0xFFFFFFFF`.                |
|  `16 + S` | `uint32`    | Low 32 bits of the shader-property flags.             |
|  `20 + S` | `uint32`    | High 32 bits of the shader-property flags.            |
|  `24 + S` | `float`     | UV offset X.                                          |
|  `28 + S` | `float`     | UV offset Y.                                          |
|  `32 + S` | `float`     | UV scale X.                                           |
|  `36 + S` | `float`     | UV scale Y.                                           |
|  `40 + S` | `uint32`    | Required `BSShaderTextureSet` reference.              |
|  `44 + S` | `float`     | Emissive color, red.                                  |
|  `48 + S` | `float`     | Emissive color, green.                                |
|  `52 + S` | `float`     | Emissive color, blue.                                 |
|  `56 + S` | `float`     | Emissive multiplier; must be finite and non-negative. |
|  `60 + S` | `uint32`    | Texture clamp mode; must be in `[0, 3]`.              |
|  `64 + S` | `float`     | Material alpha; must be finite and in `[0, 128]`.     |
|  `68 + S` | `float`     | Refraction power; must be finite and in `[0, 1]`.     |
|  `72 + S` | `uint32`    | PBR payload version; must be `2`.                     |
|  `76 + S` | `uint32`    | PBR feature flags.                                    |
|  `80 + S` | `float[25]` | PBR parameters in the order defined below.            |
| `180 + S` | —           | End of the block.                                     |

The block size must therefore be exactly:

```text
180 + 4 * N bytes
```

With no extra-data references, the PBR payload starts at offset `72`, and the
complete block is `180` bytes long.

## PBR Payload

The PBR payload is `108` bytes long:

```text
4-byte version + 4-byte feature flags + 25 * 4-byte parameters
```

### Feature Flags

| Bit |   Value | Meaning             |
| --: | ------: | ------------------- |
|   0 | `0x001` | Subsurface          |
|   1 | `0x002` | Two-layer coat      |
|   2 | `0x004` | Colored coat        |
|   3 | `0x008` | Interlayer parallax |
|   4 | `0x010` | Coat normal         |
|   5 | `0x020` | Fuzz                |
|   6 | `0x040` | Marschner hair      |
|   7 | `0x080` | Glint               |
|   8 | `0x100` | Foliage             |
|   9 | `0x200` | Thin surface        |

Flag combinations must satisfy these rules:

- Bits above bit 9 are invalid.
- Marschner hair must be the only enabled PBR feature.
- Colored coat, interlayer parallax, and coat normal require the two-layer coat
  feature.
- Two-layer coat cannot be combined with subsurface, fuzz, glint, foliage, or
  thin surface.
- Fuzz and glint cannot be combined.
- Foliage implies thin-surface shading even when the thin-surface bit is not
  set explicitly.

These flags belong to the PBR payload. They are independent of the
shader-property flags used by older TruePBR material encodings.

### Parameters

All 25 values must be present, finite, and non-negative, including values for
features that are disabled. Colors are linear RGB multipliers. The defaults
below are the neutral runtime defaults; they are not optional fields that can be
omitted from the payload.

| Index | Parameter                          | Default | Additional constraint |
| ----: | ---------------------------------- | ------: | --------------------- |
|     0 | Roughness scale                    |   `1.0` | —                     |
|     1 | Specular level                     |  `0.04` | At most `1.0`.        |
|     2 | Displacement scale                 |   `1.0` | —                     |
|     3 | Subsurface color, red              |   `1.0` | —                     |
|     4 | Subsurface color, green            |   `1.0` | —                     |
|     5 | Subsurface color, blue             |   `1.0` | —                     |
|     6 | Subsurface opacity                 |   `0.0` | At most `1.0`.        |
|     7 | Coat color, red                    |   `1.0` | —                     |
|     8 | Coat color, green                  |   `1.0` | —                     |
|     9 | Coat color, blue                   |   `1.0` | —                     |
|    10 | Coat strength                      |   `1.0` | At most `1.0`.        |
|    11 | Coat roughness                     |   `1.0` | At most `1.0`.        |
|    12 | Coat specular level                |  `0.04` | At most `1.0`.        |
|    13 | Fuzz color, red                    |   `1.0` | —                     |
|    14 | Fuzz color, green                  |   `1.0` | —                     |
|    15 | Fuzz color, blue                   |   `1.0` | —                     |
|    16 | Fuzz weight                        |   `0.0` | At most `1.0`.        |
|    17 | Glint screen-space scale           |   `1.5` | At least `1.0`.       |
|    18 | Glint log microfacet density       |  `40.0` | In `[1, 40]`.         |
|    19 | Glint microfacet roughness         | `0.015` | In `[0.005, 0.3]`.    |
|    20 | Glint density randomization        |   `2.0` | At most `5.0`.        |
|    21 | Subsurface balance                 |  `0.75` | At most `1.0`.        |
|    22 | Thin-surface front specular weight |   `0.5` | At most `1.0`.        |
|    23 | Thin-surface back specular weight  |   `0.5` | At most `1.0`.        |
|    24 | Thin-surface back roughness scale  |   `1.0` | At most `3.0`.        |

The four thin-surface values are stored at the end of the parameter array. The
array is serialized explicitly in this order; it is not a dump of the C++
structure's in-memory layout.

## Texture Set Layout

The referenced `BSShaderTextureSet` uses the standard nine-slot layout:

| Slot | Role               | Requirement                       |
| ---: | ------------------ | --------------------------------- |
|    0 | Base color         | Non-empty path required.          |
|    1 | Normal             | Non-empty path required.          |
|    2 | Emissive           | Optional.                         |
|    3 | Displacement       | Required for interlayer parallax. |
|    4 | Reserved           | Must be empty.                    |
|    5 | RMAOS              | Non-empty path required.          |
|    6 | Features texture 1 | Required for coat normal.         |
|    7 | Features texture 0 | Optional.                         |
|    8 | Reserved           | Must be empty.                    |

RMAOS stores roughness, metallic, ambient occlusion, and non-metal reflectance
in its four channels. Features texture 0 contains subsurface or coat data;
features texture 1 contains fuzz or coat-normal data, depending on the enabled
PBR features.

## Extra Data and Runtime Overrides

Values are applied in this order:

1. Read the NIF payload.
2. Apply a matching TXST configuration, including its PBR feature switches.
3. Apply the optional `PBRThinSurface` and `PBRFuzz` extra-data blocks.

`PBRThinSurface` must be a `NiFloatsExtraData` block containing exactly four
finite values:

| Index | Override                           |
| ----: | ---------------------------------- |
|     0 | Subsurface balance                 |
|     1 | Thin-surface front specular weight |
|     2 | Thin-surface back specular weight  |
|     3 | Thin-surface back roughness scale  |

These values override the corresponding payload and TXST values for that NIF.
They do not change the binary payload layout.

`PBRFuzz` is a `NiFloatsExtraData` block with two finite values: fuzz
roughness (negative uses base roughness; non-negative values are clamped to
`[0, 1]`) and a displacement-green roughness switch (`0` or `1`). These
override TXST `fuzzRoughness` and `fuzzRoughnessTexture` and survive TXST
reloads. The texture is the green channel of displacement slot **3**, not
reserved slot 4. Without either override, fuzz uses base roughness. The v2
payload remains 25 floats; it does not include these extra-data values.

Projected-material MATO data is applied at runtime and is not part of the base
PBR payload.

## Validation and Authoring Checklist

Before writing a block, verify that:

1. The NIF container uses version `0x14020007`, user-defined version `12`, and
   header version `100`.
2. The shader feature is `21`, and the payload version is `2`.
3. The block size is exactly `180 + 4 * N` bytes.
4. The texture-set reference is valid, required texture paths are present, and
   reserved slots 4 and 8 are empty.
5. All common floating-point fields are finite, and all 25 PBR parameters are
   finite and within their documented ranges.
6. The PBR feature flags form a supported combination.
7. The shader-property flags do not request an incompatible material layout.

The incompatible shader-property flags are environment map, face, parallax,
multi-texture landscape, eye reflection, hair tint, face-gen RGB tint,
parallax occlusion, LOD landscape, glow map, multi-index snow, multi-layer
parallax, and cloud LOD.

Invalid blocks are logged and replaced with a safe fallback block for parsing.
The resulting material is marked invalid so that PBR rendering remains blocked.
