#ifndef __TREE_LOD_WIND_HLSLI__
#define __TREE_LOD_WIND_HLSLI__

// Wind sway for DynDOLOD billboard tree LOD. The Windswept Tree LOD plugin binds both resources,
// and sets Params.z only while it draws a tree LOD shape. Params.w carries the plugin's tag, so a draw
// with nothing or another feature's buffer at b13 (GrassOptimizations binds one) is untouched.
namespace TreeLODWind
{
	static const uint kBindingTag = 0x574E4454u;  // must match the plugin

	cbuffer TreeLODWindCB : register(b13)
	{
		float4 Wind;    // phase, previous phase, wind speed 0-1, wind angle (radians)
		float4 Params;  // amplitude, near distance, enabled, binding tag
		float4 Shape;   // bend exponent, use bend weights, flutter strength, per-tree phase spread
		float4 Detail;  // flutter share, flutter speed, near scale, near ramp end
		float4 Gusts;   // gust strength, gust rarity, unused, unused
	};

	// Per vertex: bend weight, tree random, tree scale, distance from the trunk
	Buffer<float4> VertexData : register(t120);

	bool IsEnabled()
	{
		return Params.z > 0.5 && asuint(Params.w) == kBindingTag;
	}

	float2 Sway(float3 positionMS, float phase)
	{
		// Long-wavelength gusts travelling across the land, plus some sideways spread
		float p = dot(positionMS.xy, float2(0.00047, 0.00061)) + dot(positionMS.xy, float2(0.0013, -0.0011));
		float gust = sin(phase * 1.10 + p) + 0.35 * sin(phase * 2.70 + p * 1.7);
		float side = 0.30 * sin(phase * 1.90 + p * 1.3);
		float2 dir = float2(sin(Wind.w), cos(Wind.w));
		float2 perp = float2(-dir.y, dir.x);
		float strength = Params.x * (0.35 + 0.65 * saturate(Wind.z));
		return (dir * (0.5 + 0.5 * gust) + perp * side) * strength;
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
}

#endif  // __TREE_LOD_WIND_HLSLI__
