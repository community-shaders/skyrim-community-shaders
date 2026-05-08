#pragma once

#include <RE/B/BSRenderPass.h>

namespace ExternalEmittance
{
	bool ShouldSuppress(const RE::BSRenderPass* a_pass);
	void UpdatePermutation(const RE::BSRenderPass* a_pass);
}
