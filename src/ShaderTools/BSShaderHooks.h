#pragma once

#include "BSShader.h"
#include "ShaderCompiler.h"

namespace BSShaderHooks
{
	void hk_LoadShaders(REX::BSShader* bsShader, std::uintptr_t stream);
}
