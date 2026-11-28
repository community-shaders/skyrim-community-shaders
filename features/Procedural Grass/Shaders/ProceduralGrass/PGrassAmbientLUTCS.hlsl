#define CSHADER

#include "Common/ColorManagement.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "IBL/IBL.hlsli"
#include "ProceduralGrass/PGrassCommon.hlsli"

// Cache directional ambient and IBL for reduced-lighting grass.
RWTexture2D<float4> DistantAmbientLUT : register(u0);

static const uint LUT_DIM = 32;

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= LUT_DIM))
		return;

	float2 encodedNormal = (float2(dispatchID.xy) + 0.5f) / LUT_DIM;
	float3 normal = GBuffer::DecodeNormal(encodedNormal);
	float3 ambient = Color::Ambient(max(0.0f, SharedData::GetAmbient(normal)));

	if (SharedData::iblSettings.EnableIBL)
		ambient = ImageBasedLighting::GetDiffuseIBL(ambient, -normal);

	DistantAmbientLUT[dispatchID.xy] = float4(ambient, 1.0f);
}
