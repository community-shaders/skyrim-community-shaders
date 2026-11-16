// Far-field scalar diffraction from quadratic arcs of the traced exit pupil.
// Green's identity reduces the high-frequency pupil transform to its boundary;
// the FFT retains the interior aberration field and the low-frequency core.
float2 WaveComplexMultiply(float2 a, float2 b) { return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
float2 WavePhaseFactor(float phase)
{
	float2 r;
	sincos(phase, r.y, r.x);
	return r;
}
// Fresnel C(x),S(x), with the asymptotic +/- (1+i)/2 removed to avoid
// cancellation between two endpoints on the same side of a stationary point.
float2 FresnelTail(float value)
{
	float x = abs(value);
	float2 result;
	if (x < 1.6) {
		float z = -.25 * PI * PI * x * x * x * x;
		float2 term = float2(x, .5 * PI * x * x * x);
		result = term / float2(1, 3);
		[unroll] for (uint n = 1; n <= 10; ++n)
		{
			term *= z / float2((2 * n) * (2 * n - 1), (2 * n + 1) * (2 * n));
			result += term / float2(4 * n + 1, 4 * n + 3);
		}
		result -= .5;
	} else {
		// Independently fitted Chebyshev approximations to the smooth Fresnel
		// auxiliary functions on [1.6,infinity); double reference error <3e-9.
		static const float2 coefficients[13] = {
			float2(.98633632016, .94099995677), float2(-.017434711908, -.073238014855),
			float2(-.0032843322964, -.011080882004), float2(.00047917602876, .0029448995903),
			float2(-1.8619584893e-5, -.00027899500943), float2(-8.1508884584e-6, -3.8060222724e-5),
			float2(2.5877651224e-6, 2.3134965355e-5), float2(-3.92496571e-7, -5.4026722605e-6),
			float2(-2.1062572888e-9, 5.1764842264e-7), float2(2.2874521817e-8, 1.6589424866e-7),
			float2(-8.4393834709e-9, -1.063138297e-7), float2(1.8900801592e-9, 3.3321821901e-8),
			float2(-9.3161047359e-12, -5.1293917661e-9)
		};
		float t = 2 * (1.6 / x) * (1.6 / x) - 1;
		float2 b1 = 0, b2 = 0;
		[unroll] for (int i = 12; i >= 1; --i)
		{
			float2 b = 2 * t * b1 - b2 + coefficients[i];
			b2 = b1;
			b1 = b;
		}
		float2 fg = (t * b1 - b2 + coefficients[0]) / float2(PI * x, PI * PI * x * x * x);
		result = WaveComplexMultiply(float2(-fg.y, -fg.x), WavePhaseFactor(.5 * PI * x * x));
	}
	return value < 0 ? -result : result;
}
float2 QuadraticEdgeIntegral(float a, float b, float c0, float c1)
{
	if (abs(b) > 16 + 8 * abs(a)) {
		float2 result = 0;
		[unroll] for (uint end = 0; end < 2; ++end)
		{
			float t = end == 0 ? -1 : 1, slope = 2 * a * t + b, g = c0 + c1 * t;
			float2 factor = float2((c1 - g * 2 * a / slope) / (slope * slope), -g / slope);
			result += t * WaveComplexMultiply(factor, WavePhaseFactor(a + b * t));
		}
		return result;
	}
	if (abs(a) < .05) {
		static const float2 nodes[8] = { float2(.09501250984, .18945061046), float2(.28160355078, .18260341504),
			float2(.45801677766, .16915651940), float2(.61787624440, .14959598882),
			float2(.75540440836, .12462897126), float2(.86563120239, .09515851168),
			float2(.94457502307, .06225352394), float2(.98940093499, .02715245941) };
		float2 result = 0;
		[unroll] for (uint i = 0; i < 8; ++i)
		{
			float t = nodes[i].x;
			result += nodes[i].y * ((c0 + c1 * t) * WavePhaseFactor(a * t * t + b * t) + (c0 - c1 * t) * WavePhaseFactor(a * t * t - b * t));
		}
		return result;
	}
	float root = sqrt(2 * abs(a) / PI), shift = b / (2 * a);
	float lo = root * (-1 + shift), hi = root * (1 + shift);
	float2 f = FresnelTail(hi) - FresnelTail(lo) + .5 * (sign(hi) - sign(lo));
	f.y *= sign(a);
	float2 i0 = WaveComplexMultiply(f, WavePhaseFactor(-b * b / (4 * a))) / root;
	float2 endpoints = WavePhaseFactor(a + b) - WavePhaseFactor(a - b);
	float2 i1 = float2(endpoints.y, -endpoints.x) / (2 * a) - shift * i0;
	return c0 * i0 + c1 * i1;
}
StructuredBuffer<float4> WaveBoundary : register(t3);
RWStructuredBuffer<float4> WaveBoundaryOutput : register(u0);
float4 PupilBoundaryPoint(float angle, uint band)
{
	float4 transfer = PupilTransfer[band];
	float cone = 1.2 * transfer.z / transfer.y;
	float extent = abs(cone) * rsqrt(1 + cone * cone);
	float2 direction = float2(cos(angle), sin(angle));
	float radius = .8, previousRadius = 0, previousClearance = 1, sampledRadius = 0;
	float wavelength = band == 0 ? .65 : band == 1 ? .55 :
	                                                 .45;
	RayOutput hit = (RayOutput)0;
	// On-axis pupil clearance is radial and monotone. A secant solve reaches
	// the actual curved boundary without twenty serial binary-search traces.
	[loop] for (uint iteration = 0; iteration < 6; ++iteration)
	{
		float2 q = direction * (radius * extent);
		RayInput ray;
		ray.PupilField = float4(0, 0, q * rsqrt(max(1 - dot(q, q), 1e-8)));
		ray.Conjugate = float4(0, wavelength, 0, 0);
		RayOutput candidate = Trace(ray, true, .5);
		if (candidate.SensorEnergy.z <= 0) {
			radius *= .75;
			continue;
		}
		hit = candidate;
		sampledRadius = radius;
		float clearance = candidate.SensorEnergy.w;
		if (abs(clearance) < 2e-7)
			break;
		float derivative = (clearance - previousClearance) / (radius - previousRadius);
		previousRadius = radius;
		previousClearance = clearance;
		radius = clamp(radius - clearance / min(derivative, -1e-4), .01, 1);
	}
	float objectZ = 1 - InverseFocusDistance * SensorZ;
	float r2 = dot(hit.SensorEnergy.xy, hit.SensorEnergy.xy);
	float reference = r2 * InverseFocusDistance / max(sqrt(objectZ * objectZ + r2 * InverseFocusDistance * InverseFocusDistance) + objectZ, 1e-8);
	return float4(direction * sampledRadius, 2 * PI * (hit.Direction.w + reference) / (wavelength * .001), sqrt(max(hit.SensorEnergy.z, 0)));
}
[numthreads(64, 1, 1)] void BuildWaveBoundary(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	uint count = max(Blades, 3), band = group.x;
	if (lane >= count)
		return;
	float step = 2 * PI / count;
	float angle = Rotation - PI / max(Blades, 3) + (lane + .5) * step + (PupilTransfer[band].y < 0 ? PI : 0);
	float4 a = PupilBoundaryPoint(angle - .5 * step, band), b = PupilBoundaryPoint(angle, band), c = PupilBoundaryPoint(angle + .5 * step, band);
	uint index = (band * 64 + lane) * 3;
	WaveBoundaryOutput[index] = float4(b.xy, (c.xy - a.xy) * .5);
	WaveBoundaryOutput[index + 1] = float4((c.xy + a.xy) * .5 - b.xy, b.z, (c.z - a.z) * .5);
	WaveBoundaryOutput[index + 2] = float4((c.z + a.z) * .5 - b.z, b.w, 0, 0);
} float BoundaryDiffraction(float2 coordinate, uint band, float pupilEnergy)
{
	float2 k = PI * WavePupilSize / 512 * coordinate;
	float k2 = max(dot(k, k), 1e-20);
	float2 sum = 0;
	[loop] for (uint arc = 0; arc < max(Blades, 3); ++arc)
	{
		uint index = (band * 64 + arc) * 3;
		float4 p = WaveBoundary[index], q = WaveBoundary[index + 1], r = WaveBoundary[index + 2];
		float a = r.x - dot(k, q.xy), b = q.w - dot(k, p.zw), c = q.z - dot(k, p.xy);
		float c0 = k.x * p.w - k.y * p.z, c1 = 2 * (k.x * q.y - k.y * q.x);
		sum += r.y * WaveComplexMultiply(QuadraticEdgeIntegral(a, b, c0, c1), WavePhaseFactor(c));
	}
	float scale = WavePupilSize * .5;
	return dot(sum, sum) / (k2 * k2) * scale * scale * scale * scale / max(512 * 512 * pupilEnergy, 1e-20);
}
