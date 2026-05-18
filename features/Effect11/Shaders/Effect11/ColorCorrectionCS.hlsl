cbuffer ColorCorrectionParams : register(b0)
{
	float Brightness;
	float GammaCurve;
	uint FrameCount;
};

RWTexture2D<float4> OutputTexture : register(u0);

uint3 pcg3d(uint3 v)
{
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v ^= v >> 16u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	return v;
}

float3 TriDither(float2 screenPos, uint frameCount)
{
	uint3 seed1 = uint3(screenPos, frameCount);
	uint3 seed2 = uint3(screenPos, frameCount + 4729u);
	return (pcg3d(seed1) - pcg3d(seed2)) / float(0xFFFFFFFFu);
}

[numthreads(8, 8, 1)] void main(uint3 id
	: SV_DispatchThreadID)
{
	uint width, height;
	OutputTexture.GetDimensions(width, height);
	if (id.x >= width || id.y >= height) {
		return;
	}

	float4 color = OutputTexture[id.xy];
	color.rgb = pow(abs(color.rgb), GammaCurve);
	color.rgb *= Brightness;
	color.rgb += TriDither(float2(id.xy), FrameCount) / 1023.0;
	OutputTexture[id.xy] = color;
}
