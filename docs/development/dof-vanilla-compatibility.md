# DoF vanilla compatibility

The optional **Vanilla Compatibility Mode** switch in Post Processing > Depth of
Field defaults to off. It derives the signed CoC used by the existing gather
pipeline from Skyrim's DoF controls. Saved physical lens, focus and bokeh settings
are preserved. Gather quality remains configurable. The game's own DoF enable
state still applies.

## Parameter source and mapping

Use `ImageSpaceManager::GetImageSpaceData().modData` for the resolved strength,
distance, range and mode. These are the inputs read by the native DoF compositor;
do not blend in the weather/base ImageSpace a second time or multiply by
`modAmount`. This also follows temporary ImageSpace modifiers.

| Native control | Community Shaders mapping                                                                     |
| -------------- | --------------------------------------------------------------------------------------------- |
| Distance       | Manual focus plane; game units converted to metres/km                                         |
| Range          | Linear distance from focus at which the CoC reaches its maximum                               |
| Strength       | Scale of the depth-dependent CoC                                                              |
| Mode bits 0–1  | Front/back, front only, back only, neither                                                    |
| Mode bit 2     | Exclude sky, including native nine-tap sky-edge attenuation                                   |
| Mode bits 3–6  | Circular maximum radius based on the native downsampled target; encoded zero selects radius 3 |
| Mode bit 7     | Dynamic focus and interpolation of the near/far focus range and strength                      |

Dynamic focus samples the average-depth render target selected by the native
effect. Until that target is available, it samples the screen centre. Native
focus adaptation is not filtered a second time. Dynamic parameters use the game's
`fDynamicDOF*` settings, including its blur multiplier. When native HDR DoF has
already updated this frame, the actual compositor constants take precedence,
including `ConfigureDDOF` changes. Native LDR DoF executes after our pipeline and
therefore uses the INI values and the last selected focus target.

The native first-person/world depth remapping is used for this mode. Physical
camera and target-focus overrides do not apply. Highlight boosts, custom bokeh,
Petzval distortion and post smoothing are neutralised for rendering only.

## Native pass coexistence

The `ImageSpaceEffectDepthOfField::UpdateParams` hook lets native preparation run,
then zeros only the normal compositor's blur strengths while the replacement is
ready and runnable. Dynamic DoF has a second strength in `params2.w`; static DoF
uses that slot as a sky flag, so it must not be cleared indiscriminately. The hook
never edits ImageSpace records or the game's DoF enable flag. Each native update
repopulates the constants, allowing immediate fallback when the mode is disabled,
the pipeline is bypassed or shaders are unavailable.

Fully underwater and waterline/masked passes remain native, preserving their fog
and per-pixel ImageSpace blending. The CS compatibility pass does not run there.
Loading screens, UI rendering and Effects 11 tonemap ownership also retain native
behaviour.

## Verification basis and limits

Read-only Ghidra analysis of Skyrim SE 1.6.1170 confirmed:

-   `ImageSpaceEffectDepthOfField::UpdateParams` at `0x1414e4d80`: resolved inputs,
    mode decoding, dynamic interpolation and native shader constant layout.
-   `IsActive` at `0x1414e4d00`: effect enable state and strength gate.
-   `BorrowTextures` at `0x1414e52a0`: average-depth and downsampled blur targets.
-   `ReturnTextures` at `0x1414e5520`: the average-depth target descriptor persists.
-   ImageSpace rendering at `0x141481b80`: HDR DoF runs before tonemapping; LDR DoF
    runs afterwards.

The depth ramp follows `package/Shaders/ISDepthOfField.hlsl`. Native DoF blends a
fixed Gaussian image with the sharp scene; CS instead varies a circular gather
radius. Their kernels and compositing are intentionally only an approximation,
not a pixel-identical reproduction. LDR dynamic focus can also use the previous
selection of the native focus target. No additional user tuning parameters are
introduced. The implementation still requires in-game visual validation; no
build or runtime test is part of this change's static verification.
