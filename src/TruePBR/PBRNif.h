#pragma once

#include "BSLightingShaderMaterialPBR.h"

namespace PBRNif
{
	using LoadFunction = void (*)(RE::BSLightingShaderProperty*, RE::NiStream&);
	/** @brief Bounds and validates PBR blocks before invoking the native property reader. */
	void Load(RE::BSLightingShaderProperty* property, RE::NiStream& stream, LoadFunction original);
	/** @brief Rejects invalid PBR materials and flags requiring a different memory layout. */
	bool CanRender(const RE::BSShaderProperty* property);
}
