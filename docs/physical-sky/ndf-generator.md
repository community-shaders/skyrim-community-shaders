# Procedural NDF generation

Four locally generated scalar noise inputs drive the low-cloud NDF. Every input
can instead use DDS. Optional local NDF maps blend before bottom height variation.
No offline preparation or world-space voxel volume is required.

## Two-texture contract

| Texture  | Channels                                                   | Generated storage             | Density binding |
| -------- | ---------------------------------------------------------- | ----------------------------- | --------------- |
| Height   | R: bottom height, G: top height; internal B: shaping start | RGBA16_FLOAT                  | t7              |
| Modeling | R: coverage, G: top type, B: bottom type                   | 512 x 512 RGBA16_FLOAT, A = 0 | t8              |

Values are linear and normalized. Altitude is `ndfAltitudeOffset +
ndfAltitudeScale * height` metres above the worldspace reference altitude.
The Low Clouds controls default to a 256 m base and a 1792 m span. The resulting
shell bounds traversal and storage; each NDF column supplies its own bottom/top. Types select the existing Nubis profile LUTs. Both generated and
imported maps use wrap filtering at mip 0. Empty/reversed height intervals produce
no density. Height bounds the profile; continuous coverage and Nubis noise then
reconstruct density inside it. No dome caps or height reordering are generated.

Texture mode selects `cloudMap.texture.heightPath` and `modelingPath`.
Supported linear 2D DDS formats, with unused channels ignored:

- Noise/masks: R8/R16 UNORM, R16/R32 float, BC4 UNORM, or any format below.
- Height: RG8/RG16 UNORM, RG16/RG32 float, BC5 UNORM, or any format below.
- Modeling: RGB32 float, RGBA8/RGBA16 UNORM, RGBA16/RGBA32 float, BC1/2/3/7 UNORM.

Arrays, volumes, integer and sRGB views are rejected. Inputs should contain
normalized finite values and share spatial extent; resolution may differ.
The old five-slice array is no longer accepted.

## Composition equations

`cloudMap.procedural.parameters` contains six noise layers, each with a slot,
frequency, offset, exponent and range (input min/max, output min/max).

```text
R(x, r) = saturate((x - r.x) / (r.y - r.x)) * (r.w - r.z) + r.z
S(layer) = 2 * noise[layer.slot](((layer.offset - wind) + uv) * layer.frequency) - 1
P(x, e) = exp2(log2(x) * e)
G(layer) = layer.range.w if range.z == range.w, otherwise R(S(layer), layer.range)

first  = P(R(S(primary), primary.range), primary.exponent)
second = P(R(S(secondary), secondary.range), secondary.exponent)
coverage = G(coverageGain) * max(first, second)
topType = G(modelingGain) * P(R(S(modeling), modeling.range), modeling.exponent)
bottomType = G(modelingGain) * P(R(S(modeling), bottomTypeRange), bottomTypeExponent)
```

When the primary output range is constant, **both** coverage inputs become that
constant and bypass power. Secondary settings then have no effect. Types share
one sampled noise but use independent remaps/exponents. Slot 4 returns zero
before signed conversion, hence S = -1.

The default top-type response maps signed noise from `[-0.5, 0.6]` to `[0, 0.5]`
before applying exponent `1.6`, with unit type gain. Its final type range is
`[0, 0.32988]`. This selects lower profiles across the default fine type field;
using most of the LUT's type range there produces narrow, tall columns even with
unchanged coverage. These are project weather defaults, not a restriction on
authored types or the height interval. Higher types remain available for weather
authoring and imported maps. **Restore default response** in **Top type / shared
type noise** resets only the remap and exponent, retaining noise selection,
frequency, offset, coverage, height and local instances.

Local maps are sampled at `uv - wind * localWindScale`:

```text
mode 0: model = (mask * localModelingWeight) * (localRGB - model) + model
mode 1: model = max(model, localRGB * localModelingWeight)
height = baseHeight + saturate(mask) * localHeightWeight * (localRG - baseHeight)
driver = heightFromCoverage ? model.coverage : S(heightVariation)
variation = R(P(driver, heightVariation.exponent), heightVariation.range)
height.r += variation
```

Maximum blending deliberately ignores the mask. Optional `local.heightPath` and
`local.modelingPath` independently enable their weights. `localMaskPath` supplies
R influence for both blends; absent means one. Keeping influence separate avoids
giving modeling A a second contract. Zero weights preserve procedural values.

Height power occurs **before** remapping. Its coverage driver is post-blend,
pre-saturation. A constant variation output range bypasses driver and power.
Only height R changes; G stays at its base/local value. Final height RG and
modeling RGB are saturated. Before saturation, top type drives
`heightAuxiliary = saturate(R(topType, heightAuxiliaryRange))`. Internal height B
stores this value; A stays zero. The default output interval is [0, 0], so it
preserves the previous density until intentionally enabled. The optional [storm core](cloud-storms.md)
is evaluated in world space during density queries, independently of this repeating generator.

The density sampler reads auxiliary height at the unsheared height-map UV.
Only bottom shaping uses `saturate((h - auxiliary) / max(1 - auxiliary, 1e-6))`;
boost, density exponent, profile lookup and lighting continue to use `h`.
Imported RG/RGB pairs are also converted to this internal height representation;
extra channels in an imported height texture are ignored. Height conversion keeps
the imported height resolution, and rendering retains the original modeling SRV.

The CPU sanitizes finite parameters and zero-width remaps. Power returns zero
for nonpositive inputs; exponent is limited to 0.01–8. This defines otherwise
invalid logarithmic inputs while retaining positive-input operation order.

## Noise synthesis

`cloudMap.procedural.noise` has four entries, each with `parameters` and optional
`texturePath`. DDS R replaces the generated input. Slot choices are editable;
defaults are project tuning, not recovered synthesis settings or descriptor order.

| Slot | Algorithm                          | Frequency | Octaves | Persistence | Repetitions | Contrast | Response exponent |  Bias |
| ---- | ---------------------------------- | --------: | ------: | ----------: | ----------: | -------: | ----------------: | ----: |
| 0    | Alligator                          |         4 |       4 |         0.5 |           1 |        1 |                 1 |     0 |
| 1    | Perlin fBm, coarse response preset |         4 |       6 |         0.6 |           2 |      1.6 |               1.8 | -0.02 |
| 2    | Perlin fBm                         |        32 |       4 |         0.5 |           1 |        1 |                 1 |     0 |
| 3    | Perlin-Worley                      |        12 |       4 |         0.5 |           1 |      1.1 |                 2 | -0.05 |

Seed defaults to 1337 and integer lacunarity to 2. Coarse and ordinary Perlin
use the same gradient family with different spectral and response settings.
Repetitions tile the underlying field within the input texture. Noise presets
are calibrated independently from any particular weather composition. Matching
the family and distribution does not establish identical authored texels,
spectra or original synthesis recipes.

Perlin uses unit gradients and quintic interpolation in 2D. Alligator samples a
3D random cell field: the two largest random-amplitude smooth radial contributions
are subtracted. This follows the public
[SideFX definition](https://www.sidefx.com/docs/hdk/alligator_2alligator_8_c-example.html),
using project PCG hashes, a fixed Z slice and factor-two range adjustment. It is
not a nearest-distance difference. Perlin-Worley uses `lerp(W, 1, P)` per octave,
where W is inverted nearest distance and P is normalized Perlin. Octaves are
normalized by persistence weights, followed by:

```text
value = (normalizedNoise - 0.5) * contrast + 0.5
response = responseExponent == 1 ? value : pow(saturate(value), responseExponent)
output = saturate(response + bias)
```

Exponent one bypasses the power input clamp to preserve the original linear
contrast/bias operation for existing settings.

The default Alligator response keeps input bounds [0, 1]. Its four octave
frequencies are 4, 8, 16 and 32, with weights 1, 0.5, 0.25 and 0.125.
The seed is 1337 and the fixed Z slice is 0.5. Saved settings keep their explicit
noise tuning; a valid DDS override takes precedence over generated noise.

Inputs use R16_FLOAT with full mips. Integer periods wrap XY. The base frequency
is bounded and octaves are omitted above half the resolution, accounting for
repetitions. Compute sampling derives mip from source dimensions,
output dimensions and layer frequency, replacing implicit pixel derivatives.
Imported inputs use available mips. Noninteger layer frequencies can introduce
a seam across the NDF tile; integer frequencies are preferable for repeating skies.

Default primary uses slot 3 with output [0, 0.65], exponent 1.4; secondary uses
slot 1 with output [0, 0.4], exponent 1; types use slot 2. Default coverage stays
below one, preserving Nubis threshold sensitivity. There is no hidden final cap;
local maps or edited ranges can intentionally produce full coverage.

Composition controls remain independent. A weather may deliberately use the same
noise, frequency and offset for coverage and types, but that is not a universal
rule. A single weather sample does not define default height, coverage, types or
which layers must be disabled. Comparing two weather outputs without matching
their inputs cannot establish an error in the composition equations.

## Coordinates and lifecycle

Generation evaluates one canonical UV tile, corresponding to a centered
16384-unit weather domain before normalization. Static `windOffset` is multiplied
by 0.00005 before each layer's frequency. Physical repeat length is `low.ndfScale`.
Animated common wind and height shear remain in density queries/reprojection;
time does not enter generation.

UI exposes all composition parameters, noise inputs, DDS loading and local maps.
Selected paths persist with settings and load on first use. Missing paths are
not retried every frame; Load retries/reloads explicitly. Failed reloads preserve
valid resources. Noise edits rebuild the affected input. Composition/source
changes regenerate the NDF and refresh occupancy/distance maps independently of
image history. Imported acceleration uses actual bilinear coverage support.
World scale changes sampling, not the normalized map.

Continuous weather, density, layer geometry and lighting changes retain image
history and the ambient transport iterations. Changed weather gives untraced
checkerboard pixels a bounded 1/32 current-frame update; directly traced pixels
keep their normal history blend. This lets transitions follow the new weather
without repeatedly replacing the accumulated image with sparse reconstruction.
Both the main view and cubemap use this policy. Source/noise-topology switches,
layer or representation toggles, resource reloads and reference-world changes
still reset history. Height bounds have their own dirty flag, so retaining
history never freezes the weather acceleration data. Curvature transitions use
the previous radius in low- and middle-cloud motion instead of resetting each frame.

CPU/HLSL constant-buffer payloads are 384 bytes for composition and 48 for noise.
Compute restores its sampler and clears SRV/UAV bindings. All view, light,
shadow and cubemap consumers use the pair. Cirrus uses a separate coverage/type map; ordinary stratus remains a main-NDF type.

`cloudMap.version` is 2. Type 0 imports a pair; type 1 generates locally.
Older settings and five-slice selections reset to procedural defaults, requiring
visual retuning rather than silently reinterpreting incompatible parameters.
Existing version 2 settings retain their saved noise tuning. Omitted repetitions
and response exponent default to one, preserving the previous noise operation.

## Independent middle clouds (As / Ac)

`cloudLayer.middle` adds a finite middle-cloud layer alongside the existing low
NDF and cirrus. It is disabled by default. Enable **Middle Clouds (As / Ac)** in
the Clouds panel; the initial altitude range is 3500–4300 m above the atmosphere
base. Coverage controls thickness and density; type 0 is wispy As and type 1
is billowy Ac. The macro envelope follows HP's coverage/type and warped 2D-cell
approach; its horizon lowering is handled by the existing cloud curvature.
Final density shaping samples the existing cloud shape volume by default, without
allocating a new volume. A **2D surface** mode is available for comparison.

Coverage and type each select from the four shared Cloud Map noises. Cell and
warp use slots 3 and 1 respectively. Changing those global
inputs affects both layers, but middle-cloud controls do not modify low NDF inputs.
One 512 × 512 RGBA16_FLOAT map is sampled at t99; two textures (4 MiB total)
alternate while accumulating scene endpoints. RGBA contains density and
density-weighted normalized bottom, top and type, so clear-weather fades retain
the occupied height band. Only macro shape is baked here; maps regenerate when
weather inputs, the displacement support or the coverage-to-height response change. The
existing mipmapped noise textures supply warp at t101 and cells at t103.
Warp uses a broad filtered field, with displacement expressed in cell coordinates;
it no longer feeds fine noise octaves into a large UV displacement. Cell values
retain the source texture's response without another square operation.

After cell modulation, density is `densityScale * max(C * P(h) + S - 1, 0)`,
where `C` is macro coverage capped at 0.7, `P` is the soft vertical envelope and
`S = 0.975 * lerp(Wisp, Billow, type)`. The Billow/Wisp reconstruction functions
are shared with low cloud, including ordinary and NVDF channel conventions.
The middle layer uses type directly, without low-cloud base/ceiling type blends.
Both As and Ac therefore receive final density shaping; there is no separate
edge-erosion strength. The obsolete `wispStrength` setting is ignored on load.
The 3D mode takes one packed volume sample; the 2D comparison samples the same
volume at a fixed middle-layer altitude, retaining both noise components.
**Detail texture size** controls world-space repeat independently of Weather Size
(default 1200 m). Both modes follow upper-cloud wind; 3D also follows curved-layer
altitude. They do not adopt low-cloud wind or NDF coordinates.

**Cloud bands** uses a continuous world-space phase following upper-cloud wind.
Direction runs along the bands; spacing is the distance between bands in metres,
independent of Weather Size. The former four-wave tile interpolation and 1–64
cycles-per-map clamp are removed: they caused position-dependent cancellation
and erased macro gaps. No band calculations enter the low-cloud density marcher.

For weather density `C` and organization `s`, signed condensate surplus is
`lerp(C, -cos(pi*C), s) + s*cos(phase)`, clamped to [0,1]. At `s=1`, constant
weather occupies exactly fraction `C` of a period before cells and final shape.
Signed blending lets gaps open before full organization; occupied depth also
decreases towards a gap. This is a visual approximation of condensation across
a saturation threshold, not a thermodynamic simulation or a measured distribution.
It follows the [wave-cloud formation mechanism](https://resources.eumetrain.org/data/4/452/navmenu.php?page=1.0.0&tab=2)
and [separate/merged Ac undulatus morphology](https://cloudatlas.wmo.int/en/varieties-altocumulus-undulatus-ac-un.html).

Bend uses shared noise slot 1 along the band direction only, with displacement
measured in wavelengths. One noise period spans 64 band wavelengths, with a
fixed world-space low-pass of eight wavelengths (eight coarse bend intervals).
This mapping is independent of Weather Size. The filter
limits slope and prevents small noise features from folding the cloud pattern;
this is a numerical smoothness bound, not an observed atmospheric correlation
length. Thickness contrast changes the top relative to the bottom, and undulation
moves both bounds equally. Undulation remains a fraction of nominal thickness
(up to 0.5), expanding trace support. All three organization/contrast/undulation
strengths default to zero, preserving unbanded clouds. Existing setting keys are
retained, but old nonzero band presets need visual retuning.

Middle density queries evaluate the phase and, when bend is enabled, one filtered
noise lookup. Under-resolved bands use the integral of clamped condensate over
the sampling footprint, rather than attenuating the wave before thresholding.
Both view segments and lighting queries supply their footprint. This avoids
phase aliasing without adding march steps; lighting still consumes only the
resulting column/density. The debug weather-map preview shows the unbanded input.

The **Middle cloud weather** editor stores a complete `SceneBlend` state. Coverage
is `coverageGain * max(primary, secondary)`, using independently selected shared
noise slots, frequencies, offsets and input/output remaps. Coverage, gain and
thickness remap outputs are not clamped before composition; only the As/Ac type
field is limited to `[0,1]`. A signed thickness-variation field is added after `pow(saturate(coverage), heightPower)`;
the result is clamped to the nominal layer height range before deriving bottom/top.
Variation is measured as a fraction of nominal layer height, not as extra density.
A collapsed layer contributes zero density-weighted geometry to scene blending.

Legacy `layers[0]` and `layers[1]` remain primary coverage and type. Missing
`secondaryCoverage`, `coverageGain` and `thicknessVariation` deserialize to neutral
output intervals `[0,0]`, `[1,1]` and `[0,0]`. These defaults also apply to newly
created states: adjust the output intervals to enable the new fields. Constant
fields bypass texture queries. All five controls belong to each scene endpoint,
so changing noise slots or textures still crossfades generated maps rather than
interpolating discrete selectors. Middle generation constants occupy 192 bytes;
the output map format and downstream raymarch queries are unchanged.

The optional linear RGB DDS uses R = coverage, G = As/Ac type, B = interpolation mask.
Set influence to 1 and use a white B channel to replace procedural coverage/type;
independent thickness variation and cell shaping still apply. Maximum blending takes the larger procedural/local
RG values and ignores B, matching the existing cirrus editor. Invalid DDS inputs
fall back to procedural weather. The map repeats every `16384 * uvSize` metres
and follows upper-cloud wind. For gradual clear/cloudy transitions, leave the
layer enabled and animate coverage or density; the enable flag is discrete.

Middle macro shape remains a 2D field of density and weighted bottom/top heights,
reconstructed with an eroded soft vertical profile. An independent upper-cloud march
renders middle cloud and cirrus, then composes with the low/voxel trace at the
same checkerboard phase before temporal reconstruction. Low-cloud marching no
longer contains middle intervals, density, step constraints or material blends.
Middle visibility follows its own layer intersections and scene/planet occlusion;
the low-cloud Ray March Range does not truncate it, including in reflections.
Clear distance before the layer is skipped rather than marched.
The upper march targets 16 vertical samples with a two-weather-texel horizontal
footprint, bounded to 64 steps per crossing. Each macro step reconstructs the
carrier continuously between start, midpoint and end weather columns, reusing
the previous endpoint. Two density intervals integrate the truncated vertical
profile with the sampled shape noise frozen in each half, combining mass and centroid.
Lighting runs once per macro step at that centroid. This preserves thin columns
without doubling the light probes; it approximates varying detail within a half-step.
View detail mip selection uses the screen-pixel footprint. The view step's
horizontal extent must not blur detail perpendicular to the ray; lateral
integration remains sampled quadrature. Solar macro probes use their horizontal
segment length; the low-cloud shadow cache uses its texel footprint. Filtering the underlying
noise before nonlinear shaping is an approximation, not an exact average of
the final density. No additional noise volume allocation or light-march steps are used.
Carrier reconstruction and detail sampling are deterministic. A frozen midpoint
column produces spatial terraces even without noise; randomizing that column
independently instead creates inconsistent envelopes and lighting centroids
between checkerboard phases. Reflections distribute the upper march's existing
macro steps across the same 64 lanes used by low clouds, then reuse their ordered
reduction. Direct and ambient transmission stay separate until composition;
an exclusive transmission prefix retains the march's early exit, including for
cirrus behind an opaque middle layer. The primary-view march is unchanged.
Layer composition follows the existing integral-depth convention used by voxels;
interpenetrating layers are approximate. Wind and curvature displacement retain
the existing world anchoring and motion channels.

Lighting consumes extinction, height, optical depths and incident radiance;
shape generation remains separate. The upper layer uses the existing Mie phase,
solar spectrum, multiple-scattering calibration and atmospheric composition.
Middle self-shadow uses eight local solar-ray probes, limited to 3 km or
the upper-layer exit. This is our sampling budget, not a fixed HP requirement.
Each interval integrates the shaped profile between its endpoint weather columns,
preserving thin clouds between probes.
The first two probes sample local detail; the remaining six share one path-filtered
packed shape sample but keep their own positional weather, heights and type. This
reduces repeated volume reads while retaining macro band shadows. It is a coarse
optical-mass approximation, not exact integration of the 3D density field.
The upper view unrolls the fixed eight-interval loop so FXC can fold probe spacing
and specialize detail versus shared-noise queries without changing the samples.
Reflections retain the compact light loop alongside their parallel view steps.
Below the middle layer, a separate 8-by-8 tile classification skips Upper
work only when the entire tile is empty or hidden behind zero-transmission low
cloud, including the composition depth margin. Other tiles retain the original
march. Cirrus-only and inside/above-layer views use the direct dispatch.
A 32-square min/max weather hierarchy is rebuilt only after weather updates;
bilinear support and vertical band displacement enlarge its conservative bounds.
Extrapolated types bypass height culling. Cubemap rays use the same empty-space
and foreground checks before their upper-layer work. No light or density cache
replaces the existing low/middle self-shadow integrals.
Lateral weather still uses midpoint quadrature. Both
visible middle cloud and its outgoing environment radiance use this integral.
Local upper/lower optical depths use one central erosion sample filtered to at
least half the column thickness, integrating the resulting profile on both sides
of the query point. Their sum does not vary with the query height. The separate
shadow cast onto low cloud retains its positional one-column, equal-area soft-slab
approximation, scaled by this eroded mass and independent of camera view range.
A 256-square RG16F cache stores
upper-layer solar depth at the low-cloud trace bottom/top; low-cloud lighting
reads this cache rather than tracing upper weather per view sample. Heights
between the endpoints are interpolated; fine shadow detail and out-of-band
storm illumination remain approximate.

Cirrus radiance and transmission first compose with atmosphere lighting.
Middle-cloud outgoing surface radiance then becomes the low-cloud environment
boundary, using the same lighting functions as the visible middle cloud.
Middle and cirrus density do not enter the low-cloud ambient transport volume.
Its spatial boundary is evaluated per cell, while the external SH fallback is
cached using the camera column at five altitudes. The fallback therefore also
sees the cloud environment rather than unfiltered sky. Middle density remains
included separately in the ground-shadow integration.

In-game appearance and pass timings still require validation. This layer does
not implement Tcu/Cb growth.

## Shared cirrus inputs

Cirrus coverage and type select independently from the same four noise slots.
They have their own frequency, offset and remap, without a power operation or a
final output clamp. Defaults select slots 1 and 2. Weather offset uses 0.0001
for cirrus and 0.00005 for the main map. Cirrus weather is sampled in world UV
without a second runtime wind displacement; its pattern DDS uses high-cloud wind.
Cirrus generation parameters occupy 96 bytes, including local RGB map blending
and a Scene Manager endpoint weight. Shared input changes refresh dependent maps.
Patterns are loaded from `Data/Textures/PhysicalSky/cirrus.dds`; no pattern generator
is used. Complete cirrus weather states crossfade through Scene Manager.

## Optional resources and previews

A missing/incompatible noise override uses that slot's procedural settings.
An incomplete imported NDF pair falls back to procedural composition. Missing
local influence inputs disable their corresponding weights; a missing mask acts
as one. Missing cirrus local maps preserve generated coverage/type; missing
`cirrus.dds` disables cirrus without disabling the low clouds.
Failed paths are cached rather than retried every frame; Load explicitly retries.

Cloud Shape debugging provides scalar previews for both coverage branches,
coverage/type gains, final coverage/types, height bounds, shaping start and
height variation. Preview allocation and generation happen only when requested.
Changing only the preview does not invalidate cloud temporal history. Branch
previews have no meaning for imported pairs and are zero (gains are one).

See [resource fallback](volumetric-cloud-texture-resources.md#optional-resource-fallback)
for the zero-DDS and Top/Bottom-only distribution paths.

## Validation limits

Static review and external CPU evaluation cover expression order, branches,
local blends, buffer layout, density reconstruction and resource lifecycle.
The 100,000-case algebra comparison differs by at most 8.9e-16 in double
precision. Density slices use the bundled Nubis volume and both profile LUTs.
A 2,048-point periodicity check and an expanded cellular-neighbor search agree.
An independent weather sample also exercises shared coverage/type input, zero
secondary coverage and zero bottom type; these are sample-specific controls.
Its local-noise reconstruction has 43.90% zero coverage versus 44.25% using the
comparison input with identical composition settings. This checks one input
case, not general cloud quality or the identity of an external noise enum.
No C++ build, shader compilation or GPU acceptance is included. Runtime review
must inspect silhouettes, details, lighting, motion and imported replacement.
CPU density slices are diagnostic, not a rendered-cloud quality claim.
