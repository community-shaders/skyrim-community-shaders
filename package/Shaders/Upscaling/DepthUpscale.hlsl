struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

#if defined(VSHADER)

VS_OUTPUT main(uint vertexId : SV_VertexID)
{
	VS_OUTPUT vsout;

	// Generate fullscreen triangle using vertex ID
	// Vertex 0: (-1, -1) -> (0, 1) UV
	// Vertex 1: (-1,  3) -> (0, -1) UV  
	// Vertex 2: ( 3, -1) -> (2, 1) UV
	vsout.Position.x = (float)(vertexId / 2) * 4.0 - 1.0;
	vsout.Position.y = (float)(vertexId % 2) * 4.0 - 1.0;
	vsout.Position.z = 0.0;
	vsout.Position.w = 1.0;

	vsout.TexCoord.x = (float)(vertexId / 2) * 2.0;
	vsout.TexCoord.y = 1.0 - (float)(vertexId % 2) * 2.0;

	return vsout;
}

#endif

#if defined(PSHADER)

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float Depth : SV_Depth;
};

SamplerState LinearSampler : register(s0);

Texture2D<float4> DepthTex : register(t0);

#if defined(VR)
Texture2D<uint> StencilTex : register(t1);
#endif

cbuffer PerFrame : register(b0)
{
	float4 ResolutionScale;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	
#if defined(VR)
	// For VR, upscale stencil using point sampling with minimum value selection
	float2 scaledUV = input.TexCoord.xy * ResolutionScale.x;
	
	// Get stencil texture dimensions
	uint2 stencilDims;
	StencilTex.GetDimensions(stencilDims.x, stencilDims.y);
	
	// Convert UV to pixel coordinates
	float2 pixelCoord = scaledUV * float2(stencilDims);
	
	// Calculate sample positions for 2x2 neighborhood
	int2 baseCoord = int2(floor(pixelCoord));
	
	// Sample 4 neighboring stencil values using point sampling
	uint stencil0 = StencilTex.Load(int3(baseCoord + int2(0, 0), 0));
	uint stencil1 = StencilTex.Load(int3(baseCoord + int2(1, 0), 0));
	uint stencil2 = StencilTex.Load(int3(baseCoord + int2(0, 1), 0));
	uint stencil3 = StencilTex.Load(int3(baseCoord + int2(1, 1), 0));
	
	// Choose the maximum stencil value
	uint maxStencil = max(max(stencil0, stencil1), max(stencil2, stencil3));
	
	if (maxStencil > 0x00)
		discard;
#endif
	
	// Upscale depth using linear sampling
	float depth = DepthTex.SampleLevel(LinearSampler, input.TexCoord.xy * ResolutionScale.x, 0);
	psout.Depth = depth;

	return psout;
}

#endif