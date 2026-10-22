#include "FeatureBuffer.h"

#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/Effects11.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/HorizonFix.h"
#include "Features/IBL.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/LinearLighting.h"
#include "Features/PhysicalSky.h"
#include "Features/PseudoSunBounce.h"
#include "Features/RainRefraction.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpacePointLightShadows.h"
#include "Features/ScreenSpaceReflections.h"
#include "Features/Skin.h"
#include "Features/Skylighting.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainShadows.h"
#include "Features/TerrainVariation.h"
#include "Features/VanillaFresnel.h"
#include "Features/WetnessEffects.h"
#include "TruePBR.h"

template <class... Ts>
std::pair<unsigned char*, size_t> _GetFeatureBufferData(Ts... feat_datas)
{
	size_t totalSize = (... + sizeof(Ts));
	auto data = new unsigned char[totalSize];
	size_t offset = 0;

	([&] {
		*((decltype(feat_datas)*)(data + offset)) = feat_datas;
		offset += sizeof(decltype(feat_datas));
	}(),
		...);

	return std::make_pair(data, totalSize);
}

std::pair<unsigned char*, size_t> GetFeatureBufferData(bool a_inWorld)
{
	return _GetFeatureBufferData(
		globals::features::grassLighting.settings,
		globals::features::extendedMaterials.settings,
		globals::features::dynamicCubemaps.GetCommonBufferData(),
		globals::features::terrainShadows.GetCommonBufferData(),
		globals::features::lightLimitFix.GetCommonBufferData(),
		globals::features::wetnessEffects.GetCommonBufferData(),
		globals::features::skylighting.GetCommonBufferData(a_inWorld),
		globals::features::cloudShadows.GetCommonBufferData(),
		globals::features::lodBlending.settings,
		globals::features::hairSpecular.settings,
		globals::features::terrainVariation.settings,
		globals::features::ibl.GetCommonBufferData(),
		globals::features::extendedTranslucency.GetCommonBufferData(),
		globals::features::linearLighting.GetCommonBufferData(),
		globals::features::effects11.GetCommonBufferData(),
		globals::features::terrainBlending.settings,
		globals::features::exponentialHeightFog.GetCommonBufferData(),
		globals::features::truePBR.settings,
		globals::features::screenSpaceGI.GetCommonBufferData(),
		globals::features::screenSpaceReflections.GetCommonBufferData(),
		globals::features::skin.GetCommonBufferData(),
		globals::features::screenSpacePointLightShadows.GetCommonBufferData(),
		globals::features::vanillaFresnel.settings,
		globals::features::horizonFix.GetCommonBufferData(),
		globals::features::physicalSky.cbData,
		globals::features::pseudoSunBounce.settings,
		globals::features::rainRefraction.GetCommonBufferData());
}
