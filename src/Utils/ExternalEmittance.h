#pragma once

namespace RE
{
	class BSRenderPass;
}

namespace ExternalEmittance
{
	bool ShouldSuppress(const RE::BSRenderPass* a_pass);
	void UpdatePermutation(const RE::BSRenderPass* a_pass);
}
