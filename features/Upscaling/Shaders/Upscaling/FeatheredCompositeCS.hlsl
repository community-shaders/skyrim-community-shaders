cbuffer FeatherCB : register(b0)
{
	uint CropX;         // paste position X in output space
	uint CropY;         // paste position Y in output space
	uint CropW;         // crop width
	uint CropH;         // crop height
	float FeatherWidth; // feather distance in pixels (inward from crop edge)
	float3 pad;
};

Texture2D<float4> CropTexture : register(t0);       // DLSS output (crop-sized, at {0,0})
RWTexture2D<float4> OutputTexture : register(u0);    // vrFinalOutput (already filled with periphery)

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	// dispatchID is in crop-local space (0..CropW-1, 0..CropH-1)
	int2 cropLocal = int2(dispatchID.xy);
	if (cropLocal.x >= (int)CropW || cropLocal.y >= (int)CropH)
		return;

	// Output pixel = crop-local + paste offset
	int2 pixel = cropLocal + int2(CropX, CropY);

	// Distance from nearest crop edge (positive = inside)
	float distLeft = (float)cropLocal.x;
	float distRight = (float)(CropW - 1 - cropLocal.x);
	float distTop = (float)cropLocal.y;
	float distBottom = (float)(CropH - 1 - cropLocal.y);
	float distFromEdge = min(min(distLeft, distRight), min(distTop, distBottom));

	float4 dlss = CropTexture.Load(int3(cropLocal, 0));

	if (FeatherWidth <= 0.0 || distFromEdge >= FeatherWidth) {
		// Inside crop interior or no feathering: 100% DLSS
		OutputTexture[pixel] = dlss;
	} else {
		// Feather zone: smooth blend from periphery (TAA-stabilized) to DLSS
		float blend = smoothstep(0.0, FeatherWidth, distFromEdge);
		float4 periphery = OutputTexture[pixel];
		OutputTexture[pixel] = lerp(periphery, dlss, blend);
	}
}
