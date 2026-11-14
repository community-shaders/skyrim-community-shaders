# Voxel clouds

Physical Sky → **Voxel Clouds** places authored NVDF volumes alongside NDF clouds.
An instance folder under `Textures/PhysicalSky/VoxelClouds/<ID>/` contains
`Instance.json`, `Modeling.dds`, and `Distance.dds`. All instances share
`Data/Textures/PhysicalSky/NoiseVoxel.dds`, loaded once by the voxel-cloud manager
and bound as a single 3D texture. The noise remains a shared Physical Sky resource.

Use **Add voxel cloud** to place an instance at the camera, then set its centre,
rotation and uniform scale. Coordinates are metres; Z is relative to Physical
Sky's worldspace height zero, as with storms. The example's native box is
4096 × 4096 × 512 m. It is available in the picker, not enabled globally.

With a scene armed, **Override voxel clouds in this scene** stores that scene's
complete placement list. An empty list fades existing placements out. Scene
Manager's transition duration controls the fade. Different transforms retain
their own endpoints and crossfade extinction; identical placements merge their
weights. Thus rotating or moving between scenes does not shear the authored
cloud or sweep it through the intervening world. An instance's **Height reveal**
separately applies the local-cloud height mask in local Z.

`Instance.json` retains the exported reference field names. Texture references are
relative filenames within the instance folder, rather than Odradek object IDs:

| Asset field                                       | Instance behavior                                                                                |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| `SignedDistanceFieldTexture`                      | Packed RGB SDF, `Distance.dds`                                                                   |
| `ModelingDataTexture`                             | RGB modeling volume, `Modeling.dds`                                                              |
| `BoxCenter`                                       | Default placement centre, `{ "X": ..., "Y": ..., "Z": ... }`; scene placement overrides it       |
| `BoxSize`                                         | Longest box axis in metres; all three extents are derived from the SDF texture dimensions        |
| `HighQualityModeNoiseFrequencyScale`              | World-space noise frequency multiplier; default 1.2, shader frequency is `0.01 * value`          |
| `HighQualityModeNoiseFrequencyAdjustmentDistance` | Original 256 m value is retained; the exported voxel shader does not read its constant (`c53.z`) |

The example retains the source centre `(1000, -5600, 512)` for reference.
**Add voxel cloud** deliberately places it at the Skyrim camera; it does not
interpret exported world coordinates as Skyrim game units. `placement.center` in
the asset, when supplied, overrides `BoxCenter` as its placement default.
Legacy `modeling`, `distance`, `size` and `noiseFrequency` fields remain readable.
An explicit `BoxSize` takes precedence over legacy `size`.
`noiseMipDistance` controls the mip rule. Noise texture selection is global;
instance JSON does not select or bundle a noise resource.
Placement settings are `asset`,
`worldspace`, `enabled`, `center`, `rotation`, `scale`, `strength` and `density`.
Each placement also exposes `boxSize`, `noiseFrequency`,
`noiseFrequencyAdjustmentDistance`, `noiseMipsEnabled` and `noiseMipDistance`
in the scene editor. Edits are saved with that scene's placement, not shared
across every placement of the asset. Zero size/frequency/mip distance and -1
adjustment distance/mip toggle inherit the asset; **Restore asset size and noise
settings** restores that inheritance. Box size preserves the volume's aspect
ratio and uniform scale applies afterwards. Texture filenames are shown for
inspection and are selected by the asset's `Instance.json`.
Up to eight distinct asset sets can be bound together, including both sides of
a scene transition; repeated placements share their textures.

With `noiseMipsEnabled: true`, the noise mip is `log2(1 + distance / 1500)`.
The reference example disables that distance mip, as the inspected high-quality branch
does (`FUN_1404ce000` → `c52.y = 0`). Existing assets retain the enabled default. The exported 256 m `HighQualityModeNoiseFrequencyAdjustmentDistance`
is a separate setting; it is not the denominator of this mip expression.

### Source cross-check

Odradek on the inspected reference build (PC 1.5.80.0) resolves `VoxelCloudSettings 92368:2` to modeling
`92368:0` and SDF `92368:3`. `CloudRendererSystemAssets 2:11052` resolves
Noise3dC to `2:2782`, the view shader to `2:4554`, and the lighting shader to
`2:5831`. Fresh DDS exports match the three local reference DDS files byte for byte;
fresh `2:4554` DXIL converted with the local dxil-spirv tool matches the supplied
`VoxelCloudComputeShader/compute.spv` byte for byte.

Ghidra `FUN_1404c9c30` reads settings offsets `0x20/0x28` for the textures,
`0x30` for the centre, `0x48` for box size, and `0x4c/0x50` for the two noise
settings. Box extents are `textureDimensions * BoxSize / max(textureDimensions)`.
`FUN_1404c9ec0` maps the noise settings to `c53.y/z` and independently writes
`1/1500` to `c52.z`. The original voxel shader samples Noise3dC using `c53.y`,
optionally computes its mip using `c52.z`, and never consumes `c53.z`. This
establishes the setting mapping, not equivalence of our lighting or temporal
reconstruction to the reference renderer.

The upload mapping was checked against Odradek's actual `2:4554` shader data,
not inferred from the translated HLSL. Its binding-table record 2 copies 708
bytes from CPU offset 0 to GPU offset 160. Thus CPU `0x2b4/0x2b8` become GPU
`852/856` (`c53.y/z`). `FUN_140178f10` performs this table-driven copy.
`FUN_1404cdb60` uses the same block for both near/far dispatches; its viewport
helpers do not transform either noise parameter. The alternate and cached
copies in `FUN_1404ccae0` and `FUN_1404cd350` also preserve both values.
The shipped voxel DXIL extracts only components 0 and 1 from register 53;
the coarse cubemap shader extracts only component 0. The lighting-cache
builder uses a separate modeling/SDF block and does not sample Noise3dC.
This traces the inspected PC 1.5.80.0 paths through resource, CPU preparation,
binding and consumers; the adjustment value is preserved without adding an
unobserved frequency-adjustment formula.

## Rendering

The packed RGB distance decode, Noise3dC reconstruction, near detail and
distance-dependent step rule are retained. The SDF is tested before jittering:
only instances with negative SDF evaluate their modeling and detail at that step.
Positive distances skip empty space;
jumps stop before other cloud intervals or opaque scene geometry. Cubemaps use
the coarse modeling density and larger steps. BC6H modeling and BC1 distance data retain their compression in 2D slice
arrays, with explicit Z interpolation for D3D11. Noise remains a 3D texture.
Single-placement main views specialize the fixed instance and asset slot,
eliminating per-step instance loops and resource switches. Multiple placements
and scene transitions use the generic shader. Both paths use identical density,
lighting, step length and resolution.

Main-view NVDF now has an independent current-frame raymarch and depth-aware
reconstruction. Its outputs are premultiplied radiance/opacity and
opacity-weighted cloud distance plus scene-ray distance, matching
`VoxelCloudComputeShader`. NDF's temporal reconstruction and blur do not process
those outputs. The voxel pass uses the native SDF gate before jitter, independent
voxel steps, 0.1 transmittance termination and the native opacity remap. Aerial
perspective is integrated at occupied steps. Two-by-two scene depth alternates
minimum/maximum; the composite uses a scene-depth-weighted bilinear rule.

The current Skyrim implementation uses one half-resolution range, restricted to visible instance screen bounds. The reference renderer's optional
100/200 m near/far split with a 30 m overlap is not implemented here. The local NVDF foreground integral is composited over distant NDF:
`L = L_voxel + T_voxel * L_ndf`, `T = T_voxel * T_ndf`. Mean cloud depths
only update the visible-depth metadata; crossing mean depths never switches
the order of whole translucent layers. This separate-pass composition does not
resolve arbitrarily interleaved NDF/voxel volumes. Their histories remain independent. Off-screen volumes
submit neither voxel view nor composite work.

Direct lighting, multiple scattering, shadow volume and ambient illumination
remain shared with NDF as requested. The coarse sun-occlusion cache is generated
at 256 × 256 × 32 per placement, with three near-detail probes. The
coarse estimator starts 16 m towards the sun, takes up to 256 samples with
`max(1, SDF)` advancement, and accumulates native SDF-weighted modeling density.
Fine probes use spatial jitter and 16/3 m spacing. Cubemaps retain the
coarse combined integration; scene endpoint crossfading and multiple transformed
instances are Skyrim adaptations, rather than reference instance behavior.

The noise equations follow `VoxelCloudComputeShader/compute.spv` and its raw
decompilation. Nubis Cubed pages 94–109 explain the four-channel construction,
but its published billow expression differs from this shipped shader: the game
uses `pow(profile, 0.0625)` and an alpha-channel bias, rather than the slide's
`pow(profile, 0.25)`. The supplied instance follows the game version.

The inspected reference hide/transform events are switches, not a generic scene-fade
system. Endpoint extinction crossfading is our Scene Manager adaptation; the
height-reveal and density formulas come from its voxel shader.
