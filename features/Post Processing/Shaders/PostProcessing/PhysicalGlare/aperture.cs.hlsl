// PhysicalGlare - Aperture image generation
//
// Reference:
//   Delavennat, J. (2021). Physically-based Real-time Glare.
//   Master's thesis (LIU-ITN-TEK-A--21/068-SE), Linköping University.
//   https://www.diva-portal.org/smash/record.jsf?pid=diva2:1629565
//
// Renders a regular N-sided polygon (pupil aperture) as a white shape on black background.
// The Fourier transform of this image produces the monochromatic diffraction PSF.
// Pipeline: aperture → FFT → |F|² = diffraction pattern.

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
	float RcpFFTResolution;
	float ScreenWidth;
	float ScreenHeight;

	uint ChannelIndex;
	uint EnableEyelashes;
	uint EyelashCount;
	float EyelashLength;

	float EyelashCurvature;
	float FresnelExponent;
	float ChromaticSpread;
	float ApertureSize;

	uint ParticleCount;
	float ParticleSize;
	uint GratingCount;
	float GratingStrength;
};

static const float PI = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Eyelash occlusion mask (paper section 3.1 step 4, fig 3.6)
// Models individual STRAIGHT eyelash hairs extending from the top and bottom
// edges of the aperture.  Straight lines produce straight streaks after FFT;
// curvature is added later via UV bending in the chromatic blur shader
// (paper fig 3.7: sin(x) vertical offset in psf.cs.hlsl).
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
// 4×4 sub-pixel supersampling (paper p.17: Ritschel recommendation).
// Uniform smoothstep edge in distorted space.  The ±3px transition
// suppresses grid aliasing while keeping the aperture rotationally
// symmetric — any angular modulation of edge width would itself
// introduce directional FFT artifacts.
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
				float projDist = subR * cos(localAngle);
				subValue = 1.0 - smoothstep(apothem - edgeW, apothem + edgeW, projDist);
			}
			float subR2 = dot(subPos, subPos);
			float R2 = radius * radius;
			float phase = FresnelExponent * subR2 / max(R2, 1.0);

			complexSum += float2(subValue * cos(phase), subValue * sin(phase));
		}
	}

	complexSum *= (rcpSS * rcpSS);

	// ----- Eyelash & particle masks (centre position only) -----
	// High-frequency features that intentionally create their own
	// diffraction patterns; evaluated once at texel centre.
	float2 pos = float2(tid) + 0.5 - center;
	pos.y *= aspect;

	if (EnableEyelashes != 0) {
		complexSum *= EyelashMask(pos, radius);
	}

	// Intraocular scatter particles (paper section 2.4).
	// Pupil mode only — camera lenses don't have biological particles.
	if (ApertureMode == 1 && ParticleCount > 0) {
		uint pCount = min(ParticleCount, 1000u);
		float particleMask = 1.0;
		for (uint p = 0; p < pCount; p++) {
			float2 ppos = ParticlePosition(p, radius * 0.92);
			float d = length(pos - ppos);
			float alpha = 1.0 - smoothstep(ParticleSize - 0.5, ParticleSize + 0.5, d);
			particleMask *= (1.0 - alpha * saturate(ScatterStrength));
		}
		complexSum *= particleMask;
	}

	// Lens gratings (paper section 2.4, fig 2.12).
	// ~200 transparent radial structures in the crystalline lens with
	// higher refractive index near the edges, producing the lenticular
	// halo via edge diffraction.  Modeled as semi-opaque radial lines
	// whose opacity increases toward the aperture edge (paper: "there is
	// more diffraction happening [near the edges]").
	// Pupil mode only — anatomical eye feature.
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

	RWTexAperture[tid] = complexSum;
}
