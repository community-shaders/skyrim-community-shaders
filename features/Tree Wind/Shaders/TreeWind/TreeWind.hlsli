#ifndef __TREE_WIND_HLSLI__
#define __TREE_WIND_HLSLI__

// Wind sway for tree LOD. The Tree Wind feature binds the wind buffer for every Lighting and Utility
// draw, with Params.z set only while it draws a DynDOLOD tree LOD shape, and for every standard tree LOD
// (DistantTree) draw.
namespace TreeWind
{
	cbuffer TreeWindCB : register(b3)
	{
		float4 Wind;    // phase, previous phase, wind speed 0-1, wind angle (radians)
		float4 Params;  // amplitude, near distance, enabled, unused
		float4 Shape;   // bend exponent, use bend weights, flutter strength, per-tree phase spread
		float4 Detail;  // flutter share, flutter speed, near scale, near ramp end
		float4 Gusts;   // gust strength, gust rarity, unused, unused
	};

	// Per vertex: bend weight, tree random, tree scale, distance from the trunk
	Buffer<float4> VertexData : register(t120);

	bool IsEnabled()
	{
		return Params.z > 0.5;
	}

	float2 Sway(float3 positionMS, float phase)
	{
		// Long-wavelength gusts travelling across the land, plus some sideways spread
		precise float p = dot(positionMS.xy, float2(0.00047, 0.00061)) + dot(positionMS.xy, float2(0.0013, -0.0011));
		precise float gust = sin(phase * 1.10 + p) + 0.35 * sin(phase * 2.70 + p * 1.7);
		precise float side = 0.30 * sin(phase * 1.90 + p * 1.3);
		precise float2 dir = float2(sin(Wind.w), cos(Wind.w));
		precise float2 perp = float2(-dir.y, dir.x);
		precise float strength = Params.x * (0.35 + 0.65 * saturate(Wind.z));
		precise float2 result = (dir * (0.5 + 0.5 * gust) + perp * side) * strength;
		return result;
	}

	// Gentler sway in the LOD band next to the loaded trees, full sway from the ramp end outwards
	float GetNearScale(float3x4 world, float3 positionMS)
	{
		precise float3 positionWS = mul(world, float4(positionMS, 1.0));
		precise float dist = length(positionWS);
		return lerp(Detail.z, 1.0, saturate((dist - Params.y) / max(Detail.w - Params.y, 1.0)));
	}

	float2 GetOffset(float3 positionMS, float phase, uint vertexID, float nearScale)
	{
		float4 data = VertexData.Load(vertexID);
		float bend = Shape.y > 0.5 ? pow(saturate(data.x), max(Shape.x, 0.05)) : 1.0;
		float scale = bend * data.z * nearScale;
		float2 offset = Sway(positionMS, phase + data.y * Shape.w) * scale;

		// A rare, short, strong gust per tree
		float gustPulse = pow(saturate(sin(phase * 0.23 + data.y * 37.0 + dot(positionMS.xy, float2(0.00031, 0.00027)))), max(Gusts.y, 1.0));
		offset *= 1.0 + Gusts.x * gustPulse;

		// Branch-tip flutter on a share of the trees, with its own fast timing per vertex
		float h = frac(vertexID * 0.61803399) * 6.2831853;
		float flutter = (frac(data.y * 7.31) < Detail.x) ? sin(phase * Detail.y + h) * Params.x * Shape.z * saturate(data.w) * scale : 0.0;
		return offset + float2(sin(h), cos(h)) * flutter;
	}

	// Standard tree LOD: one billboard per tree, so the instance gives the tree's foot and size directly.
	// a_heightMS is the vertex height above the foot and a_cameraDistance its distance from the camera.
	float2 GetBillboardOffset(float3 a_footWS, float a_heightMS, float a_instanceScale, float a_cameraDistance, float phase)
	{
		// precise throughout: the depth and colour shaders must compute the same offset to the bit
		// The quad's top vertices bend, its foot stays put
		precise float bend = saturate(a_heightMS * a_instanceScale / 64.0);
		precise float random = frac(sin(dot(floor(a_footWS.xy), float2(12.9898, 78.233))) * 43758.5453);
		// Same world-space sway as the DynDOLOD shapes: height-scaled, as at LOD4
		precise float scale = clamp(a_heightMS * a_instanceScale / 1600.0, 0.25, 1.25) * (0.8 + 0.4 * random) * 4.0;
		precise float nearScale = lerp(Detail.z, 1.0, saturate((a_cameraDistance - Params.y) / max(Detail.w - Params.y, 1.0)));
		precise float2 offset = Sway(a_footWS, phase + random * Shape.w) * scale * bend * nearScale;

		precise float gustPulse = pow(saturate(sin(phase * 0.23 + random * 37.0 + dot(a_footWS.xy, float2(0.00031, 0.00027)))), max(Gusts.y, 1.0));
		precise float2 result = offset * (1.0 + Gusts.x * gustPulse);
		return result;
	}
}

#endif  // __TREE_WIND_HLSLI__
