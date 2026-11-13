# Cloud lighting

## Local solar occlusion

The main view and cubemap share local weighted sunlight probes. Their occlusion
feeds the non-storm profile response below. Probe count is
`int(10 - 6 * saturate((viewDistanceMetres - 512) * 0.00040192925))`.
The nominal extent is `240 - saturate(height * 3.3333333) * 120` metres.
Fractional sample indices start at 0.5; cubic spacing, a deterministic spatial
jitter, and forward-angle density weights form the local estimate. All local
noise probes use `floor(dimensionalProfile * 3 + 2)`. Their B/G wisp transition
uses the originating view sample's distance, not each probe's camera distance.

The response is paired with that local estimator, not an integral through the
entire cloud shell. A long grazing column can drive the soft-transmission term
to zero and over-attenuate the scattering volume. Distant same-layer occluders
outside the local probe support and cross-layer occlusion are not represented
by this lighting approximation.

Local probes use `extinction * (1 + 3 * localHeight^4)` in metres. The same density
feeds ground cloud shadows; `sunExtinction` scales solar attenuation separately
from view opacity.

The NDF layer has one `densityScale` in inverse metres. Its normalized noise
reconstruction is multiplied by that scale once; zero removes its view
extinction and shadow density. The initial value 0.75 comes from one captured
frame and remains provisional; neither the capture nor the formula comparison
establishes a suitable density default for the project.

## Profile scattering response

The response combines a soft-transmission term and a scattering volume, then
applies one blended dual-lobe phase to their sum. The default Henyey-Greenstein
response adds a broad lobe and a narrow forward lobe. Both controls also allow
negative eccentricity when configured.
The empirical terms are not separate physical scattering orders.

With local height `h`, dimensional profile `d`, coverage/type product `p`,
light density `rhoL`, view/light cosine `mu`, and occlusion `o`:

```text
e = saturate((d - 0.05) / 0.95)
t = 0.1 * (1 - saturate(mu))
a = saturate((e - t) / (1 - t))
q = sunExtinction * o
volume = 8 * scatterVolumeStrength * h^scatterVolumeHeightPower
       * (1 - 0.75 * saturate((mu - 0.5) * 2.0408163))
       * saturate(a * 6.666667 - 0.11111112) * p^0.25
       * exp(-q * scatterVolumeDepthPower)
softT = 1 - saturate(0.025 * q)
soft = 42.375 * softScatteringStrength * (a + rhoL)
     * [0.3 * (softT^(8 - 7*p^0.8) + softT^(16 - 14*p^0.8))
        + softT^(32 - 28*p^0.8)] * bottomAngularResponse
phase = HG(mu, phasePrimaryG) * phasePrimaryIntensity
      + HG(mu, phaseSecondaryG) * phaseSecondaryIntensity
direct = 1.5 * (soft + volume) * phase * lerp(1, powder, powderStrength)
```

`bottomAngularResponse` and `powder` depend on height, view/light cosine,
profile potential, light density and occlusion. Their full expressions are in
`CloudLightResponse`. They do not depend on the view integration step length.

## Brightness controls and defaults

All lighting controls live under `cloudLayer.lighting` and are numeric Scene
Manager controls. The UI separates shared lighting from low-cloud profile
scattering. `brightness` scales both layers; `lowBrightness` and
`cirrusBrightness` are independent relative gains. Cirrus retains a fixed 0.5
conversion before its sheet response. Exposure is applied by the existing
rendering pipeline, not by these controls. Changing brightness does not alter
view extinction or cloud shadows.

Phase, `sunExtinction` and `ambientStrength` are shared. Scattering volume,
soft scattering, powder, and the ambient floor/profile/base shaping affect only
low clouds. Cirrus has its own sheet response and still receives sunlight and
ambient radiance evaluated at its configured altitude.

The default uses one complete authored lighting preset rather than constructor
fallbacks or independently selected per-field averages. Its broad lobe supports
side and back illumination, while a weaker, narrower lobe supplies the forward
peak. Volume, powder and ambient shaping are kept together with their associated
gains. This is a practical model baseline, not an absolute radiometric calibration
or a guarantee of appearance under every density and atmospheric source.

The controls have distinct contracts:

- Source quantities, such as solar irradiance and atmospheric transmission,
  retain their physical units and normalization.
- Brightness corrections multiply the evaluated response. One preserves that
  response; it does not prove that an empirical response conserves energy.
- Phase intensities, phase G, attenuation and height powers, and ambient shaping
  define the approximation. They remain editable and Scene Manager controlled.
- Fixed coefficients belonging to the response formula stay with that formula.
  They must not absorb weather settings or arbitrary exposure compensation.

| Setting                                            |  Default | Model role                                                     |
| -------------------------------------------------- | -------: | -------------------------------------------------------------- |
| `brightness`                                       |      0.5 | Common response gain                                           |
| `lowBrightness`, `cirrusBrightness`                |     1, 1 | Independent layer gains                                        |
| `sunExtinction`                                    |        1 | Additional solar occlusion multiplier                          |
| `phasePrimaryG`, `phaseSecondaryG`                 | 0.2, 0.9 | Broad response plus narrow forward peak                        |
| `phasePrimaryIntensity`, `phaseSecondaryIntensity` |  1, 0.25 | Independent lobe intensities, not normalized blend weights     |
| `scatterVolumeStrength`                            |      0.5 | Scattering volume gain                                         |
| `softScatteringStrength`                           |        1 | Soft-transmission response gain                                |
| `scatterVolumeDepthPower`                          |     0.05 | Attenuation of the volume response                             |
| `scatterVolumeHeightPower`                         |      0.5 | Sublinear vertical response                                    |
| `powderStrength`                                   |      0.5 | Half-strength in-scattering probability modulation             |
| `ambientStrength`                                  |        2 | Ambient source gain                                            |
| `ambientFloor`                                     |      0.1 | Minimum ambient response before source and gain multiplication |
| `ambientProfilePower`                              |      4.1 | Actual empty-profile exponent                                  |
| `ambientBase`                                      |        1 | No additional suppression by the ambient base-height factor    |

### Why these response curves

Each phase lobe contains `1/(4*pi)`. The default weighted sum integrates to
1.25, so it is an empirical angular response, not a normalized scattering PDF.
Before brightness scaling it is 3.929138 sr^-1 toward the sun, 0.073582 at
90 degrees and 0.044761 opposite the sun. The broad lobe supplies most of the
side/back response. Normalizing this sum alone would change brightness without
making the full model energy conserving.

Nubis Cubed p29 illustrates separately phased transmission and multiple
scattering. This implementation instead phases the sum of its empirical soft
and volume terms. The illustration explains their intended roles; it does not
justify substituting its phase arrangement or a microscopic water-droplet G
without changing the response model as well. The voxel treatment on p136 uses
voxel distance and different optical-path data, so its attenuation coefficients
are not defaults for the local-probe model used here.

Let `q = sunExtinction * occlusion`. The soft response's base is
`max(0, 1 - 0.025*q)`, so it reaches zero at q=40. The volume response is
`exp(-q*scatterVolumeDepthPower)`, equivalent to raising `exp(-q)` to that power,
as in Nubis Evolved p54. Its role is to retain an illumination tail; the power
is not a scattering phase moment or a physical distance.

|   q | Soft base | Volume at power 0.05 | Ambient broad transmission |
| --: | --------: | -------------------: | -------------------------: |
|   5 |     0.875 |             0.778801 |                   0.860708 |
|  10 |     0.750 |             0.606531 |                   0.740818 |
|  20 |     0.500 |             0.367879 |                   0.548812 |
|  40 |         0 |             0.135335 |                   0.301194 |
|  80 |         0 |             0.018316 |                   0.090718 |

The volume has an e-folding q of 20; its default coefficient is
`8 * 0.5 = 4` before height, angle and profile modulation. The separate ambient
broad-transmission term retains its formula coefficient 0.03. Neither coefficient
is inferred from `1-g`.

These q values are relevant to the actual sampling code. In a homogeneous
region above normalized height 0.3, the 120 m probe extent gives
`q = 0.45*120*rhoL*(1+mu)*(1+0.2*N*mu)` for nonnegative view/light cosine mu,
unit solar extinction and N probes. At rhoL=0.25/m, q is 13.5 sideways and
48.6/81 toward the sun for 4/10 probes. This is a controlled homogeneous example,
not a distribution measured from a scene; jitter and spacing affect nonuniform
clouds. Its view-angle and probe-count dependence also shows why q cannot be
interpreted as a physical solar optical depth.

The volume height power 0.5 gives factors 0.316 at height 0.1, 0.5 at 0.25 and
0.707 at 0.5. Powder strength 0.5 gives `0.5 + 0.5*powder`, preserving half the
unmodulated response. Ambient base 1 makes its own height factor one; the
separate height dependence coupled to broad solar transmission remains.

`ambientProfilePower` stores the actual exponent. An authored normalized
ambient definition of 0.5 maps through `0.2 + 7.8*definition` to 4.1. The
published square-root illustration describes the approach but does not override
this fitted exponent. At eroded profiles 0.1, 0.5 and 0.9, the isolated
`(1-e)^4.1` factor is 0.649224, 0.058315 and 0.00007943.

The ambient floor and gain must be considered with that exponent. At height
0.5, coverage/type product 0.5 and q=20, the complete default ambient response
including low-cloud brightness is 0.456738 at e=0, 0.296525 at e=0.1 and 0.1
at e=0.5 or higher. The latter is the floor:
`brightness * lowBrightness * ambientStrength * ambientFloor = 0.1`.
It multiplies ambient source radiance, so it cannot emit light when that source
is zero. It is still an empirical minimum, not a physically solved transport
term. Increasing the profile exponent without retaining the paired floor and
gain would produce a different preset.

### Parameter upload and limits

```text
lowLightingScale    = brightness * lowBrightness
cirrusLightingScale = brightness * cirrusBrightness * 0.5
ambientStrengthGPU  = ambientStrength
softStrengthGPU     = softScatteringStrength
ambientProfilePowerGPU = ambientProfilePower
```

With the default brightness settings, the uploaded low-cloud scale is 0.5 and
the cirrus scale is 0.25 (0.5 _ 1 _ 0.5).

The fixed sheet conversion of 0.5 is retained as part of the source model's
parameter contract. It is not a physical albedo, a phase normalization, or a
reason to divide all cloud radiance by 32. The sheet's sunlight coefficient 64
and the low-cloud soft and volume coefficients remain unchanged.

Run `python tools/analyze-cloud-lighting.py` to reproduce the tables from the
current header defaults, without compiling or running the renderer. This checks
component behavior, not cloud reflectance, energy conservation or runtime image
quality. The local probes and empirical scattering approximation cannot provide
a unique physically exact preset for every geometry and optical thickness.

Existing configurations are not overwritten. **Reset Cloud Lighting** applies
the full preset; scene overrides are edited separately. Removed settings keys
are ignored. Density reconstruction is independent of lighting gains.

### Directional ambient transport

Low-cloud ambient illumination solves a directional radiance field. Solar single
scattering and local multiple scattering remain separate. `ambientStrength`
scales the ambient result once; albedo and droplet size define its medium response.
Cirrus retains its thin-layer lighting.

The phase function is decomposed as `p = f delta + (1-f) p_r`. Using the first two
Legendre moments of the water-droplet phase, the positive second-order residual is

```text
f = beta1^2
K = 1 - albedo f
sigma_t' = K sigma_t
albedo' = albedo (1-f) / K
4 pi p_r(mu) = 1 + 3 a mu + 5 b P2(mu)
a = (beta1-f)/(1-f)
b = (beta2-f)/(1-f)
```

The host computes the two moments analytically from the HG/Draine mixture. A
positive first-order residual is used if the quadratic fails its positivity
check. Absorption remains `sigma_t (1-albedo)`. Source moments use an orthonormal
real basis for `dOmega/(4 pi)` and store `Q_k = sqrt(beta_k,r) <Y_k I>`.
The directional source is `sum sqrt(beta_k,r) Y_k(d) Q_k`.

The boundary environment samples Physical Sky at five altitudes and 512 angular
positions. Ground radiance uses the reflection bottom-face average transported
through the atmosphere, with the existing DALC fallback. Sky and ground enter
the same transport equation. The dominant sky bright band is estimated after
subtracting each elevation ring's azimuthal mean luminance. A 26-point Lebedev
quadrature rotates to this axis. Its weights remain unchanged. Each environment
sample is mapped into the quadrature's cube frame and distributed over a
face-center, edge, and corner triangle. Nonnegative barycentric weights sum to
one and agree across triangle and cube-face boundaries. Directional averages
use these weights with the sample solid angle; RGB normalization preserves the
full-sphere mean. The lighting and boundary tables use FP32 to retain smooth
changes in the bright-band axis and boundary radiance.

### Grid transport and iteration

Four grids cover widths of 4, 16, 64, and 256 km. The near grid is 32x32x16; the
others are 16x16x8. Height follows the NDF height bounds with a 64 m total halo;
XY and Z origins snap to cell boundaries. Density generation uses four fixed,
vertically clipped cell quadrature points. It stores extinction in inverse metres,
including view extinction and `K`. Density is not blurred for light propagation.

For each selected direction, a group sweeps one sheared lattice ray. Every cell
provides an affine radiance interval `(emission, transmission)`. Parallel prefix
composition carries these intervals across the whole grid in five stages or
fewer. The result at each cell centre uses a half-cell interval. All 26 directions run in one dispatch, using separate depth slabs in the interval
atlas. A second pass interpolates their results at regular cell centres and
accumulates nine RGB moments in registers before writing each moment once. The
26 directions form one complete source iteration.

One cascade updates per rendered frame, following `0,1,0,2,0,3`. Each update uses
all 26 directions; it does not select spatial blocks or random directions. All
four view-source grids resolve every frame. Each field retains its lattice,
curvature anchor, wind offset, and phase parameters. Reads reproject translation
by the accumulated wind offset and rescale the stored basis when phase moments
change. Cloud evolution and spatially varying wind are followed through subsequent
density updates rather than an exact deformation map.

The solver uses source iteration with a persistent initial guess, not the offline
per-voxel 9x9 inverse. Reset and uncovered regions use the external environment's
phase-convolved source. Resource or shader reloads, worldspace or altitude-origin
changes, and disabling ambient lighting invalidate the layouts. Screen-space
history resets and weather-map updates retain the transport field as the initial
guess for the updated density and illumination. Initial iterations and rapidly
changing weather have a transient response. Convergence under motion must be
checked in the game; the offline 32-iteration results are not runtime acceptance
measurements.

Cascade reads blend source values over the outer three XY cells. Vertical reads
retain the source through the entire stored layer. Missing coverage falls back
to the same boundary source, preserving a constant, non-absorbing illumination
field. At long distances the field is a coarse representation of cloud geometry;
profile-altitude transport also approximates curvature within a grid.

### View integration

The main view and cloud cubemap share an observation point. A resolve pass
contracts each grid's nine moments against the direction from that point to the
voxel. The primary march then usually performs one trilinear RGB source lookup;
cascade overlaps use two. The observation-dependent cache is rebuilt each frame,
so turning the camera does not reuse an old directional convolution.

Ambient radiance integrates against its own delta-scaled transmittance `T'`, using
`albedo' (1-exp(-K sigma_t ds)) S_r`. The collapsed forward peak belongs to the
incident background: the output transmission is `T_view = T + g (T' - T)`, with
`g = saturate(ambientStrength * cloudLightingScale)`. Final compositing supplies
the actual sky or surface radiance. Substituting sky radiance here would inject
a bright rim where a partially transparent cloud overlaps dark terrain.
The original density function, step selection and solar-light sampling remain in
use. A ray continues while either solar/view or ambient transmittance exceeds the
termination threshold; dense clouds can consequently need more density steps.
Cirrus sheet transmission attenuates both paths. Aerial perspective is applied
to their combined cloud radiance.

A sheet behind low clouds is part of the incident background. Its premultiplied
radiance `C` contributes `T C + g (T' - T) C`, where
`g = saturate(ambientStrength * cloudLightingScale)` is the forward-boundary gain. The sheet
then multiplies both transmittances by its own transmission. The sheet remains
eligible for integration while either active path exceeds the termination
threshold. With unit gain, the result is `T' (C + T_sheet L_boundary)` plus
the low-cloud scattering source, preserving the order of the two layers.

The termination remap `T_out = saturate((T_view - 0.1) / 0.9)` keeps radiance
premultiplied by the same output opacity. Before aerial perspective, cloud radiance
is multiplied by `(1 - T_out) / (1 - T_view)`, evaluated as
`1 / (1 - min(T_view, 0.1))` to retain a finite empty-ray limit. A constant source
matching the background therefore remains constant through the silhouette.

### Resources and cost

| Resource                                          | Format / storage   |           Size |
| ------------------------------------------------- | ------------------ | -------------: |
| Four source fields, double buffered, nine moments | RGBA16F 3D atlases |       3.09 MiB |
| Four view-source fields                           | RGBA16F            |       0.17 MiB |
| Batched sheared intervals, 26 directions          | 64x64x832 RGBA16F  |      26.00 MiB |
| Shared extinction scratch                         | 32x32x16 R16F      |       0.03 MiB |
| Boundary environment and grid metadata            | RGBA32F 2D tables  | about 0.06 MiB |

The ambient resources total about **29.3 MiB**. Each direction occupies 32 slices
of the interval atlas. Integer major-axis coordinates keep interpolation within
that direction's slab. Moment atlases clamp trilinear Z coordinates to each
moment's own slices. Projection uses local RGB accumulators; it does not require
an intermediate structured moment buffer or typed UAV loads.

Per frame: one environment pass, one boundary projection, one density pass,
one batched sweep, one fused projection, one layout update, and one batched view resolve:
7 compute dispatches, in addition to the existing ground/sky input preparation.
The boundary pass computes three continuous direction weights for each of 512
samples once per altitude, then reduces bin colors and projects the moments.
Far-grid updates process one eighth of the near-grid cells. Rays whose sheared
footprint misses the volume skip the prefix scan.

Runtime feedback on 2026-10-21 reported approximately **7 ms** for the preceding
96x96x48 sequential-direction transport. Its near-grid accumulator issued about
2.42 GB of logical reads/writes, with another 0.74 GB of interval writes. The
current near-grid batched interval writes and fused projection total about
31.9 MB. These are shader-level traffic counts, not measured DRAM traffic or
predicted GPU milliseconds; they exclude source texture sampling and input
preparation.

The initial target was **under 0.1 ms per frame** for the added ambient path.
On 2026-10-21, the user reported **0.139 ms** and accepted the optimized version.
The subsequent continuous boundary-weight correction retains seven dispatches
and the same volume dimensions; its timing still requires runtime measurement.
Input preparation, transport, view resolves, and additional primary-march work
must all be accounted for. `PhysicalSky::CloudAmbientTransport` identifies the
transport section. Ambient Response Strength zero skips transport and view
resolves. No weather parameters or CB padding fields are added; the transport
CB is two 16-byte registers.

The smaller-grid version has passed the user's performance acceptance.
Offline checks distinguish filtering a converged high-resolution field from
solving directly on the coarse grid. CPU reproduction of the sheared coordinates,
half-cell integration, and inverse-shear interpolation preserves constant colored
illumination to 1.2e-7 for both grid widths and all six major-axis orientations.
Under two concentrated sunset environments, the converged coarse solver has
8.7% aggregate RGB L1 error for the upper cloud view and 16.2% for the thick
underside view against the accepted high-resolution field. The underside mean
is 8–16% brighter by channel. The comparison retains the reflected surface
variation and silver edges, with smoother small-scale response. These are
floating-point CPU comparisons; they exclude FP16 storage and cascade motion.
Reflection, silver edges, thin clouds,
concentrated sunset illumination, wind, cascade crossings, and cold-start
convergence remain part of visual acceptance. GPU timing and in-game visual
acceptance are performed by the user.

A moving concentrated-sunset CPU check advances the light by 0.005 degrees per
frame. The nearest-bin boundary has a maximum directional RGB step of 0.02255,
which remains 0.02255 when the angular increment is halved. Continuous weights
reduce it to 0.00005956 and 0.00002978 respectively. The largest moment step falls
from 0.002151 to 0.00001060. These absolute values use the synthetic environment's
radiance units. Constant RGB illumination and full-sphere RGB mean are preserved
to floating-point precision. This isolates angular reassignment; it does not
measure in-game flicker or GPU time.

## Height contract

Low-cloud NDF height decodes as `ndfAltitudeOffset + ndfAltitudeScale * value`
metres above the worldspace reference altitude. The Low Clouds controls are
NDF Base Altitude (default 256 m) and NDF Height Span (default 1792 m). The NDF
RG pair controls each column's bottom and top within that interval. The same
validated interval bounds ray traversal and ground cloud shadows.
The base is limited to 0–20000 m and the span to 1–20000 m. Changing either
control invalidates temporal history.

Shear shifts modeling coverage/type using the local NDF height fraction and
blends an upstream modeling sample, while leaving height sampling at the original column. Generated and imported maps
share this decoding. Missing settings use the defaults; obsolete low-cloud
base/thickness keys are ignored. Cirrus has one spherical altitude, default
8000 m; it has no volumetric thickness or low-NDF height channel.

## Cirrus sheet

Cirrus uses a separate two-dimensional weather map: R is coverage and G is type.
The RGB pattern channels are wispy, round and streaky; each is squared before
interpolating B to R to G at type 0, 0.5 and 1. With coverage `c` and interpolated
pattern `p`, the unscaled profile is `max(p, 1e-10)^(1.9 - 1.8*c) * saturate(2*c^3)`.
`cirrus.densityScale` is the clear-weather density multiplier (default 1).
Density is `2 * profile * cirrus.densityScale`; the fixed factor 2 is part of
the profile model. Storm modulation is not implemented.

Four deterministic light probes use indices 0.5, 1.5, 2.5 and 3.5, angular cubic
spacing over a nominal 120 m extent, and `13.5 * sqrt(density)` weights. Solar
response is `cirrusLightingScale * 64 * exp(-0.1 * sunExtinction * occlusion)`
times the shared phase function and per-position atmospheric sunlight. Ambient
response is `ambientStrengthGPU * cirrusLightingScale * (viewDirection.z + 1)`
times `(1 - 0.3*profile)^0.2` and the ambient radiance at the sheet's position.
Both clear-weather storm factors
are one. The sheet integrates `T_step = exp(-10*density)` once; its opacity does
not gain a grazing-angle path-length multiplier.

The main volume and sheet compose in distance order, then use the existing
representative-depth aerial perspective and full-resolution temporal history.
Sheet altitude is independent of the low-cloud trace interval and ambient-probe
reference height. The spherical sheet uses the cloud curvature radius rather
than the atmosphere radius; density and lighting use its ray intersection.
The global Cirrus UV Size enlarges both weather and detail patterns without
changing sheet altitude or physical wind speed. Self-shadow probes retain their
metre-based distances and sample the same scaled field. Raising the sheet or
changing UV size does not add view-ray steps or light samples. The
sheet intersection is not clipped by the low-cloud march range, and cloud depth
is stored in kilometres. Scene geometry and the planet can still occlude it;
aerial perspective can reduce distant contrast. Height and the lighting controls are
numeric Scene Manager controls.
The old weather/cell/warp/wisp volume and its view-step budget are absent.
Old `cloudLayer.high` saved settings are ignored; `cloudLayer.cirrus` loads its
own defaults. See [cirrus inputs](volumetric-cloud-texture-resources.md#cirrus-inputs).

## Lightning photometry

Lightning uses the solar control's existing calibration: linear working-gamut
Y is illuminance in lux. Cloud and channel peak luminances are in cd/m²; their
shared color is normalized to working-gamut Y = 1 after color conversion. The
Intensity control multiplies both peaks. Changing solar illuminance does not
rescale lightning; both use the same fixed unit convention, including at night.

An equivalent finite emitter of luminance `L`, radius `R` and distance `d`
supplies perpendicular illuminance `E = pi * L * R² / (d² + R²)`. This tends
to `L * projectedArea / d²` in the far field and remains finite nearby.
The cloud radius is an equivalent emitting-area parameter; the visible cloud
still follows the normalized cloud-base or embedded-cloud profile. The
channel emitter uses the main channel's area-weighted centroid and broadside
projected area `sum(2 * channelRadius * segmentLength)`; branches are visual
detail. These are finite-emitter approximations, not electrical discharge or
full local cloud light-transport simulations.

Clustered scene lights, rain and the shared atmosphere use the same illuminance
expression. They no longer use independent lightning brightness constants.
Surface BRDFs, visibility, user lighting multipliers and exposure apply afterward.
Calibration matches the sun under Linear Lighting and PBR; radiance stored in
half-float cloud buffers retains the existing HDR range limit.

Rate is in discharges per minute, distances in km/m, and return-stroke duration
and start-to-start interval in ms. The rate range is 0–600 per minute; each
discharge supports 1–1024 strokes, with duration and interval up to 60000 ms.
The interval cannot be shorter than the stroke duration. A decay of 1 keeps
successive strokes equally bright for sustained bursts. The UI shows the total
discharge duration. Storm activity accumulates during a discharge and there is
no fixed wait afterward. One active source and one pending source share RG16F
mask textures: each channel keeps its own history, and promotion changes the
active channel without discarding the pending source's 20 prepared cloud updates.
Both masks are evaluated within the existing cloud march, not a second march.
Only the active source contributes channel radiance, scene lighting and thunder.
Preparation starts near the requested event time and near the end of a long
active discharge. New-source frequency remains limited by cloud updates and
the active discharge duration; discharges do not overlap.

The optical envelope rises linearly over 5% of the duration, then follows a
truncated exponential over the remaining 95%, reaching zero continuously after
six decay time constants. These proportions are an artistic approximation,
not measured current-waveform parameters. Its peak remains one, so the peak
luminance controls retain their units; its time integral is smaller than the
previous triangle. An analytic integral over each display interval preserves
short strokes at low frame rates. The rapid return-stroke motivation follows
[NWS's negative-flash description](https://www.weather.gov/safety/lightning-science-negative-charged-flash).
Ground and cloud-base percentages leave the remainder for embedded discharges.
Replay retains source geometry and placement but uses current luminance and
stroke timing; radius, distance and event-type changes apply to new sources.

## Validation

Static/CPU checks cover host/shader fields and resources, the non-storm response
equations, local probe positions/weights, and parameterized NDF height decoding. CPU atmosphere checks
cover spatial RGB differences and altitude-dependent sunset visibility. These checks do not establish runtime appearance or performance.
GPU acceptance still needs camera motion, layer overlap, sunrise/sunset, and
generated/imported NDF heights.

- [Nubis Evolved, SIGGRAPH 2022](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-NubisEvolved-NoVideos.pdf)
- [Nubis Cubed, SIGGRAPH 2023 materials](https://advances.realtimerendering.com/s2023/)
- [Sampling and temporal reconstruction](cloud-sampling.md)
- [NDF generation and input contract](ndf-generator.md)

Scattering references:

- [PBRT: Phase functions](https://pbr-book.org/4ed/Volume_Scattering/Phase_Functions) — normalization of individual phase functions and interpretation of mean cosine.
- [PBRT: Volume scattering processes](https://www.pbr-book.org/4ed/Volume_Scattering/Volume_Scattering_Processes) — scattering albedo and extinction.
