#include "PBRControllers.h"

#include "BSLightingShaderMaterialPBR.h"
#include "BSLightingShaderMaterialPBRLandscape.h"

namespace PBRControllers
{
	namespace
	{
		uint32_t Variable(const RE::NiTimeController* controller)
		{
			uint32_t variable;
			std::memcpy(&variable, reinterpret_cast<const uint8_t*>(controller) + 0x50, sizeof(variable));
			return variable;
		}

		RE::BSLightingShaderProperty* Target(const RE::NiTimeController* controller)
		{
			auto* property = netimmerse_cast<RE::BSLightingShaderProperty*>(controller->target);
			return property && (BSLightingShaderMaterialPBR::IsPBR(property->material) ||
								   BSLightingShaderMaterialPBRLandscape::IsPBR(property->material)) ?
			           property :
			           nullptr;
		}

		bool CommonFloat(uint32_t variable)
		{
			return variable == 0 || variable == 9 || variable == 10 || variable == 11 || variable == 12 ||
			       (variable >= 20 && variable <= 23);
		}

		struct FloatUpdate
		{
			static void thunk(RE::NiTimeController* controller, void* updateData)
			{
				if (Target(controller) && !CommonFloat(Variable(controller))) {
					return;
				}
				func(controller, updateData);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct FloatValue
		{
			static void thunk(RE::NiTimeController* controller, float* value)
			{
				auto* property = Target(controller);
				if (!property) {
					func(controller, value);
					return;
				}
				const auto* material = static_cast<const RE::BSLightingShaderMaterialBase*>(property->material);
				switch (Variable(controller)) {
				case 0:
					*value = material->refractionPower;
					break;
				case 9:
					*value = material->specularPower;
					break;
				case 10:
					*value = material->specularColorScale;
					break;
				case 11:
					*value = property->emissiveMult;
					break;
				case 12:
					*value = material->materialAlpha;
					break;
				case 20:
					*value = material->texCoordOffset[0].x;
					break;
				case 21:
					*value = material->texCoordScale[0].x;
					break;
				case 22:
					*value = material->texCoordOffset[0].y;
					break;
				case 23:
					*value = material->texCoordScale[0].y;
					break;
				default:
					*value = 0.f;
					break;
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ColorUpdate
		{
			static void thunk(RE::NiTimeController* controller, void* updateData)
			{
				if (auto* property = Target(controller)) {
					const auto variable = Variable(controller);
					if (variable > 1 || (variable == 1 && !property->emissiveColor)) {
						return;
					}
				}
				func(controller, updateData);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ColorValue
		{
			static void thunk(RE::NiTimeController* controller, RE::NiColor* value)
			{
				auto* property = Target(controller);
				if (!property) {
					func(controller, value);
					return;
				}
				const auto variable = Variable(controller);
				if (variable == 0) {
					*value = static_cast<const RE::BSLightingShaderMaterialBase*>(property->material)->specularColor;
				} else if (variable == 1 && property->emissiveColor) {
					*value = *property->emissiveColor;
				} else {
					*value = {};
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct UShortValue
		{
			static void thunk(RE::NiTimeController* controller, float* value)
			{
				if (Target(controller)) {
					*value = 0.f;
				} else {
					func(controller, value);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void Install()
	{
		stl::write_vfunc<0x27, FloatUpdate>(RE::VTABLE_BSLightingShaderPropertyFloatController[0]);
		stl::write_vfunc<0x3D, FloatValue>(RE::VTABLE_BSLightingShaderPropertyFloatController[0]);
		stl::write_vfunc<0x27, ColorUpdate>(RE::VTABLE_BSLightingShaderPropertyColorController[0]);
		stl::write_vfunc<0x3D, ColorValue>(RE::VTABLE_BSLightingShaderPropertyColorController[0]);
		stl::write_vfunc<0x3D, UShortValue>(RE::VTABLE_BSLightingShaderPropertyUShortController[0]);
	}
}
