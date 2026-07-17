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

#ifndef H_FRAGMENT_LIST
#define H_FRAGMENT_LIST

#include "OIT/OITCommon.hlsli"

//////////////////////////////////////////////
// Structs
//////////////////////////////////////////////

struct FragmentListNode
{
	uint next;
	float packedDepthAndFlags;
	uint2 packedColorRGBA;
};

//////////////////////////////////////////////
// Resources
//////////////////////////////////////////////

RWTexture2D<uint> gFragmentListFirstNodeAddressUAV : register(u3);
RWStructuredBuffer<FragmentListNode> gFragmentListNodesUAV : register(u4);

Texture2D<uint> gFragmentListFirstNodeAddressSRV : register(t1);
StructuredBuffer<FragmentListNode> gFragmentListNodesSRV : register(t2);

//////////////////////////////////////////////
// Helper Functions
//////////////////////////////////////////////

int2 FL_GetDimensions()
{
	int2 dim;
	gFragmentListFirstNodeAddressSRV.GetDimensions(dim.x, dim.y);

	return dim;
}

static const float FL_HALF_MAX = 65504.0;

float FL_PackDepthAndFlags(in float depth, in uint flags)
{
	float packedDepth = saturate(1.0 - depth);
	return (flags & OIT_FLAGS_DEPTH_WRITE) ? -packedDepth : packedDepth;
}

void FL_UnpackDepthAndFlags(in float packedDepthAndFlags, out float depth, out uint flags)
{
	depth = 1.0 - abs(packedDepthAndFlags);
	flags = packedDepthAndFlags < 0 ? OIT_FLAGS_DEPTH_WRITE : 0;
}

uint FL_PackHalf2(in float2 unpackedInput)
{
	uint2 packed = f32tof16(clamp(unpackedInput, 0.0.xx, FL_HALF_MAX.xx));
	return packed.x | (packed.y << 16UL);
}

float2 FL_UnpackHalf2(in uint packedInput)
{
	return f16tof32(uint2(packedInput & 0xFFFFUL, packedInput >> 16UL));
}

uint2 FL_PackColor(in float4 unpackedInput)
{
	float4 clampedColor = clamp(unpackedInput, 0.0.xxxx, FL_HALF_MAX.xxxx);
	return uint2(FL_PackHalf2(clampedColor.xy), FL_PackHalf2(clampedColor.zw));
}

float4 FL_UnpackColor(in uint2 packedColorRGBA)
{
	return float4(FL_UnpackHalf2(packedColorRGBA.x), FL_UnpackHalf2(packedColorRGBA.y));
}


uint FL_GetFirstNodeOffset(int2 screenAddress)
{
	return gFragmentListFirstNodeAddressSRV[screenAddress];
}

bool FL_AllocNode(out uint newNodeAddress1D)
{
	// alloc a new node
	newNodeAddress1D = gFragmentListNodesUAV.IncrementCounter();

	uint maxNodes, stride;
	gFragmentListNodesUAV.GetDimensions(maxNodes, stride);

	return newNodeAddress1D < maxNodes; //SharedData::orderIndependentTransparencySettings.MaxListNodes;
}

// Insert a new node at the head of the list
void FL_InsertNode(in int2 screenAddress, in uint newNodeAddress, in FragmentListNode newNode)
{
	uint oldNodeAddress;
	InterlockedExchange(gFragmentListFirstNodeAddressUAV[screenAddress], newNodeAddress, oldNodeAddress);

	newNode.next = oldNodeAddress;
	gFragmentListNodesUAV[newNodeAddress] = newNode;
}

bool OIT_CaptureImpl(in int2 screenAddress, in float4 color, in float depth, uint flags)
{
	uint2 packedColor = FL_PackColor(color);
	// discard fully transparent black pixels
	if (packedColor.x == 0 && packedColor.y == 0)
		return true;

	uint newNodeAddress;
	if (FL_AllocNode(newNodeAddress))
	{
		FragmentListNode node;
		node.packedDepthAndFlags = FL_PackDepthAndFlags(depth, flags);
		node.packedColorRGBA = packedColor;
		FL_InsertNode(screenAddress, newNodeAddress, node);
		return true;
	}
	return false; // return original color if we failed to allocate a new node
}

FragmentListNode FL_GetNode(uint nodeAddress)
{
	return gFragmentListNodesSRV[nodeAddress];
}

#endif // H_FRAGMENT_LIST
