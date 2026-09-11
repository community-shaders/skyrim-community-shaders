#include "LegacyGraphicsCompatibility.h"

#include "Globals.h"

#include <bit>
#include <mutex>

namespace LegacyGraphicsCompatibility
{
	namespace detail
	{
		void InstallShaderAdapters();
	}

	namespace
	{
		constexpr std::size_t fullScreenBlurSlotCount = 11;
		constexpr std::size_t fractionalCopySlot = 9;
		constexpr std::size_t dynamicFetchDisabledCopySlot = 10;
		constexpr std::uint32_t usePreservedCameraProjectionScale = 1U << 9;

		struct CameraProjectionSnapshot
		{
			RE::BSGraphics::State* state{};
			float x{};
			float y{};
			bool valid{};
		};

		thread_local CameraProjectionSnapshot cameraProjectionSnapshot;

		[[nodiscard]] std::uintptr_t ReadRelativeCallTarget(std::uintptr_t a_callSite) noexcept
		{
			std::int32_t displacement{};
			std::memcpy(std::addressof(displacement), reinterpret_cast<const void*>(a_callSite + 1), sizeof(displacement));
			return a_callSite + 5 + displacement;
		}

		struct AlphaBlend_SetViewport
		{
			static void thunk(
				RE::BSGraphics::Renderer* a_renderer,
				std::int32_t a_left,
				std::int32_t a_top,
				std::int32_t a_right,
				std::int32_t a_bottom)
			{
				const auto width = std::bit_cast<std::int32_t>(
					static_cast<std::uint32_t>(a_right) - static_cast<std::uint32_t>(a_left));
				const auto height = std::bit_cast<std::int32_t>(
					static_cast<std::uint32_t>(a_bottom) - static_cast<std::uint32_t>(a_top));
				func(a_renderer, a_left, a_top, width, height);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// BSUtilityShader's shadow-mask pass scissors to BSShadowLight::projectedBoundingBox.
		// 1.7.99 builds that rect from screen dimensions already multiplied by the dynamic
		// resolution ratio, then passes extents; older runtimes pass unscaled bounds. The rect
		// has no other consumer, so scaling here is equivalent to scaling it at the source.
		struct ShadowBounds_SetViewport
		{
			static void thunk(
				RE::BSGraphics::Renderer* a_renderer,
				std::int32_t a_left,
				std::int32_t a_top,
				std::int32_t a_right,
				std::int32_t a_bottom)
			{
				auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
				const float widthRatio = runtimeData.dynamicResolutionLock ? 1.0f : runtimeData.dynamicResolutionWidthRatio;
				const float heightRatio = runtimeData.dynamicResolutionLock ? 1.0f : runtimeData.dynamicResolutionHeightRatio;

				// Truncate each edge independently, matching the per-edge CVTTSS2SI the engine emits.
				a_left = static_cast<std::int32_t>(static_cast<float>(a_left) * widthRatio);
				a_right = static_cast<std::int32_t>(static_cast<float>(a_right) * widthRatio);
				a_top = static_cast<std::int32_t>(static_cast<float>(a_top) * heightRatio);
				a_bottom = static_cast<std::int32_t>(static_cast<float>(a_bottom) * heightRatio);

				const auto width = std::bit_cast<std::int32_t>(
					static_cast<std::uint32_t>(a_right) - static_cast<std::uint32_t>(a_left));
				const auto height = std::bit_cast<std::int32_t>(
					static_cast<std::uint32_t>(a_bottom) - static_cast<std::uint32_t>(a_top));
				func(a_renderer, a_left, a_top, width, height);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		class ScopedCameraProjectionScale
		{
		public:
			ScopedCameraProjectionScale(
				RE::BSGraphics::State& a_state,
				const CameraProjectionSnapshot& a_snapshot) noexcept :
				state(a_state),
				x(a_state.projectionPosScaleX),
				y(a_state.projectionPosScaleY)
			{
				state.projectionPosScaleX = a_snapshot.x;
				state.projectionPosScaleY = a_snapshot.y;
			}

			~ScopedCameraProjectionScale()
			{
				state.projectionPosScaleX = x;
				state.projectionPosScaleY = y;
			}

			ScopedCameraProjectionScale(const ScopedCameraProjectionScale&) = delete;
			ScopedCameraProjectionScale& operator=(const ScopedCameraProjectionScale&) = delete;

		private:
			RE::BSGraphics::State& state;
			float x;
			float y;
		};

		struct State_SetCameraData
		{
			static void thunk(
				RE::BSGraphics::State* a_state,
				const RE::NiCamera* a_camera,
				std::uint32_t a_flags)
			{
				if ((a_flags & usePreservedCameraProjectionScale) == 0 || !a_state ||
					!cameraProjectionSnapshot.valid || cameraProjectionSnapshot.state != a_state) {
					if ((a_flags & usePreservedCameraProjectionScale) != 0 && a_state &&
						(!cameraProjectionSnapshot.valid || cameraProjectionSnapshot.state != a_state)) {
						static std::once_flag logOnce;
						std::call_once(logOnce, [] {
							logger::warn("Legacy SetCameraData bit-9 call has no matching vanilla-jitter snapshot; using live projection scale");
						});
					}
					func(a_state, a_camera, a_flags);
					return;
				}

				ScopedCameraProjectionScale projectionScale(*a_state, cameraProjectionSnapshot);
				func(a_state, a_camera, a_flags);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_UpdateJitter
		{
			static void thunk(RE::BSGraphics::State* a_state)
			{
				func(a_state);
				if (!a_state) {
					cameraProjectionSnapshot = {};
					return;
				}

				cameraProjectionSnapshot = {
					a_state,
					a_state->projectionPosScaleX,
					a_state->projectionPosScaleY,
					true,
				};
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct FullScreenBlur_Setup
		{
			static void thunk(
				RE::ImageSpaceEffect* a_effect,
				RE::ImageSpaceManager* a_manager,
				RE::ImageSpaceEffectParam* a_param)
			{
				func(a_effect, a_manager, a_param);
				if (!a_effect || !a_manager || !HasElevenSlots(*a_effect)) {
					return;
				}

				constexpr auto effectIndex = RE::ImageSpaceManager::GetSEIndex(
					RE::ImageSpaceManager::ISCopyDynamicFetchDisabled);
				if (a_manager->effects.size() <= effectIndex || !a_manager->effects[effectIndex]) {
					static std::once_flag logOnce;
					std::call_once(logOnce, [] {
						logger::error("Legacy FullScreenBlur cannot resolve ISCopyDynamicFetchDisabled; leaving its 1.7 stage disabled");
					});
					return;
				}

				configureSlot(
					a_effect,
					dynamicFetchDisabledCopySlot,
					a_manager->effects[effectIndex],
					nullptr,
					0);
				configureInput(a_effect, dynamicFetchDisabledCopySlot, 1, 0);
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static inline REL::Relocation<void(
				RE::ImageSpaceEffect*,
				std::uint32_t,
				RE::ImageSpaceEffect*,
				RE::ImageSpaceEffectParam*,
				std::int32_t)>
				configureSlot;
			static inline REL::Relocation<void(RE::ImageSpaceEffect*, std::uint32_t, std::uint32_t, std::uint32_t)>
				configureInput;

		private:
			[[nodiscard]] static bool HasElevenSlots(const RE::ImageSpaceEffect& a_effect) noexcept
			{
				return a_effect.effects.size() >= fullScreenBlurSlotCount &&
				       a_effect.effectParams.size() >= fullScreenBlurSlotCount &&
				       a_effect.effectTextures.size() >= fullScreenBlurSlotCount &&
				       a_effect.effectInputs.size() >= fullScreenBlurSlotCount &&
				       a_effect.unk70.size() >= fullScreenBlurSlotCount;
			}

			friend struct FullScreenBlur_Render;
		};

		class ScopedFullScreenBlurFlags
		{
		public:
			using RequestedFlag = std::pair<RE::ImageSpaceEffect*, bool>;

			[[nodiscard]] bool Apply(std::span<const RequestedFlag> a_requested) noexcept
			{
				for (std::size_t i = 0; i < a_requested.size(); ++i) {
					if (!a_requested[i].first) {
						continue;
					}
					for (std::size_t j = 0; j < i; ++j) {
						if (a_requested[i].first == a_requested[j].first &&
							a_requested[i].second != a_requested[j].second) {
							return false;
						}
					}
				}

				for (const auto& [effect, value] : a_requested) {
					if (!effect || Contains(effect)) {
						continue;
					}
					entries[count++] = { effect, effect->unk88 };
					effect->unk88 = value;
				}
				return true;
			}

			~ScopedFullScreenBlurFlags()
			{
				while (count > 0) {
					const auto& [effect, value] = entries[--count];
					effect->unk88 = value;
				}
			}

			ScopedFullScreenBlurFlags() = default;
			ScopedFullScreenBlurFlags(const ScopedFullScreenBlurFlags&) = delete;
			ScopedFullScreenBlurFlags& operator=(const ScopedFullScreenBlurFlags&) = delete;

		private:
			[[nodiscard]] bool Contains(const RE::ImageSpaceEffect* a_effect) const noexcept
			{
				return std::ranges::any_of(std::span(entries).first(count), [&](const auto& a_entry) {
					return a_entry.effect == a_effect;
				});
			}

			struct Entry
			{
				RE::ImageSpaceEffect* effect{};
				bool value{};
			};

			std::array<Entry, 10> entries{};
			std::size_t count{};
		};

		class ScopedFullScreenBlurSlotSwap
		{
		public:
			ScopedFullScreenBlurSlotSwap(RE::ImageSpaceEffect& a_effect, bool a_active) noexcept :
				effect(a_effect), active(a_active)
			{
				if (active) {
					Swap();
				}
			}

			~ScopedFullScreenBlurSlotSwap()
			{
				if (active) {
					Swap();
				}
			}

			ScopedFullScreenBlurSlotSwap(const ScopedFullScreenBlurSlotSwap&) = delete;
			ScopedFullScreenBlurSlotSwap& operator=(const ScopedFullScreenBlurSlotSwap&) = delete;

		private:
			void Swap() noexcept
			{
				std::swap(effect.effects[fractionalCopySlot], effect.effects[dynamicFetchDisabledCopySlot]);
				std::swap(effect.effectParams[fractionalCopySlot], effect.effectParams[dynamicFetchDisabledCopySlot]);
				std::swap(effect.effectTextures[fractionalCopySlot], effect.effectTextures[dynamicFetchDisabledCopySlot]);
				std::swap(effect.effectInputs[fractionalCopySlot], effect.effectInputs[dynamicFetchDisabledCopySlot]);
				std::swap(effect.unk70[fractionalCopySlot], effect.unk70[dynamicFetchDisabledCopySlot]);
			}

			RE::ImageSpaceEffect& effect;
			bool active;
		};

		struct FullScreenBlur_Render
		{
			static void thunk(
				RE::ImageSpaceEffect* a_effect,
				RE::BSTriShape* a_shape,
				RE::ImageSpaceEffectParam* a_param)
			{
				if (!a_effect || !FullScreenBlur_Setup::HasElevenSlots(*a_effect) ||
					!a_effect->effects[dynamicFetchDisabledCopySlot]) {
					func(a_effect, a_shape, a_param);
					return;
				}

				const bool parentFlag = a_effect->unk88;
				const auto selectedCopySlot = static_cast<std::uint16_t>(
					parentFlag ? fractionalCopySlot : dynamicFetchDisabledCopySlot);
				const std::array<ScopedFullScreenBlurFlags::RequestedFlag, 10> requestedFlags{
					ScopedFullScreenBlurFlags::RequestedFlag{ a_effect->effects[0], false },
					{ a_effect->effects[1], parentFlag },
					{ a_effect->effects[2], parentFlag },
					{ a_effect->effects[3], parentFlag },
					{ a_effect->effects[4], parentFlag },
					{ a_effect->effects[5], parentFlag },
					{ a_effect->effects[6], parentFlag },
					{ a_effect->effects[7], parentFlag },
					{ a_effect->effects[selectedCopySlot], parentFlag },
					{ a_effect->effects[8], parentFlag },
				};

				ScopedFullScreenBlurFlags flags;
				if (!flags.Apply(requestedFlags)) {
					static std::once_flag logOnce;
					std::call_once(logOnce, [] {
						logger::error("Legacy FullScreenBlur has conflicting aliased child flags; using the unmodified legacy chain");
					});
					func(a_effect, a_shape, a_param);
					return;
				}

				ScopedFullScreenBlurSlotSwap slotSwap(*a_effect, !parentFlag);
				func(a_effect, a_shape, a_param);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct FullScreenBlur_UpdateParams
		{
			static bool thunk(RE::ImageSpaceEffect* a_effect, RE::ImageSpaceEffectParam* a_param)
			{
				const bool result = func(a_effect, a_param);
				if (!a_effect || a_effect->effectParams.size() <= 8 || !a_effect->effectParams[8]) {
					return result;
				}

				auto* shaderParam = static_cast<RE::ImageSpaceShaderParam*>(a_effect->effectParams[8]);
				if (shaderParam->pixelConstantGroup && shaderParam->pixelConstantGroupSize > 0) {
					shaderParam->pixelConstantGroup[0] = 1.0F;
				}
				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ShadowSceneNode_ctor
		{
			static RE::ShadowSceneNode* thunk(
				RE::ShadowSceneNode* a_this,
				std::uint32_t a_arg2,
				std::uint32_t a_arg3,
				std::uint32_t a_arg4,
				std::uint32_t a_arg5,
				std::uint32_t a_arg6,
				std::uint32_t a_arg7)
			{
				auto* result = func(a_this, a_arg2, a_arg3, a_arg4, a_arg5, a_arg6, a_arg7);
				if (result) {
					std::memset(reinterpret_cast<std::byte*>(result) + 0x2CC, 0, 0x18);
				}
				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct VerifiedVFuncPatch
		{
			std::uintptr_t slot{};
			std::uintptr_t original{};
			std::uintptr_t replacement{};
			const char* name{};
			bool written{};
		};

		template <std::size_t Index, class Hook>
		[[nodiscard]] VerifiedVFuncPatch MakeVFuncPatch(
			REL::VariantID a_vtable,
			REL::RelocationID a_expected,
			const char* a_name)
		{
			REL::Relocation<std::uintptr_t> vtable{ a_vtable };
			REL::Relocation<std::uintptr_t> expected{ a_expected };
			return {
				vtable.address() + Index * sizeof(void*),
				expected.address(),
				stl::unrestricted_cast<std::uintptr_t>(Hook::thunk),
				a_name,
				false,
			};
		}

		[[nodiscard]] bool VerifyVFuncPatch(const VerifiedVFuncPatch& a_patch) noexcept
		{
			const auto current = *reinterpret_cast<const std::uintptr_t*>(a_patch.slot);
			if (current == a_patch.original) {
				return true;
			}

			logger::error(
				"Legacy graphics compatibility refused {} vfunc: expected {:X}, found {:X}",
				a_patch.name,
				a_patch.original,
				current);
			return false;
		}

		[[nodiscard]] bool WriteVFuncPatch(VerifiedVFuncPatch& a_patch) noexcept
		{
			if (!REL::safe_write(
					a_patch.slot,
					std::addressof(a_patch.replacement),
					sizeof(a_patch.replacement),
					std::addressof(a_patch.original),
					sizeof(a_patch.original))) {
				logger::error("Legacy graphics compatibility failed to write {} vfunc", a_patch.name);
				return false;
			}
			a_patch.written = true;
			return true;
		}

		[[nodiscard]] bool RestoreVFuncPatch(VerifiedVFuncPatch& a_patch) noexcept
		{
			if (!a_patch.written) {
				return true;
			}
			const bool restored = REL::safe_write(
				a_patch.slot,
				std::addressof(a_patch.original),
				sizeof(a_patch.original),
				std::addressof(a_patch.replacement),
				sizeof(a_patch.replacement));
			a_patch.written = !restored;
			return restored;
		}

		void InstallAlphaBlendExtentsAdapter()
		{
			const auto callSite = REL::RelocationID(100950, 107732).address() + REL::Relocate(0x151, 0x148);
			const auto expectedTarget = REL::RelocationID(75564, 77365).address();
			constexpr auto callPattern = REL::make_pattern<"E8 ?? ?? ?? ??">();
			if (!REL::verify_code(callSite, callPattern) || ReadRelativeCallTarget(callSite) != expectedTarget) {
				logger::error("Legacy AlphaBlend viewport call does not match the verified 1.5.97/1.6.1170 binary; adapter not installed");
				return;
			}

			AlphaBlend_SetViewport::func = expectedTarget;
			SKSE::GetTrampoline().write_call<5>(callSite, AlphaBlend_SetViewport::thunk);
			logger::info("Installed legacy AlphaBlend bounds-to-extents adapter");
		}

		void InstallShadowBoundsExtentsAdapter()
		{
			const auto callSite = REL::RelocationID(100979, 107762).address() + REL::Relocate(0x3B5, 0x360);
			const auto expectedTarget = REL::RelocationID(75564, 77365).address();
			constexpr auto callPattern = REL::make_pattern<"E8 ?? ?? ?? ??">();
			if (!REL::verify_code(callSite, callPattern) || ReadRelativeCallTarget(callSite) != expectedTarget) {
				logger::error("Legacy shadow bounds viewport call does not match the verified 1.5.97/1.6.1170 binary; adapter not installed");
				return;
			}

			ShadowBounds_SetViewport::func = expectedTarget;
			SKSE::GetTrampoline().write_call<5>(callSite, ShadowBounds_SetViewport::thunk);
			logger::info("Installed legacy shadow bounds-to-extents adapter");
		}

		void InstallStateCameraProjectionAdapter()
		{
			const auto updateJitter = REL::RelocationID(75709, 77518).address();
			const auto setCameraData = REL::RelocationID(75694, 77503).address();
			const bool updateJitterVerified = REL::Module::IsSE() ?
			                                      REL::verify_code(
													  updateJitter,
													  REL::make_pattern<
														  "48 8B 05 ?? ?? ?? ?? 48 8B 90 F0 01 00 00 80 7A 18 00 74 7A 8B 81 B8 00 00 00">()) :
			                                      REL::verify_code(
													  updateJitter,
													  REL::make_pattern<
														  "48 8B 05 ?? ?? ?? ?? 0F 57 C0 48 8B 90 F0 01 00 00 80 7A 18 00 74 68 8B 81 C0 00 00 00">());
			const bool setCameraDataVerified = REL::Module::IsSE() ?
			                                       REL::verify_code(
													   setCameraData,
													   REL::make_pattern<
														   "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20">()) :
			                                       REL::verify_code(
													   setCameraData,
													   REL::make_pattern<
														   "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20">());
			if (!updateJitterVerified || !setCameraDataVerified) {
				logger::error("Legacy jitter/SetCameraData functions do not match the verified 1.5.97/1.6.1170 binaries; camera-projection adapters not installed");
				return;
			}

			Main_UpdateJitter::func = updateJitter;
			State_SetCameraData::func = setCameraData;
			stl::detour_thunk<Main_UpdateJitter>(updateJitter);
			stl::detour_thunk<State_SetCameraData>(setCameraData);
			logger::info("Installed legacy jitter-preservation and State::SetCameraData adapters");
		}

		[[nodiscard]] bool InstallFullScreenBlurAdapters()
		{
			const auto setupAddress = REL::RelocationID(101564, 108562).address();
			const auto shutdownAddress = REL::RelocationID(101562, 108560).address();
			const auto destructorAddress = REL::RelocationID(101570, 108568).address();
			constexpr auto setupContext = REL::make_pattern<
				"48 8B EA 48 8B D9 BA 0A 00 00 00 E8 ?? ?? ?? ??">();
			constexpr auto shutdownContext = REL::make_pattern<
				"48 89 2C 06 FF C7 83 FF 0A 7C AF">();
			constexpr auto destructorContext = REL::make_pattern<
				"4C 89 34 06 FF C7 83 FF 0A 7C AF">();
			if (!REL::verify_code(setupAddress + 0x24, setupContext) ||
				!REL::verify_code(shutdownAddress + 0x86, shutdownContext) ||
				!REL::verify_code(destructorAddress + 0x86, destructorContext)) {
				logger::error("Legacy FullScreenBlur stage-count opcode contexts do not match the verified 1.5.97/1.6.1170 sequences; no blur adapters installed");
				return false;
			}

			std::array patches{
				MakeVFuncPatch<0x2, FullScreenBlur_Setup>(
					RE::VTABLE_ImageSpaceEffectFullScreenBlur[0],
					REL::RelocationID(101564, 108562),
					"FullScreenBlur::Setup"),
				MakeVFuncPatch<0x1, FullScreenBlur_Render>(
					RE::VTABLE_ImageSpaceEffectFullScreenBlur[0],
					REL::RelocationID(101565, 108563),
					"FullScreenBlur::Render"),
				MakeVFuncPatch<0x7, FullScreenBlur_UpdateParams>(
					RE::VTABLE_ImageSpaceEffectFullScreenBlur[0],
					REL::RelocationID(101566, 108564),
					"FullScreenBlur::UpdateParams"),
			};
			if (!std::ranges::all_of(patches, VerifyVFuncPatch)) {
				logger::error("Legacy FullScreenBlur preflight failed; no blur writes performed");
				return false;
			}

			FullScreenBlur_Setup::func = patches[0].original;
			FullScreenBlur_Render::func = patches[1].original;
			FullScreenBlur_UpdateParams::func = patches[2].original;
			FullScreenBlur_Setup::configureSlot = REL::RelocationID(100668, 107449);
			FullScreenBlur_Setup::configureInput = REL::RelocationID(100669, 107450);

			constexpr std::uint8_t legacyCount = 0x0A;
			constexpr std::uint8_t latestCount = 0x0B;
			const std::array countAddresses{
				setupAddress + 0x2B,
				shutdownAddress + 0x8E,
				destructorAddress + 0x8E,
			};
			std::array<bool, countAddresses.size()> countWritten{};
			const auto restoreCounts = [&]() noexcept {
				bool restored = true;
				for (std::size_t i = countAddresses.size(); i-- > 0;) {
					if (!countWritten[i]) {
						continue;
					}
					const bool currentRestored = REL::safe_write(
						countAddresses[i], legacyCount, std::array{ latestCount });
					countWritten[i] = !currentRestored;
					restored = currentRestored && restored;
				}
				return restored;
			};
			for (std::size_t i = 0; i < countAddresses.size(); ++i) {
				if (REL::safe_write(countAddresses[i], latestCount, std::array{ legacyCount })) {
					countWritten[i] = true;
					continue;
				}
				const bool rollbackSucceeded = restoreCounts();
				logger::error("Legacy FullScreenBlur stage-count write failed; no blur vfuncs installed");
				if (!rollbackSucceeded) {
					logger::critical("Legacy FullScreenBlur stage-count rollback failed");
				}
				return false;
			}

			for (auto& patch : patches) {
				if (WriteVFuncPatch(patch)) {
					continue;
				}

				bool rollbackSucceeded = true;
				for (auto iterator = patches.rbegin(); iterator != patches.rend(); ++iterator) {
					rollbackSucceeded = RestoreVFuncPatch(*iterator) && rollbackSucceeded;
				}
				rollbackSucceeded = restoreCounts() && rollbackSucceeded;
				if (!rollbackSucceeded) {
					logger::critical("Legacy FullScreenBlur transactional rollback failed");
				}
				return false;
			}

			logger::info("Installed legacy Skyrim 1.7 FullScreenBlur CPU chain");
			return true;
		}

		void InstallShadowSceneNodeInitialization()
		{
			const auto constructor = REL::RelocationID(99686, 106320).address();
			const bool verified = REL::Module::IsSE() ?
			                          REL::verify_code(
										  constructor,
										  REL::make_pattern<"48 8B C4 48 89 48 08 57 41 54 41 55 41 56 41 57 48 83 EC 50">()) :
			                          REL::verify_code(
										  constructor,
										  REL::make_pattern<"48 8B C4 48 89 48 08 57 41 54 41 55 41 56 41 57 48 83 EC 60">());
			if (!verified) {
				logger::error("Legacy ShadowSceneNode constructor does not match the verified 1.5.97/1.6.1170 binary; initialization fix not installed");
				return;
			}

			ShadowSceneNode_ctor::func = constructor;
			stl::detour_thunk<ShadowSceneNode_ctor>(constructor);
			logger::info("Installed legacy ShadowSceneNode 1.7 tail initialization");
		}
	}

	void Install()
	{
		if (!IsLegacyVersion()) {
			return;
		}

		detail::InstallShaderAdapters();
		InstallAlphaBlendExtentsAdapter();
		InstallShadowBoundsExtentsAdapter();
		InstallStateCameraProjectionAdapter();
		(void)InstallFullScreenBlurAdapters();
		InstallShadowSceneNodeInitialization();
	}
}
