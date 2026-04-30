// SDFGI Voxelization Shader — VS + PS
//
// Port of Godot's MODE_RENDER_SDF fragment shader to DX11 HLSL.
// Renders scene geometry from 3 orthographic projections (X, Y, Z axes).
// The pixel shader converts each fragment's world position to voxel grid
// coordinates and writes albedo, emission, and facing data to 3D UAV textures.
//
// Godot source: scene_forward_clustered.glsl (MODE_RENDER_SDF block)

// ---- Constant buffer ----

cbuffer VoxelizeCB : register(b0)
{
	float4x4 ViewProj;
	float4x4 World;
	float3 CascadeOffset;
	float CascadeToCell;
	float3 GridSizeF;
	uint pad0;
};

// ---- Vertex Shader ----

struct VS_INPUT
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float4 Color : COLOR;
};

struct VS_OUTPUT
{
	float4 Position : SV_Position;
	float3 WorldPos : TEXCOORD0;
	float3 WorldNormal : TEXCOORD1;
	float4 Color : TEXCOORD2;
};

#ifdef VERTEXSHADER

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;
	float4 worldPos = mul(World, float4(input.Position, 1.0));
	output.WorldPos = worldPos.xyz;
	output.WorldNormal = normalize(mul((float3x3)World, input.Normal));
	output.Position = mul(ViewProj, worldPos);
	output.Color = input.Color;
	return output;
}

#endif

// ---- Pixel Shader ----

#ifdef PIXELSHADER

// UAVs bound via OMSetRenderTargetsAndUnorderedAccessViews.
// Slot 0 is the dummy render target (SV_Target0).
// UAVs start at slot 1.
RWTexture3D<uint> renderAlbedo : register(u1);      // R16_UINT: bit0=solid, bits[1:15]=RGB555
RWTexture3D<uint> renderEmission : register(u2);    // R32_UINT: RGBE9995 encoded
RWTexture3D<uint> renderGeomFacing : register(u3);  // R32_UINT: bits[0:5]=6-face flags

// RGBE9995 encode (duplicated from common.hlsli to avoid cbuffer conflicts)
uint EncodeRGBE9995_Vox(float3 color)
{
	color = max(color, 0.0);
	float maxC = max(max(color.r, color.g), color.b);
	if (maxC < 1e-10)
		return 0;
	float e = ceil(log2(maxC));
	float scale = pow(2.0, -e + 9.0);
	uint r = (uint)clamp(color.r * scale, 0, 511);
	uint g = (uint)clamp(color.g * scale, 0, 511);
	uint b = (uint)clamp(color.b * scale, 0, 511);
	uint ev = (uint)clamp(e + 15.0, 0, 31);
	return r | (g << 9) | (b << 18) | (ev << 27);
}

float4 main(VS_OUTPUT input) : SV_Target0
{
	float3 worldPos = input.WorldPos;
	float3 worldNormal = normalize(input.WorldNormal);
	float3 albedo = input.Color.rgb;
	float emissionStrength = input.Color.a;

	// Convert world position to voxel grid coordinates
	// Matches Godot: grid_pos = sdf_offset + ivec3(local_pos * sdf_size)
	float3 localPos = (worldPos - CascadeOffset) * CascadeToCell;
	int3 gridPos = int3(floor(localPos));

	// Bounds check
	if (any(gridPos < 0) || any(gridPos >= int3(GridSizeF)))
		discard;

	// Pack albedo as RGB555 + solid bit (matches Godot's albedo16 encoding)
	// Godot: bit0=solid, bits[1:5]=blue, bits[6:10]=green, bits[11:15]=red
	uint albedo16 = 1u;
	albedo16 |= (clamp((uint)(albedo.r * 31.0), 0, 31) << 11);
	albedo16 |= (clamp((uint)(albedo.g * 31.0), 0, 31) << 6);
	albedo16 |= (clamp((uint)(albedo.b * 31.0), 0, 31) << 1);
	renderAlbedo[gridPos] = albedo16;

	// Compute facing bits from world normal
	// Godot: finds dominant aniso direction, sets single bit, uses imageAtomicOr.
	// DX11 lacks atomic OR on RWTexture3D, so we set all relevant bits per fragment.
	// Directions: +X(0x01), -X(0x02), +Y(0x04), -Y(0x08), +Z(0x10), -Z(0x20)
	static const float3 anisoDirs[6] = {
		float3(1, 0, 0), float3(-1, 0, 0),
		float3(0, 1, 0), float3(0, -1, 0),
		float3(0, 0, 1), float3(0, 0, -1)
	};

	uint facing = 0;
	[unroll]
	for (uint i = 0; i < 6; i++) {
		if (dot(worldNormal, anisoDirs[i]) > 0.1)
			facing |= (1u << i);
	}
	renderGeomFacing[gridPos] = facing;

	// Emission (RGBE9995 encoded)
	if (emissionStrength > 0.001) {
		float3 emission = albedo * emissionStrength;
		renderEmission[gridPos] = EncodeRGBE9995_Vox(emission);
	}

	return float4(0, 0, 0, 0);
}

#endif
