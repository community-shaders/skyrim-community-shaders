#ifndef SKINNING_HLSL
#define SKINNING_HLSL

struct Skinning
{
	half weight[4];

#ifdef __cplusplus
	uint8_t bone[4];
#else
	uint bone;
#endif

#ifdef __cplusplus
	Skinning() = default;

	Skinning(eastl::vector<half> weights, eastl::vector<uint8_t> boneIds)
	{
		auto weightCount = weights.size();
		auto boneIdsCount = boneIds.size();

		for (size_t i = 0; i < 4; i++) {
			weight[i] = i < weightCount ? weights[i] : half(0.0f);
			bone[i] = i < boneIdsCount ? boneIds[i] : 0;
		}
	}
#else
	uint GetBone(uint idx)
	{
		uint shift = idx * 8;
		return (bone >> shift) & 0xFF;
	}
#endif
};

#endif