SamplerState DensitySampler : register(s0);
RWTexture3D<float> DensityRW : register(u0);

cbuffer PerTechnique : register(b0)
{
	float3 TextureDimensions : packoffset(c0);
	int StepIndex : packoffset(c1);
}

[numthreads(32, 32, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint depth = (uint)TextureDimensions.z;
	uint3 currCoord = uint3(dispatchID.xy, 0);
	float acc = DensityRW[currCoord];
	for (currCoord.z = 1; currCoord.z < depth; currCoord.z++) {
		acc += DensityRW[currCoord];
		DensityRW[currCoord] = acc;
	}
}
