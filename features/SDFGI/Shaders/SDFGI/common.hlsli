#ifndef SDFGI_COMMON_HLSLI
#define SDFGI_COMMON_HLSLI

#ifndef CASCADE_SIZE
#	define CASCADE_SIZE 128
#endif

#ifndef PROBE_AXIS_SIZE
#	define PROBE_AXIS_SIZE 9
#endif

#ifndef MAX_CASCADES
#	define MAX_CASCADES 4
#endif

#ifndef SH_SIZE
#	define SH_SIZE 16
#endif

#ifndef LIGHTPROBE_OCT_SIZE
#	define LIGHTPROBE_OCT_SIZE 6
#endif

#define PROBE_DIVISOR 16
#define ANISOTROPY_SIZE 6
#define HISTORY_BITS 10

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_OMNI 1
#define LIGHT_TYPE_SPOT 2

static const float PI = 3.14159265358979323846;

// Push constant equivalent - bound at b0
cbuffer SDFGIParams : register(b0)
{
	float3 GridSize;
	uint MaxCascades;

	uint Cascade;
	uint LightCount;
	uint ProcessOffset;
	uint ProcessIncrement;

	int ProbeAxisSize;
	float BounceFeedback;
	float YMult;
	uint UseOcclusion;

	int3 Scroll;
	int StepSize;

	int3 ProbeOffset;
	uint HalfSize;

	uint OcclusionIndex;
	uint HistoryIndex;
	uint HistorySize;
	uint RayCount;

	float RayBias;
	int2 ImageSize;
	uint SkyFlags;

	int3 WorldOffset;
	float SkyEnergy;

	float3 SkyColor;
	uint StoreAmbientTexture;
};

// Cascade data - bound at b1
struct CascadeData
{
	float3 Offset;
	float ToCell;
	int3 ProbeWorldOffset;
	uint Pad;
	float4 Pad2;
};

cbuffer CascadesCB : register(b1)
{
	CascadeData Cascades[8];
};

// ProcessVoxel packed structure
struct ProcessVoxel
{
	uint position;
	uint albedo;
	uint light;
	uint lightAniso;
};

// Light structure
struct SDFGILight
{
	float3 color;
	float energy;
	float3 direction;
	uint hasShadow;
	float3 position;
	float attenuation;
	uint type;
	float cosSpotAngle;
	float invSpotAttenuation;
	float radius;
};

SamplerState linearSampler : register(s0);

// ---- Encoding / Decoding Utilities ----

// RGBE9995 encode (32-bit)
uint EncodeRGBE9995(float3 color)
{
	color = max(color, 0.0);
	float maxC = max(max(color.r, color.g), color.b);

	if (maxC < 1e-10)
		return 0;

	float exp = ceil(log2(maxC));
	float scale = pow(2.0, -exp + 9.0);

	uint r = (uint)clamp(color.r * scale, 0, 511);
	uint g = (uint)clamp(color.g * scale, 0, 511);
	uint b = (uint)clamp(color.b * scale, 0, 511);
	uint e = (uint)clamp(exp + 15.0, 0, 31);

	return r | (g << 9) | (b << 18) | (e << 27);
}

float3 DecodeRGBE9995(uint encoded)
{
	float r = (float)(encoded & 0x1FF);
	float g = (float)((encoded >> 9) & 0x1FF);
	float b = (float)((encoded >> 18) & 0x1FF);
	float e = (float)((encoded >> 27) & 0x1F) - 15.0;

	float scale = pow(2.0, e - 9.0);
	return float3(r, g, b) * scale;
}

// RGBE8985 encode (30-bit, used for voxel light with 2 neighbor bits)
uint EncodeRGBE8985(float3 color)
{
	color = max(color, 0.0);
	float maxC = max(max(color.r, color.g), color.b);

	if (maxC < 1e-10)
		return 0;

	float exp = ceil(log2(maxC));
	float scale = pow(2.0, -exp + 8.0);

	uint r = (uint)clamp(color.r * scale, 0, 255) >> 1;
	uint g = (uint)clamp(color.g * scale, 0, 511);
	uint b = (uint)clamp(color.b * scale, 0, 255) >> 1;
	uint e = (uint)clamp(exp + 15.0, 0, 31);

	return (r) | (g << 7) | (b << 16) | (e << 23);
}

float3 DecodeRGBE8985(uint encoded)
{
	float r = (float)((encoded & 0x7F) << 1);
	float g = (float)((encoded >> 7) & 0x1FF);
	float b = (float)(((encoded >> 16) & 0x7F) << 1);
	float e = (float)((encoded >> 23) & 0x1F) - 15.0;

	float scale = pow(2.0, e - 8.0);
	return float3(r, g, b) * scale;
}

// Octahedral encoding
float2 OctEncode(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	float2 o;
	if (n.z >= 0.0) {
		o = n.xy;
	} else {
		o = (1.0 - abs(n.yx)) * (n.xy >= 0.0 ? 1.0 : -1.0);
	}
	return o * 0.5 + 0.5;
}

float3 OctDecode(float2 f)
{
	f = f * 2.0 - 1.0;
	float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	float t = saturate(-n.z);
	n.xy += n.xy >= 0.0 ? -t : t;
	return normalize(n);
}

// Spherical harmonic basis functions (L0-L3, 16 coefficients)
float SHBasis(uint idx, float3 dir)
{
	float x = dir.x, y = dir.y, z = dir.z;

	// L0
	if (idx == 0)
		return 0.282095;
	// L1
	if (idx == 1)
		return 0.488603 * y;
	if (idx == 2)
		return 0.488603 * z;
	if (idx == 3)
		return 0.488603 * x;
	// L2
	if (idx == 4)
		return 1.092548 * x * y;
	if (idx == 5)
		return 1.092548 * y * z;
	if (idx == 6)
		return 0.315392 * (3.0 * z * z - 1.0);
	if (idx == 7)
		return 1.092548 * x * z;
	if (idx == 8)
		return 0.546274 * (x * x - y * y);
	// L3
	if (idx == 9)
		return 0.590044 * y * (3.0 * x * x - y * y);
	if (idx == 10)
		return 2.890611 * x * y * z;
	if (idx == 11)
		return 0.457046 * y * (4.0 * z * z - x * x - y * y);
	if (idx == 12)
		return 0.373176 * z * (2.0 * z * z - 3.0 * x * x - 3.0 * y * y);
	if (idx == 13)
		return 0.457046 * x * (4.0 * z * z - x * x - y * y);
	if (idx == 14)
		return 1.445306 * z * (x * x - y * y);
	if (idx == 15)
		return 0.590044 * x * (x * x - 3.0 * y * y);

	return 0.0;
}

// Vogel hemisphere sampling
float3 VogelHemisphereDir(uint index, uint count, float offset)
{
	float goldenAngle = 2.399963;
	float theta = goldenAngle * (float)index + offset;
	float z = 1.0 - ((float)index + 0.5) / (float)count;
	float r = sqrt(1.0 - z * z);
	return float3(r * cos(theta), r * sin(theta), z);
}

// Unpack ProcessVoxel position
uint3 UnpackVoxelPosition(uint packed)
{
	return uint3(packed & 0x7F, (packed >> 7) & 0x7F, (packed >> 14) & 0x7F);
}

// Unpack ProcessVoxel albedo
float3 UnpackVoxelAlbedo(uint packed)
{
	float r = (float)(packed & 0x1F) / 31.0;
	float g = (float)((packed >> 5) & 0x1F) / 31.0;
	float b = (float)((packed >> 10) & 0x1F) / 31.0;
	return float3(r, g, b);
}

// Unpack voxel normal from 6-bit facing
float3 UnpackVoxelNormal(uint facingBits)
{
	float3 n = 0;
	if (facingBits & 0x01)
		n += float3(1, 0, 0);
	if (facingBits & 0x02)
		n += float3(-1, 0, 0);
	if (facingBits & 0x04)
		n += float3(0, 1, 0);
	if (facingBits & 0x08)
		n += float3(0, -1, 0);
	if (facingBits & 0x10)
		n += float3(0, 0, 1);
	if (facingBits & 0x20)
		n += float3(0, 0, -1);
	float len = length(n);
	return len > 0.0 ? n / len : float3(0, 1, 0);
}

// Get neighbor bit from ProcessVoxel
bool GetNeighborBit(ProcessVoxel v, uint neighborIdx)
{
	// 26 neighbors distributed across 4 fields
	if (neighborIdx < 11)
		return (v.position >> (21 + neighborIdx)) & 1;
	if (neighborIdx < 22)
		return (v.albedo >> (21 + neighborIdx - 11)) & 1;
	if (neighborIdx < 24)
		return (v.light >> (30 + neighborIdx - 22)) & 1;
	return (v.lightAniso >> (30 + neighborIdx - 24)) & 1;
}

// Get voxel world position from grid position and cascade data
float3 GetVoxelWorldPos(uint3 gridPos, uint cascadeIdx)
{
	return Cascades[cascadeIdx].Offset + (float3(gridPos) + 0.5) / Cascades[cascadeIdx].ToCell;
}

// Convert world position to cascade grid position
float3 WorldToCascadePos(float3 worldPos, uint cascadeIdx)
{
	return (worldPos - Cascades[cascadeIdx].Offset) * Cascades[cascadeIdx].ToCell;
}

// Light attenuation helpers
float GetOmniAttenuation(float dist, float invRadius, float decay)
{
	float nd = dist * invRadius;
	nd *= nd;
	nd *= nd;
	nd = max(1.0 - nd, 0.0);
	nd *= nd;
	return nd * pow(max(dist, 0.0001), -decay);
}

#endif  // SDFGI_COMMON_HLSLI
