#include "Util.h"

void Float3ToColor(const float3& newColor, RE::Color& color);
void Float3ToColor(const float3& newColor, RE::TESWeather::Data::Color3& color);

void ColorToFloat3(const RE::Color& color, float3& newColor);
void ColorToFloat3(const RE::TESWeather::Data::Color3& color, float3& newColor);

std::string ColorTimeLabel(const int i);
std::string ColorTypeLabel(const int i);

bool DrawSliderInt8(const std::string& label, int& property);
bool DrawColorEdit(const std::string& l, float3& property);
bool DrawSliderUint8(const std::string& label, int& property);
bool DrawSliderFloat(const std::string& label, float& property);

enum ControlType
{
	INT8_SLIDER = 0,
	COLOR3_PICKER,
	UINT8_SLIDER,
	FLOAT_SLIDER
};