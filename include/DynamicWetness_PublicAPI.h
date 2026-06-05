// Public, header-only C/C++ interface for DynamicWetness (SWE namespace)
// Drop-in for other SKSE plugins, no import lib required (functions are resolved via GetProcAddress).
//
// Quick start:
//   #include "DynamicWetness_PublicAPI.h"
//   bool ok = SWE::API::Init();           // resolves exports from DynamicWetness.dll
//   if (!ok) return;                      // SWE not present
//   SWE::API::SetExternalWetness(actor, "MyMod:buff", 0.5f, 8.0f);  // 8s light wetness on default category (Skin)
//   auto env = SWE::API::DecodeEnv(SWE::API::GetEnvMask(actor));     // query environment (water, rain, roof, heat,
//   ...)
//
// Notes:
//  - All intensities are clamped to [0..1].
//  - Durations use seconds, <= 0 means "indefinite until cleared".
//  - Category mask low 4 bits select target materials, high bits are behavior flags.
//  - Keys are normalized (trim + lowercase) and identify your external source per actor.
//  - Environmental wetness (water/rain) can override external sources internally.
//  - Thread-safe internally, but always pass valid Actor* (lifetime: game thread best).

#pragma once
#include <cstdint>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace RE {
    class Actor;
}

#ifndef SWE_DLL_NAME
    #define SWE_DLL_NAME "DynamicWetness.dll"
#endif

namespace SWE {
    namespace API {

        // ===========================
        // Categories (low 4 bits)
        // ===========================
        /**
         * @brief Material categories targetable by external wetness.
         *
         * Combine any of the low 4 bits to select affected materials.
         * Typical usage:
         *   unsigned mask = CAT_SKIN_FACE | CAT_HAIR;  // skin + hair
         */
        static constexpr std::uint32_t CAT_SKIN_FACE = 1u << 0;    /// Skin & face materials
        static constexpr std::uint32_t CAT_HAIR = 1u << 1;         /// Hair
        static constexpr std::uint32_t CAT_ARMOR_CLOTH = 1u << 2;  /// Armor & clothing
        static constexpr std::uint32_t CAT_WEAPON = 1u << 3;       /// Weapons
        static constexpr std::uint32_t CAT_MASK_4BIT = 0x0Fu;      /// Mask of all category bits

        // ===========================
        // Behavior flags (high bits)
        // ===========================
        /**
         * @brief Flags that modify how SWE blends your external wetness with its internal system.
         *
         * These live in the upper bits of the same integer you pass as "catMask".
         * You can OR them together with category bits.
         */
        static constexpr std::uint32_t FLAG_PASSTHROUGH =
            1u << 16;  /// Add AFTER SWE's own mixing/drying (additive post).
        static constexpr std::uint32_t FLAG_NO_AUTODRY = 1u << 17;  /// Your value won't be reduced by SWE's auto-dry.
        static constexpr std::uint32_t FLAG_ZERO_BASE = 1u << 18;  /// Base wetness in the marked categories is zeroed.

        /// @brief Handy preset: only Skin, additive post, no auto-dry, zero base contribution.
        static constexpr std::uint32_t MASK_SKIN_PASSTHROUGH =
            (CAT_SKIN_FACE | FLAG_PASSTHROUGH | FLAG_NO_AUTODRY | FLAG_ZERO_BASE);

        // ===========================
        // Environment bit mask
        // ===========================
        /**
         * @brief Bits returned by GetEnvMask() describing the actor's environment this frame.
         */
        static constexpr std::uint32_t ENV_WATER = 1u << 0;          /// Actor is in water/submerged.
        static constexpr std::uint32_t ENV_WET_WEATHER = 1u << 1;    /// Precipitation affecting actor (rain/snow).
        static constexpr std::uint32_t ENV_NEAR_HEAT = 1u << 2;      /// Near a heat source (campfire/forge/etc.).
        static constexpr std::uint32_t ENV_UNDER_ROOF = 1u << 3;     /// Under roof/cover (heuristic).
        static constexpr std::uint32_t ENV_EXTERIOR_OPEN = 1u << 4;  /// In exterior and not under cover.

        /**
         * @brief Convenience struct for decoded environment state.
         */
        struct EnvState {
            bool inWater{false};
            bool wetWeather{false};
            bool nearHeat{false};
            bool underRoof{false};
            bool exteriorOpen{false};
        };

        /**
         * @brief Decode ENV_* mask returned by GetEnvMask() into booleans.
         * @param m Bitmask from GetEnvMask(actor)
         */
        inline EnvState DecodeEnv(std::uint32_t m) {
            EnvState e;
            e.inWater = (m & ENV_WATER) != 0;
            e.wetWeather = (m & ENV_WET_WEATHER) != 0;
            e.nearHeat = (m & ENV_NEAR_HEAT) != 0;
            e.underRoof = (m & ENV_UNDER_ROOF) != 0;
            e.exteriorOpen = (m & ENV_EXTERIOR_OPEN) != 0;
            return e;
        }

        // ===========================
        // C-ABI function signatures
        // ===========================
        // These match the exported DLL functions exactly. Prefer the safe inline wrappers below.

        using PFN_GetFinalWetness = float(__cdecl*)(RE::Actor*);                  /// Final mixed wetness [0..1].
        using PFN_GetExternalWetness = float(__cdecl*)(RE::Actor*, const char*);  /// Last value set for @key [0..1].
        using PFN_GetBaseWetness = float(__cdecl*)(RE::Actor*);                   /// Internal/base wetness [0..1].
        using PFN_SetExternalWetness = void(__cdecl*)(RE::Actor*, const char*, float, float);
        using PFN_ClearExternalWetness = void(__cdecl*)(RE::Actor*, const char*);
        using PFN_SetExternalWetnessMask = void(__cdecl*)(RE::Actor*, const char*, float, float, unsigned int);
        using PFN_SetExternalWetnessEx = void(__cdecl*)(RE::Actor*, const char*, float, float, unsigned int, float,
                                                        float, float, float, float, float, float);
        using PFN_GetActorSubmergeLevel = float(__cdecl*)(RE::Actor*);  /// Submerge level [0..1].
        using PFN_IsActorInWater = bool(__cdecl*)(RE::Actor*);
        using PFN_IsWetWeatherAround = bool(__cdecl*)(RE::Actor*);
        using PFN_IsNearHeatSource = bool(__cdecl*)(RE::Actor*, float);  /// radius: Skyrim world units.
        using PFN_IsUnderRoof = bool(__cdecl*)(RE::Actor*);
        using PFN_IsActorInExteriorWet = bool(__cdecl*)(RE::Actor*);
        using PFN_GetEnvMask = unsigned(__cdecl*)(RE::Actor*);

        // Resolved at runtime by Init()/LoadFromModule()
        inline PFN_GetFinalWetness pGetFinalWetness = nullptr;
        inline PFN_GetExternalWetness pGetExternalWetness = nullptr;
        inline PFN_GetBaseWetness pGetBaseWetness = nullptr;
        inline PFN_SetExternalWetness pSetExternalWetness = nullptr;
        inline PFN_ClearExternalWetness pClearExternalWetness = nullptr;
        inline PFN_SetExternalWetnessMask pSetExternalWetnessMask = nullptr;
        inline PFN_SetExternalWetnessEx pSetExternalWetnessEx = nullptr;
        inline PFN_GetActorSubmergeLevel pGetActorSubmergeLevel = nullptr;
        inline PFN_IsActorInWater pIsActorInWater = nullptr;
        inline PFN_IsWetWeatherAround pIsWetWeatherAround = nullptr;
        inline PFN_IsNearHeatSource pIsNearHeatSource = nullptr;
        inline PFN_IsUnderRoof pIsUnderRoof = nullptr;
        inline PFN_IsActorInExteriorWet pIsActorInExteriorWet = nullptr;
        inline PFN_GetEnvMask pGetEnvMask = nullptr;

        // ===========================
        // Loader helpers
        // ===========================
        /**
         * @brief Resolve all SWE_* exports from a given module handle.
         * @return true if core functions were found (enough to use the API).
         */
        inline bool LoadFromModule(HMODULE h) {
#ifdef _WIN32
            if (!h) return false;
            auto gp = [&](const char* n) { return GetProcAddress(h, n); };

            pGetFinalWetness = (PFN_GetFinalWetness)gp("SWE_GetFinalWetness");
            pGetExternalWetness = (PFN_GetExternalWetness)gp("SWE_GetExternalWetness");
            pGetBaseWetness = (PFN_GetBaseWetness)gp("SWE_GetBaseWetness");
            pSetExternalWetness = (PFN_SetExternalWetness)gp("SWE_SetExternalWetness");
            pClearExternalWetness = (PFN_ClearExternalWetness)gp("SWE_ClearExternalWetness");
            pSetExternalWetnessMask = (PFN_SetExternalWetnessMask)gp("SWE_SetExternalWetnessMask");
            pSetExternalWetnessEx = (PFN_SetExternalWetnessEx)gp("SWE_SetExternalWetnessEx");
            pGetActorSubmergeLevel = (PFN_GetActorSubmergeLevel)gp("SWE_GetActorSubmergeLevel");
            pIsActorInWater = (PFN_IsActorInWater)gp("SWE_IsActorInWater");
            pIsWetWeatherAround = (PFN_IsWetWeatherAround)gp("SWE_IsWetWeatherAround");
            pIsNearHeatSource = (PFN_IsNearHeatSource)gp("SWE_IsNearHeatSource");
            pIsUnderRoof = (PFN_IsUnderRoof)gp("SWE_IsUnderRoof");
            pIsActorInExteriorWet = (PFN_IsActorInExteriorWet)gp("SWE_IsActorInExteriorWet");
            pGetEnvMask = (PFN_GetEnvMask)gp("SWE_GetEnvMask");

            return pGetFinalWetness && pSetExternalWetness && pSetExternalWetnessMask && pGetEnvMask;
#else
            (void)h;
            return false;
#endif
        }

        /**
         * @brief Try to find the module by name (SWE_DLL_NAME), then fallbacks.
         */
        inline HMODULE FindModule() {
#ifdef _WIN32
            HMODULE h = GetModuleHandleA(SWE_DLL_NAME);
            if (!h) {
                // Optional fallback if the DLL is named differently
                h = GetModuleHandleA("dynamicwetness.dll");
            }
            return h;
#else
            return nullptr;
#endif
        }

        /**
         * @brief One-shot init. Finds the DLL and resolves symbols.
         * @param hOverride Pass an explicit module handle if you already have one.
         * @return true if initialization succeeded.
         */
        inline bool Init(HMODULE hOverride = nullptr) {
            HMODULE h = hOverride ? hOverride : FindModule();
            return LoadFromModule(h);
        }

        /**
         * @brief Check if the core API is available (after Init()).
         */
        inline bool IsAvailable() { return pGetFinalWetness != nullptr; }

        // ===========================
        // Safe convenience wrappers
        // ===========================

        /**
         * @brief Final wetness after SWE's internal logic + all external sources.
         * @param a Actor pointer
         * @return Wetness in [0..1]. Returns 0 if SWE is not available.
         */
        inline float GetFinalWetness(RE::Actor* a) { return pGetFinalWetness ? pGetFinalWetness(a) : 0.0f; }

        /**
         * @brief Value you last set for @p key on @p a (not the final mixed wetness).
         * @param a Actor
         * @param key External source identifier (normalized: trimmed + lowercase)
         * @return [0..1], 0 if not set or SWE not available.
         */
        inline float GetExternalWetness(RE::Actor* a, const char* key) {
            return pGetExternalWetness ? pGetExternalWetness(a, key) : 0.0f;
        }

        /**
         * @brief Internal/base wetness tracked by SWE (before external sources).
         * @param a Actor
         * @return [0..1], 0 if unavailable.
         */
        inline float GetBaseWetness(RE::Actor* a) { return pGetBaseWetness ? pGetBaseWetness(a) : 0.0f; }

        /**
         * @brief Set/refresh an external wetness value for @p key on @p a.
         *
         * If this is the first time @p key is used for @p a and no category was set yet,
         * SWE defaults to CAT_SKIN_FACE. Subsequent calls keep the previously configured
         * category/flags for this @p key.
         *
         * @param a Actor
         * @param key Your unique source key, e.g. "MyMod:spell". Normalized internally.
         * @param v Intensity in [0..1]
         * @param durationSec Lifetime in seconds; <= 0 means indefinite (until ClearExternalWetness()).
         */
        inline void SetExternalWetness(RE::Actor* a, const char* key, float v, float durationSec) {
            if (pSetExternalWetness) pSetExternalWetness(a, key, v, durationSec);
        }

        /**
         * @brief Remove your external source identified by @p key from @p a.
         */
        inline void ClearExternalWetness(RE::Actor* a, const char* key) {
            if (pClearExternalWetness) pClearExternalWetness(a, key);
        }

        /**
         * @brief Set/replace @b category mask and behavior flags for @p key on @p a.
         *
         * This both sets the value and (re)defines which material categories are affected
         * and how SWE blends them (via flags). Use this when you need to change the
         * category/flag configuration of an existing key.
         *
         * @param a Actor
         * @param key Your unique source key (normalized internal storage)
         * @param v Intensity in [0..1]
         * @param durationSec Lifetime; <= 0 = indefinite
         * @param catMask Low 4 bits: categories (CAT_*). High bits: flags (FLAG_*).
         */
        inline void SetExternalWetnessMask(RE::Actor* a, const char* key, float v, float durationSec,
                                           unsigned catMask) {
            if (pSetExternalWetnessMask) pSetExternalWetnessMask(a, key, v, durationSec, catMask);
        }

        /**
         * @brief Advanced: update shader/material overrides for @p key without altering flags.
         *
         * Use this to tweak how shiny/specular the result can get per category while keeping
         * your previously set flags (e.g., NO_AUTODRY) intact. Parameters use negative
         * values to mean "leave unchanged / don't force".
         *
         * @param a Actor
         * @param key Your unique source key (normalized internal storage)
         * @param v Intensity in [0..1]
         * @param durationSec Lifetime; <= 0 = indefinite
         * @param catMask Low 4 bits: categories (CAT_*). (Flags are @b not modified by this call.)
         * @param maxGloss  [-1 or >=0] Cap for gloss when wet (per-category merge)
         * @param maxSpec   [-1 or >=0] Cap for specular intensity when wet
         * @param minGloss  [-1 or >=0] Floor gloss even at low wetness
         * @param minSpec   [-1 or >=0] Floor specular even at low wetness
         * @param glossBoost[-1 or >=0] Additive gloss boost
         * @param specBoost [-1 or >=0] Additive specular boost
         * @param skinHairMul[-1 or >=0] Extra multiplier applied to skin/hair categories
         *
         * @note Call SetExternalWetnessMask() first if you need to (re)configure flags.
         */
        inline void SetExternalWetnessEx(RE::Actor* a, const char* key, float v, float durationSec, unsigned catMask,
                                         float maxGloss, float maxSpec, float minGloss, float minSpec, float glossBoost,
                                         float specBoost, float skinHairMul) {
            if (pSetExternalWetnessEx)
                pSetExternalWetnessEx(a, key, v, durationSec, catMask, maxGloss, maxSpec, minGloss, minSpec, glossBoost,
                                      specBoost, skinHairMul);
        }

        /**
         * @brief Submerge level (0 = dry, 1 = fully submerged).
         */
        inline float GetActorSubmergeLevel(RE::Actor* a) {
            return pGetActorSubmergeLevel ? pGetActorSubmergeLevel(a) : 0.0f;
        }

        /// @brief True if the actor is in water.
        inline bool IsActorInWater(RE::Actor* a) { return pIsActorInWater ? pIsActorInWater(a) : false; }
        /// @brief True if precipitation affecting the actor is detected (rain/snow).
        inline bool IsWetWeatherAround(RE::Actor* a) { return pIsWetWeatherAround ? pIsWetWeatherAround(a) : false; }
        /// @brief True if a heat source is found within @p r (Skyrim world units).
        inline bool IsNearHeatSource(RE::Actor* a, float r) {
            return pIsNearHeatSource ? pIsNearHeatSource(a, r) : false;
        }
        /// @brief True if the actor is detected to be under a roof/cover.
        inline bool IsUnderRoof(RE::Actor* a) { return pIsUnderRoof ? pIsUnderRoof(a) : false; }
        /// @brief True if actor is in "exterior wet" area (outside & exposed).
        inline bool IsActorInExteriorWet(RE::Actor* a) {
            return pIsActorInExteriorWet ? pIsActorInExteriorWet(a) : false;
        }

        /**
         * @brief Raw environment mask (see ENV_*). Prefer DecodeEnv() for convenience.
         */
        inline unsigned GetEnvMask(RE::Actor* a) { return pGetEnvMask ? pGetEnvMask(a) : 0u; }

        /**
         * @brief Helper to build a category mask (no flags).
         */
        inline unsigned MakeCatMask(bool skin, bool hair, bool armor, bool weapon) {
            unsigned m = 0;
            if (skin) m |= CAT_SKIN_FACE;
            if (hair) m |= CAT_HAIR;
            if (armor) m |= CAT_ARMOR_CLOTH;
            if (weapon) m |= CAT_WEAPON;
            return m;
        }

    }
}
