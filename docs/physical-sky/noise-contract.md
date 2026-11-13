# Profile cloud noise reconstruction

## Resources

The regular NDF layer uses a locally generated volume or the optional
`NubisCloudShapeNoise.dds`, a linear 128³ RGBA8
volume with eight mip levels. The channels supply rounded shape (R), distant
wisps (G), nearby wisps (B), and erosion (A). `CloudNoise.hlsli` combines these
signals using dimensional profile, top type, local height and view distance.

`NubisOrographicDetailNoise.dds` is a linear 32³ RGBA8 volume with six mips,
reserved for a future orographic cloud path. That path uses its R channel for
nearby erosion. It is not sampled by the regular NDF layer.

The [voxel-cloud path](voxel-clouds.md) uses the original 128³ RGBA8 **Noise3dC**
resource, stored globally as `Textures/PhysicalSky/NoiseVoxel.dds` and shared
by every voxel instance. Its threshold and folded-detail reconstruction is separate from regular NDF profile clouds.
The current procedural generator supplies NDF shape and displacement noise;
it does not yet generate this voxel-specific four-channel resource.

Two linear lookup textures accompany the shape volume. Optional packed inputs are:

- `NubisVerticalProfile.dds`: BC5, one mip; R is the bottom profile and G is
  the top profile. U is type and V is local height, increasing bottom to top.
- `NubisVerticalAdjustment.dds`: BC7, seven mips; R expands the top profile,
  and GB supplies horizontal noise displacement near the base. Profile lookup
  clamps UVs; displacement lookup wraps and always uses mip 0.

See [resource fallback](volumetric-cloud-texture-resources.md#optional-resource-fallback)
for runtime LUT packing and operation without DDS files.

## Dimensional profile

NDF inputs remain height RG and coverage/top-type/bottom-type RGB. The curved
layer maps the height pair into physical altitude before noise evaluation. The
internal height B channel stores the start fraction for bottom density shaping;
it is generated from top type, including for imported pairs.

Modeling UVs use height-dependent shear. An upstream modeling sample, displaced
by `60 * cloudShapeShear`, blends types and increases coverage with height.
The blend is `smoothstep(0, 1, saturate((height - 0.1) * 1.5384616))`.
Top type is multiplied by `saturate(coverage * 10)`.

```text
f = saturate(height / coverageHeightRange)
coverageExponent = lerp(lerp(coverageBottomPower, 1, f), 1, f)
top = lerp(topProfile, max(topProfile, expandedTop), topExpansion)
dimensionalProfile = max(top * bottomProfile, 0) * coverage^coverageExponent
```

Lighting receives the dimensional profile before noise erosion. Its remap
`saturate((profile - 0.05) / 0.95)` is applied once in the lighting response.
The coverage/type product remains `coverage * topType`.

## Noise coordinates and mip

Positions are converted to metres relative to the existing worldspace altitude
reference; XY follows the shared wind displacement.

```text
bottomFade = saturate((height - 0.02) * 33.333332)
xy = position.xy - windOffset
displacement = adjustmentGB(xy * 0.004) * 2 - 1
noiseXY = xy * 0.0043545123 + displacement * (0.125 - 0.125*bottomFade)
noiseZ = (position.z - 20 + 10*bottomFade) * 0.0034834063
mip = floor(dimensionalProfile * 3 + mipBias)
```

View rays use mip bias 0; sunlight and ground-shadow queries use 2. The spatial
frequency is fixed by the reconstruction, independently of NDF map repeat.
Noise scale, roundness and voxel folded-detail controls are absent.

## Density

The R/A billow and B/G/A wisp branches are combined using top type and height.
B transitions to G between view distances 1000 and 2000 metres. The combination
is multiplied by 0.975, then eroded by subtracting `1-min(0.7, profile)`.
The complete channel equations are in `ReconstructCloudNoiseDensity`.

Positive eroded density `e` receives the base response:

```text
boost = 8 * (1-e)^10
width = 10 - 9 * saturate((bottomDensityWidth-1)/9)
profileHeight = saturate((height-shapingStart) / max(1-shapingStart, 1e-6))
base = profileHeight^0.3 * saturate(profileHeight*width)^bottomDensityPower
density = (lerp(boost, 1, saturate(height*5)) * e * base)
          ^(0.35 + 0.3*saturate((height-0.25)*4))
viewExtinction = density * densityScale
lightDensity = viewExtinction * (1 + 3*height^4)
```

Empty eroded samples return exactly zero before the power response. The internal
height B channel supplies `shapingStart`, remapped from the composed top type.
It is separate from the public RG height input. The optional [storm core](cloud-storms.md)
uses its own NDF profile, rotating noise, base shaping and top fade within the shared density query.

Each local sunlight probe retains the originating view sample's distance for
the B/G transition. Ground-shadow queries use distance from the camera. View
opacity, sunlight occlusion and ground shadows share the same reconstruction.

## Weather shape controls

The editable `cloudLayer.low` values describe the current weather's cloud
shape. They apply to both global and local NDF samples. They are distinct from
renderer quality, texture encoding and per-instance influence weights. A DDS
pair does not specify these values and cannot reproduce a weather by itself.

| Authoring field        | Shader coefficient    | Conversion                     | Initial authoring value |
| ---------------------- | --------------------- | ------------------------------ | ----------------------: |
| `bottomSpreadScale`    | `coverageBottomPower` | `1 - 0.75 * bottomSpreadScale` |               0.8133333 |
| `bottomSpreadHeight`   | `coverageHeightRange` | identity                       |                    0.30 |
| `bottomSoftness`       | `bottomDensityPower`  | `2 + 4 * bottomSoftness`       |                       1 |
| `bottomSoftnessHeight` | `bottomDensityWidth`  | `10 * bottomSoftnessHeight`    |                    0.68 |
| `topExpansionScale`    | `topExpansion`        | identity                       |                       1 |
| `densityScale`         | `lowDensityScale`     | identity, extinction per metre |                   0.125 |

Spread, softness, softness height and expansion are authored in 0–1. Spread
height is a local-height fraction, bounded away from zero for its division.
The initial values retain the renderer's previous coefficients (0.39, 0.30,
6, 6.8, 1 and 0.125). They are a project preset, not constants of the model.
The serialized authoring keys replace the old shader-coefficient keys; there
is no legacy-key migration. The shader constant buffer retains coefficient
names and layout.

Scene Manager discovers all six controls through the settings serializer and
menu bindings. Scene overrides store the authoring fields under
`PhysicalSky/cloudLayer/low`; location, interior and time-of-day policies allow
them, and they support weather interpolation. Resolved authoring values are
converted to shader coefficients when preparing cloud constants.

Local NDF resource and placement transitions use the atomic state described in
[scene transitions](local-ndf-transitions.md). Endpoint Interpolate and Maximum
combine NDF controls before scene weights blend their results; they do not
interpolate independent density models.

## Verification

CPU comparisons cover 200,000 noise/base responses using actual shape-volume
texels from six mip levels and 200,000 profile queries using the bundled LUTs.
Static checks cover all density callers, host/shader field order and stride,
settings serialization, and the absence of reserved detail volumes from runtime
bindings. The empty-space margin includes both the local and upstream shear
displacements. These checks do not calibrate defaults or replace game rendering
and performance tests.

## Bottom shaping start

The internal NDF height B channel is sampled before height-dependent XY shear.
For shaping start `q`, bottom density uses
`profileHeight = saturate((height - q) / max(1 - q, 1e-6))` only in
`pow(profileHeight, 0.3) * pow(saturate(profileHeight * baseWidth), bottomPower)`.
Bottom boost blending and the final density exponent retain the original height.
At the default q = 0 this preserves the previous formula; q = 1 gives no density
inside the column. This value does not move the geometric height bounds.
