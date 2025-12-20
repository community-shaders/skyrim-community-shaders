/*
 * Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#define SHARC
#define SHARC_RESOLVE 1

#include "Raytracing/Includes/RT/SHaRC.hlsli"
#include "Raytracing/Includes/Types.hlsli"

#define LINEAR_BLOCK_SIZE                   256

ConstantBuffer<FrameData>                   Frame                       : register(b0, space0);

RWStructuredBuffer<uint64_t>                u_SharcHashEntriesBuffer    : register(u0, space0);
RWStructuredBuffer<SharcAccumulationData>   u_SharcAccumulationBuffer   : register(u1, space0);
RWStructuredBuffer<SharcPackedData>         u_SharcResolvedBuffer       : register(u2, space0);

#include "Raytracing/Includes/RT/SHaRCHelper.hlsli"

[numthreads(LINEAR_BLOCK_SIZE, 1, 1)]
void main(in uint2 did : SV_DispatchThreadID)
{
    SharcParameters sharcParameters = GetSharcParameters();
    SharcResolveParameters resolveParameters = GetSharcResolveParameters();

    SharcResolveEntry(did.x, sharcParameters, resolveParameters);
}