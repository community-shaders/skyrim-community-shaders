#include "LightListSearchFix.h"

#include "Utils/PointerSearch.h"

namespace
{
	// Prologue through the lightListFence load, identical on SE, AE and VR.
	constexpr auto AddLightPrologue = REL::make_pattern<"48 89 54 24 10 41 56 48 83 EC 50 48 C7 44 24 30 FE FF FF FF 48 89 5C 24 60 48 89 74 24 70 48 89 7C 24 78 48 8B FA 48 8B D9 44 8B 51 18">();
	// Engine sentinel for "not rebuilding": AddLight then only appends lights it does not find.
	constexpr std::int32_t FenceUnset = -1;

	struct AddLight
	{
		static void thunk(RE::BSShaderPropertyLightData* a_this, RE::BSLight* a_light)
		{
			const auto& lights = a_this->lights;
			if (a_this->lightListFence == FenceUnset && Util::ContainsPointer(reinterpret_cast<const void* const*>(lights.data()), lights.size(), a_light))
				return;
			func(a_this, a_light);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void LightListSearchFix::Install()
{
	const REL::RelocationID addLight(100990, 107777);
	if (!REL::verify_code(addLight.address(), AddLightPrologue)) {
		logger::warn("[Engine Fixes] {} not applied: unexpected code at BSShaderPropertyLightData::AddLight (another mod or an unverified runtime)", GetName());
		return;
	}
	stl::detour_thunk<AddLight>(addLight);
}
