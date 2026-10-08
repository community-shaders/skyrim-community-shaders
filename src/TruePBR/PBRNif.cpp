#include "PBRNif.h"

#include "PBRMaterialUtil.h"

#include <span>

namespace PBRNif
{
	namespace
	{
		class BlockStream final : public RE::NiBinaryStream
		{
		public:
			explicit BlockStream(std::span<const uint8_t> bytes) :
				bytes(bytes)
			{
				_absoluteCurrentPos = 0;
				_readFn = Read;
			}

			bool good() const override { return _absoluteCurrentPos <= bytes.size(); }
			void seek(int32_t offset) override
			{
				_absoluteCurrentPos = static_cast<uint32_t>(std::clamp<int64_t>(
					static_cast<int64_t>(_absoluteCurrentPos) + offset, 0, bytes.size()));
			}
			void set_endian_swap(bool) override {}

		private:
			static uint32_t Read(RE::NiBinaryStream* input, void* target, uint32_t count, uint32_t*, uint32_t)
			{
				const auto& self = *static_cast<BlockStream*>(input);
				const auto available = static_cast<uint32_t>(self.bytes.size()) - self._absoluteCurrentPos;
				const auto size = std::min(count, available);
				std::memcpy(target, self.bytes.data() + self._absoluteCurrentPos, size);
				return size;
			}
			std::span<const uint8_t> bytes;
		};

		uint32_t Word(std::span<const uint8_t> bytes, size_t offset)
		{
			uint32_t value;
			std::memcpy(&value, bytes.data() + offset, sizeof(value));
			return value;
		}

		bool ConflictingFlags(uint64_t flags)
		{
			using enum RE::BSShaderProperty::EShaderPropertyFlag;
			constexpr auto mask = static_cast<uint64_t>(kEnvMap) | static_cast<uint64_t>(kFace) |
			                      static_cast<uint64_t>(kParallax) | static_cast<uint64_t>(kMultiTextureLandscape) |
			                      static_cast<uint64_t>(kEyeReflect) | static_cast<uint64_t>(kHairTint) |
			                      static_cast<uint64_t>(kFaceGenRGBTint) | static_cast<uint64_t>(kParallaxOcclusion) |
			                      static_cast<uint64_t>(kLODLandscape) | static_cast<uint64_t>(kGlowMap) |
			                      static_cast<uint64_t>(kMultiIndexSnow) | static_cast<uint64_t>(kMultiLayerParallax) |
			                      static_cast<uint64_t>(kCloudLOD);
			return (flags & mask) != 0;
		}

		const char* Validate(std::span<const uint8_t> bytes, RE::NiStream& stream)
		{
			if (stream.nifMaxVersion != 0x14020007 || stream.nifMaxUserDefinedVersion != 12 || stream.header.version != 100) {
				return "unsupported NIF container version";
			}
			if (bytes.size() < 164 || Word(bytes, 0) != 21) {
				return "unsupported shader type or truncated block";
			}
			const uint64_t count = Word(bytes, 8);
			if (bytes.size() != 164 + 4 * count) {
				return "incorrect PBR v1 block size";
			}
			const auto name = Word(bytes, 4);
			if (name != UINT32_MAX && name >= stream.fixedStrings.capacity()) {
				return "invalid name index";
			}
			const auto reference = [&stream](uint32_t index, const RE::NiRTTI* type, bool optional) {
				if (index == UINT32_MAX) {
					return optional;
				}
				if (index >= stream.objects.capacity() || !stream.objects[index]) {
					return false;
				}
				const auto* rtti = stream.objects[index]->GetRTTI();
				return rtti && rtti->IsKindOf(type);
			};
			static const auto* extraType = REL::Relocation<const RE::NiRTTI*>{ RE::NiExtraData::Ni_RTTI }.get();
			static const auto* controllerType = REL::Relocation<const RE::NiRTTI*>{ RE::NiTimeController::Ni_RTTI }.get();
			static const auto* textureType = REL::Relocation<const RE::NiRTTI*>{ RE::BSShaderTextureSet::Ni_RTTI }.get();
			for (uint32_t i = 0; i < count; ++i) {
				if (!reference(Word(bytes, 12 + 4 * i), extraType, true)) {
					return "invalid extra data reference";
				}
			}
			const size_t shift = static_cast<size_t>(count) * 4;
			if (!reference(Word(bytes, 12 + shift), controllerType, true) ||
				!reference(Word(bytes, 40 + shift), textureType, false)) {
				return "invalid controller or texture set reference";
			}
			const uint64_t flags = Word(bytes, 16 + shift) | (uint64_t{ Word(bytes, 20 + shift) } << 32);
			if (ConflictingFlags(flags)) {
				return "flags require another material layout";
			}
			for (const size_t offset : { 24, 28, 32, 36, 44, 48, 52, 56 }) {
				if (!std::isfinite(std::bit_cast<float>(Word(bytes, offset + shift)))) {
					return "non-finite common parameter";
				}
			}
			if (std::bit_cast<float>(Word(bytes, 56 + shift)) < 0.f) {
				return "negative emission multiplier";
			}
			if (Word(bytes, 72 + shift) != BSLightingShaderMaterialPBR::Version ||
				!BSLightingShaderMaterialPBR::ValidFeatures(Word(bytes, 76 + shift))) {
				return "unsupported PBR version or feature combination";
			}
			PBRParameters parameters;
			std::array<float, 21> values{};
			for (size_t i = 0; i < values.size(); ++i) {
				values[i] = std::bit_cast<float>(Word(bytes, 80 + shift + 4 * i));
			}
			parameters.SetValues(values);
			const float alpha = std::bit_cast<float>(Word(bytes, 64 + shift));
			const float refraction = std::bit_cast<float>(Word(bytes, 68 + shift));
			if (!parameters.IsValid() || Word(bytes, 60 + shift) > 3 ||
				!std::isfinite(alpha) || alpha < 0.f || alpha > 128.f ||
				!std::isfinite(refraction) || refraction < 0.f || refraction > 1.f) {
				return "invalid material parameter";
			}
			return nullptr;
		}

		std::vector<uint8_t> RejectedBlock()
		{
			std::vector<uint8_t> bytes(164, 0);
			const auto put = [&bytes](size_t offset, uint32_t value) { std::memcpy(bytes.data() + offset, &value, 4); };
			put(0, 21);
			put(4, UINT32_MAX);
			put(12, UINT32_MAX);
			put(32, std::bit_cast<uint32_t>(1.f));
			put(36, std::bit_cast<uint32_t>(1.f));
			put(40, UINT32_MAX);
			put(60, 3);
			put(72, BSLightingShaderMaterialPBR::Version);
			const auto values = PBRParameters{}.Values();
			for (size_t i = 0; i < values.size(); ++i) {
				put(80 + 4 * i, std::bit_cast<uint32_t>(values[i]));
			}
			return bytes;
		}
	}

	bool CanRender(const RE::BSShaderProperty* property)
	{
		if (!BSLightingShaderMaterialPBR::IsPBR(property->material)) {
			return true;
		}
		return static_cast<const BSLightingShaderMaterialPBR*>(property->material)->valid &&
		       !ConflictingFlags(property->flags.underlying());
	}

	void Load(RE::BSLightingShaderProperty* property, RE::NiStream& stream, LoadFunction original)
	{
		auto* input = stream.iStr;
		const auto start = input->tell();
		uint32_t feature = 0;
		const bool read = input->read(&feature, 1);
		input->seek(-static_cast<int32_t>(input->tell() - start));
		if (!read || (feature != 21 && feature != 22)) {
			original(property, stream);
			return;
		}
		const auto size = stream.load < stream.objectSizes.capacity() ? stream.objectSizes[stream.load] : 0;
		std::vector<uint8_t> bytes;
		const char* error = "invalid block size";
		if (size >= 4 && size <= 164ull + 4ull * stream.objects.capacity()) {
			bytes.resize(size);
			error = input->read(bytes.data(), size) ? Validate(bytes, stream) : "truncated PBR block";
		}
		const auto consumed = input->tell() - start;
		if (consumed < size) {
			uint32_t remaining = size - consumed;
			while (remaining) {
				const auto step = std::min(remaining, static_cast<uint32_t>(INT32_MAX));
				input->seek(static_cast<int32_t>(step));
				remaining -= step;
			}
		}
		if (error) {
			logger::error("[TruePBR] rejected material in {} (block {}): {}", stream.inputFilePath, stream.load, error);
			bytes = RejectedBlock();
		}
		BlockStream block(bytes);
		stream.iStr = &block;
		original(property, stream);
		stream.iStr = input;
		auto* material = static_cast<BSLightingShaderMaterialPBR*>(property->material);
		material->valid = material->valid && !error && block.tell() == bytes.size();
	}
}
