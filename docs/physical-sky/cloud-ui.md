# Volumetric Cloud UI

The Clouds page is organised around how a weather artist builds a sky, not around the shader's
constant layout. The page keeps the most-used controls open, folds global or legacy controls away, and
shows a live altitude profile so the deck and cirrus heights can be read at a glance.

## Authoring order

1. **Weather Map** — decide where cloud masses form, which cloud types dominate, and how their
   heights vary. This is the macro shape of the sky.
2. **Low Clouds** — place that weather in the sky (base altitude and height span), set the horizontal
   scale and curvature, then shape the silhouette (bottom spread, softness, top expansion) and set
   density (extinction).
3. **Local Cloud** — place one authored cloud map in the world for hero shapes.
4. **Cirrus** — add the high ice-cloud veil, with its own coverage/type weather and lighting.
5. **Cloud Motion** — set the wind vector, internal development and disturbance.
6. **Lighting** — tune brightness, directional/view extinction, scattering, ambient response and the
   advanced source integration.
7. **Quality & Performance** — ray range, shadow-volume range and march step size. A shorter range and
   larger steps cost less; smaller steps keep more detail at a higher cost.
8. **Vanilla Clouds** — the legacy geometry path and its relighting mixes, kept last because it is a
   separate renderer.

The order is deliberate: distribution first, then physical placement and shape, then the second layer,
then animation and light. It also keeps the expensive quality controls out of the way until the look
is worth paying for.

## Page layout

| Section               | Default  | Scope       | Notes                                                                |
| --------------------- | -------- | ----------- | -------------------------------------------------------------------- |
| Status header         | Always   | Mixed       | Enable toggle, readiness badge, scene note and altitude profile.     |
| Weather Map           | Open     | Scene       | Coverage/type/height parameters. Map source, noise and preview fold. |
| Low Clouds            | Open     | Scene       | Altitude & scale, shape and density sub-groups.                      |
| Local Cloud           | Closed   | Scene       | One complete local cloud state per scene.                            |
| Cirrus                | Enabled? | Scene       | Altitude, density, weather, optional local map and cirrus lighting.  |
| Cloud Motion          | Open     | Scene       | X/Y velocities, development, disturbance and a live speed readout.   |
| Cloud Lighting        | Open     | Scene       | Intensity, scattering, sky/ground light and advanced source details. |
| Quality & Performance | Closed   | Global only | Hidden while editing a scene.                                        |
| Vanilla Clouds        | Off/on\* | Mixed       | Toggle is scene-authorable; relighting mixes are global and hidden.  |

\* The vanilla section opens by default when volumetric clouds are disabled, so a vanilla-only setup
does not start with an empty page.

All sections use `ImGui::CollapsingHeader` with `ImGuiTreeNodeFlags_SpanAvailWidth`. The open/closed
state is remembered by ImGui; the first-use default only picks the initial state. Section labels go
through the translation system like every other control.

## Status header and altitude profile

- The **Enable Volumetric Clouds** toggle sits at the top with a `Ready` / `Not Ready` badge while
  the feature is on. The badge reflects the same `ShadersOK()` check the renderer uses.
- The **altitude profile** is drawn with `ImDrawList` from the live settings: a filled band for the
  low deck, a line for cirrus, and a ground line. It is not an input; it is a readout that makes the
  altitude sliders and the cirrus height legible together.
- The low-cloud band doubles as a **slab preview**. The generated cloud image is drawn directly into
  that band, between the base and top altitudes; the band fill is dimmed behind it and the low-cloud
  label gets a small chip so it stays readable over the clouds.
- In a scene page, a note states what is being edited and that global map/noise/quality settings are
  hidden. If volumetric clouds are globally off, a warning says to enable them in the feature page.

### Low-cloud slab preview

`CloudCrossSection.cs.hlsl` composites **eight parallel density slices along Y** into one 512x160
texture, which is drawn inside the altitude profile's low-cloud band:

- Horizontal axis: one NDF tile centred on the camera.
- Depth axis: one NDF tile along Y, compressed into the same image by the eight-slice composite, so
  the preview reads as a cloud field rather than one sparse section.
- Vertical axis: the low-cloud base-to-top range.
- Source data: the generated NDF height/modeling maps, the profile LUT and the top-expansion LUT.
- Excluded by design: local clouds, wind, evolution, detail shape noise and lighting. The result is
  a smooth, static density preview of the authored coverage and vertical profile.
- The preview refreshes when the settings change or the camera moves 32 m; otherwise it is static.
- `cloudCrossSectionDirty` / `cloudCrossSectionReady` gate the dispatch. If the shader or texture is
  unavailable, the altitude profile falls back to its plain altitude band.

## Scene Manager

The Scene Manager replays this same page through `SceneWidgetInterceptor`. The Cloud page therefore
keeps the Scene Manager surface automatically:

- Scene-authorable controls stay ordinary ImGui widgets, so the interceptor binds them and attaches
  its gutter. The page does not add a second authoring path.
- Global-only blocks are omitted while `SceneWidgetInterceptor::IsArmed()` is true: map source and
  noise, the weather-map preview, quality/performance and the vanilla relighting mixes.
- The vanilla on/off switch remains visible because the scene policy allows cloud switches in
  interior and non-periodic location contexts.
- The local cloud and cirrus weather states keep their own scene-override buttons, because each is a
  single aggregate `SceneBlend` value rather than a list of independent controls.
- Section headers are `CollapsingHeader` scopes, so the generated catalog records them in
  `displayPath` / `displayPathKeys`; the Scene Manager copy UI and feature list show the same names
  as the feature page.
- Wind keeps direct X/Y controls, so the catalog and baseline editor bind the same stored values
  without angle wraparound ambiguity.

See [scene-settings-framework.md](../development/scene-settings-framework.md#physical-sky) for the
policy table and the scene-apply contract.

## Implementation map

- `PhysicalSky::DrawCloudStatus` — enable toggle, readiness badge and scene notes.
- `PhysicalSky::DrawCloudLayerProfile` — the altitude diagram and the slab image drawn in its low band.
- `PhysicalSky::RenderCloudCrossSection` — the low-cloud slab dispatch.
- `CloudCrossSection.cs.hlsl` — the eight-slice static slab shader (NDF + profile LUT, no noise or lighting).
- `PhysicalSky::DrawWeatherMapSection` — NDF shape plus the global map-source/noise/preview folds.
- `PhysicalSky::DrawLowCloudSection` — altitude, shape and density.
- `PhysicalSky::SettingsVolumetricClouds` — local cloud, cirrus, motion, lighting, quality, in order.
- `NdfManager::DrawNdfSource` / `DrawNdfShape` / `DrawNdfOverlay` / `DrawNdfNoise` — split out of the
  former monolithic map panel so the page can place them.
- `CirrusMapManager::DrawSettings` — cirrus body, weather state and cirrus lighting.
