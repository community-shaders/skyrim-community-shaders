# Storm instances

Physical Sky loads independent instances from
`Data/Textures/PhysicalSky/Storms/<ID>/Instance.json`.
The Clouds page exposes **Storm Instances**: select an instance, edit it, and
use **Save instance**. **New instance** creates a disabled instance;
**Reload instances** rereads the files. These files are distributed with
Physical Sky and do not occupy the preset browser. Each instance belongs to a
worldspace. Several instances can be active in the same worldspace.

## Supercell generation and sampling

`StormNdf.cs.hlsl` follows the reference `ApplySupercell` and
`SampleMesocycloneNdf` sequence. A rotated/mirrored back template first blends
height, coverage and top/bottom type. The mesocyclone then supplies its own
height, coverage, types and storm mask, with the original separate influence
weights. The generated Height/Modeling pair caches the weather-independent
terms of these operations. At sampling time the live weather NDF supplies the
remaining terms; height clipping, vertical profile and erosion run once on the
combined NDF. Height, top and bottom use the original 7/3/10 influence ramps.
Coverage retains the back-template max/lerp expression, rather than taking a
maximum of two evaluated densities. The back-template alpha and final storm
mask remain separate signals. Height auxiliary survives replacement.
Instance height limits are applied after composition, retaining the original
weather range while it still contributes. No additional map or generation pass
is needed.

The density path retains the five radial noise bands, their bend/speed/frequency
ratios, radius jitter, outer weather transition, and the rising anvil's separate
noise and exponent. The final storm mask controls the mirrored profile, crown
coverage/type transition, reconstruction and base/top density response.

Adaptations to this renderer are world-anchored placement, metre-based height
zero/span, volcanic wind bending, pause-aware time and motion, independent
density/emission, and three-dimensional volcanic weather replacement.
View, shadow, ambient-density and cubemap queries use the same density function.
Generated NDFs are cached until instance parameters, resources or noise inputs
change; ordinary wind motion does not regenerate them.

`mode` selects `Supercell` (the default, five radial bands and an anvil) or
`Volcanic` (a rising, rotating eruption). Volcanic mode uses the same NDF/profile
and erosion foundation, with isotropic, metre-space cellular billows throughout
the plume. A dense Modeling interior keeps the eruption connected while erosion
breaks up its surface. Its trajectory sections use locally advected and rotating
rigid fields at a fixed noise scale. Samples blend across the transition;
coordinates are never sheared or scaled by the extending NDF envelope.
A continuous swept envelope connects the vent, column and downwind canopy.
Local rigid motion preserves rounded billows independently of the envelope
expansion and flattening. The existing shape texture's G channel comes from
`AlligatorNoise.hlsli` through `CloudNoiseGenerate.cs.hlsl`. Volcanic sampling
uses it at an isotropic 0.001 inverse-metre texture scale to displace the carrier
in both directions, then applies the finer erosion. The carrier's 200 m halo
only rejects empty space; it does not clip the resulting convex lobes. The
nominal supercell ceiling fade is disabled for Volcanic, whose deformed envelope
already supplies its ceiling. This reuses the same two texture reads and adds
no per-ray Alligator neighbour search.
Reprojection integrates storm displacement only where the
storm actually contributes extinction; an average ray depth inside the storm
bounds does not assign foreground or background weather clouds to the storm.

Storms have an independent single-scattering `albedo` (0–0.9999), controlling
sunlight, multiple scattering calibration and local ambient transport. Density
scales extinction; view and sun extinction multipliers also apply. The Red
Mountain and Supercell examples both use a density scale of 0.75.

Main-view rays skip gaps between weather and storm intervals. When all active
instances are volcanic, main-view, reflection, shadow and ambient density passes
use a specialized shader without the mesocyclone branches. The main view dispatches
this shader only inside the projected storm bounds, including local lighting
probe reach. Other pixels use a shader with storm sampling compiled out. When
the bounds miss the view, only that ordinary-cloud dispatch runs. Reflections and shadows still include
the storm. Opaque storm light probes stop only after both lighting transmission
terms fall below exp(-12). This is not a claim of zero GPU cost: verify the
in-game timings for absent, off-screen and visible storms separately.

## Resource channels

Each instance is a self-contained folder containing `Instance.json` and its DDS
assets. `asset` and `coreAsset` resolve relative to that folder: `.` selects
`Height.dds` and `Modeling.dds` beside the JSON; subfolders such as `Back` and
`Core` keep distinct pairs together. Empty values retain their existing optional
meaning. DDS validation remains unchanged.

An optional linear, single-channel `Pattern.dds` beside `Instance.json` supplies
the analytic mesocyclone's 2D turbulence input. The Supercell reference includes
the reference Perlin-Worley pattern. Without this file, instances retain the
shared weather noise slot 2 fallback; that editable slot is not guaranteed to be
that Perlin-Worley input (the current default is Perlin). Authored `coreAsset`
maps bypass the analytic mesocyclone and do not use this pattern.

| Instance field         | Height RG                                            | Modeling RGB                           | Modeling A         |
| ---------------------- | ---------------------------------------------------- | -------------------------------------- | ------------------ |
| `asset` (back)         | Template heights, decoded as `RG - 0.5 + coreHeight` | Coverage / top type / bottom type      | Template influence |
| `coreAsset` (optional) | Normalized core bottom / top                         | Core coverage / top type / bottom type | Core storm mask    |

An empty `asset` omits the back. An empty `coreAsset` uses the analytic
mesocyclone. A core asset provides the final NDF channels in its valid height
footprint, replacing the analytic core and any back beneath it. In Supercell mode, authored core profiles taper at their finite height bounds.
Volcanic maps instead use a broad height support, with the Modeling coverage and
profile types defining the cloud surface before erosion, as in the back maps. Missing or invalid
configured resources disable that instance.

`center`, `radius`, `backHalfSize`, `altitude`, `height`, and `topOffset` use
metres. `altitude` is relative to the worldspace atmosphere base; the core's
actual foot follows its generated height, rather than necessarily starting at
height zero. `coreHeight` is the normalized height used by the analytic core
and the back template. `backRotation` is in degrees. `rotation` multiplies the
original noise-band speeds; `updraft` is the anvil lift speed (Supercell), or the
vent speed that scales the rise and crown outflow (Volcanic), in metres/second.

`topOffset` and `windInfluence` apply only to Volcanic instances. Supercell
samples its Height and Modeling at the same undeformed instance coordinates;
the weather-driven height shear and upstream shape blending do not apply.
Its rotating noise bands, anvil updraft and horizontal noise advection remain active.

### Replacement of ordinary tropo clouds

Supercell modifies the weather NDF before vertical clipping and erosion. Full
replacement removes the old weather profile even above or below the new cloud;
partial influence blends its height, coverage and profile types. Supercell
instances are applied in reverse name order so earlier names have priority.
The CPU screen rectangle, GPU tile classification and ray interval include both
the storm and curved weather height ranges, so excluded ordinary clouds cannot
reappear through the storm-free dispatch.

Volcanic instances instead replace weather within their swept three-dimensional
carrier. This follows the bent column and outflow rather than clearing the whole
vertical footprint underneath the canopy. Erosion and dilution do not restore
weather inside the carrier. Protruding lobes extend replacement outside it;
the replacement mask does not clip or flatten those lobes.

Volcanic instances blend extinction and shadow density in name order over the
combined Supercell/weather field. Full volcanic replacement skips that field.
Albedo, ambient transport and temporal ownership follow extinction contributions;
the dominant storm supplies scattering calibration and emission. Volcanic-only
shaders compile out Supercell NDF composition.

### Wind bending and downwind outflow (Volcanic)

`windInfluence` defaults to zero for existing instances; Red Mountain uses 1.
`topOffset` remains an additional manual offset. Eight centre/radius nodes
define a continuous swept envelope: five along the rising column and three along the downwind
outflow. The shape joins adjacent nodes rather than taking a union of balls. The vent stays anchored. Every subsequent section responds to wind,
including the lower column; there is no 45%-height dead zone or whole-plume
one-radius displacement limit.

The CPU integrates horizontal momentum entrainment along each rising section,
with response time proportional to section radius / updraft. Wind interpolates
between low-cloud and cirrus altitudes. Rise speed decreases along the column;
the upper outflow travels with the wind and slowly descends. Weather changes
relax each section toward its new position. Adjacent-section separation is
bounded to preserve connected support even in a strong wind. These are analytic
artistic approximations motivated by
[wind-bent plume models](https://agupubs.onlinelibrary.wiley.com/doi/10.1029/2012JB009592),
not a thermodynamic eruption simulation or a fit to measured eruption data.

Local noise moves along the trajectory and rolls around independently oriented
axes. The CPU integrates unit quaternions and translation offsets. A sample
blends at most two adjacent rigid noise fields, always at an isotropic metre
scale. It never bends or stretches the noise coordinates with the column.
Previous main-view transforms recover each field's motion for reprojection.
Pausing stops both trajectory response and noise transport.

The downwind sections widen horizontally and flatten vertically. Their
concentration falls with radius cubed and age, and their profile becomes
progressively more eroded. Dilution changes plume density without restoring ordinary clouds inside it. The dense
rising column retains its density. Authored Modeling travels with the local
section, so the old fixed Height footprint cannot slice off displaced billows.
The continuous swept support supplies the eruption's height boundaries;
Supercell retains its original Height/Modeling evaluation.

CPU screen bounds and ambient support enclose the current trajectory. View rays
intersect finite segment bounds and skip gaps when no ordinary weather
occupies them. This adds neither noise fetches nor light-march steps; a larger
visible footprint still needs GPU time and must be measured in-game.

## Geometry replacement and the Red Mountain instance

An instance can supply any combination of these exact-match rules (OR semantics):

- `replacementReference`: the original single `0xFormID~Plugin` reference.
- `replacementReferences`: additional references in the same format.
- `replacementNodes`: node names, matching the geometry or any ancestor.
- `replacementTextures`: effect source-texture paths, case-insensitive and treating
  slash directions equally. These are full paths, including `textures\`.

Rules only apply while that instance is active in its assigned worldspace.
Reference and node matches cull in `BSCullingProcess::AppendVirtual`. The effect
property's `GetRenderPasses` also rejects matching geometry before it produces
render passes, covering ordered sky effects that bypass the culling queue.
Nothing changes reference enable states, save data or persistent scene flags.
There is no replacement pixel-shader discard. The Red Mountain rules include
the root name retained by the local DynDOLOD resource and the three plume textures,
so cloned references do not depend on a fixed DynDOLOD-generated FormID.

The included `Storms/RedMountain/Instance.json` is scoped to `DLC2SolstheimWorld` and replaces
Dragonborn's persistent `VvardenfellPlume` reference. Its centre is derived from
that reference's translation using `1 Skyrim unit = 0.01428 m`. The original
NIF's local Z range (-10190.08 to 105101.46 units), placed at Z=43113.67,
gives a 1646.36 m span; its base also subtracts Solstheim's 256-unit
atmosphere zero. The instance uses that span instead of the former 4500 m.
Its Modeling channels carry separate multiscale coverage and top/bottom types; the height map provides broad support from the vent to the canopy. Its NDF resources are locally authored; no original mesh is bundled. The generator is
`tools/generate-red-mountain-ndf.py` (NumPy and Pillow required). Large Modeling
top-type brushes are calibrated through `NubisVerticalProfile.dds`, rather than
treating type as a linear height. Coverage and bottom-type fields have independent
variation; the Height support extends outside their footprint to remain stable
under bilinear filtering.
