#pragma once

#include "BSShader.h"
#include "ShaderCompiler.h"

namespace BSShaderHooks
{
	/**
	 * @brief Hook for BSShader::LoadShaders that replaces pixel shaders with on-disk HLSL or precompiled overrides.
	 * @param bsShader The shader object whose pixel shader table will be patched.
	 * @param stream Original stream parameter (unused by the hook).
	 */
	void hk_LoadShaders(REX::BSShader* bsShader, std::uintptr_t stream);
}
