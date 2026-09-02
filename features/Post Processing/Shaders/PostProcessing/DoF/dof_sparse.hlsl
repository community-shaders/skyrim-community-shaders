#include "Common/Math.hlsli"

struct SparseBokehData
{
	float2 center;
	float radiusInPixels;
	float signedCoC;
	float3 color;
	float pad;
};

StructuredBuffer<SparseBokehData> SparseBokehList : register(t0);
Texture2D<float4> CustomAperture : register(t1);
SamplerState LinearSampler : register(s0);

cbuffer DoFCB : register(b1)
{
	float TransitionSpeed;
	float2 FocusCoord;
	float ManualFocusPlane;
	float FocalLength;
	float FNumber;
	float FarPlaneMaxBlur;
	float NearPlaneMaxBlur;
	float BlurQuality;
	float NearFarDistanceCompensation;
	float BokehBusyFactor;
	float HighlightBoost;
	float PostBlurSmoothing;
	uint HighlightShape;
	float HighlightShapeRotationAngle;
	float PetzvalStrength;
	uint AutoFocus;
	float MaxNearCoCRadius;
	float MaxFarCoCRadius;
	uint TileDilateRadius;
	uint2 CoCTileDim;
	uint2 HalfResDim;
	uint BokehMode;
	float CustomShapeRadiusScale;
	float BokehMaxRadius;
	float NearMaxReachPx;
	uint SparseHighlightEnabled;
	uint SparseHighlightMaxCount;
	float SparseHighlightThreshold;
	float SparseHighlightContrast;
	uint BokehBladeCount;
	float BokehBladeRoundness;
	float ProceduralBokehAreaScale;
	uint SparseHighlightWorkBudget;
};

struct VertexOutput
{
	float4 position: SV_Position;
	float2 apertureCoord: TEXCOORD0;
	float3 color: TEXCOORD1;
	float signedCoC: TEXCOORD2;
	float inverseArea: TEXCOORD3;
};

float2 Rotate(float2 value, float angle)
{
	float sine;
	float cosine;
	sincos(angle, sine, cosine);
	return float2(value.x * cosine - value.y * sine, value.x * sine + value.y * cosine);
}

VertexOutput VS_SparseBokeh(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
	static const float2 corners[4] = {
		float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, -1.0f), float2(1.0f, 1.0f)
	};

	SparseBokehData item = SparseBokehList[instanceID];
	float2 corner = corners[vertexID];
	float2 centerUV = item.center / float2(HalfResDim);
	float2 positionCorner = corner;
	float2 fromCenter = centerUV - 0.5f;
	float distanceFromCenter = length(fromCenter);
	if (PetzvalStrength > 0.001f && distanceFromCenter > 0.0001f) {
		float2 radialAxis = fromCenter / distanceFromCenter;
		float2 tangentialAxis = float2(-radialAxis.y, radialAxis.x);
		float amount = PetzvalStrength * saturate(distanceFromCenter * 2.0f) * saturate(distanceFromCenter * 2.0f) * 1.35f;
		float tangentialScale = 1.0f + amount;
		positionCorner = radialAxis * (dot(corner, radialAxis) / tangentialScale) +
		                 tangentialAxis * (dot(corner, tangentialAxis) * tangentialScale);
	}
	float2 halfResRadius = item.radiusInPixels * BokehMaxRadius;
	float2 halfExtentUV = halfResRadius / float2(HalfResDim);
	float2 uv = centerUV + positionCorner * halfExtentUV;

	VertexOutput output;
	output.position = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
	// Gather reflects the offset while locating the source, so the resulting output silhouette uses
	// the forward texture rotation here. This matters for non-centrally-symmetric custom masks.
	output.apertureCoord = Rotate(corner * BokehMaxRadius, Math::TAU * HighlightShapeRotationAngle);
	output.color = item.color;
	output.signedCoC = item.signedCoC;
	output.inverseArea = rcp(max(Math::PI * item.radiusInPixels * item.radiusInPixels, 1.0f));
	return output;
}

float ProceduralApertureCoverage(float2 inputPoint)
{
	float radius = length(inputPoint);
	if (radius <= 1e-5f)
		return 1.0f;

	float angle = atan2(inputPoint.y, inputPoint.x);
	angle = angle < 0.0f ? angle + Math::TAU : angle;
	float sector = Math::TAU / max((float)BokehBladeCount, 4.0f);
	float edgeNormal = (floor(angle / sector) + 0.5f) * sector;
	float alpha = angle - edgeNormal;
	float circumRadius = sqrt((2.0f * Math::PI) / (BokehBladeCount * sin(sector)));
	float incircleRadius = circumRadius * cos(Math::PI / BokehBladeCount);
	float polygonRadius = incircleRadius / max(cos(alpha), 1e-4f);
	float edgeRadius = lerp(polygonRadius, 1.0f, BokehBladeRoundness) * ProceduralBokehAreaScale;
	float edgeWidth = max(fwidth(radius), 1e-4f);
	return 1.0f - smoothstep(edgeRadius - edgeWidth, edgeRadius + edgeWidth, radius);
}

struct PixelOutput
{
	float4 farLayer: SV_Target0;
	float4 nearLayer: SV_Target1;
};

PixelOutput PS_SparseBokeh(VertexOutput input)
{
	float coverage;
	float3 tint = 1.0f;
	if (BokehMode == 1) {
		float2 customCoord = input.apertureCoord / max(CustomShapeRadiusScale, 1e-4f);
		float4 aperture = CustomAperture.SampleLevel(LinearSampler, customCoord * 0.5f + 0.5f, 0);
		coverage = all(abs(customCoord) <= 1.0f) ? saturate(dot(aperture.rgb, float3(0.2126f, 0.7152f, 0.0722f)) * aperture.a) : 0.0f;
		tint = aperture.rgb / max(dot(aperture.rgb, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f);
	} else {
		coverage = ProceduralApertureCoverage(input.apertureCoord);
	}

	float3 energy = input.color * tint * coverage * input.inverseArea;
	PixelOutput output;
	output.farLayer = input.signedCoC >= 0.0f ? float4(energy, coverage) : 0.0f;
	output.nearLayer = input.signedCoC < 0.0f ? float4(energy, coverage) : 0.0f;
	return output;
}
