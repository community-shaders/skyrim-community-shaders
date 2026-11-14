# Cloud sampling and motion

## Ray integration

The step follows Nubis Evolved slide 39: `3 + 60 * distance / 16384`, with
distances in metres, scaled by `marchStepScale` (default 0.5). The march ends
at the occupied interval's far bound, at `T <= 0.1`, or at `rayMarchRange`
measured from the camera. Long grazing intervals take as many probes as the
step function yields. Cirrus uses one sheet intersection and four light probes.

Parabolic intersections retain both near and far pieces of the NDF layer and
clip against geometry and the planet. The cirrus intersection splits these
pieces when needed, preserving front-to-back compositing even above the sheet
or when NDF heights extend across it. `rayMarchRange` includes the clear
approach and gaps; it does not restart at cloud entry. Its default remains
32 km, independently of the step's 16384 m adjustment distance.

The boundary prepass narrows the main-view interval where its finite grid
covers the ray. Cubemap height bounds provide a separate conservative clip.
A dimensional profile at or below 0.05 doubles the next step. Neither path
retries the previous coarse step or samples a horizontal distance field.
Noise mip is `floor(dimensionalProfile * 3 + mipBias)`, with bias 0 for view
queries and 2 for light queries. See the [profile noise contract](noise-contract.md).
The XY noise-warp lookup is skipped when its weight is exactly zero, above
approximately 5% of local cloud height; the noise coordinates remain unchanged.

Samples within 250 m use temporal ray-start jitter. Distant samples use stable
spatial jitter. Lighting uses deterministic local midpoints, independently of
the view-ray jitter. Integration uses `T_step = exp(-extinction * stepMetres)`
and `weight = T * (1 - T_step)`. Marching stops at `T <= 0.1`. Final transmittance
is remapped to `saturate((T - 0.1) / 0.9)`. This cutoff/remap is an artistic
approximation; the individual Beer step integral is invariant to subdivision for a constant source.

## Wind

Wind speed is metres per unpaused second. `PhysicalSky::Reset` integrates the
normalized horizontal direction and speed once per frame using
`RE::GetSecondsSinceLastFrame()`. Displacement and elapsed time accumulate in
double precision, independently of render passes and history validity. A zero
direction retains the positive-X fallback; zero speed stops translation at its
current position. Speed or direction changes affect subsequent displacement,
without re-evaluating earlier motion. Paused frames do not advance either state.

The GPU receives displacement in game units. Density queries subtract it before
converting to noise coordinates. NDF height/modeling, shape noise and its XY warp
share this translation. Cirrus patterns use high-cloud displacement; cirrus
weather uses its generator offset and is sampled without another translation.
This shared transport of static weather maps is a project adaptation; the
generator's weather offset remains a separate authoring input. It does not
reproduce independently animated weather and detail wind fields.

Main-view, cubemap and shadow passes read the same displacement. Reprojection
subtracts the displacement since the previous successful history capture from
the current cloud position. This delta is computed in double precision before
conversion to game units. Clearing history does not reset wind. History gaps
are measured in seconds, with a 0.25-second rejection threshold.

## Full-resolution 16-phase reconstruction

`CloudTemporal.hlsli` traces one actual full-resolution pixel from each 4 x 4
block. A 16-entry permutation visits all offsets. The compact trace textures
have dimensions `ceil(width / 4)` by `ceil(height / 4)` and store those rays;
they do not define a lower-resolution camera or pixel footprint.

History and resolved output both have full framebuffer dimensions. Each frame
reprojects history using opacity-weighted cloud depth, the previous successful
capture's camera matrix/position, and wind displacement. The camera matrix
includes the capture's projection jitter, matching current depth/ray coordinates.
The active viewport comes from the draw's per-frame DR parameters and the cloud
texture allocation. Ray, depth and motion UVs use that same coordinate system.
History stores the previous successful capture's viewport extent, including its
fractional part, and clamps taps to that capture's integer active bounds. DR size
changes retain history and do not reset cubemap accumulation. Trace dispatches
cover only the current active region. Traced pixels update from their actual
sample. Other valid pixels retain reprojected history. Four trace neighbours
supply only missing history, with scene-depth rejection; there is no spatial
upscale pass applied to the accumulated result.

Temporary hole fills have validity 0.5; actual samples have validity 1. Their
first traced update replaces the fill. A traced pixel mixes its reprojected
history with its own new sample by `(1 - 0.8 * sqrt(relativeColourDifference))`
times the projected-motion weight; `temporalAccumulationFactor` scales how far
that weight is applied, and is 1 by default. Untraced pixels keep the
reprojected history unchanged. Colour and opacity come from the same weight;
clear sky does not contribute to cloud-depth moments. Configuration changes,
shader/texture reload, time reversal and gaps over 0.25 seconds invalidate
history. Snapshots and the phase index advance only after main and cubemap
reconstruction complete.

The 64 x 64 x 6 cubemap follows the same schedule: 16 x 16 actual rays on every
face, then per-face full-resolution temporal reconstruction. Cube history is
sampled as a cube to cross face boundaries. It compensates camera translation
and wind, independently of screen rotation. Depth is stored as an
opacity-weighted moment for cube filtering; metadata W is reserved. Steady-state ray count
is 1,536 per frame, versus 8,192 for the former two-full-face schedule.

The NDF volume and cirrus sheet share wind displacement and one representative
depth. Parallax between layers, disocclusion and rapidly changing light still
need runtime assessment; one depth cannot represent arbitrary multilayer motion.

## Resources and validation

Cloud parameters use a 480-byte constant buffer at compute b1. CPU members
follow HLSL 16-byte register packing, including the row-major history matrix.
Shared textures occupy t1–t12; t0 is unused. Ambient hemisphere normalization
and unattenuated ground radiance are evaluated once per ray. Sunlight and
ground-column transmittance remain positional evaluations at occupied samples.

Transmittance is scalar R16_FLOAT. Trace, output and history radiance all use
RGBA16_FLOAT with equal RGB precision; metadata uses RGBA16_FLOAT. Full-resolution
outputs and history swap ownership after unbinding their views. Screen storage
is 37.125 bytes per framebuffer pixel before dimension rounding, excluding
shadows and atmosphere resources.

Static/CPU checks cover the buffer contract, 16-phase coverage (including odd
sizes), native pixel recovery, all cube texel orientations, ordered interval
coverage, Beer integration and edge-depth moments.
No C++ build, shader compilation, GPU render or timing is part of these checks.

## Reference

[Nubis Evolved, SIGGRAPH 2022](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-NubisEvolved-NoVideos.pdf):
slides 39, 48, 54, 59, 157 and 187. Curved interval ordering,
phase permutation, cube history and storage formats are adaptations for Physical Sky.
