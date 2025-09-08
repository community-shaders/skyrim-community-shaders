#include "Upscaling/UpscaleVS.hlsl"

#ifdef PSHADER

Texture2D<float> InputTexture : register(t0);

float main(VS_OUTPUT input) : SV_Target
{
	return InputTexture.Load(int3(input.Position.xy, 0));
}

#endif