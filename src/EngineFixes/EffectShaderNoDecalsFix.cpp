#include "EffectShaderNoDecalsFix.h"

void EffectShaderNoDecalsFix::Install()
{
	stl::write_vfunc<0x27, BSEffectShaderProperty_SetupGeometry>(RE::VTABLE_BSEffectShaderProperty[0]);
}

bool EffectShaderNoDecalsFix::BSEffectShaderProperty_SetupGeometry::thunk(RE::BSEffectShaderProperty* a_property, RE::BSGeometry* a_geometry)
{
	auto result = func(a_property, a_geometry);

	if (a_property && a_geometry && a_property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSoftEffect)) {
		a_geometry->GetFlags().set(RE::NiAVObject::Flag::kNoDecals);
	}

	return result;
}
