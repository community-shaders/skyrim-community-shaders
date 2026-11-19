#ifndef PHYSICAL_LENS_DIFFRACTION_PARAMETERS
#define PHYSICAL_LENS_DIFFRACTION_PARAMETERS
#ifndef WAVE_PARAMETER_REGISTER
#	define WAVE_PARAMETER_REGISTER b2
#endif
cbuffer DiffractionParameters : register(WAVE_PARAMETER_REGISTER)
{
	float4 WaveSampling[3];
	uint2 WaveImageSize;
	// 0: inactive, 1: identity image mapping, 2: lens image mapping.
	float DiffractionEnabled;
	uint WavePupilSize;
	float4 WaveSurface;
	float4 WaveStarburst;
	float4 WavePupilDirt;
};
#endif
