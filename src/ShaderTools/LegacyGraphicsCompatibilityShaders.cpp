#include "LegacyGraphicsCompatibility.h"

#include "Utils/VersionedRelocation.h"

namespace LegacyGraphicsCompatibility
{
	namespace detail
	{
		void InstallShaderAdapters();
	}

	namespace
	{
		thread_local std::optional<bool> depthOfFieldSelectorContext;

		class ScopedDepthOfFieldSelectorContext
		{
		public:
			explicit ScopedDepthOfFieldSelectorContext(bool a_value) : previous(depthOfFieldSelectorContext)
			{
				depthOfFieldSelectorContext = a_value;
			}

			~ScopedDepthOfFieldSelectorContext()
			{
				depthOfFieldSelectorContext = previous;
			}

			ScopedDepthOfFieldSelectorContext(const ScopedDepthOfFieldSelectorContext&) = delete;
			ScopedDepthOfFieldSelectorContext& operator=(const ScopedDepthOfFieldSelectorContext&) = delete;

		private:
			std::optional<bool> previous;
		};

		template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType>
		inline constexpr bool usesDepthOfFieldParentSelector =
			EffectType == RE::ImageSpaceManager::ISDepthOfField ||
			EffectType == RE::ImageSpaceManager::ISDepthOfFieldFogged ||
			EffectType == RE::ImageSpaceManager::ISDepthOfFieldMaskedFogged ||
			EffectType == RE::ImageSpaceManager::ISDistantBlur ||
			EffectType == RE::ImageSpaceManager::ISDistantBlurFogged ||
			EffectType == RE::ImageSpaceManager::ISDistantBlurMaskedFogged;

		consteval bool ValidateImageSpaceSelectorTranslations()
		{
			using enum RE::ImageSpaceManager::ImageSpaceEffectEnum;
			constexpr std::array expected{
				std::tuple{ ISDoubleVision, std::size_t{ 0 }, ImageSpaceSelectorSource::kAlwaysAdjusted },
				std::tuple{ ISDepthOfField, std::size_t{ 2 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
				std::tuple{ ISDepthOfFieldFogged, std::size_t{ 2 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
				std::tuple{ ISDepthOfFieldMaskedFogged, std::size_t{ 2 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
				std::tuple{ ISDistantBlur, std::size_t{ 2 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
				std::tuple{ ISDistantBlurFogged, std::size_t{ 2 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
				std::tuple{ ISDistantBlurMaskedFogged, std::size_t{ 2 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
				std::tuple{ ISRadialBlur, std::size_t{ 7 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
				std::tuple{ ISRadialBlurMedium, std::size_t{ 7 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
				std::tuple{ ISRadialBlurHigh, std::size_t{ 7 }, ImageSpaceSelectorSource::kImageSpaceEffectUnk88 },
			};

			for (const auto& [effect, index, source] : expected) {
				const auto translation = GetLegacyImageSpaceSelectorTranslation(effect);
				if (!translation || translation->floatIndex != index || translation->source != source) {
					return false;
				}

				std::array constants{ -1.0F, -1.0F, -1.0F, -1.0F,
					-1.0F, -1.0F, -1.0F, -1.0F };
				if (!TranslateLegacyImageSpaceConstants(effect, false, constants) ||
					constants[index] != (source == ImageSpaceSelectorSource::kAlwaysAdjusted ? 1.0F : 0.0F) ||
					!TranslateLegacyImageSpaceConstants(effect, true, constants) || constants[index] != 1.0F) {
					return false;
				}
			}

			std::array tooSmall{ -1.0F, -1.0F };
			return !TranslateLegacyImageSpaceConstants(ISRadialBlur, true, tooSmall) &&
			       !TranslateLegacyImageSpaceConstants(ISMinify, true, tooSmall);
		}

		static_assert(ValidateImageSpaceSelectorTranslations());

		struct ImageSpaceEffectDepthOfField_Render
		{
			static void thunk(RE::ImageSpaceEffectDepthOfField* a_effect, RE::BSTriShape* a_shape, RE::ImageSpaceEffectParam* a_param)
			{
				if (!IsLegacyVersion() || !a_effect) {
					func(a_effect, a_shape, a_param);
					return;
				}

				const ScopedDepthOfFieldSelectorContext selectorContext(a_effect->unk88);
				func(a_effect, a_shape, a_param);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType>
		struct BSImagespaceShader_Render
		{
			static void thunk(void* a_imageSpaceShader, RE::BSTriShape* a_shape, RE::ImageSpaceEffectParam* a_param)
			{
				if (!IsLegacyVersion()) {
					func(a_imageSpaceShader, a_shape, a_param);
					return;
				}

				const auto translation = GetLegacyImageSpaceSelectorTranslation(EffectType);
				auto* shaderParam = static_cast<RE::ImageSpaceShaderParam*>(a_param);
				if (!translation || !a_imageSpaceShader || !shaderParam || !shaderParam->pixelConstantGroup ||
					translation->floatIndex >= shaderParam->pixelConstantGroupSize) {
					func(a_imageSpaceShader, a_shape, a_param);
					return;
				}

				auto constants = std::span(
					shaderParam->pixelConstantGroup,
					static_cast<std::size_t>(shaderParam->pixelConstantGroupSize));
				float* const selector = &constants[translation->floatIndex];
				const float previous = *selector;
				struct RestoreSelector
				{
					float* address;
					float value;
					~RestoreSelector() { *address = value; }
				} restore{ selector, previous };

				bool imageSpaceEffectUnk88 = static_cast<RE::ImageSpaceEffect*>(a_imageSpaceShader)->unk88;
				if constexpr (usesDepthOfFieldParentSelector<EffectType>) {
					if (depthOfFieldSelectorContext) {
						imageSpaceEffectUnk88 = *depthOfFieldSelectorContext;
					}
				}
				(void)TranslateLegacyImageSpaceConstants(EffectType, imageSpaceEffectUnk88, constants);
				func(a_imageSpaceShader, a_shape, a_param);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	bool IsLegacyVersion() noexcept
	{
		return !Util::VersionedRelocation::IsAtLeastAE1799();
	}

	void BindLegacyGrassPerGeometryToPixelShader()
	{
		if (!IsLegacyVersion()) {
			return;
		}

		constexpr std::size_t grassPerGeometrySize = 22 * 4 * sizeof(float);
		static_assert(grassPerGeometrySize == 0x160);

		const auto level = RE::BSGraphics::ConstantGroupLevel::PerGeometry;
		auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
		if (!shadowState) {
			logger::error("Cannot bind the Skyrim 1.7 grass pixel contract without renderer shadow state");
			return;
		}

		auto& vertexGroup = shadowState->GetVSConstantGroup(level);
		if (!vertexGroup.buffer) {
			return;
		}
		REX::W32::D3D11_BUFFER_DESC bufferDesc{};
		vertexGroup.buffer->GetDesc(&bufferDesc);
		if (bufferDesc.byteWidth < grassPerGeometrySize) {
			logger::error("Grass vertex PerGeometry buffer is {} bytes; the Skyrim 1.7 contract requires {}", bufferDesc.byteWidth, grassPerGeometrySize);
			return;
		}

		auto& pixelGroup = shadowState->GetPSConstantGroup(level);
		struct RestoreBuffer
		{
			REX::W32::ID3D11Buffer*& slot;
			REX::W32::ID3D11Buffer* value;
			~RestoreBuffer() { slot = value; }
		} restore{ pixelGroup.buffer, pixelGroup.buffer };
		pixelGroup.buffer = vertexGroup.buffer;
		RE::BSGraphics::Renderer::ApplyPSConstantGroup(level);
	}

	void detail::InstallShaderAdapters()
	{
		if (!IsLegacyVersion()) {
			return;
		}

		logger::info("Installing legacy image-space shader contract adapters");
		stl::write_vfunc<0x1, ImageSpaceEffectDepthOfField_Render>(RE::VTABLE_ImageSpaceEffectDepthOfField[0]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDoubleVision>>(
			RE::VTABLE_BSImagespaceShaderDoubleVision[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfField>>(
			RE::VTABLE_BSImagespaceShaderDepthOfField[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldFogged[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldMaskedFogged[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlur>>(
			RE::VTABLE_BSImagespaceShaderDistantBlur[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlurFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurFogged[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlurMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurMaskedFogged[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlur>>(
			RE::VTABLE_BSImagespaceShaderRadialBlur[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlurMedium>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurMedium[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlurHigh>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurHigh[3]);
	}
}
