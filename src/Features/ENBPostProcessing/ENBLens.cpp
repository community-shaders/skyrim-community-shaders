#include "ENBLens.h"
#include <imgui.h>

void ENBLens::Execute(RE::BSGraphics::RenderTargetData& input,
	RE::BSGraphics::RenderTargetData& swap,
	RE::BSGraphics::RenderTargetData& output)
{
	ExecuteTechniqueSequence(GetSelectedTechnique(), input, swap, output);
}

void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;
}