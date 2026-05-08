# ISHDR Bloom Highlight Regression — Legacy Weather Mods

## Summary

After commit `e9190e6c` (feat: HDR rendering, #1692), older weather mods that do **not**
enable the filmic tonemapper toggle in ISHDR (`Param.z ≤ 0.5`) exhibit blown-out highlights
and clipping. Mods that use the filmic path are unaffected because they take a different
code path that was not changed.

---

## Root Cause

### Primary: bloom additive formula on the Reinhard branch

The commit changed the bloom-addition expression inside the non-filmic (Reinhard) `BLEND`
branch of `package/Shaders/ISHDR.hlsl`.

**Pre-commit:**
```hlsl
blendedColor += saturate(Param.x - blendedColor) * bloomColor;
```

**Post-commit:**
```hlsl
blendedColor += saturate(Param.x - (1.0 - exp2(-blendedColor))) * bloomColor;
```

The filmic branch inside `package/Shaders/Common/DisplayMapping.hlsli` was **not** changed
and still uses the old form:
```hlsl
perChannelCompressed += saturate(Param.x - perChannelCompressed) * bloomCol;
```

### Why the new expression blows out highlights

`1 - exp2(-x)` is a soft-saturation curve bounded to `[0, 1]`.  For any `x > 0` it is
strictly *less* than `x`, so the subtrahend is always smaller than in the old formula and
the bloom-add factor is always larger — most severely in the highlights:

| post-tonemap brightness `x` | old `saturate(0.5 − x)` | new `saturate(0.5 − (1−2⁻ˣ))` |
|---|---|---|
| 0.3 | 0.20 | 0.31 (+56 %) |
| 0.5 | **0.00 (cutoff)** | 0.21 |
| 0.7 | 0.00 | 0.12 |
| 1.0+ | 0.00 | ~0 |

For `Param.x ≥ 1` — common in legacy ImageSpace records authored before filmic existed —
the old expression gave a clean, hard shoulder at `blendedColor = Param.x`.  The new
soft-shoulder adds bloom **on top of already-tonemapped highlights**, producing clipping.

Legacy weather mods had their bloom-intensity values tuned against the linear cutoff.  The
new behaviour invalidates that calibration for every mod that stays on the Reinhard path.

---

## Contributing (non-root-cause) changes in the same commit

These changes were either correctly gated or did not affect SDR highlights:

| Change | File | Impact |
|---|---|---|
| Reinhard HDR piecewise extension | `ISHDR.hlsl` | Gated on `isHDR && p < 1` — SDR unaffected |
| Power-law contrast rework | `ISHDR.hlsl` | Lerp falls back to old linear-lerp in highlights (value > 0.1) |
| LL+gamma gamma direction swap | `ISHDR.hlsl` | Only affects LL+gamma-correction combo; orthogonal to filmic toggle |
| Sky HDR sun boost | `Sky.hlsl` | Fully gated on `HDRData.x > 0.5` |
| Lighting HDR_OUTPUT gate | `Lighting.hlsl` | SDR pipeline unchanged |

---

## Fix Applied

The bloom expression on the Reinhard path is now gated so SDR users restore the original
hard-shoulder behaviour while HDR users keep the soft-shoulder:

```hlsl
// package/Shaders/ISHDR.hlsl — Reinhard BLEND branch
float bloomMask = isHDR ? saturate(Param.x - (1.0 - exp2(-blendedColor)))
                        : saturate(Param.x - blendedColor);
blendedColor += bloomMask * bloomColor;
```

This preserves the SDR / legacy-weather pixel output bit-for-bit versus the pre-HDR
codebase while keeping the HDR-targeted soft-shoulder for HDR displays.

---

## Reproduction

1. Load a legacy weather mod that sets filmic-toggle OFF in its ImageSpace record.
2. Observe blown-out sun, sky, and bright interior highlights.
3. With the fix, highlights roll off cleanly as they did pre-`e9190e6c`.
