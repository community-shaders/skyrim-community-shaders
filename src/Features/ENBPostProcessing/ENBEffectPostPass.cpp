#include "ENBEffectPostPass.h"
#include <imgui.h>

void ENBEffectPostPass::Execute(RE::BSGraphics::RenderTargetData& input,
	RE::BSGraphics::RenderTargetData& swap,
	RE::BSGraphics::RenderTargetData& output)
{
	ExecuteTechniqueSequence(GetSelectedTechnique(), input, swap, output);
}

void ENBEffectPostPass::UpdateEffectVariables()
{
	if (!effect)
		return;
}