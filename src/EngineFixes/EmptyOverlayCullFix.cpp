#include "EmptyOverlayCullFix.h"

#include "Utils/FileSystem.h"
#include "Utils/Format.h"

namespace
{
	// RaceMenu's sDefaultTexture when the setting is unset.
	constexpr std::string_view RaceMenuDefaultTexture = "textures\\actors\\character\\overlays\\default.dds";

	// RaceMenu reads this one setting through SKSE's GetConfigOption, so from skse.ini; the copy in skee64.ini is never read.
	std::string ReadRaceMenuDefaultTexture()
	{
		const auto iniPath = Util::PathHelpers::GetDataPath() / "SKSE" / "skse.ini";
		std::array<wchar_t, MAX_PATH> value{};
		GetPrivateProfileStringW(L"Overlays/Data", L"sDefaultTexture", L"", value.data(), static_cast<DWORD>(value.size()), iniPath.c_str());
		return value[0] != L'\0' ? Util::WStringToString(value.data()) : std::string(RaceMenuDefaultTexture);
	}

	// RaceMenu's default iAlphaFlags 4845 blends SrcAlpha / InvSrcAlpha, so alpha 0 leaves the target untouched.
	// Alpha testing alone proves nothing: at RaceMenu's threshold 0 the engine's test passes alpha 0.
	bool BlendsAwayZeroAlpha(const RE::NiAlphaProperty* alpha)
	{
		using enum RE::NiAlphaProperty::AlphaFunction;
		return alpha && alpha->GetAlphaBlending() && alpha->GetSrcBlendMode() == kSrcAlpha && alpha->GetDestBlendMode() == kInvSrcAlpha;
	}

	std::string_view GetDiffusePath(RE::BSLightingShaderProperty* property)
	{
		auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(property->material);
		const char* path = material && material->textureSet ? material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr;
		return path ? path : "";
	}

	// Ordered cheapest first: most lighting geometry fails the flag test before any pointer is followed.
	// RaceMenu's overlay nifs name the texture "Textures\Actors\...", its built-in default "textures\actors\...", so case is ignored.
	bool IsEmptyOverlay(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::string_view emptyOverlayTexture)
	{
		return property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned) &&
		       BlendsAwayZeroAlpha(static_cast<RE::NiAlphaProperty*>(geometry->GetGeometryRuntimeData().alphaProperty.get())) &&
		       Util::IEquals(GetDiffusePath(property), emptyOverlayTexture);
	}
}

void EmptyOverlayCullFix::Install()
{
	emptyOverlayTexture = ReadRaceMenuDefaultTexture();
	logger::info("[Engine Fixes] Empty Overlay Cull Fix skips skinned SrcAlpha/InvSrcAlpha shapes whose diffuse is {}", emptyOverlayTexture);
	stl::write_vfunc<0x2A, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE_BSLightingShaderProperty[0]);
}

RE::BSShaderProperty::RenderPassArray* EmptyOverlayCullFix::BSLightingShaderProperty_GetRenderPasses::thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, uint32_t renderMode, RE::BSShaderAccumulator* accumulator)
{
	if (geometry && IsEmptyOverlay(property, geometry, emptyOverlayTexture)) {
		// Clear returns each pass to the engine's pool; dropping the head would leak them.
		property->renderPassList.Clear();
		return &property->renderPassList;
	}
	return func(property, geometry, renderMode, accumulator);
}
