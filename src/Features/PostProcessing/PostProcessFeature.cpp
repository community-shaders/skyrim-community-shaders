#include "PostProcessFeature.h"

#include "CODBloom.h"
#include "ColourTransforms.h"
#include "HistogramAutoExposure.h"
#include "LUT.h"
#include "Vignette.h"
#include "pCamera.h"
#include "LensFlare.h"
#include "VanillaImagespace.h"
#include "DoF.h"

template <class T>
std::pair<std::string, PostProcessFeatureConstructor> GetFeatureConstructorPair()
{
	return { T().GetType(), { []() { return new T(); }, T().GetType(), T().GetDesc() } };
};

const ankerl::unordered_dense::map<std::string, PostProcessFeatureConstructor>& PostProcessFeatureConstructor::GetFeatureConstructors()
{
	static ankerl::unordered_dense::map<std::string, PostProcessFeatureConstructor> retval = {
		GetFeatureConstructorPair<CODBloom>(),
		GetFeatureConstructorPair<HistogramAutoExposure>(),
		GetFeatureConstructorPair<Vignette>(),
		GetFeatureConstructorPair<ColourTransforms>(),
		GetFeatureConstructorPair<LUT>(),
		GetFeatureConstructorPair<Camera>(),
		GetFeatureConstructorPair<LensFlare>(),
		GetFeatureConstructorPair<VanillaImagespace>(),
		GetFeatureConstructorPair<DoF>(),
	};
	return retval;
}
