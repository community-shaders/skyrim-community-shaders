#pragma once
#include "Features/LightLimitFix.h"

struct ISLCommon
{
	enum class TES_LIGHT_FLAGS_EXT
	{
		kInverseSquare = 1 << 14
	};

	struct RuntimeLightDataExt
	{
		stl::enumeration<LightLimitFix::LightFlags> flags;
		float cutoffOverride;
		RE::FormID lighFormId;
		RE::NiColor diffuse;
		RE::NiPoint3 radius;
		float fade;
		std::uint32_t unk138;

		static RuntimeLightDataExt* Get(RE::NiLight* niLight)
		{
			return reinterpret_cast<RuntimeLightDataExt*>(&niLight->GetLightRuntimeData());
		}
	};
};