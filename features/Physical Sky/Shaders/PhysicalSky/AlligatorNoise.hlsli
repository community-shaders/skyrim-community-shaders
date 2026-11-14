/*
 * Copyright (c) 2026 Side Effects Software Inc. All rights reserved.
 *
 * Redistribution and use of Houdini Development Kit samples in source and
 * binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. The name of Side Effects Software may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SIDE EFFECTS SOFTWARE `AS IS' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL SIDE EFFECTS SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alligator Noise by Side Effects Software Inc., CC BY-SA 4.0.
 * https://www.sidefx.com/docs/hdk/alligator_2alligator_8_c-example.html
 * https://creativecommons.org/licenses/by-sa/4.0/
 * HLSL adaptation with periodic PCG hashing and streaming selection.
 */
#ifndef PHYSICAL_SKY_ALLIGATOR_NOISE
#define PHYSICAL_SKY_ALLIGATOR_NOISE

#include "Common/Random.hlsli"

namespace AlligatorNoise
{
	float3 Hash(int3 cell, uint3 period, uint seed, uint salt)
	{
		const int3 divisor = int3(max(period, 1u));
		const int3 wrapped = (cell % divisor + divisor) % divisor;
		cell = int3(period.x ? wrapped.x : cell.x, period.y ? wrapped.y : cell.y, period.z ? wrapped.z : cell.z);
		return float3(Random::pcg3d(asuint(cell) + uint3(seed, salt, seed ^ salt)) >> 8u) / 16777216.0;
	}

	float Sample(float3 position, uint3 period, uint seed, uint salt)
	{
		const int3 cell = int3(floor(position));
		const float3 f = frac(position);
		float largest = 0.0;
		float second = 0.0;
		[loop] for (int z = -1; z <= 1; ++z)
			[loop] for (int y = -1; y <= 1; ++y)
				[loop] for (int x = -1; x <= 1; ++x)
		{
			const int3 offset = int3(x, y, z);
			const float3 delta = float3(offset) + Hash(cell + offset, period, seed, salt) - f;
			const float t = saturate(1.0 - length(delta));
			const float contribution = Hash(cell + offset, period, seed, salt + 12u).x * t * t * (3.0 - 2.0 * t);
			second = max(second, min(largest, contribution));
			largest = max(largest, contribution);
		}
		return largest - second;
	}
}

#endif
