// Forces alpha to 1.0 across the entire texture.
// Used after DLSS center paste onto submit texture to ensure Scaleform UI renders.
// DLSS output may have alpha=0 (from R11G11B10→R8G8B8A8 conversion with no alpha source),
// which can prevent UI compositing in the DLSS center area.

RWTexture2D<float4> ColorInOut : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float4 c = ColorInOut[dispatchID.xy];
	c.a = 1.0;
	ColorInOut[dispatchID.xy] = c;
}
