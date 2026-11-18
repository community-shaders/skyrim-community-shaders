#define COMPUTESHADER
#include "Common/Math.hlsli"

cbuffer DiskPhaseConstants : register(b0)
{
	float4 DropletPhase;
	float4 DiskRadii;        // xyz are Sun, Masser, Secunda.
	float4 PhaseParameters;  // cirrus forward/backward g, aerosol g, vanilla cloud forward g.
};
RWTexture2D<float4> OutputPhase : register(u0);

// Gauss-Legendre nodes on [0, 1]. The HG CDF is the integration coordinate,
// so even a sub-degree diffraction peak is integrated without missing its energy.
static const float2 Quadrature[32] = {
	float2(0.0013680691, 0.0035093050), float2(0.0071942442, 0.0081371974),
	float2(0.0176188722, 0.0126960327), float2(0.0325469620, 0.0171369315),
	float2(0.0518394221, 0.0214179490), float2(0.0753161931, 0.0254990296),
	float2(0.1027581020, 0.0293420467), float2(0.1339089406, 0.0329111114),
	float2(0.1684778665, 0.0361728971), float2(0.2061421214, 0.0390969479),
	float2(0.2465500455, 0.0416559621), float2(0.2893243619, 0.0438260465),
	float2(0.3340656989, 0.0455869393), float2(0.3803563189, 0.0469221995),
	float2(0.4277640192, 0.0478193600), float2(0.4758461672, 0.0482700443),
	float2(0.5241538328, 0.0482700443), float2(0.5722359808, 0.0478193600),
	float2(0.6196436811, 0.0469221995), float2(0.6659343011, 0.0455869393),
	float2(0.7106756381, 0.0438260465), float2(0.7534499545, 0.0416559621),
	float2(0.7938578786, 0.0390969479), float2(0.8315221335, 0.0361728971),
	float2(0.8660910594, 0.0329111114), float2(0.8972418980, 0.0293420467),
	float2(0.9246838069, 0.0254990296), float2(0.9481605779, 0.0214179490),
	float2(0.9674530380, 0.0171369315), float2(0.9823811278, 0.0126960327),
	float2(0.9928057558, 0.0081371974), float2(0.9986319309, 0.0035093050)
};

// asin's shader-model-5 approximation has an absolute error at the origin.
// A reduced Taylor series preserves relative accuracy for sub-degree discs.
float DiskAsin(float x)
{
	const bool upper = x > 0.7071067812;
	const float reduced = upper ? sqrt(max(1.0 - x * x, 0.0)) : x;
	const float square = reduced * reduced;
	float polynomial = 0.00424090709368;
	polynomial = 0.00466014348692 + square * polynomial;
	polynomial = 0.00515330968232 + square * polynomial;
	polynomial = 0.00574003767084 + square * polynomial;
	polynomial = 0.00644721031189 + square * polynomial;
	polynomial = 0.0073125258736 + square * polynomial;
	polynomial = 0.00839033580962 + square * polynomial;
	polynomial = 0.00976160952919 + square * polynomial;
	polynomial = 0.0115518008961 + square * polynomial;
	polynomial = 0.01396484375 + square * polynomial;
	polynomial = 0.0173527644231 + square * polynomial;
	polynomial = 0.0223721590909 + square * polynomial;
	polynomial = 0.0303819444444 + square * polynomial;
	polynomial = 0.0446428571429 + square * polynomial;
	polynomial = 0.075 + square * polynomial;
	polynomial = 0.166666666667 + square * polynomial;
	const float angle = reduced * (1.0 + square * polynomial);
	return upper ? 0.5 * Math::PI - angle : angle;
}

// Integrate one full or partial ring interval about the outgoing direction.
// beta is the particle's scattering angle. A partial ring contributes the
// fraction of its azimuth lying inside the emitter's spherical cap.
float IntegrateRings(float theta, float radius, float g, float alpha, float lower, float upper, bool partial)
{
	if (upper <= lower)
		return 0.0;
	const float loSin = sin(0.5 * lower);
	const float hiSin = sin(0.5 * upper);
	const float lo = loSin * loSin;
	// sin^2(b/2) - sin^2(a/2), stable for small intervals near either pole.
	const float span = sin(0.5 * (upper + lower)) * sin(0.5 * (upper - lower));
	const float width = 1.0 - g;
	const float d0 = sqrt(width * width + 4.0 * g * lo);
	const float d1 = sqrt(width * width + 4.0 * g * hiSin * hiSin);
	const float mass = 2.0 * (1.0 - g * g) * span / (d0 * d1 * (d0 + d1));
	float sum = 0.0;
	[loop] for (uint i = 0u; i < 32u; ++i)
	{
		// Smooth the square-root endpoints of the cap/ring intersection.
		const float x = Quadrature[i].x;
		const float u = 0.5 - 0.5 * cos(Math::PI * x);
		const float jacobian = 0.5 * Math::PI * sin(Math::PI * x);
		const float inverseD = lerp(1.0 / d0, 1.0 / d1, u);
		const float d = 1.0 / inverseD;
		// Inverse HG CDF with a factored difference. Also valid at g = 0.
		const float halfSinSquared = saturate(lo + u * span * (d + d0) / ((d0 + d1) * d1 * inverseD));
		const float mu = 1.0 - 2.0 * halfSinSquared;
		float fraction = 1.0;
		if (partial) {
			const float beta = 2.0 * DiskAsin(sqrt(halfSinSquared));
			const float denominator = sin(theta) * sin(beta);
			const float capIntersection = 2.0 * sin(0.5 * (radius + theta - beta)) * sin(0.5 * (radius - theta + beta));
			fraction = 2.0 * DiskAsin(sqrt(saturate(0.5 * capIntersection / max(denominator, 1e-20)))) / Math::PI;
		}
		sum += Quadrature[i].y * jacobian * fraction * (1.0 + alpha * mu * mu);
	}
	return mass * sum / (1.0 + alpha * (1.0 + 2.0 * g * g) / 3.0);
}

// (p * D)(theta), D = 1 / [2 pi (1 - cos(radius))] inside the cap.
// Source illuminance stays fixed when the disc changes size. Particle phase
// coefficients and the transport moments derived from them retain their meaning.
float ConvolveDisk(float theta, float radius, float g, float alpha)
{
	if (theta > 0.5 * Math::PI) {
		g = -g;
		theta = Math::PI - theta;
	}
	if (radius == 0.0) {
		const float s = sin(0.5 * theta);
		const float denominator = (1.0 - g) * (1.0 - g) + 4.0 * g * s * s;
		const float mu = cos(theta);
		return (1.0 - g * g) * (1.0 + alpha * mu * mu) /
		       (4.0 * Math::PI * denominator * sqrt(denominator) * (1.0 + alpha * (1.0 + 2.0 * g * g) / 3.0));
	}
	const float lower = abs(theta - radius);
	const float upper = min(theta + radius, 2.0 * Math::PI - theta - radius);
	float result = IntegrateRings(theta, radius, g, alpha, lower, upper, true);
	result += IntegrateRings(theta, radius, g, alpha, 0.0, max(radius - theta, 0.0), false);
	result += IntegrateRings(theta, radius, g, alpha, min(2.0 * Math::PI - theta - radius, Math::PI), Math::PI, false);
	const float halfSin = sin(0.5 * radius);
	return result / (4.0 * Math::PI * halfSin * halfSin);
}

[numthreads(64, 1, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	if (pixel.x >= 2048u || pixel.y >= 3u)
		return;
	const float coordinate = float(pixel.x) / 2047.0;
	const float edge = min(coordinate, 1.0 - coordinate);
	const float poleAngle = 2.0 * DiskAsin(sqrt(8.0) * edge * edge);
	const float theta = coordinate <= 0.5 ? poleAngle : Math::PI - poleAngle;
	const float radius = DiskRadii[pixel.y];
	const float low = lerp(ConvolveDisk(theta, radius, DropletPhase.x, 0.0), ConvolveDisk(theta, radius, DropletPhase.y, DropletPhase.z), DropletPhase.w);
	const float cirrus = 0.5 * (ConvolveDisk(theta, radius, PhaseParameters.x, 0.0) + ConvolveDisk(theta, radius, PhaseParameters.y, 0.0));
	const float aerosol = ConvolveDisk(theta, radius, PhaseParameters.z, 1.0);
	const float vanilla = ConvolveDisk(theta, radius, PhaseParameters.w, 0.0) + 0.45 * ConvolveDisk(theta, radius, 0.78, 2.0);
	OutputPhase[pixel] = float4(low, cirrus, aerosol, vanilla);
}
