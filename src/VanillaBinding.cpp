#include "VanillaBinding.h"

#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

namespace VanillaBinding
{
	void BindSharedConstantBuffers()
	{
		auto* state = globals::state;
		auto* context = globals::d3d::context;
		ID3D11Buffer* buffers[] = { state->permutationCB->CB(), state->sharedDataCB->CB(), state->featureDataCB->CB() };
		context->PSSetConstantBuffers(4, static_cast<UINT>(std::size(buffers)), buffers);
		context->CSSetConstantBuffers(5, 2, buffers + 1);
	}

	void BindSceneDepth()
	{
		auto* srv = Util::GetCurrentSceneDepthSRV(true);
		globals::d3d::context->PSSetShaderResources(17, 1, &srv);
	}

	void BindSkyResources(const RE::BSRenderPass* a_pass)
	{
		if (!a_pass || !a_pass->shaderProperty)
			return;

		const auto* skyProperty = static_cast<const RE::BSSkyShaderProperty*>(a_pass->shaderProperty);
		if (skyProperty->uiSkyObjectType != RE::BSSkyShaderProperty::SkyObject::SO_SUN_GLARE)
			return;

		auto* state = globals::state;
		auto* context = globals::d3d::context;
		ID3D11Buffer* buffers[] = { state->permutationCB->CB(), state->sharedDataCB->CB() };
		context->VSSetConstantBuffers(4, static_cast<UINT>(std::size(buffers)), buffers);
		// The sky draws with the z-prepass copy as its DSV, which would null an SRV of it; kMAIN holds the same depth
		auto* depthSRV = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
		context->VSSetShaderResources(17, 1, &depthSRV);
	}

	void BindDrawResources()
	{
		if (!globals::shaderCache->IsEnabled())
			return;

		const auto* state = globals::state;
		const auto* shader = state->currentShader;
		if (!state->customPixelShader || !shader || shader->shaderType.get() != RE::BSShader::Type::ImageSpace)
			return;

		if (std::string_view(static_cast<const RE::BSImagespaceShader*>(shader)->name) == "BSImagespaceShaderReflectionsRayTracing") {
			auto* reflections = globals::game::renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS].SRV;
			globals::d3d::context->PSSetShaderResources(4, 1, &reflections);
			globals::game::shadowState->GetRuntimeData().PSResourceModifiedBits |= (1u << 4);
		}
	}
}
