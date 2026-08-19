#include "Upscaling/UpscaleVS.hlsl"

#ifdef PSHADER

Texture2D<float4> ColorInput : register(t0);

float4 main(VS_OUTPUT input) : SV_Target
{
	return ColorInput.Load(int3(input.Position.xy, 0));
}

#endif
