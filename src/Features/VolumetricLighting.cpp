#include "VolumetricLighting.h"

#include "ShaderCache.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VolumetricLighting::TextureSize,
	Width,
	Height,
	Depth);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VolumetricLighting::Settings,
	ExteriorEnabled,
	ExteriorQuality,
	ExteriorCustomSize,
	InteriorEnabled,
	InteriorQuality,
	InteriorCustomSize);

void VolumetricLighting::DrawSettings()
{
	// Display feature description at the top
	Util::DisplayFeatureDescription(GetFeatureDescription());
	
	if (ImGui::TreeNodeEx("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox("Enable Volumetric Lighting in Exteriors", &settings.ExteriorEnabled))
			SetupVL();

		if (settings.ExteriorEnabled)
			DrawVolumetricLightingSettings(settings.ExteriorQuality, settings.ExteriorCustomSize, false, !inInterior);

		if (ImGui::Checkbox("Enable Volumetric Lighting in Interiors", &settings.InteriorEnabled))
			SetupVL();

		if (settings.InteriorEnabled)
			DrawVolumetricLightingSettings(settings.InteriorQuality, settings.InteriorCustomSize, true, inInterior);

		ImGui::Spacing();
		ImGui::TreePop();
	}
	
	// Display version info at the bottom
	Util::DisplayVersionInfo(version);
}

void VolumetricLighting::DrawVolumetricLightingSettings(int32_t& quality, TextureSize& customSize, const bool isInterior, const bool inLocationType)
{
	auto& [Width, Height, Depth] = FetchCurrentSizeInUnits(isInterior);

	if (ImGui::SliderInt(isInterior ? "Interior Quality" : "Exterior Quality", &quality, 0, static_cast<uint8_t>(Quality::Count) - 1, QualityNames[quality])) {
		if (inLocationType)
			SetupVL();
	}

	const bool isCustomQuality = static_cast<Quality>(quality) == Quality::Custom;
	if (!isCustomQuality)
		ImGui::BeginDisabled();

	if (ImGui::SliderInt(isInterior ? "Interior Width" : "Exterior Width", &Width, 1, 20, FromUnits(Width, 32), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Width = Width * 32;
		if (inLocationType)
			SetupVL();
	}

	if (ImGui::SliderInt(isInterior ? "Interior Height" : "Exterior Height", &Height, 1, 20, FromUnits(Height, 32), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Height = Height * 32;
		if (inLocationType)
			SetupVL();
	}

	if (ImGui::SliderInt(isInterior ? "Interior Depth" : "Exterior Depth", &Depth, 1, 64, FromUnits(Depth, 10), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Depth = Depth * 10;
		if (inLocationType)
			SetupVL();
	}

	if (!isCustomQuality)
		ImGui::EndDisabled();
}

inline const char* VolumetricLighting::FromUnits(const int32_t value, const int32_t unitScale)
{
	static std::string s;
	s = std::to_string(value * unitScale);
	return s.c_str();
}

VolumetricLighting::TextureSize& VolumetricLighting::FetchCurrentSizeInUnits(const bool interior)
{
	auto& size = interior ? interiorSizeInUnits : exteriorSizeInUnits;
	if (interior) {
		switch (static_cast<Quality>(settings.InteriorQuality)) {
		case Quality::Low:
			size = *gVolumetricLightingSizeLow;
			break;
		case Quality::Medium:
			size = *gVolumetricLightingSizeMedium;
			break;
		case Quality::High:
			size = defaultSizeHigh;
			break;
		case Quality::Custom:
			size = settings.InteriorCustomSize;
			break;
		default:
			break;
		}
	} else {
		switch (static_cast<Quality>(settings.ExteriorQuality)) {
		case Quality::Low:
			size = *gVolumetricLightingSizeLow;
			break;
		case Quality::Medium:
			size = *gVolumetricLightingSizeMedium;
			break;
		case Quality::High:
			size = defaultSizeHigh;
			break;
		case Quality::Custom:
			size = settings.ExteriorCustomSize;
			break;
		default:
			break;
		}
	}

	size.Height /= 32;
	size.Width /= 32;
	size.Depth /= 10;

	return size;
}

void VolumetricLighting::LoadSettings(json& o_json)
{
	settings = o_json;
}

void VolumetricLighting::SaveSettings(json& o_json)
{
	o_json = settings;
}

void VolumetricLighting::RestoreDefaultSettings()
{
	settings = {};
	if (globals::game::isVR)
		Util::ResetGameSettingsToDefaults(hiddenVRSettings);
}

void VolumetricLighting::DataLoaded()
{
	auto shaderCache = globals::shaderCache;
	const static auto address = REL::Offset{ 0x1ec6b88 }.address();
	bool& bDepthBufferCulling = *reinterpret_cast<bool*>(address);

	if (REL::Module::IsVR() && bDepthBufferCulling && shaderCache->IsDiskCache()) {
		// clear cache to fix bug caused by bDepthBufferCulling
		logger::info("Force clearing cache due to bDepthBufferCulling");
		shaderCache->Clear();
	}
}

void VolumetricLighting::PostPostLoad()
{
	if (REL::Module::IsVR()) {
		if (settings.ExteriorEnabled || settings.InteriorEnabled)
			EnableBooleanSettings(hiddenVRSettings, GetName());
		auto address = REL::RelocationID(100475, 0).address() + 0x45b;  // AE not needed, VR only hook
		logger::info("[{}] Hooking CopyResource at {:x}", GetName(), address);
		REL::safe_fill(address, REL::NOP, 7);
		stl::write_thunk_call<CopyResource>(address);

		// Skip volumetric lighting rendering
		REL::safe_write(REL::RelocationID(35560, 0).address() + REL::Relocate(0x254, 0), &REL::JMP8, 1);
		// Move it to render after depth to ensure camera matches rest of scene
		stl::write_thunk_call<RenderDepth>(REL::RelocationID(35560, 0).address() + REL::Relocate(0x2EE, 0));
	}

	bEnableVolumetricLighting = reinterpret_cast<bool*>(REL::RelocationID(527940, 414913).address());
	gVolumetricLightingSizeLow = reinterpret_cast<TextureSize*>(REL::RelocationID(527970, 414916).address());
	gVolumetricLightingSizeMedium = reinterpret_cast<TextureSize*>(REL::RelocationID(527973, 414919).address());
	gVolumetricLightingSizeHigh = reinterpret_cast<TextureSize*>(REL::RelocationID(527976, 414922).address());
	defaultSizeHigh = *gVolumetricLightingSizeHigh;
}

void VolumetricLighting::EarlyPrepass()
{
	const auto interiorCell = globals::game::tes->interiorCell;
	const bool currentlyInInterior = interiorCell != nullptr;

	if (initialised && currentlyInInterior == inInterior)
		return;

	initialised = true;
	inInterior = currentlyInInterior;
	inInteriorWithSunShadows = interiorCell && interiorCell->cellFlags.all(RE::TESObjectCELL::Flag::kIsInteriorCell, RE::TESObjectCELL::Flag::kShowSky, RE::TESObjectCELL::Flag::kUseSkyLighting);
	SetupVL();
}

void VolumetricLighting::SetupVL()
{
	if (inInterior) {
		if (globals::game::isVR)
			SetBooleanSettings(hiddenVRSettings, GetName(), settings.InteriorEnabled && inInteriorWithSunShadows);
		else
			*bEnableVolumetricLighting = settings.InteriorEnabled && inInteriorWithSunShadows;
		*gVolumetricLightingSizeHigh = static_cast<Quality>(settings.InteriorQuality) == Quality::Custom ? settings.InteriorCustomSize : defaultSizeHigh;
		SetVLQuality(GetVLDescriptor(), settings.InteriorQuality);
	} else {
		if (globals::game::isVR)
			SetBooleanSettings(hiddenVRSettings, GetName(), settings.ExteriorEnabled);
		else
			*bEnableVolumetricLighting = settings.ExteriorEnabled;
		*gVolumetricLightingSizeHigh = static_cast<Quality>(settings.ExteriorQuality) == Quality::Custom ? settings.ExteriorCustomSize : defaultSizeHigh;
		SetVLQuality(GetVLDescriptor(), settings.ExteriorQuality);
	}
}

VolumetricLighting::VolumetricLightingDescriptor& VolumetricLighting::GetVLDescriptor()
{
	using func_t = decltype(&VolumetricLighting::GetVLDescriptor);
	static REL::Relocation<func_t> func{ REL::RelocationID(100297, 107014) };
	return func();
}

void VolumetricLighting::SetVLQuality(VolumetricLightingDescriptor& descriptor, const uint32_t quality)
{
	using func_t = decltype(&VolumetricLighting::SetVLQuality);
	static REL::Relocation<func_t> func{ REL::RelocationID(100299, 107016).address() };
	func(descriptor, std::clamp<uint32_t>(quality, 0, 2));
}

void VolumetricLighting::RenderVolumetricLighting(VolumetricLightingDescriptor* descriptor, RE::NiCamera* camera, bool flag)
{
	using func_t = decltype(&VolumetricLighting::RenderVolumetricLighting);
	static REL::Relocation<func_t> func{ REL::RelocationID(100306, 0) };
	func(descriptor, camera, flag);
}

void VolumetricLighting::RenderDepth::thunk()
{
	func();
	if (*GetSingleton()->bEnableVolumetricLighting)
		RenderVolumetricLighting(&GetVLDescriptor(), RE::Main::WorldRootCamera(), false);
}

bool VolumetricLighting::DrawFailLoadMessage() const {
    return false; 
}

void VolumetricLighting::DrawUnloadedUI()
{
	Feature::DrawUnloadedUI();

	// Feature-specific description
	ImGui::TextWrapped(
		"Volumetric Lighting is a technique that simulates the way light interacts with and\n"
		"is scattered by particles in the air, creating visible beams of light.");

	ImGui::Spacing();
	ImGui::TextWrapped("Key features:");
	ImGui::BulletText("Realistic God Rays");
	ImGui::BulletText("Applies in interiors and exteriors");
	ImGui::BulletText("Affected by day and night cycles");
}

