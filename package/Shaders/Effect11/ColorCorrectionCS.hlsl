cbuffer ColorCorrectionParams : register(b0)
{
	float Brightness;
	float GammaCurve;
};

RWTexture2D<float4> OutputTexture : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id
	: SV_DispatchThreadID)
{
	uint width, height;
	OutputTexture.GetDimensions(width, height);
	if (id.x >= width || id.y >= height) {
		return;
	}

	float4 color = OutputTexture[id.xy];
	color.rgb = pow(color.rgb, GammaCurve);
	color.rgb *= Brightness;
	OutputTexture[id.xy] = max(0, color);
}
