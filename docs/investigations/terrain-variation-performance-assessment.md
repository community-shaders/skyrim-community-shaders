# Terrain Variation — Performance Optimization Assessment

## Architecture Overview

The Terrain Variation feature implements **stochastic texture sampling** (Deliot & Heitz, "Procedural Stochastic Textures by Tiling and Blending") to break up repeating tile patterns on terrain. It only applies to `RE::BSShader::Type::Lighting` shaders and uses a dual-toggle architecture:

- **Compile-time**: `#if defined(TERRAIN_VARIATION)` — gates code inclusion in shader binaries
- **Runtime**: `useTerrainVariation` / `enableTilingFix` cbuffer bool — dynamic `[branch]` to enable/disable without recompilation

### Key Files

| File | Role |
|---|---|
| `features/Terrain Variation/Shaders/TerrainVariation/TerrainVariation.hlsli` | Core stochastic sampling algorithm (~205 lines) |
| `features/Extended Materials/Shaders/ExtendedMaterials/ExtendedMaterials.hlsli` | Parallax height, POM ray-march, & shadow integration |
| `package/Shaders/Lighting.hlsl` | All 6 landscape layer sampling + LOD + shadow paths |
| `src/Features/TerrainVariation.h` / `.cpp` | C++ feature class, settings, ImGui UI |
| `src/FeatureBuffer.cpp` | GPU cbuffer packing (16 bytes: 2 uints + 8 padding) |

### Shader Permutation Model

`TERRAIN_VARIATION` is a **global define** applied to all 523 Lighting permutations — it is **not** a permutation toggle and adds **zero** extra shader variants. However, only ~27 LANDSCAPE permutations contain meaningful code paths (the compiler dead-code-eliminates it from the other ~496 non-LANDSCAPE permutations).

---

## GPU Performance (FPS) Analysis

### The Core Cost: Texture Fetch Explosion

The single biggest performance concern is the **3× texture fetch multiplier** per terrain layer:

| Path | Without TV | With TV | Multiplier |
|---|---|---|---|
| **Per-layer diffuse** (`StochasticEffect`) | 1 `SampleBias` | 3 `SampleLevel` + `CalculateLevelOfDetail` | ~3.3× |
| **Per-layer normal** (same) | 1 `SampleBias` | 3 `SampleLevel` + LOD calc | ~3.3× |
| **Per-layer RMAOS** (TruePBR only) | 1 `SampleBias` | 3 `SampleLevel` + LOD calc | ~3.3× |
| **Parallax height per layer** (`StochasticEffectParallax`) | 1 `SampleLevel` | 3 `SampleLevel` | 3× |

With 6 landscape layers active, terrain variation can drive **up to 36 extra texture fetches** (diffuse + normal) on close-up terrain, or **54 extra** with TruePBR (+ RMAOS). In POM ray-marching, this cost is multiplied again by the number of POM steps.

### Specific GPU Bottlenecks

1. **`StochasticEffect` per-call overhead** — Each call:
   - `CalculateLevelOfDetail` (~1 cycle)
   - 3× `SampleLevel` (~4–8 cycles each on texture-bound GPUs)
   - Height-based weight computation: `pow()`, luminance dots, `NormalizeWeights` with `rcp` and a conditional branch
   - **Total: ~30–40 cycles per call**, vs ~4–8 cycles for a single `SampleBias`

2. **`StochasticEffectParallax` inside POM loop** — `GetTerrainHeight` is called **4× per POM step** (batched `currHeight.xyzw`). Each call invokes `StochasticEffectParallax` per active layer. With 6 layers, 3 samples each, that's **up to 72 texture fetches per POM iteration**, repeated for 4–16 steps.

3. **Redundant `CalculateLevelOfDetail`** — `StochasticEffect` calls `tex.CalculateLevelOfDetail(samp, uv)` every single time, even though the mip level is the same for all 6 layers at the same UV. This is recomputed 12–18 times when it could be done once.

4. **`NormalizeWeights` divergent branch** — Contains `if (abs(weightSum - 1.0) < 0.01)` early exit that causes GPU warp divergence. Both paths execute if any thread in the wave differs, so the branch rarely saves work.

5. **LOD path recomputes offsets** — The LODLANDSCAPE and LOD_LAND_BLEND paths each independently call `ddx`/`ddy` and `ComputeStochasticOffsetsLOD` when they could share computation.

---

## Compile Speed Analysis

### Good News: No Permutation Multiplication

`TERRAIN_VARIATION` adds **zero** extra shader variants. The total permutation count is unchanged at 523.

### Compile Concerns

1. **Massive code duplication in Lighting.hlsl** — The identical `[branch] if (useTerrainVariation) { StochasticEffect(...) } else { SampleBias(...) }` pattern is copy-pasted **27 times** (diffuse × 6 + normal × 6 + RMAOS × 6 = 18 base blocks, plus parallax/shadow blocks). Each adds ~8 lines of nearly identical code.

2. **Duplicated `#if TERRAIN_VARIATION` guards in ExtendedMaterials.hlsli** — `GetTerrainHeight` exists in **two nearly identical copies** (TRUE_PBR and non-PBR), each with 6 layers × 2 branches = 24 switches. `GetParallaxSoftShadowMultiplierTerrain` is also duplicated.

3. **Included unconditionally in non-LANDSCAPE paths** — The `#include "TerrainVariation/TerrainVariation.hlsli"` in Lighting.hlsl is guarded only by `#if defined(TERRAIN_VARIATION)`, not `#if defined(TERRAIN_VARIATION) && defined(LANDSCAPE)`. FXC preprocesses the full TerrainVariation.hlsli + Random.hlsli include chain for all 523 permutations, even though only 27 use it.

---

## Optimization Recommendations

### 🟢 High Impact — FPS

| # | Optimization | Expected Gain | Effort |
|---|---|---|---|
| **F1** | **Distance-based sample count reduction** — `StochasticEffect` always takes 3 samples. At medium distance (mip > 2), fall back to 2 samples; at far distance (mip > 4), use 1 sample. The constants `DISTANCE_SAMPLE_REDUCTION` and `FAR_DISTANCE_THRESHOLD` exist but are **never used**. | **15–30% FPS recovery** on terrain | Low |
| **F2** | **Hoist `CalculateLevelOfDetail` out of `StochasticEffect`** — Compute the mip level once per layer set in Lighting.hlsl and pass it in (like `StochasticEffectParallax` already does). Eliminates 12–18 redundant LOD calculations per pixel. | **5–10% per-pixel savings** | Low |
| **F3** | **Skip normal stochastic sampling** — Normal maps are far less susceptible to visible tiling than diffuse/color. Sampling normals with `SampleBias` while applying stochastic only to diffuse would cut texture fetches by ~33% with minimal visual difference. | **~33% texture fetch reduction** | Low |
| **F4** | **Remove the `NormalizeWeights` branch** — Replace the early-exit branch with unconditional `weights * rcp(max(sum, 1e-6))`. Eliminates warp divergence on GPUs for near-zero savings. | Minor but free | Trivial |
| **F5** | **Reduce `StochasticEffectParallax` to 2 samples** — In POM ray-marching, precision matters less since it's already approximate. Using 2 samples instead of 3 saves ~33% of POM texture fetches. The LOD path already does this. | **Significant in POM-heavy scenes** | Low |
| **F6** | **Skip terrain variation for low-weight layers** — Layers with `LandBlendWeight < 0.1` are barely visible. Apply stochastic sampling only to the top 2–3 layers by weight, using standard sampling for minor layers. | **20–40% savings in multi-layer terrain** | Medium |

### 🟡 Medium Impact — Compile Speed

| # | Optimization | Expected Gain | Effort |
|---|---|---|---|
| **C1** | **Guard the include properly** — Change Lighting.hlsl from `#if defined(TERRAIN_VARIATION)` to `#if defined(TERRAIN_VARIATION) && defined(LANDSCAPE)`. Prevents FXC from processing TerrainVariation.hlsli + Random.hlsli for 496 non-LANDSCAPE permutations. | **Saves preprocessing for ~95% of permutations** | Trivial |
| **C2** | **Create a `StochasticSample` wrapper** — Replace the 27 copy-pasted `[branch] if (useTerrainVariation) { ... } else { ... }` blocks with a single inline helper. Reduces FXC parse/optimize workload and massively improves maintainability. | **~200 fewer LOC in LANDSCAPE permutations** | Medium |
| **C3** | **Merge the PBR/non-PBR `GetTerrainHeight` bodies** — Both versions have identical TERRAIN_VARIATION integration. Factor the `#if defined(TERRAIN_VARIATION)` branches into a shared inner function. | **Halves TV code surface in ExtendedMaterials.hlsli** | Medium |
| **C4** | **Consolidate `GetParallaxSoftShadowMultiplierTerrain` TV branches** — Factor the shadow contrast logic into a shared helper. | **~50 fewer lines of duplication** | Low |

### 🔵 Architecture Improvements

| # | Optimization | Benefit |
|---|---|---|
| **A1** | **Use the unused distance thresholds** — Same as F1; the constants were designed for this purpose but never wired up. | Unlocks progressive quality degradation |
| **A2** | **Consider making TERRAIN_VARIATION a LANDSCAPE-only define** — Either check for LANDSCAPE in `HasShaderDefine`, or fix at the shader level via C1. | Cleaner architecture |
| **A3** | **Pre-compute stochastic offsets in vertex shader** — `ComputeStochasticOffsets` depends only on `input.TexCoord0.zw` (interpolated landscape UVs). Moving this to the vertex shader and passing offsets via interpolators eliminates per-pixel hash computation. | Moves ALU from pixel to vertex shader |

---

## Summary: Top 3 Highest-Impact Changes

1. **F1 (Distance-based sample reduction)** — The infrastructure already exists in the constants. Implementing it would be the single biggest FPS improvement, reducing 3 → 2 → 1 samples based on mip level/distance.

2. **C1 (Guard include with `LANDSCAPE`)** — A one-line change that prevents ~496 unnecessary preprocessor expansions of TerrainVariation.hlsli + Random.hlsli.

3. **C2 (Helper function for sampling pattern)** — Eliminates ~200 lines of copy-pasted code from Lighting.hlsl, improving both compile time and long-term maintainability.
