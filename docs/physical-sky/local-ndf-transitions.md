# Local cloud scene transitions

## Authored state

A scene stores one atomic local-cloud state at `PhysicalSky/cloudMap/localCloud`.
Asset identity, worldspace, placement, altitude decoding and composition options
belong to that state. They cannot be interpolated as unrelated scalar entries.
The menu has one state editor, with explicit override/inherit and enable controls.
There is no instance list, priority stack, asset-slot limit or external manifest.

## Scene Manager

The catalog supports a `Blend` value and editor semantic backed by `SceneBlend`.
It is a persisted, transitionable atomic value. A serialized value has a `states`
array, each element containing a complete `value` object and a nonnegative finite
`weight`; total weight is at most one (within floating-point tolerance).
An empty array is the identity state.

The ordinary baseline, time-of-day, weather, location and user-overwrite ordering
is retained. Period resolution accumulates endpoint weights using the same time
factors as numeric settings. Weather resolution then blends those distributions
using the weather factor. Equal state objects merge instead of becoming duplicate
render endpoints. Thus overlapping weather and period transitions retain all
contributing resources, not merely whichever two entries were resolved last.
The feature-independent resolver does not interpret cloud resource names or
placement data. Numeric weather interpolation keeps its existing formula.

Scene saving, loading, copying, overwrite traversal, baseline restoration and
export treat the blend envelope as one value. Cloud DDS files remain ordinary
texture-mod dependencies under `Textures/PhysicalSky/LocalNdf`; the scene pack
exports settings, not those asset directories.

Flat location changes apply this resource state immediately; the existing
location movement easing remains for scalar settings. Weather and time-of-day
transitions blend it continuously. A new weather uses the endpoints and weights
provided by Scene Manager; no independent cloud weather clock or scheduler is
introduced. The implementation does not add an interruption policy beyond the
existing weather resolver.

## Rendering

Each active endpoint is validated independently and cached in a texture-array
slice. The current worldspace filters compatible endpoints. Missing/invalid
resources fall back to the global NDF for their share of the transition.
Resource loading uses the existing synchronous texture cache; weight-only ticks
neither reload DDS files nor dispatch the local map generator.

All slices share the union footprint and altitude envelope. Encoded heights are
converted into that common physical interval. Each endpoint first applies its
Interpolate or Maximum operator to the current global NDF, then the resulting
control fields are blended by scene weights. Mixing maximum floors before
applying the operator would produce incorrect intermediate cloud shapes.

Different transforms crossfade their stationary world-space fields. This avoids
interpolating incompatible rotations, dimensions or DDS height encodings, and
requires no artificial patch velocity. Fading to a disabled or empty state
restores global clouds continuously. This is a control-field transition, not a
crossfade of independently lit volumes or a guarantee of topology-preserving
cloud morphing.

A structured weight buffer changes during blending; cached slice content changes
only with endpoint identity/content, source revision or atlas settings. Local
weights are excluded from the whole-cloud history-reset key. Explicit authoring
edits still reset history. Debug exposes endpoint weights, cached slices and the
source textures. Simultaneous weather/time blends can cost more than a stable
single state because each contributing operator must be evaluated.

## Shared weather shape

`bottomSpreadScale`, `bottomSpreadHeight`, `bottomSoftness`,
`bottomSoftnessHeight`, `topExpansionScale` and `densityScale` remain independent
Scene Manager scalar controls. They resolve with curvature, global NDF controls,
wind and cirrus state before GPU coefficient conversion. Local endpoint objects
carry NDF placement/composition only; they do not define independently lit or
independently shaped weather volumes.
