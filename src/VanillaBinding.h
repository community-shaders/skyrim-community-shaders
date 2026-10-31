#pragma once

namespace RE
{
	class BSRenderPass;
}

namespace VanillaBinding
{
	void BindSharedConstantBuffers();
	void BindSceneDepth();
	void BindSkyResources(const RE::BSRenderPass* a_pass);
	void BindDrawResources();
}
