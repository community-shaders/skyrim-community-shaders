#include "ExternalEmittance.h"

#include "Globals.h"
#include "State.h"
#include "Util.h"

namespace ExternalEmittance
{
	using ShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;
	static constexpr auto ExternalEmittanceFlag = ShaderPropertyFlag::kExternalEmittance;
	static constexpr auto SuppressionDescriptor = static_cast<std::uint32_t>(State::ExtraShaderDescriptors::SuppressExternalEmittance);

	static RE::TESObjectREFR* GetReference(const RE::BSGeometry* a_geometry)
	{
		return a_geometry ? a_geometry->GetUserData() : nullptr;
	}

	static const RE::ExtraEmittanceSource* GetEmittanceSource(const RE::TESObjectREFR* a_ref)
	{
		return a_ref ? a_ref->extraList.GetByType<RE::ExtraEmittanceSource>() : nullptr;
	}

	static bool HasEmittanceSource(const RE::BSGeometry* a_geometry)
	{
		const auto* source = GetEmittanceSource(GetReference(a_geometry));
		return source && source->source;
	}

	static bool HasExternalEmittance(const RE::BSShaderProperty* a_shaderProperty)
	{
		return a_shaderProperty && a_shaderProperty->flags.any(ExternalEmittanceFlag);
	}

	static bool ShouldSuppress(const RE::BSShaderProperty* a_shaderProperty, const RE::BSGeometry* a_geometry)
	{
		return Util::IsInterior() && HasExternalEmittance(a_shaderProperty) && !HasEmittanceSource(a_geometry);
	}

	bool ShouldSuppress(const RE::BSRenderPass* a_pass)
	{
		return a_pass && ShouldSuppress(a_pass->shaderProperty, a_pass->geometry);
	}

	void UpdatePermutation(const RE::BSRenderPass* a_pass)
	{
		assert(globals::state);
		if (!globals::state) {
			return;
		}

		auto& descriptor = globals::state->permutationData.ExtraShaderDescriptor;
		descriptor &= ~SuppressionDescriptor;
		if (ShouldSuppress(a_pass)) {
			descriptor |= SuppressionDescriptor;
		}
	}
}
