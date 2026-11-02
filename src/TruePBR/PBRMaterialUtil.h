#pragma once

#include "TruePBR.h"

#include <atomic>
#include <bit>
#include <concepts>

namespace PBRMaterialUtil
{
	using Key = std::vector<uint8_t>;

	/** @brief Uses the engine material's lock word for texture replacement. */
	class TextureLock
	{
	public:
		explicit TextureLock(uint32_t& word) :
			word(word)
		{
			while (this->word.exchange(1, std::memory_order_acquire)) {
				Sleep(0);
			}
		}
		~TextureLock() { word.store(0, std::memory_order_release); }
		TextureLock(const TextureLock&) = delete;
		TextureLock& operator=(const TextureLock&) = delete;

	private:
		std::atomic_ref<uint32_t> word;
	};

	template <std::integral T>
	void Append(Key& key, T value)
	{
		for (size_t i = 0; i < sizeof(T); ++i) {
			key.push_back(static_cast<uint8_t>(static_cast<uint64_t>(value) >> (i * 8)));
		}
	}

	inline void Append(Key& key, float value)
	{
		Append(key, std::bit_cast<uint32_t>(value == 0.f ? 0.f : value));
	}

	inline void Append(Key& key, const RE::NiColor& value)
	{
		Append(key, value.red);
		Append(key, value.green);
		Append(key, value.blue);
	}

	inline void Append(Key& key, const GlintParameters& value)
	{
		Append(key, value.enabled);
		Append(key, value.screenSpaceScale);
		Append(key, value.logMicrofacetDensity);
		Append(key, value.microfacetRoughness);
		Append(key, value.densityRandomization);
	}

	template <class T>
	void Append(Key& key, const RE::NiPointer<T>& value)
	{
		Append(key, reinterpret_cast<uintptr_t>(value.get()));
	}

	inline bool IsTextureSet(const RE::BSTextureSet* textureSet)
	{
		if (!textureSet) {
			return false;
		}
		static const auto* type = REL::Relocation<const RE::NiRTTI*>{ RE::BSTextureSet::Ni_RTTI }.get();
		const auto* rtti = textureSet->GetRTTI();
		return rtti && rtti->IsKindOf(type);
	}

	inline Key BaseKey(const RE::BSLightingShaderMaterialBase& material)
	{
		Key key;
		Append(key, static_cast<uint32_t>(material.GetFeature()));
		for (uint32_t i = 0; i < 2; ++i) {
			Append(key, material.texCoordOffset[i].x);
			Append(key, material.texCoordOffset[i].y);
			Append(key, material.texCoordScale[i].x);
			Append(key, material.texCoordScale[i].y);
		}
		Append(key, material.textureClampMode);
		Append(key, material.diffuseRenderTargetSourceIndex);
		Append(key, material.materialAlpha);
		Append(key, material.refractionPower);
		Append(key, material.specularColor);
		Append(key, material.specularPower);
		Append(key, material.specularColorScale);
		Append(key, material.subSurfaceLightRolloff);
		Append(key, material.rimLightPower);
		Append(key, material.diffuseTexture);
		Append(key, material.normalTexture);
		Append(key, material.rimSoftLightingTexture);
		Append(key, material.specularBackLightingTexture);
		for (uint32_t i = 0; i < 9; ++i) {
			const auto* path = IsTextureSet(material.textureSet.get()) ?
			                       material.textureSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(i)) :
			                       nullptr;
			const auto length = path ? static_cast<uint32_t>(std::strlen(path)) : 0;
			Append(key, length);
			if (length) {
				key.insert(key.end(), path, path + length);
			}
		}
		return key;
	}

	inline uint32_t Hash(Key key, uint32_t seed)
	{
		Append(key, seed);
		return RE::detail::GenerateCRC32({ key.data(), key.size() });
	}
}
