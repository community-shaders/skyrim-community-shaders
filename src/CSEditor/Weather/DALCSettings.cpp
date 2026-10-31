#include "DALCSettings.h"

#include "../../I18n/I18n.h"
#include "../WeatherUtils.h"
#include "../Widget.h"
#include "Utils/UI.h"

void DALCSettings::Read(const RE::BGSDirectionalAmbientLightingColors& source)
{
	ColorToFloat3(source.directional.x.max, directional[0].max);
	ColorToFloat3(source.directional.x.min, directional[0].min);
	ColorToFloat3(source.directional.y.max, directional[1].max);
	ColorToFloat3(source.directional.y.min, directional[1].min);
	ColorToFloat3(source.directional.z.max, directional[2].max);
	ColorToFloat3(source.directional.z.min, directional[2].min);
	ColorToFloat3(source.specular, specular);
	fresnelPower = source.fresnelPower;
}

void DALCSettings::Write(RE::BGSDirectionalAmbientLightingColors& target) const
{
	Float3ToColor(directional[0].max, target.directional.x.max);
	Float3ToColor(directional[0].min, target.directional.x.min);
	Float3ToColor(directional[1].max, target.directional.y.max);
	Float3ToColor(directional[1].min, target.directional.y.min);
	Float3ToColor(directional[2].max, target.directional.z.max);
	Float3ToColor(directional[2].min, target.directional.z.min);
	Float3ToColor(specular, target.specular);
	target.fresnelPower = fresnelPower;
}

#define I18N_KEY_PREFIX "cs_editor."

bool DrawDALCControls(Widget& widget, DALCSettings& dalc, const char* inheritedTooltip)
{
	bool changed = false;
	auto draw = [&](auto&& control) { changed |= DrawInherited(inheritedTooltip, control); };
	auto colorEdit = [&](const char* label, float3& color) {
		draw([&] { return WeatherUtils::DrawColorEdit(label, color); });
	};

	if (widget.MatchesAnySearch({ DALCSetting::kSpecular, DALCSetting::kFresnelPower })) {
		ImGui::SeparatorText(T(TKEY("dalc_header"), "Directional Ambient Lighting (DALC)"));
		colorEdit(DALCSetting::kSpecular, dalc.specular);
		draw([&] { return WeatherUtils::DrawSliderFloat(DALCSetting::kFresnelPower, dalc.fresnelPower, 0.0f, 10.0f); });
	}

	if (widget.MatchesAnySearch({ DALCSetting::kXPlus, DALCSetting::kXMinus, DALCSetting::kYPlus,
			DALCSetting::kYMinus, DALCSetting::kZPlus, DALCSetting::kZMinus })) {
		ImGui::SeparatorText(T(TKEY("directional_colors"), "Directional Colors"));
		colorEdit(DALCSetting::kXPlus, dalc.directional[0].max);
		colorEdit(DALCSetting::kXMinus, dalc.directional[0].min);
		colorEdit(DALCSetting::kYPlus, dalc.directional[1].max);
		colorEdit(DALCSetting::kYMinus, dalc.directional[1].min);
		colorEdit(DALCSetting::kZPlus, dalc.directional[2].max);
		colorEdit(DALCSetting::kZMinus, dalc.directional[2].min);
	}
	return changed;
}

#undef I18N_KEY_PREFIX
