#include "FeatureBuffer.h"

#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/Effect11.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/IBL.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/LinearLighting.h"
#include "Features/Skylighting.h"
#include "Features/TerrainBlending.h"
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

/**
 * @brief Assembles a contiguous buffer containing serialized configuration and common-data for all rendering features.
 *
 * Packs each feature's settings or common buffer payload into a single heap-allocated byte array in a fixed order and returns that buffer with its size.
 *
 * @param a_inWorld If true, provides "in-world" skylighting common-data when collecting skylighting payload.
 * @return std::pair<unsigned char*, size_t> First: pointer to a heap-allocated contiguous byte buffer containing the packed feature data. Second: size of the buffer in bytes. The caller takes ownership of the pointer and must free it (delete[]). 
 */
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
		globals::features::cloudShadows.GetCommonBufferData(),
		globals::features::lodBlending.settings,
		globals::features::hairSpecular.settings,
		globals::features::terrainVariation.settings,
		globals::features::ibl.GetCommonBufferData(),
		globals::features::extendedTranslucency.GetCommonBufferData(),
		globals::features::linearLighting.GetCommonBufferData(),
		globals::features::effect11.GetCommonBufferData(),
		globals::features::terrainBlending.settings,
		globals::features::exponentialHeightFog.settings);
}