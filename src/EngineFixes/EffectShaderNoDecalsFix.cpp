#include "EffectShaderNoDecalsFix.h"

void EffectShaderNoDecalsFix::Install()
{
	stl::write_vfunc<0x19, BSTriShape_LinkObject>(RE::VTABLE_BSTriShape[0]);
}

void EffectShaderNoDecalsFix::BSTriShape_LinkObject::thunk(RE::BSTriShape* a_this, RE::NiStream& a_stream)
{
	func(a_this, a_stream);

	if (auto* prop = a_this->GetGeometryRuntimeData().shaderProperty.get()) {
		if (prop->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSoftEffect)) {
			a_this->GetFlags().set(RE::NiAVObject::Flag::kNoDecals);
		}
	}
}
