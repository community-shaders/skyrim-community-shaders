// Physical Glare — Aperture transmittance and wavefront generation
// Community Shaders / Post Processing — Author: Jiaye, 2026
//
// Generates the complex-valued aperture function A(x,y) = t(x,y) · exp(iφ(x,y))
// where t is the amplitude transmittance and φ encodes wavefront aberrations.
// The Fraunhofer diffraction PSF is obtained as |FFT(A)|².
//
// Supported aperture geometries:
//   - Lens mode: regular N-polygon with optional blade roughness, dust
//     particles, and surface scratches.
//   - Eye mode: circular pupil with eyelash occlusion, scatter particles,
//     crystalline lens fiber gratings, Y-pattern suture lines, fiber cell
//     starburst (radial phase grating), and tear film phase perturbation.
//
// Wavefront aberrations (phase modulation):
//   - Fresnel defocus: φ_defocus = k · (r/R)²        [1, section 2.2]
//   - Spherical aberration: φ_SA = W₀₄₀ · (r/R)⁴    (Seidel coefficient)
//   - Tear film: temporal angular harmonics            [1, section 3.1]
//   - Suture/starburst: radial phase gratings from fiber cell structure
//
// 4×4 sub-pixel supersampling for anti-aliased aperture edges [2].
//
// References:
//   [1] Delavennat (2021), Physically-based Real-time Glare, LiU.
//   [2] Ritschel et al. (2009), Temporal Glare, CGF 28(2).

RWTexture2D<float2> RWTexAperture : register(u0);

cbuffer GlareCB : register(b1)
{
	float Threshold;
	float Intensity;
	float ScatterStrength;
	uint ApertureMode;

	int ApertureBlades;
	float ApertureRotation;
	float AdaptSpeed;
	float DeltaTime;

	uint FFTResolution;
	float PaddingRatio;
	float ScreenWidth;
	float ScreenHeight;

	uint ChannelIndex;
	float FresnelExponent;
	float ChromaticSpread;
	float ApertureSize;

	float PSFSharpness;
	float PSFNoiseFloor;
	uint EnableEyelashes;
	float EyelashCurvature;

	uint EyelashCount;
	float EyelashLength;
	uint ParticleCount;
	float ParticleSize;

	uint GratingCount;
	float GratingStrength;
	float TearFilmStrength;
	float TearFilmSpeed;

	uint TearFilmComplexity;
	float TearFilmTime;
	uint SutureBranches;
	float SutureStrength;

	float SutureWidth;
	uint StarburstCount;
	float StarburstStrength;
	float StarburstIrregularity;

	uint DustCount;
	float DustSize;
	uint BladeRoughnessFreq;
	float BladeRoughnessAmp;

	uint ScratchCount;
	float ScratchOpacity;
	float ScratchLength;
	float ScratchWidth;

	float SphericalAberration;
	uint UseAP1;
	float2 _pad11;
};
static const float PI = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Eyelash occlusion mask [1, section 3.1, step 4, fig. 3.6]
//
// Models individual straight eyelash hairs extending radially inward from
// the upper and lower arcs of the circular pupil aperture.  Straight
// spatial-domain lines produce straight frequency-domain streaks after FFT;
// curvature is applied later via UV bending in the chromatic blur pass
// [1, fig. 3.7].
// ---------------------------------------------------------------------------
float EyelashMask(float2 pos, float radius)
{
	if (EyelashCount == 0)
		return 1.0;

	float mask = 1.0;
	uint totalLashes = EyelashCount;
	// Upper lashes: ~60% of count, lower lashes: ~40%
	uint upperCount = max(totalLashes * 3 / 5, 1u);
	uint lowerCount = max(totalLashes - upperCount, 1u);

	float lashLen = EyelashLength * radius;
	float lineHalfWidth = 1.2;  // pixels

	// Upper eyelashes: distributed along the top arc
	for (uint i = 0; i < upperCount; i++) {
		// Base angle along upper arc (roughly -70° to +70° from top)
		float t = (float(i) + 0.5) / float(upperCount);
		float baseAngle = PI * 0.5 + (t - 0.5) * PI * 0.78;  // ~140° arc at top

		// Base position on circular aperture edge
		float2 base = float2(cos(baseAngle), sin(baseAngle)) * radius;

		// Lash direction: radially inward toward center
		float2 dir = -normalize(base);

		// Point-to-straight-line-segment distance
		float2 toP = pos - base;
		float proj = dot(toP, dir);
		if (proj < 0.0 || proj > lashLen)
			continue;

		float2 closestPoint = base + dir * proj;
		float dist = length(pos - closestPoint);

		// AA: smoothstep fade over ~1.5px
		float paramT = proj / lashLen;
		float lashAlpha = 1.0 - smoothstep(lineHalfWidth - 0.75, lineHalfWidth + 0.75, dist);
		// Taper toward tip
		lashAlpha *= 1.0 - paramT * paramT;

		mask *= (1.0 - lashAlpha);
	}

	// Lower eyelashes: distributed along the bottom arc (shorter, less dense)
	for (uint j = 0; j < lowerCount; j++) {
		float t = (float(j) + 0.5) / float(lowerCount);
		float baseAngle = -PI * 0.5 + (t - 0.5) * PI * 0.56;  // ~100° arc at bottom

		float2 base = float2(cos(baseAngle), sin(baseAngle)) * radius;
		float2 dir = -normalize(base);

		// Lower lashes are shorter
		float lowerLen = lashLen * 0.6;

		float2 toP = pos - base;
		float proj = dot(toP, dir);
		if (proj < 0.0 || proj > lowerLen)
			continue;

		float2 closestPoint = base + dir * proj;
		float dist = length(pos - closestPoint);

		float paramT = proj / lowerLen;
		float lashAlpha = 1.0 - smoothstep(lineHalfWidth - 0.75, lineHalfWidth + 0.75, dist);
		lashAlpha *= 1.0 - paramT * paramT;

		mask *= (1.0 - lashAlpha);
	}

	return mask;
}

// ---------------------------------------------------------------------------
// Pseudo-random hash for deterministic particle placement (Wang hash)
// ---------------------------------------------------------------------------
float HashToFloat(uint seed)
{
	seed = (seed ^ 61u) ^ (seed >> 16u);
	seed *= 9u;
	seed = seed ^ (seed >> 4u);
	seed *= 0x27d4eb2du;
	seed = seed ^ (seed >> 15u);
	return float(seed) / 4294967295.0;
}

// Generate position of particle at given index within a disk of given radius.
// sqrt(random) gives uniform area distribution on a disk.
float2 ParticlePosition(uint index, float diskRadius)
{
	float angle = HashToFloat(index * 2u) * 2.0 * PI;
	float r = sqrt(HashToFloat(index * 2u + 1u)) * diskRadius;
	return float2(cos(angle), sin(angle)) * r;
}

[numthreads(8, 8, 1)] void CS_Aperture(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	float2 center = float2(FFTResolution, FFTResolution) * 0.5;
	float aspect = ScreenWidth / max(ScreenHeight, 1.0);
	float radius = float(FFTResolution) * ApertureSize;

	// ----- Supersampled aperture shape + Fresnel phase -----
	// 4×4 sub-pixel supersampling [2, p.17].
	// Uniform smoothstep edge with ±3 px transition for rotational
	// symmetry (directional edge width would introduce FFT artefacts).
#define APERTURE_SS 4
	float rcpSS = 1.0 / float(APERTURE_SS);
	float edgeW = 3.0;  // smoothstep half-width in pixels

	float2 complexSum = float2(0.0, 0.0);

	for (int sy = 0; sy < APERTURE_SS; sy++) {
		for (int sx = 0; sx < APERTURE_SS; sx++) {
			float2 subPos = float2(tid) + (float2(float(sx), float(sy)) + 0.5) * rcpSS - center;
			subPos.y *= aspect;

			float subR = length(subPos);
			float subValue = 0.0;

			if (ApertureMode == 1 || ApertureBlades <= 2) {
				subValue = 1.0 - smoothstep(radius - edgeW, radius + edgeW, subR);
			} else {
				float sectorAngle = 2.0 * PI / float(ApertureBlades);
				float rawAngle = atan2(subPos.y, subPos.x) - ApertureRotation;
				float localAngle = frac(rawAngle / sectorAngle) * sectorAngle - sectorAngle * 0.5;
				float apothem = radius * cos(sectorAngle * 0.5);

				// Blade edge roughness: micro-serrations on aperture blade edges
				if (ApertureMode == 0 && BladeRoughnessAmp > 0.0 && BladeRoughnessFreq > 0) {
					float bladeIdx = floor((rawAngle + PI) / sectorAngle);
					float edgeT = localAngle / (sectorAngle * 0.5);  // [-1,1] along blade edge
					uint bladeHash = uint(abs(bladeIdx) + 0.5) + 8000u;
					float roughFreq = float(min(BladeRoughnessFreq, 400u));
					float noise = 0.0;
					[unroll] for (int k = 0; k < 4; k++)
					{
						float freq = roughFreq * (1.0 + float(k) * 0.7);
						float ph = HashToFloat(bladeHash * 7u + uint(k) * 31u) * 2.0 * PI;
						noise += sin(edgeT * freq + ph) / float(k + 1);
					}
					apothem += noise * BladeRoughnessAmp * 1.5;
				}

				float projDist = subR * cos(localAngle);
				subValue = 1.0 - smoothstep(apothem - edgeW, apothem + edgeW, projDist);
			}
			float subR2 = dot(subPos, subPos);
			float R2 = radius * radius;
			float rNorm2 = subR2 / max(R2, 1.0);
			// Fresnel (r^2 defocus) + Seidel spherical aberration (r^4 lens curvature)
			float phase = FresnelExponent * rNorm2 + SphericalAberration * rNorm2 * rNorm2;

			complexSum += float2(subValue * cos(phase), subValue * sin(phase));
		}
	}

	complexSum *= (rcpSS * rcpSS);

	// ----- Tear film phase perturbation -----
	// The pre-corneal tear film (~3–5 µm) introduces time-dependent
	// optical path differences concentrated near the pupil edge,
	// producing temporally varying sharp diffraction spikes [1, section 3.1].
	// Eye mode only.
	if (ApertureMode == 1 && TearFilmStrength > 0.0) {
		float2 tfPos = float2(tid) + 0.5 - center;
		tfPos.y *= aspect;
		float r = length(tfPos);
		float theta = atan2(tfPos.y, tfPos.x);

		// Phase perturbation strongest in outer 40% of aperture
		float edgeFactor = smoothstep(radius * 0.55, radius * 0.95, r);

		// Sum angular harmonics with time-varying phases
		float phaseOffset = 0.0;
		uint complexity = min(TearFilmComplexity, 16u);
		for (uint h = 0; h < complexity; h++) {
			// Deterministic random amplitude and speed per harmonic
			float amp = HashToFloat(h * 3u + 7000u) * 0.6 + 0.4;
			float angularFreq = float(h + 2);
			float timeSpeed = (HashToFloat(h * 3u + 7001u) * 1.6 + 0.2) * TearFilmSpeed;
			float timePhase = HashToFloat(h * 3u + 7002u) * 2.0 * PI;
			phaseOffset += amp * sin(angularFreq * theta + timeSpeed * TearFilmTime + timePhase);
		}
		phaseOffset *= TearFilmStrength * edgeFactor * 2.5;

		// Apply as complex phase rotation
		float c = cos(phaseOffset);
		float s = sin(phaseOffset);
		complexSum = float2(complexSum.x * c - complexSum.y * s,
			complexSum.x * s + complexSum.y * c);
	}

	// ----- Eyelash & particle masks (centre texel only) -----
	// High-frequency amplitude features evaluated at texel centre;
	// their own diffraction patterns are intentional.
	float2 pos = float2(tid) + 0.5 - center;
	pos.y *= aspect;

	// Eyelash occlusion — eye mode only
	if (ApertureMode == 1 && EnableEyelashes != 0) {
		complexSum *= EyelashMask(pos, radius);
	}

	// Scatter particles — eye mode only
	if (ApertureMode == 1 && ParticleCount > 0) {
		uint pCount = min(ParticleCount, 1000u);
		float opacity = saturate(ScatterStrength);
		float particleMask = 1.0;
		for (uint p = 0; p < pCount; p++) {
			float2 ppos = ParticlePosition(p, radius * 0.92);
			float d = length(pos - ppos);
			float alpha = 1.0 - smoothstep(ParticleSize - 0.5, ParticleSize + 0.5, d);
			particleMask *= (1.0 - alpha * opacity);
		}
		complexSum *= particleMask;
	}

	// Lens dust — lens mode only
	if (ApertureMode == 0 && DustCount > 0) {
		uint pCount = min(DustCount, 1000u);
		float particleMask = 1.0;
		for (uint p = 0; p < pCount; p++) {
			float2 ppos = ParticlePosition(p, radius * 0.92);
			float d = length(pos - ppos);
			float alpha = 1.0 - smoothstep(DustSize - 0.5, DustSize + 0.5, d);
			particleMask *= (1.0 - alpha);
		}
		complexSum *= particleMask;
	}

	// Lens surface scratches — lens mode only.
	// Linear scratches produce perpendicular streak patterns after FFT.
	if (ApertureMode == 0 && ScratchCount > 0 && ScratchOpacity > 0.0) {
		uint scratchCnt = min(ScratchCount, 20u);
		float scratchLen = ScratchLength * radius * 2.0;
		float scratchHW = ScratchWidth * 0.5;
		float scratchOpc = saturate(ScratchOpacity);

		for (uint si = 0; si < scratchCnt; si++) {
			// Deterministic random position and angle per scratch
			float2 scratchCenter = float2(
				(HashToFloat(si * 3u + 9000u) - 0.5) * radius * 1.6,
				(HashToFloat(si * 3u + 9001u) - 0.5) * radius * 1.6);
			float scratchAngle = HashToFloat(si * 3u + 9002u) * PI;
			float2 scratchDir = float2(cos(scratchAngle), sin(scratchAngle));

			// Point-to-line-segment distance
			float2 toP = pos - scratchCenter;
			float proj = dot(toP, scratchDir);
			if (abs(proj) > scratchLen * 0.5)
				continue;

			float2 cp = scratchCenter + scratchDir * proj;
			float dist = length(pos - cp);
			float alpha = 1.0 - smoothstep(scratchHW - 0.5, scratchHW + 0.5, dist);

			// Taper toward ends for natural look
			float endFade = 1.0 - smoothstep(scratchLen * 0.35, scratchLen * 0.5, abs(proj));

			complexSum *= (1.0 - alpha * endFade * scratchOpc);
		}
	}

	// Crystalline lens fiber gratings [1, section 2.4, fig. 2.12]
	// ~200 radial structures with higher refractive index near the pupil
	// edge, producing a diffuse lenticular halo via edge diffraction.
	// Modelled as semi-opaque radial lines with radial falloff.
	// Eye mode only.
	if (ApertureMode == 1 && GratingCount > 0) {
		float r = length(pos);
		float angle = atan2(pos.y, pos.x);

		// Angular spacing between gratings
		float gratingCountF = float(min(GratingCount, 400u));
		float sectorAngle = 2.0 * PI / gratingCountF;

		// Fractional position within the nearest grating sector [-0.5, 0.5)
		float localAngle = frac(angle / sectorAngle + 0.5) - 0.5;

		// Arc distance from the nearest grating line centre (in pixels)
		float arcDist = abs(localAngle) * sectorAngle * r;

		// Thin line AA: each grating is ~1 pixel wide
		float lineAlpha = 1.0 - smoothstep(0.0, 1.5, arcDist);

		// Radial falloff: gratings affect mostly the outer ring of the lens
		// (paper: "higher refractive index near the edges")
		float edgeFade = smoothstep(radius * 0.4, radius, r);

		// Combined grating mask: reduce transmission at grating locations
		float gratingMask = 1.0 - lineAlpha * edgeFade * saturate(GratingStrength);
		complexSum *= gratingMask;
	}

	// Crystalline lens suture lines.
	// Elongated fiber cells meet at Y-shaped suture lines: N anterior
	// branches and N posterior branches rotated by half a sector.  The
	// refractive index discontinuity introduces phase shifts that produce
	// prominent star spikes after FFT.  Fewer and thicker than gratings,
	// sutures define the base starburst geometry of human glare.
	// Eye mode only.
	if (ApertureMode == 1 && SutureBranches > 0) {
		float r = length(pos);
		float angle = atan2(pos.y, pos.x);
		uint branches = min(SutureBranches, 8u);

		// Anterior sutures: N branches from center outward
		float sectorAngle = 2.0 * PI / float(branches);
		float suturePhase = 0.0;

		// Anterior branches (angle offset 0)
		{
			float localAngle = frac(angle / sectorAngle + 0.5) - 0.5;
			float arcDist = abs(localAngle) * sectorAngle * r;
			float lineAlpha = 1.0 - smoothstep(SutureWidth * 0.5 - 0.75, SutureWidth * 0.5 + 0.75, arcDist);
			// Sutures run from center to ~90% radius
			float radialMask = smoothstep(0.0, radius * 0.08, r) * (1.0 - smoothstep(radius * 0.85, radius * 0.95, r));
			suturePhase += lineAlpha * radialMask;
		}

		// Posterior branches: same count, rotated by half a sector (60° for N=3)
		{
			float rotAngle = angle - sectorAngle * 0.5;
			float localAngle = frac(rotAngle / sectorAngle + 0.5) - 0.5;
			float arcDist = abs(localAngle) * sectorAngle * r;
			float lineAlpha = 1.0 - smoothstep(SutureWidth * 0.5 - 0.75, SutureWidth * 0.5 + 0.75, arcDist);
			float radialMask = smoothstep(0.0, radius * 0.08, r) * (1.0 - smoothstep(radius * 0.85, radius * 0.95, r));
			suturePhase += lineAlpha * radialMask;
		}

		// Apply as phase shift (refractive index difference → optical path difference)
		float phase = suturePhase * SutureStrength * PI * 0.5;
		float sc = cos(phase);
		float ss = sin(phase);
		complexSum = float2(complexSum.x * sc - complexSum.y * ss,
			complexSum.x * ss + complexSum.y * sc);
	}

	// Fiber cell starburst (radial phase grating).
	// Thousands of radially arranged fiber cells create high-angular-
	// frequency refractive index discontinuities.  The FFT of these thin
	// radial phase lines produces numerous sharp radial spikes.
	// Unlike sutures (few thick spikes) or gratings (amplitude → diffuse
	// halo), this is high-count phase modulation → sharp thin spikes.
	// Eye mode only.
	if (ApertureMode == 1 && StarburstCount > 0) {
		float r = length(pos);
		float angle = atan2(pos.y, pos.x);
		uint spikeCount = min(StarburstCount, 128u);
		float spikeCountF = float(spikeCount);
		float sectorAngle = 2.0 * PI / spikeCountF;

		// O(1) nearest-sector lookup: check self + 2 neighbors for jitter safety
		float sectorIdx = angle / sectorAngle;
		float burstPhase = 0.0;

		for (int di = -1; di <= 1; di++) {
			int idx = int(floor(sectorIdx + 0.5)) + di;
			uint wrappedIdx = uint((idx % int(spikeCount) + int(spikeCount)) % int(spikeCount));

			// Per-fiber angular jitter: shifts each line off the regular grid
			float jitter = (HashToFloat(wrappedIdx * 2u + 5000u) - 0.5) * StarburstIrregularity;
			float fiberAngle = (float(idx) + jitter) * sectorAngle;

			// Arc distance to this fiber line (in pixels)
			float angleDiff = angle - fiberAngle;
			float arcDist = abs(angleDiff) * r;

			// Very thin line (~0.8px): sharp phase discontinuity → sharp PSF spikes
			float lineAlpha = 1.0 - smoothstep(0.0, 0.8, arcDist);

			// Per-fiber random strength variation for natural look
			float fiberStr = 1.0 - StarburstIrregularity * 0.5 * (1.0 - HashToFloat(wrappedIdx * 2u + 5001u));

			burstPhase += lineAlpha * fiberStr;
		}

		// Radial mask: present from near-center to outer edge
		float radialMask = smoothstep(radius * 0.05, radius * 0.3, r) * (1.0 - smoothstep(radius * 0.88, radius * 0.98, r));

		// Apply as phase modulation (refractive index difference)
		float phase = burstPhase * radialMask * StarburstStrength * PI;
		float cb = cos(phase);
		float sb = sin(phase);
		complexSum = float2(complexSum.x * cb - complexSum.y * sb,
			complexSum.x * sb + complexSum.y * cb);
	}

	RWTexAperture[tid] = complexSum;
}
