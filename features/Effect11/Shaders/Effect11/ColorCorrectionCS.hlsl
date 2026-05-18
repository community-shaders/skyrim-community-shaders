cbuffer ColorCorrectionParams : register(b0)
{
	float Brightness;
	float GammaCurve;
	float Timer;
};

RWTexture2D<float4> OutputTexture : register(u0);

float3 ScreenSpaceDither(float2 vScreenPos)
{
	float3 vDither = dot(float2(171.0, 231.0), vScreenPos.xy + Timer).xxx;
	vDither.rgb = frac(vDither.rgb / float3(103.0, 71.0, 97.0)) - float3(0.5, 0.5, 0.5);
	return vDither.rgb / 255.0;
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
	color.rgb += ScreenSpaceDither(float2(id.xy));
	OutputTexture[id.xy] = color;
}
