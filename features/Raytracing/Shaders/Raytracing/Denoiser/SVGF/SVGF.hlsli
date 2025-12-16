#ifndef SVGF_HLSI
#define SVGF_HLSI

struct SVGF
{
	half Alpha;
	half MomentsAlpha;
	half PhiColor;
	half PhiNormal;
	uint16_t StepSize;
	uint16_t PerformModulation;
};

#endif // SVGF_HLSI