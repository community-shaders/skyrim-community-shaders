// AMD Capsaicin GI-1.1 — Hemi-octahedral mapping + TBN construction
// Ported from Capsaicin math/sampling.hlsl

#ifndef HEMI_OCTAHEDRAL_HLSLI
#define HEMI_OCTAHEDRAL_HLSLI

#include "SSRT/GI1Common.hlsli"

float hmax(float2 val)
{
	return max(val.x, val.y);
}

float hmin(float2 val)
{
	return min(val.x, val.y);
}

float hadd(float2 val)
{
	return val.x + val.y;
}

// Hemi-octahedral mapping: [0,1]^2 -> direction on upper hemisphere
float3 mapToHemiOctahedron(float2 samples)
{
	float2 st = 2.0f * samples - 1.0f;

	// Transform from unit square to diamond corresponding to +hemisphere
	st = float2(st.x + st.y, st.x - st.y) * 0.5f;

	float2 absMapped = abs(st);
	float distance2 = 1.0f - hadd(absMapped);
	float radius = 1.0f - abs(distance2);

	float phi = (radius == 0.0f) ? 0.0f : QUARTER_PI * ((absMapped.y - absMapped.x) / radius + 1.0f);
	float radiusSqr = radius * radius;
	float sinTheta = radius * sqrt(2.0f - radiusSqr);
	float sinPhi, cosPhi;
	sincos(phi, sinPhi, cosPhi);
	float x = sinTheta * sign(st.x) * cosPhi;
	float y = sinTheta * sign(st.y) * sinPhi;
	float z = sign(distance2) * (1.0f - radiusSqr);

	return float3(x, y, z);
}

// Inverse hemi-octahedral mapping: direction -> [0,1]^2
float2 mapToHemiOctahedronInverse(float3 direction)
{
	float3 absDir = abs(direction);

	float radius = sqrt(1.0f - absDir.z);
	float a = hmax(absDir.xy);
	float b = hmin(absDir.xy);
	b = a == 0.0f ? 0.0f : b / a;

	float phi = atan(b) * TWO_INV_PI;
	phi = (absDir.x >= absDir.y) ? phi : 1.0f - phi;

	float t = phi * radius;
	float s = radius - t;
	float2 st = float2(s, t);
	st *= sign(direction).xy;

	// Rescale to occupy the whole unit square
	st = float2(st.x + st.y, st.x - st.y);

	// Transform from [-1,1] to [0,1]
	st = 0.5f * st + 0.5f;

	return st;
}

// Orthonormal basis from normal (Capsaicin's GetOrthoVectors)
void GetOrthoVectors(in float3 n, out float3 b1, out float3 b2)
{
	bool sel = abs(n.z) > 0;
	float3 p2 = sel ? n : n.zyx;
	float k = 1.0f / sqrt(p2.z * p2.z + n.y * n.y);
	b1 = float3(0.0f, -p2.z * k, n.y * k);
	b1 = sel ? b1 : b1.zyx;
	b2 = cross(n, b1);
}

// TBN matrix from normal (transpose of [u, v, n] for transforming directions *into* tangent space)
float3x3 CreateTBN(in float3 n)
{
	float3 u, v;
	GetOrthoVectors(n, u, v);

	float3x3 TBN;
	TBN[0] = u;
	TBN[1] = v;
	TBN[2] = n;

	return transpose(TBN);
}

#endif  // HEMI_OCTAHEDRAL_HLSLI
