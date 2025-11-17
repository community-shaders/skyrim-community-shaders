#include "Util.h"

void Float3ToColor(const float3& newColor, RE::Color& color);

void Float3ToColor(const float3& newColor, RE::TESWeather::Data::Color3& color);

void ColorToFloat3(const RE::Color& color, float3& newColor);
void ColorToFloat3(const RE::TESWeather::Data::Color3& color, float3& newColor);
std::string ColorTimeLabel(const int i);
std::string ColorTypeLabel(const int i);