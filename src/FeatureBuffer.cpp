#include "FeatureBuffer.h"

#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ENBPostProcessing.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/IBL.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/Skylighting.h"
#include "Features/TerrainShadows.h"
#include "Features/TerrainVariation.h"
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
		globals::features::dynamicCubemaps.settings,
		globals::features::terrainShadows.GetCommonBufferData(),
		globals::features::lightLimitFix.GetCommonBufferData(),
		globals::features::wetnessEffects.GetCommonBufferData(),
		globals::features::skylighting.GetCommonBufferData(a_inWorld),
		globals::features::cloudShadows.settings,
		globals::features::lodBlending.settings,
		globals::features::hairSpecular.settings,
		globals::features::terrainVariation.settings,
		globals::features::ibl.settings,
		globals::features::extendedTranslucency.GetCommonBufferData(),
		globals::features::enbPostProcessing.GetCommonBufferData());
}