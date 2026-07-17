/////////////////////////////////////////////////////////////////////////////////////////////
// Copyright 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/////////////////////////////////////////////////////////////////////////////////////////////

#ifndef H_AIOT_RESOLVE
#define H_AIOT_RESOLVE
#define OIT_RESOLVE_FUNC AOITResolve

Texture2D<unorm float> TexWaterDepth : register(t0);
#include "OIT/AOIT.hlsli"

void AOITResolve(uint2 pixelAddr, out float4 ocolor, out float4 wcolor
#if OIT_WRITE_DEPTH
	, out float odepth
#endif
)
{
	ocolor = float4(0, 0, 0, 1);
	wcolor = float4(0, 0, 0, 1);

	float waterDepth = TexWaterDepth[pixelAddr];
	waterDepth = waterDepth > 0.f ? waterDepth : OIT_EMPTY_NODE_DEPTH;
#if OIT_WRITE_DEPTH
	odepth = waterDepth;
#endif

	// Load control surface
	AOITCtrlSurface ctrlSurface;
	AOITLoadControlSurfaceSRV(pixelAddr, ctrlSurface);

	// Any transparent fragment contributing to this pixel?
	if (!ctrlSurface.clear)
	{
		// Load all nodes for this pixel    
		ATSPNode nodeArray[OIT_NODE_COUNT];
		AOITSPLoadDataSRV(pixelAddr, nodeArray);

		// Accumulate final transparent colors
		float trans = 1;
		float3 color = 0;
		float3 wcolor3 = 0;
		float wtrans = 1;
		[unroll]
		for (uint i = 0; i < OIT_NODE_COUNT; i++)
		{
			color += trans * nodeArray[i].color;
			trans = nodeArray[i].trans;
			[flatten]
			if (nodeArray[i].depth < waterDepth)
			{
				wcolor3 = color;
				wtrans = trans;
			}
		}
		wcolor = float4(color, trans);
		ocolor = float4(wcolor3, wtrans);
	}
	// wcolor = ocolor;
}

#endif // H_AIOT_RESOLVE
