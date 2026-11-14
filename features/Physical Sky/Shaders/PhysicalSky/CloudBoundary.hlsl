#include "Common/FrameBuffer.hlsli"
#include "Common/Game.hlsli"
#include "PhysicalSky/CloudGeometry.hlsli"
#include "PhysicalSky/CloudMotion.hlsli"
#include "PhysicalSky/CloudPhase.hlsli"

cbuffer CloudBoundaryParameters : register(b0)
{
	float4 gridOriginSpacing;
	float4 fieldFrequencyWind;
	float4 shearAltitude;
	float4 evolution;
	float4 frameDimensions;
	float cloudCurvatureRadius;
	float bottomZ;
	uint gridCellCount;
	uint cloudFrameIndex;
	float4 localRect;
	float4 localAltitude;
};

Texture2D<float2> CloudHeight : register(t0);
Texture2D<float3> CloudModeling : register(t1);
SamplerState FieldSampler : register(s0);

struct BoundaryVertex
{
	float4 position: SV_Position;
	float viewDepth: TEXCOORD0;
	nointerpolation uint lowerBoundary: TEXCOORD1;
};

float2 FieldUv(float2 worldXY)
{
	return CloudFieldPosition((worldXY - fieldFrequencyWind.zw) * GAME_UNIT_TO_M, evolution.xy) * (fieldFrequencyWind.xy / GAME_UNIT_TO_M) + 0.5;
}

BoundaryVertex vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
	const uint rowLength = gridCellCount + 1u;
	const float2 worldXY = float2(vertexId % rowLength, vertexId / rowLength) * gridOriginSpacing.zw + gridOriginSpacing.xy;
	const float2 heights = saturate(CloudHeight.SampleLevel(FieldSampler, FieldUv(worldXY), 0));
	const float lower = heights.x - 0.02;
	const float heightRange = shearAltitude.w - shearAltitude.z;
	float altitude = shearAltitude.z + heightRange * lower;
	[branch] if (instanceId == 0u)
	{
		const float topType = CloudModeling.SampleLevel(FieldSampler, FieldUv(worldXY), 0).g;
		const float upstream = CloudModeling.SampleLevel(FieldSampler, FieldUv(worldXY) - shearAltitude.xy * 60.0 * fieldFrequencyWind.xy, 0).g;
		const float thicknessScale = max(0.2, pow(saturate(max(topType, upstream)), 0.1));
		altitude += heightRange * thicknessScale * (heights.y - lower);
	}
	// Enclose complete neighboring cells so small authored patches cannot fall between vertices.
	const float2 localUv = (worldXY * GAME_UNIT_TO_M - localRect.xy) * localRect.zw;
	const float2 margin = (gridOriginSpacing.zw + abs(shearAltitude.xy) * 61.0) * GAME_UNIT_TO_M * localRect.zw;
	if (localAltitude.z > 0.0 && all(localUv >= -margin) && all(localUv <= 1.0 + margin)) {
		const float bottom = localAltitude.y > 0.0 ? min(shearAltitude.z, localAltitude.x / GAME_UNIT_TO_M) : shearAltitude.z;
		const float top = localAltitude.y > 0.0 ? max(shearAltitude.w, (localAltitude.x + localAltitude.y) / GAME_UNIT_TO_M) : shearAltitude.w;
		altitude = instanceId == 0u ? top : bottom;
	}
	const float2 relativeXY = worldXY - FrameBuffer::CameraPosAdjust.xy;
	const float curvatureMargin = instanceId == 0u ? dot(gridOriginSpacing.zw, gridOriginSpacing.zw) / (4.0 * cloudCurvatureRadius) : 0.0;
	const float height = altitude - CloudCurvatureOffset(relativeXY, cloudCurvatureRadius) + curvatureMargin;
	const float3 relative = float3(relativeXY, bottomZ + height - FrameBuffer::CameraPosAdjust.z);
	BoundaryVertex output;
	output.position = mul(FrameBuffer::CameraViewProj, float4(relative, 1));
	output.viewDepth = mul(FrameBuffer::CameraView, float4(relative, 1)).z;
	output.lowerBoundary = instanceId;
	const float2 scale = frameDimensions.xy / FrameBuffer::DynamicResolutionParams2.xy / (4.0 * frameDimensions.zw);
	const float2 offset = (scale - 1.0 + (1.5 - CloudPhaseOffset(cloudFrameIndex)) / (2.0 * frameDimensions.zw)) * float2(1, -1);
	output.position.xy = output.position.xy * scale + output.position.w * offset;
	// Cloud boundaries extend beyond the scene projection's far plane.
	output.position.z = output.position.w * 0.5;
	return output;
}

float4 pixelMain(BoundaryVertex input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	const bool entering = frontFace != (input.lowerBoundary != 0u);
	const float depthKm = abs(input.viewDepth) * GAME_UNIT_TO_M * 0.001;
	const float empty = asfloat(0x7f7fffffu);
	return entering ? float4(depthKm, 0.0, empty, 0.0) : float4(empty, -depthKm, depthKm, 0.0);
}
