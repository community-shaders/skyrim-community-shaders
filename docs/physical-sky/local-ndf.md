# Local cloud NDF assets

## Asset layout

Local clouds use optional, world-anchored NDF patches. They work with either the
procedural or imported global NDF. No cloud DDS is required for ordinary clouds.

```text
Data/Textures/PhysicalSky/
  LocalNdf/
    MyCloud/
      Height.dds     (required only with nonzero height influence)
      Modeling.dds
      Mask.dds       (optional)
```

An asset ID is one folder name using ASCII letters, digits, `_` or `-`. It is not
a filesystem path. There is no per-asset manifest. Use distinct IDs ignoring case.
Assets can be reused by different scene states, and can be installed as ordinary
texture mods. Scan Asset Folders discovers directories; Reload Cloud Textures
reloads selected inputs, retries failed loads, and rescans directories. Loading
failures show the path and HRESULT in Texture Inputs. A bad local pair skips that
endpoint; it never replaces the global procedural fallback with a null texture.

| Input          | Channels | Meaning                                           |
| -------------- | -------- | ------------------------------------------------- |
| Height         | RG       | Bottom and top, normalized to 0–1                 |
| Modeling       | RGB      | Coverage, top profile type, bottom profile type   |
| Modeling       | A        | Interpolation/height mask; ignored by RGB Maximum |
| Mask, optional | R        | Multiplier on the influence mask                  |

When height influence is nonzero, Height and Modeling must have matching dimensions and be linear, single 2D
textures. UNORM/float and suitable BC formats are accepted; sRGB, integer,
SNORM, arrays, cubemaps and volumes are rejected. Mask dimensions may differ;
it uses the same normalized UVs. BC5 Height and BC7 Modeling with full mip chains
are supported directly. RGB-only Modeling has alpha 1. Disable Use Modeling Alpha
as Mask if A contains unrelated data. An explicitly present but invalid Mask
rejects the endpoint instead of silently removing its influence constraint.
With zero height influence, Height.dds is neither loaded nor validated, and its
altitude/span do not expand the cloud height envelope. Modeling remains required.

Empty and reversed height intervals are empty cloud, not automatically reordered.
In Interpolate mode, transparent influence leaves the underlying field unchanged. Zero coverage with
nonzero influence clears cloud coverage; coverage must not be reused as a mask.
Maximum mode never clears a channel: it takes the larger value of the existing
channel and the weighted source. RGB remains effective even where Modeling A is zero.
Alpha interpolation preserves the background NDF where alpha is zero; disabling
alpha at full strength replaces it, including clearing coverage where source R
is zero. Height influence uses the same alpha, so a taller background interval
remains outside the mask and transitions toward the source interval at its edge.
These are control-field operations, not alpha blending of rendered cloud volumes.

Maximum can combine coverage from one input with top/bottom types from another.
It retains the background height when height influence is zero. Consequently a
local high top type over a tall background interval can produce a much taller
cloud than the same source with full height replacement. Maximum does not mean
a union of independently rendered clouds. An authored overlay needs its
background weather's heights and shape controls as well as its texture and blend
mode. The [weather shape controls](noise-contract.md#weather-shape-controls) apply
to every instance; they are not inferred from alpha or supplied per instance.

No vertical flip is performed on NDF input. UV (0,0) is the negative local X/Y
corner, and positive rotation turns local +X toward world +Y.

## Placement and persistence

Each scene edits one complete local cloud state: asset ID, worldspace, enabled,
centre, size, rotation, altitude/span, strength, modeling/height influence,
modeling alpha, blend mode and edge feather. There are no instance IDs, names,
priorities or ordered layout lists. The Global Generator Overlay remains a
separate tileable input to global map generation.

Positions and dimensions use metres; altitude is relative to the atmosphere
base for the selected worldspace. Rotation is around world Z. The cloud remains
anchored independently of global weather advection. Move to Camera also assigns
the current worldspace. The stationary Maximum preset retains its existing
world-origin, 16384 m, zero-height-influence behavior.

Physical Sky stores the state under `cloudMap.localCloud`. Its atomic scene
value is a `SceneBlend` envelope containing weighted endpoint objects. The
editor writes one endpoint with weight 1. An empty envelope, or a disabled
endpoint, supplies no local contribution. Disabled states retain their placement
for later editing. An old `instances` list is no longer read; existing global
map settings are retained when loading version 2. The new map schema is version 3.

In Scene Manager, open Physical Sky's Clouds tab, expand Local Cloud, and choose
Override Local Cloud in This Scene. Edit the resource and placement there.
Remove Override restores inheritance; disabling the cloud instead explicitly
selects a state without local clouds. Settings use the existing scene save,
copy, overwrite and weather/time-of-day paths; no per-asset JSON is introduced.

`PhysicalSky::SetLocalCloudState(const LocalNdfState&)` sets a single baseline
state immediately. Weather/time interpolation belongs to Scene Manager; callers
must not repeatedly invoke this API with partially interpolated resource IDs.

## Composition and renderer contract

For mask and feather combined as `m`, source alpha `a` (or 1 when disabled),
strength `s`, modeling influence `wm`, and height influence `wh`, an endpoint is:

```text
Interpolate: F(X) = X * (1 - m*a*s*wm) + source.rgb * m*a*s*wm
Maximum:     F(X) = max(X, source.rgb * m*s*wm)
Height:      H(Z) = Z * (1 - m*a*s*wh) + sourceAbsoluteHeight * m*a*s*wh
```

Scene weights are independent of these authoring influences. For endpoint
weights `ti`, rendering evaluates `X + sum(ti * (Fi(X) - X))`. Any missing
weight retains the global field. In particular Maximum is applied before the
endpoint blend. Blending maximum floors before applying `max` is not equivalent.
A failed asset or an endpoint in another worldspace contributes the global field
at its original weight; other endpoints are not renormalized to hide the failure.

The renderer caches three RGBA16_FLOAT texture arrays, one slice per valid
endpoint: height RG / influence B, modeling additive RGB / opacity A, and maximum
floor RGB. Height values are normalized to the common altitude envelope, then
decoded to absolute metres before blending with global heights. They are not
independently lerped in incompatible encoded ranges. Source mips follow the
atlas footprint; endpoint slices use bilinear mip 0. No typed UAV reads are used.

The finite atlas encloses the active rotated rectangles with guard texels and
is independent of the camera. Requested texel size defaults to 32 m, with a
maximum dimension of 2048. Inputs, placement, source revision, shader reload or
endpoint membership changes rebuild the cached slices. Weight changes only
update a small structured buffer. Three arrays replace the old six ping-pong
textures; memory and density sampling cost scale with active transition endpoints.
Debug preview copies are allocated only when their resources are inspected.

Main view, cubemap, shadows and height bounds use the same endpoint weights and
union altitude envelope. Rays outside the local footprint skip local samples.
Zero height influence skips Height.dds and does not expand altitude bounds.
Local model sampling retains the common shear and upstream density conventions.
Temporal motion weights local anchoring by each endpoint's influence. Automatic
local-state weight changes do not invalidate cloud history; direct menu edits do.

This blends NDF control fields, not separately rendered cloud radiance. Different
positions crossfade in world space instead of making a patch travel between
positions. Global coverage-driven height variation is not regenerated from local
coverage. Source filtering and finite support do not promise bitwise equivalence
to a fully baked repeating global NDF.

## Debug and validation

Debug > Cloud Shape > Local NDF Resources shows every active endpoint's weight,
height, modeling and maximum slices, effective texel size, common rectangle and
altitude interval. Source Height, Modeling and Mask views remain available.
Both English and Simplified Chinese labels are provided.

Inspect and stage source assets with:

```powershell
python tools/validate-local-ndf.py HeightInput.dds ModelingInput.dds --output build/local-ndf-check --stage-root build/local-ndf-test/Data/Textures/PhysicalSky/LocalNdf/MyCloud
```

Check endpoint composition and optional DDS channel semantics with:

```powershell
python tools/test-local-ndf-composition.py --modeling ModelingInput.dds --output build/local-ndf-composition-check
```

These CPU checks cover endpoint weights, identity/empty states, Maximum versus
Interpolate, independent height decoding, endpoint order and FP16 storage error.
The source catalog/policy tests cover atomic persistence and permission to blend.
They do not execute GPU shaders or establish frame time. Game validation should
cover simultaneous weather/time transitions, different placements and altitude
ranges, missing assets, worldspace changes, saved scene reload, and main/cubemap/
shadow agreement. See [scene transitions](local-ndf-transitions.md).
