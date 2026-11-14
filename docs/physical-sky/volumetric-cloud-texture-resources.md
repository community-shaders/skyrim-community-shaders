# Volumetric Cloud Texture Resources

See [cloud sampling and motion](cloud-sampling.md) for the view march,
empty-space acceleration, motion units and history metadata. See
[cloud lighting and reconstruction](cloud-lighting.md) for local solar occlusion, profile scattering
and render-target formats.

## Low-cloud model

Physical Sky uses a vertical-profile representation inspired by publicly
described Nubis techniques. Its profile-noise reconstruction is documented
in [noise reconstruction](noise-contract.md). Low-cloud density
combines a control field with lookup textures and shape noise:

1. a two-texture NDF supplies minimum height, maximum height, coverage, top type,
   bottom type, and an internal bottom-shaping start fraction;
2. a locally generated volume or `NubisCloudShapeNoise.dds` supplies the tileable three-dimensional density-noise composite.

The noise volume does not generate the NDF, and the NDF is not a second erosion
pass. The profile determines where cloud mass may exist; the composite describes
the internal and boundary variation of that mass.

## Runtime bindings

| Resource                         |                   Binding | Type                              |
| -------------------------------- | ------------------------: | --------------------------------- |
| Cloud parameters                 |                      `b1` | 480-byte constant buffer          |
| Cloud shape noise                |                      `t4` | linear RGBA 3D                    |
| Global NDF height / modeling     |               `t5` / `t6` | linear 2D RG / RGB                |
| AP shadow / sky view             |               `t7` / `t8` | renderer textures                 |
| Cirrus weather / patterns        |              `t9` / `t10` | linear 2D RG / RGB                |
| Vertical profile / adjustment    |             `t11` / `t12` | linear 2D RG / RGB                |
| Ambient SH / shadow volume       |             `t13` / `t14` | generated                         |
| Boundary / global height bounds  |             `t15` / `t16` | generated                         |
| Main history / traces            | `t17`–`t19` / `t20`–`t22` | transmittance, radiance, metadata |
| Cube history / traces            | `t23`–`t25` / `t26`–`t28` | transmittance, radiance, metadata |
| Composed local height / modeling |             `t32` / `t33` | premultiplied RGBA16_FLOAT arrays |
| Local modeling maximum floor     |                     `t34` | RGB floor, RGBA16_FLOAT array     |

| Local endpoint weights / mode | `t35` | StructuredBuffer float4 |

Main reconstruction output is read at t29–t31 by the foreground refinement pass.
See [local NDF assets](local-ndf.md) for finite world-space placement, alpha masks,
endpoint composition, persistence and Scene Manager transitions. See
[cloud sampling](cloud-sampling.md) for temporal scheduling.

The three optional packed assets are loaded from `Data/Textures/PhysicalSky/` and live in
`features/Physical Sky/Textures/PhysicalSky/` in the source tree.

## Two-texture NDF

Global generated and imported NDFs use two linear 2D textures, sampled with wrap
filtering at mip 0. Generated maps are 512 x 512: height and modeling use
RGBA16_FLOAT with unused A zero. The external height input remains RG; runtime
conversion adds the shaping-start value in B.

| Texture  | R                         | G                         | B                      |
| -------- | ------------------------- | ------------------------- | ---------------------- |
| Height   | minimum normalized height | maximum normalized height | internal shaping start |
| Modeling | coverage                  | top profile type          | bottom profile type    |

At a ray sample the shader computes:

The local height selects the vertical LUT row. Modeling coverage receives a
height-dependent power, while top and bottom types select their profile
columns. The adjustment LUT can expand the top profile. Both generated and
imported NDFs follow the same rules; see [noise reconstruction](noise-contract.md).

`NDF Base Altitude` and `NDF Height Span` map normalized NDF height 0–1 into
physical altitude. `NDF Scale` independently controls the X/Y repeat length.

The generator remaps and powers two coverage signals, combines them with max
and gain, and separately remaps shared noise into profile types. Optional local
maps blend before bottom height variation. Four scalar noise inputs use local
Alligator, Perlin fBm or Perlin-Worley, individually replaceable by DDS.
See [procedural NDF generation](ndf-generator.md) for equations and noise presets.

Texture mode selects `heightPath` and `modelingPath` with the same channel
contract. Arrays, integer and sRGB views are rejected. Optional authored maps
enter the same density query as generated maps.

## Shape and reserved noise volumes

`NubisCloudShapeNoise.dds` is a 128³ linear RGBA8 volume with eight mips,
sampled at t4. Its R/A billow and B/G/A wisp signals are combined with NDF
profile, top type and height. The result is eroded, then receives a separate
base-density response. Noise coordinates, channel formulas, profile-dependent
mip selection, and parameter defaults are specified in the
[noise contract](noise-contract.md).

`NubisOrographicDetailNoise.dds` (32³, six mips) is reserved for nearby erosion
in a future orographic layer. `NoiseVoxel.dds` (128³, eight mips) retains
the voxel detail volume unchanged. It lives in `Textures/PhysicalSky/` and is
loaded once for all voxel clouds, independently of instance modeling and distance
fields. The shared voxel noise remains in Physical Sky; the reference instances
are supplied by the local `PSkyTest` mod outside the project and package. Neither detail volume is loaded by the regular NDF path; replacing them
does not change regular cloud rendering. See [voxel clouds](voxel-clouds.md).

## Profile LUTs

`NubisVerticalProfile.dds` is 64² BC5: R stores bottom profile and G top profile.
`NubisVerticalAdjustment.dds` is 64² BC7: R stores top expansion, GB horizontal
noise warp. All lookups use mip 0. Profile UVs are `(type, localHeight)` with
clamping; the adjustment GB lookup wraps in horizontal world coordinates.
The separate R8 top/bottom assets feed runtime packing when packed LUTs are absent.

## Cirrus inputs

Cirrus uses a spherical sheet with a configurable physical altitude (default
8000 m) and the shared cloud curvature radius. `cloudLayer.cirrus.uvSize` is a
global size multiplier, default 1 and limited to 0.1–32; Scene Manager cannot
override it. The coverage/type field repeats every `16384 * uvSize` metres,
independently of the low-cloud NDF scale. Pattern frequency is
`0.0002331 / uvSize` per metre. Larger values enlarge both inputs without
changing their relative scale. Pattern coordinates subtract the high-cloud
wind displacement before scaling. Weather generation divides the shared
weather offset by `uvSize` to preserve its world-space speed; rendering does
not translate the weather field again. Changing the size rebuilds the weather
map and invalidates cloud history.

| Texture                                     | Channels                                 |
| ------------------------------------------- | ---------------------------------------- |
| Generated weather, t9                       | R coverage, G type                       |
| `Data/Textures/PhysicalSky/cirrus.dds`, t10 | R wispy, G round, B streaky              |
| Optional local weather input                | R coverage, G type, B interpolation mask |

The fixed pattern DDS must be a linear 2D RGB-capable texture. It is sampled
without channel repacking, procedural warping or an sRGB conversion, at mip 0
with linear wrapping. Missing or incompatible patterns disable only cirrus;
there is no procedural pattern fallback. The texture loader's Reload action
retries the file after it has been replaced.

Coverage and type each select one of four shared weather noise inputs, apply
frequency and offset, convert the sample to [-1, 1], then remap input/output
intervals. There is no exponent. Optional local weather blends by its B mask
and influence, or takes the component-wise maximum of RG times influence.
The local map has an independent multiplier for the generator's wind offset.
Storm modulation is not implemented.

`cloudLayer.cirrus.weather` is a SceneBlend value containing complete weather
states. Scene Manager blends the resulting RG maps, so changing noise slots,
local DDS paths or blend modes does not switch an input halfway through a
transition. A single active state needs one 512-square generation dispatch;
additional transition endpoints use alternating RG16_FLOAT targets. Unchanged
inputs reuse the result. Only the final map receives mip generation. Altitude,
density remains a numeric scene control. Shared and per-layer brightness live
under `cloudLayer.lighting` and also support numeric scene transitions.

Debug shows the active weather, the fixed pattern DDS, and active local inputs.

## Generator lifecycle

The procedural low NDF and acceleration rebuild on composition, noise or source
changes. Shader reload invalidates generation. Animated wind and world repeat
scale apply during sampling. Map replacement invalidates temporal history.
Imported pairs refresh acceleration before use. Cirrus input changes rebuild
its maps and invalidate cloud history. Disabled cirrus skips generation.

## Static validation checklist

- `NubisCloudShapeNoise.dds` loads as a tileable 3D RGBA texture and is bound at `t4`.
- Low NDF uses linear height RG and modeling RGB textures at t5/t6.
- NDF coverage is generated independently from `NubisCloudShapeNoise.dds`.
- Empty or reversed height intervals are rejected by the density query.
- Noise is queried only inside positive NDF/profile support, with zero density outside it.
- Cirrus weather/pattern inputs are linear 2D RG/RGB resources at t9/t10.

## Implementation references

- low NDF generation and texture selection: `src/Features/PhysicalSky/Ndf.cpp`
- low NDF settings: `src/Features/PhysicalSky/Ndf.h`
- DDS loading and bindings: `src/Features/PhysicalSky/VolumetricClouds.cpp`
- low NDF generator shader: `features/Physical Sky/Shaders/PhysicalSky/NdfGenerate.cs.hlsl`
- cirrus weather generator: `features/Physical Sky/Shaders/PhysicalSky/CirrusGenerate.cs.hlsl`
- density sampling: `features/Physical Sky/Shaders/PhysicalSky/Volumetrics.cs.hlsl`
- noise reconstruction: `features/Physical Sky/Shaders/PhysicalSky/CloudNoise.hlsli`

## Optional resource fallback

Cloud rendering can initialize without bundled cloud DDS files. Resolution order:

| Resource            | Optional input                                       | Fallback                                      |
| ------------------- | ---------------------------------------------------- | --------------------------------------------- |
| Shape noise         | `NubisCloudShapeNoise.dds` when DDS mode is selected | Local RGBA volume generator                   |
| Vertical profile    | `NubisVerticalProfile.dds`                           | Runtime packed bottom/top profile             |
| Vertical adjustment | `NubisVerticalAdjustment.dds`                        | Runtime top expansion plus procedural GB warp |
| Main weather        | Imported Height RG + Modeling RGB pair               | Procedural NDF                                |
| Weather noise slots | Per-slot DDS override                                | Local scalar noise generator                  |
| Cirrus weather      | Optional local RGB map                               | Shared weather noise                          |
| Cirrus patterns     | `cirrus.dds`                                         | Cirrus disabled                               |

When packed LUTs are absent, `bottom_lut.dds` supplies bottom profile R and
`top_lut.dds` supplies top expansion R. These scalar images use the presentation
orientation (height decreases down the image); packing flips their V axis.
An optional G channel in `top_lut.dds` stores the separate ordinary top profile,
with the same orientation as R. This permits all three profile functions in just
two files: Top RG = expansion/ordinary top, Bottom R = bottom. Single-channel Top
uses an analytic smooth envelope for the missing ordinary top profile. If either
scalar file is absent, its profile also uses an analytic smooth envelope.
These envelopes are fallback approximations, not reconstructions of missing LUT
texels. In particular, retaining only the two original scalar images does not preserve the
independent top-profile channel of a packed vertical-profile texture. Supplying
Top G or complete LUT overrides preserves that independently authored profile.
The original scalar Bottom also contains a zero-density band at low normalized
heights that is absent from the bundled packed profile. It can suppress low-type
clouds even when Top G is supplied. These scalar assets are alternative profiles,
not lossless split versions of the packed LUTs; changing weather type ranges
therefore does not produce equivalent shapes across the two sets.

Packing produces 64-square linear RGBA float textures with mip chains; the
adjustment's neutral GB channels are replaced by the noise generator. No new DDS
files are written, and neither scalar source is modified. Thus a release may
retain just `top_lut.dds` and `bottom_lut.dds`, or remove all cloud DDS files.

Noise preparation runs before the final cloud-resource readiness gate. On first
startup without shape noise, the 128-slice volume takes 32 prepasses at four
slices per pass; only complete volumes and mip chains are published. Reload Cloud
Textures resolves the same fallback order again. DDS mode also generates missing
shape/adjustment resources instead of disabling the feature.

This path has been statically reviewed; texture-free startup and visual agreement
still require an in-game run. Analytic fallback quality is distinct from resource
availability.

## Debug resource inspection

`Debug > Cloud Shape` displays the active NDF height/modeling maps, all four shared
weather noise inputs, active shape noise, packed profile/adjustment LUTs, and
active cirrus weather/pattern textures. The generated/source group also exposes
the completed procedural shape/adjustment outputs and imported/fallback inputs,
including when a different source is selected for rendering. Incomplete noise
back buffers are not published; generation progress remains in Cloud Noise Inputs.

Every cloud resource preview supports RGB or R/G/B/A isolation, mip selection,
a display range, and Z-slice selection for volumes. Alpha is displayed as data,
not transparency. Scalar weather inputs start in R mode. Conversion dispatches
only for expanded, visible images, and preserves the compute bindings it uses.
NDF component previews retain their separate selector. All new controls and
messages have English and Simplified Chinese translations (`en`, `zh_CN`).

Shape and adjustment fallback selection is independent: a missing adjustment
source does not override a valid imported shape volume in DDS mode. Fixed 3D
shape input requires linear RGBA; profile inputs and top/bottom packing reject
sRGB and incompatible formats. Reload also refreshes custom NDF/noise/cirrus paths
and local assets. Invalid selected global pairs require matching dimensions and
fall back together. The input manager normalizes separators, dot segments and
ASCII case, supports UTF-8 paths, lists successful loads only, and retains failed
load diagnostics separately for explicit retry.
