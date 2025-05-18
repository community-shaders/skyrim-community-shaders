#include "VR.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VR::Settings,
	EnableDepthBufferCulling,
	MinOccludeeBoxExtent)

void VR::DrawSettings()
{
	if (ImGui::Checkbox("Enable Depth Buffer Culling", &settings.EnableDepthBufferCulling))
		*gDepthBufferCulling = settings.EnableDepthBufferCulling;
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Enables a depth buffer culling solution that checks object bounds against the depth buffer before rendering. "
			"Provides a significant performance boost and includes fixes for game engine bugs. ");
	}

	if (settings.EnableDepthBufferCulling) {
		if (ImGui::SliderFloat("Min Occludee Box Extent", &settings.MinOccludeeBoxExtent, 0.1f, 500.0f, "%.1f"))
			*gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Sets the minimum bounding box size to use for objects when testing them against the depth buffer. "
				"Helps prevent small objects from flickering due to precision issues. "
				"Lower values will give better performance. ");
		}
	}
}

void VR::LoadSettings(json& o_json)
{
	settings = o_json;
}

void VR::SaveSettings(json& o_json)
{
	o_json = settings;
}

void VR::RestoreDefaultSettings()
{
	settings = {};
}

void VR::PostPostLoad()
{
	gDepthBufferCulling = reinterpret_cast<bool*>(REL::Offset(0x1EC6B88).address());
	gMinOccludeeBoxExtent = reinterpret_cast<float*>(REL::Offset(0x1ED64E8).address());

	// Patches BSGeometry::CopyTransformAndBounds to copy the model-bound translation across correctly instead of overwriting it with the bounding sphere centre
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xD9) + 0x2, 0x148);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xE5) + 0x2, 0x14C);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xF1) + 0x2, 0x150);
}

void VR::DataLoaded()
{
	*gDepthBufferCulling = settings.EnableDepthBufferCulling;
	*gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
}