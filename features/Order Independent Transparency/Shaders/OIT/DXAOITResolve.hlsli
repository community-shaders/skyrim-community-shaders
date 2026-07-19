#ifndef DXAOITRESOLVE_H
#define DXAOITRESOLVE_H
#define OIT_RESOLVE 1

Texture2D<unorm float> TexWaterDepth : register(t0);

#include "OIT/DXAOIT.hlsli"
#include "OIT/FragmentList.hlsli"

#if defined(OIT_DEBUG)
#define OIT_RESOLVE_FUNC AOITDebug
#elif OIT_BLENDED
#define OIT_RESOLVE_FUNC WeightBlendedOITResolve
#else
#define OIT_RESOLVE_FUNC AOITResolve
#endif

void AOITDebug(uint2 screenAddress, out float4 color, out float4 wcolor
#if OIT_WRITE_DEPTH
	, out float odepth
#endif
)
{
	float waterDepth = TexWaterDepth[screenAddress];
	waterDepth = waterDepth > 0.f ? waterDepth : OIT_EMPTY_NODE_DEPTH;
#if OIT_WRITE_DEPTH
	odepth = waterDepth;
#endif

	const float3 colors[8] =
	{
		float3(0.1, 0.1, 1.0),
		float3(0.1, 0.4, 0.8),
		float3(0.1, 0.8, 0.4),
		float3(0.1, 1.0, 0.1),
		float3(0.5, 0.5, 0.1),
		float3(0.8, 0.4, 0.1),
		float3(1.0, 0.1, 0.1),
		float3(0.8, 0.1, 0.8),
	};
	
	FragmentListNode node;
	uint firstNodeOffset = FL_GetFirstNodeOffset(screenAddress);
	// Debug visualization of the scene complexity
	int count = 0;
	[loop]
	for (uint nodeOffset = firstNodeOffset; nodeOffset; nodeOffset = node.next)
	{
		node = FL_GetNode(nodeOffset);
		count++;
	}
	count = min(count, 8);
	color = count > 0 ? float4(colors[count - 1], 1) : float4(0, 0, 0, 1);
	wcolor = float4(0, 0, 0, 1);
}

void AOITResolve(uint2 screenAddress, out float4 ocolor, out float4 owcolor
#if OIT_WRITE_DEPTH
	, out float odepth
#endif
)
{
	uint i;
	uint nodeOffset;

	// Get offset to the first node
	uint firstNodeOffset = FL_GetFirstNodeOffset(screenAddress);
	float waterDepth = TexWaterDepth[screenAddress];
	waterDepth = waterDepth > 0.f ? waterDepth : OIT_EMPTY_NODE_DEPTH;
#if OIT_WRITE_DEPTH
	odepth = waterDepth;
#endif

	FragmentListNode node;
	AOITData data;
	// Initialize AVSM data
	[unroll]
	for (i = 0; i < OIT_RT_COUNT; ++i)
	{
		data.depth[i] = OIT_EMPTY_NODE_DEPTH.xxxx;
		data.trans[i] = OIT_FIRST_NODE_TRANS.xxxx;
	}

	// Fetch all nodes and add them to our visibility function
	bool water = false;
	[loop]
	for (nodeOffset = firstNodeOffset; nodeOffset; nodeOffset = node.next)
	{
		// Get node..
		node = FL_GetNode(nodeOffset);

		float depth;
		uint flags;
		FL_UnpackDepthAndFlags(node.packedDepthAndFlags, depth, flags);
		float4 nodeColor = FL_UnpackColor(node.packedColorRGBA);

		[flatten]
		if (depth > waterDepth)
			water = true;

		// Additive blend does not affect visibility function
		if (nodeColor.w >= 0.00392156863)
		{
			float vis = saturate(1.0 - nodeColor.w);
			AOITInsertFragment(depth, vis, data);
		}
	}
	float3 color = float3(0, 0, 0); // color with pixels in front of water surface
	float3 wcolor = float3(0, 0, 0); // color with pixels behind water surface

	// Fetch all nodes again and composite them
	[loop]
	for (nodeOffset = firstNodeOffset; nodeOffset; nodeOffset = node.next)
	{
		// Get node..
		node = FL_GetNode(nodeOffset);

		float depth;
		uint flags;
		FL_UnpackDepthAndFlags(node.packedDepthAndFlags, depth, flags);
#if OIT_WRITE_DEPTH
		if (flags & OIT_FLAGS_DEPTH_WRITE) odepth = min(odepth, depth);
#endif
		
		float4 nodeColor = FL_UnpackColor(node.packedColorRGBA);
		
		AOITFragment frag = AOITFindFragment(data, depth);
		float vis = frag.index == 0 ? 1.0f : frag.transA;
		float3 visColor = nodeColor.xyz * vis.xxx;

		wcolor += visColor; // Accumulate pixels behind water surface in RT_ALPHA for refraction in next frame
		[flatten]
		if (depth <= waterDepth)
			color += visColor;
	}

	float wtrans = data.trans[OIT_RT_COUNT - 1][3];
	float trans;
	if (!water)
	{
		trans = wtrans;
	}
	else
	{
		AOITFragment wfrag = AOITFindFragment(data, waterDepth);
		trans = wfrag.index == 0 ? 1.0 : wfrag.transA;
	}
	
	ocolor = float4(color, trans);
	owcolor = float4(wcolor, wtrans);
}

void WeightBlendedOITResolve(uint2 screenAddress, out float4 ocolor, out float4 owcolor
#if OIT_WRITE_DEPTH
	, out float odepth
#endif
)
{
	// Get offset to the first node
	uint firstNodeOffset = FL_GetFirstNodeOffset(screenAddress);
	float waterDepth = TexWaterDepth[screenAddress];
	waterDepth = waterDepth > 0.f ? waterDepth : OIT_EMPTY_NODE_DEPTH;
#if OIT_WRITE_DEPTH
	odepth = waterDepth;
#endif

	// Weighted, Blended, OIT for reference
	float trans = 1.f;
	float asum = 0.0001f;
	float3 csum = 0.0001f.xxx;
	// Accumulate fragments behind water surface separately
	float wtrans = 1.f;
	float wasum = 0.f;
	float3 wcsum = 0.f.xxx;

	FragmentListNode node;
	[loop]
	for (uint nodeOffset = firstNodeOffset; nodeOffset; nodeOffset = node.next)
	{
		node = FL_GetNode(nodeOffset);

		float depth;
		uint flags;
		FL_UnpackDepthAndFlags(node.packedDepthAndFlags, depth, flags);
#if OIT_WRITE_DEPTH
		if (flags & OIT_FLAGS_DEPTH_WRITE) odepth = min(odepth, depth);
#endif
		float4 color = FL_UnpackColor(node.packedColorRGBA);
		float a = max(0.01, color.w); // wboit does not support additive natrually
	
		//float w = 1.f; // weight(alpha, depth), just need to be higher for closer layers
		float d = depth;
		float w = max(0.01, 3000 * (1 - d) * (1 - d) * (1 - d));
		w *= color.w > 0.f ? a : 0.2; // force weight of additive blend layer
		if (depth <= waterDepth)
		{
			csum += w * color.xyz;
			asum += w * a;
			trans *= (1.f - a);
		}
		else
		{
			wcsum += w * color.xyz;
			wasum += w * a;
			wtrans *= (1.f - a);
		}
	}
	float3 color = (1 - trans) * csum / asum;
	
	ocolor = float4(color, trans);
	
	csum += wcsum;
	asum += wasum;
	trans *= wtrans;
	color = (1 - trans) * csum / asum;

	owcolor = float4(color, trans);
}
#endif
