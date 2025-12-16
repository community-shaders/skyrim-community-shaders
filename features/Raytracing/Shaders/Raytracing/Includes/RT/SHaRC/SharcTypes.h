/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef SHARC_TYPES_H
#define SHARC_TYPES_H

struct SharcAccumulationData
{
    uint4 data;
};

struct SharcPackedData
{
    float16_t4 radianceData;
    uint sampleData;
    uint luminanceM2;
};

#if SHARC_ENABLE_GLSL
layout(buffer_reference, std430, buffer_reference_align = 16) buffer RWStructuredBuffer_SharcAccumulationData
{
    SharcAccumulationData data[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) buffer RWStructuredBuffer_SharcPackedData
{
    SharcPackedData data[];
};
#endif // SHARC_ENABLE_GLSL

#endif // SHARC_TYPES_H
