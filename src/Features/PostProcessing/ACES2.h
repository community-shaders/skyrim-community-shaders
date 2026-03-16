#pragma once

// ACES 2.0 Output Transform
// Ported from the official aces-aswf/aces-core CTL reference implementation (Apache-2.0 license)
// Reference: https://github.com/ampas/aces-core (dev branch)

#include <array>
#include <cmath>

namespace ACES2
{
	static constexpr int TABLE_SIZE = 360;

	// Tone scale parameters (Michaelis-Menten based curve)
	struct TSParams
	{
		float n;              // peakLuminance / n_r
		float n_r;            // reference peak luminance (100.0)
		float g;              // contrast exponent (1.15)
		float t_1;            // shadow toe threshold (0.04)
		float c_t;            // crosstalk parameter
		float s_2;            // shoulder offset
		float u_2;            // mid-shadow parameter
		float m_2;            // max output in normalized space
		float forward_limit;  // input clamping limit
		float inverse_limit;  // inverse clamping limit
	};

	// JMh color appearance model parameters (CAM16-based)
	struct JMhParams
	{
		float XYZ_to_RGB[9];    // Input primaries XYZ→RGB
		float RGB_to_XYZ[9];    // Input primaries RGB→XYZ
		float XYZ_to_CAM16[9];  // CAM16 forward
		float CAM16_to_XYZ[9];  // CAM16 inverse
		float refWhiteXYZ[3];   // Reference white in XYZ (D65)
		float coneResponseMtx[9];
		float invConeResponseMtx[9];
	};

	// GPU constant buffer data — all parameters the shader needs
	// Packed for 16-byte alignment
	struct alignas(16) ACES2CB
	{
		// Tone scale params (padded to float4s)
		float ts_n;
		float ts_n_r;
		float ts_g;
		float ts_t_1;

		float ts_c_t;
		float ts_s_2;
		float ts_u_2;
		float ts_m_2;

		float ts_forward_limit;
		float ts_inverse_limit;
		float peakLuminance;
		float chromaCompressScale;

		// Matrices — stored as 3x float4 (row-major, .w = 0)
		float inputMtx[12];  // 3×4 AP0 RGB → JMh  input  matrix (with CAM16)
		float limitMtx[12];  // 3×4 limit gamut RGB → XYZ
		float reachMtx[12];  // 3×4 reach gamut RGB → XYZ

		float boundaryRGB_to_XYZ[12];
		float boundaryXYZ_to_RGB[12];

		// Gamut cusp table: J, M, h for TABLE_SIZE entries
		float gamutCuspTableJ[TABLE_SIZE];
		float gamutCuspTableM[TABLE_SIZE];
		float gamutCuspTableh[TABLE_SIZE];

		// Reach M table
		float reachMTable[TABLE_SIZE];

		// Upper hull gamma table
		float upperHullGamma[TABLE_SIZE];

		// lower hull gamma table
		float lowerHullGamma[TABLE_SIZE];
	};

	// Initialize all ACES 2.0 parameters for SDR (100 nits, sRGB output)
	ACES2CB ComputeParams(float peakLuminance = 100.0f);
}
