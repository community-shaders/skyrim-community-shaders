#define COMPUTESHADER

cbuffer Parameters : register(b1)
{
	uint mip;
	uint slice;
	uint channel;
	uint volume;
	float2 displayRange;
	uint densityWeighted;
	uint padding;
};

Texture2D<float4> Image2D : register(t0);
Texture3D<float4> Image3D : register(t1);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)] void main(uint2 tid : SV_DispatchThreadID) {
	uint2 size;
	Output.GetDimensions(size.x, size.y);
	if (any(tid >= size))
		return;
	float4 value;
	if (volume != 0u)
		value = Image3D.Load(int4(tid, slice, mip));
	else {
		uint width, height, levels;
		Image2D.GetDimensions(mip, width, height, levels);
		uint2 source = min(uint2((float2(tid) + 0.5) * float2(width, height) / float2(size)), uint2(width, height) - 1u);
		value = Image2D.Load(int3(source, mip));
	}
	if (densityWeighted != 0u)
		value.yzw /= max(value.x, 1e-6);
	float3 color = value.rgb;
	if (channel != 0u)
		color = value[min(channel - 1u, 3u)].xxx;
	Output[tid] = float4(saturate((color - displayRange.x) / max(displayRange.y - displayRange.x, 1e-6)), 1.0);
}
