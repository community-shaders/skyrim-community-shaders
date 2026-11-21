#include "EditorIDCache.h"

#include <shared_mutex>

// Adapted from powerofthree's Tweaks (GPL-3), src/Fixes/CacheEditorIDs.cpp at https://github.com/powerof3/po3-Tweaks.
// It hooks ~130 form types; this covers only those CS queries, so add more from its list as needed.
namespace
{
	constexpr std::size_t kSetFormEditorIDIndex = 0x33;
	constexpr std::uintptr_t kWeatherEditorIDReadOffset = 0x403;

	std::shared_mutex cacheMutex;
	std::unordered_map<RE::FormID, std::string> editorIds;

	/** @brief Records a form's editor ID; later plugins overwrite earlier ones so the winning override's ID is kept. */
	void Store(RE::FormID formId, const char* editorId)
	{
		if (!editorId || editorId[0] == '\0')
			return;
		std::unique_lock lock(cacheMutex);
		editorIds.insert_or_assign(formId, editorId);
	}

	/** @brief One instantiation per vtable, so each keeps its own original (po3 Tweaks may already hook it). */
	template <class Form>
	struct SetFormEditorID
	{
		static bool thunk(RE::TESForm* form, const char* editorId)
		{
			if (!form->IsDynamicForm())
				Store(form->GetFormID(), editorId);
			return func(form, editorId);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief The EDID chunk read in AE 1.6.1170+ TESWeather::Load, which po3 found bypasses the vfunc hook. */
	struct TESWeather_Load_ReadEditorID
	{
		static bool thunk(RE::TESFile* file, void* buffer, std::uint32_t size)
		{
			const bool read = func(file, buffer, size);
			if (read && file->currentform.formID != 0)
				Store(file->currentform.formID, static_cast<const char*>(buffer));
			return read;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <class... Forms>
	void HookSetFormEditorID()
	{
		(stl::write_vfunc<kSetFormEditorIDIndex, SetFormEditorID<Forms>>(Forms::VTABLE[0]), ...);
	}
}

void Util::EditorIDCache::Install()
{
	HookSetFormEditorID<
		RE::TESRegion, RE::BGSLocation,
		RE::TESWeather, RE::BGSLightingTemplate, RE::TESImageSpace, RE::BGSVolumetricLighting,
		RE::BGSShaderParticleGeometryData, RE::BGSLensFlare, RE::BGSReferenceEffect, RE::BGSArtObject,
		RE::TESEffectShader, RE::BGSTextureSet, RE::TESLandTexture, RE::BGSMaterialObject>();

	if (REL::Module::IsAE() && REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_6_1170)
		stl::write_thunk_call<TESWeather_Load_ReadEditorID>(
			REL::RelocationID(0, 20396).address() + kWeatherEditorIDReadOffset);

	logger::info("[EditorIDCache] Installed");
}

const char* Util::EditorIDCache::Find(RE::FormID formId)
{
	std::shared_lock lock(cacheMutex);
	const auto entry = editorIds.find(formId);
	return entry != editorIds.end() ? entry->second.c_str() : "";
}
