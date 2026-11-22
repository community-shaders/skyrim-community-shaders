#pragma once
#include "PhysicalLensCoating.h"
#include "PhysicalLensSpectrum.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Public patent reconstructions, NOT measured production copies or proprietary
// coatings. Radii, thickness, glass nd/Vd and clear diameters (mm) transcribed
// from the patent tables as catalogued by Bill Claff's Optical Bench:
// https://www.photonstophotos.net/GeneralTopics/Lenses/OpticalBench/OpticalBench.htm
// Master Prime: DE102005025204A1, example 1 (2005), asphere at patent surface 15.
// Sigma Art: JP2015114366A, example 1 (2015), infinity configuration;
// aspheres 22/23. Nominal commercial f/1.4 is f/1.46 in this patent example.
// Canon RF85: US20200012073A1 example 1 (2020), f/1.24, EFL 86.53 mm.
// This models the optical prescription; proprietary DS apodization is omitted.
// Leica Summilux-M35 Aspherical: US5161060A example 1 (1992); Optical Bench
// fits the patent's non-standard aspheric equation to A4/A6/A8 coefficients.
// The rounded Leica table gives a 35.41 mm paraxial EFL (nominal 35 mm).
// Sony/Zeiss Planar50 ZA: WO2017138250A1 example 2 (2017), f/1.45.
// The commercial 85/35/50 names are retained; no scaling of patent focal lengths.
// Clear diameters follow the catalogue reconstruction. Coatings are synthetic
// lossless two-layer stacks, not proprietary Zeiss / Sigma / Canon / Sony data.
namespace PhysicalLensOptics
{
	inline constexpr unsigned MaxSurfaces = 40, MaxPaths = 768, MaxConstraints = 120;
	inline constexpr unsigned PresetCount = 5;
	// Published iris counts: Sony SEL50F14Z 11; Sigma A014 50/1.4 and Canon
	// RF85/1.2 + RF24-70/2.8 9. Master Prime specifies at least 9; the exact
	// original Leica AA iris is unverified, so both retain the 9-blade estimate.
	// https://www.sony.com/electronics/support/lenses-e-mount-lenses/sel50f14z/specifications
	// https://www.sigma-global.com/en/lenses/a014_50_14/
	// https://global.canon/en/c-museum/product/rf482.html
	// https://global.canon/en/c-museum/product/rf485.html
	inline constexpr unsigned ApertureBladeCount(unsigned preset, int manualCount = 0)
	{
		return manualCount == 0 ? (preset == 4 ? 11u : 9u) : unsigned(std::clamp(manualCount, 3, 16));
	}
	// Offline convergence harness can use a denser pupil without changing the
	// shipping ray budget. Its shader compilation passes the same define.
#ifndef PHYSICAL_LENS_PUPIL_GRID
#	define PHYSICAL_LENS_PUPIL_GRID 33
#endif
	inline constexpr unsigned SourceCount = 16, PathsPerSource = 24, Grid = PHYSICAL_LENS_PUPIL_GRID;
	inline constexpr unsigned Vertices = Grid * Grid, DrawVertices = Vertices * 6 + 6;
	inline constexpr unsigned BeamDrawVertices = 6 + 12 * (Grid - 1) * (Grid - 1);
	template <class Index = std::uint16_t>
	inline std::array<Index, BeamDrawVertices> BeamIndices()
	{
		static_assert(sizeof(Index) > 2 || 4 + 8 * (Grid - 1) * (Grid - 1) <= 65536);
		std::array<Index, BeamDrawVertices> indices{ 0, 1, 2, 2, 1, 3 };
		unsigned at = 6;
		for (unsigned quad = 0; quad < 2 * (Grid - 1) * (Grid - 1); ++quad) {
			unsigned first = 4 + quad * 4;
			for (unsigned corner : { 0u, 1u, 2u, 2u, 1u, 3u }) indices[at++] = Index(first + corner);
		}
		return indices;
	}
	template <class Index = std::uint16_t>
	inline std::array<Index, DrawVertices> SplatIndices()
	{
		static_assert(sizeof(Index) > 2 || 4 + 4 * Vertices <= 65536);
		std::array<Index, DrawVertices> indices{ 0, 1, 2, 2, 1, 3 };
		unsigned at = 6;
		for (unsigned sample = 0; sample < Vertices; ++sample) {
			unsigned a = 4 + sample * 4;
			for (unsigned index : { a, a + 1, a + 2, a + 2, a + 1, a + 3 }) indices[at++] = Index(index);
		}
		return indices;
	}
	struct Surface
	{
		float radius, thickness, nd, dispersion, semiDiameter;
		std::array<float, 4> asphere{};
	};
	struct Prescription
	{
		std::array<Surface, MaxSurfaces> surfaces;
		unsigned count;
		float minimumFNumber, gateWidth, gateHeight;
	};
	constexpr float FromAbbe(float nd, float vd) { return (nd - 1) / vd / (1 / (.48613f * .48613f) - 1 / (.65627f * .65627f)); }
	// clang-format off
inline constexpr Prescription MasterPrime{ {{
  { -290.07f, 4.0f, 1.48751f, FromAbbe(1.48751f, 70.4f), 31.44f },
  { 50.119f, 16.2f, 1.0f, 0.f, 29.925f },
  { -122.32f, 7.5f, 1.75528f, FromAbbe(1.75528f, 27.58f), 29.955f },
  { -70.795f, 11.7f, 1.0f, 0.f, 29.955f },
  { -52.708f, 4.1f, 1.65416f, FromAbbe(1.65416f, 39.63f), 30.815f },
  { -658.79f, 1.3f, 1.0f, 0.f, 32.83f },
  { 434.01f, 18.0f, 1.52875f, FromAbbe(1.52875f, 76.97f), 34.965f },
  { -60.43f, 0.3f, 1.0f, 0.f, 34.965f },
  { 78.863f, 15.4f, 1.49702f, FromAbbe(1.49702f, 81.54f), 33.48f },
  { -196.68f, 10.361f, 1.0f, 0.f, 33.48f },
  { 46.639f, 16.7f, 1.72005f, FromAbbe(1.72005f, 43.69f), 27.52f },
  { -143.3f, 4.0f, 1.65416f, FromAbbe(1.65416f, 39.63f), 25.775f },
  { 103.66f, 1.0f, 1.0f, 0.f, 23.315f },
  { 92.48961f, 4.0f, 1.71743f, FromAbbe(1.71743f, 29.62f), 20.745f, { -1.132e-06f, 4.95e-10f, -1.325e-13f, 6.213e-16f } },
  { 31.623f, 9.32f, 1.0f, 0.f, 17.56f },
  { 0.0f, 7.0f, 0.0f, 0.f, 16.4575f },
  { -31.851f, 4.0f, 1.67276f, FromAbbe(1.67276f, 32.21f), 15.925f },
  { 114.65f, 3.56f, 1.0f, 0.f, 16.615f },
  { -170.31f, 7.3f, 1.74104f, FromAbbe(1.74104f, 52.64f), 17.02f },
  { -47.315f, 0.2f, 1.0f, 0.f, 17.93f },
  { 202.42f, 7.5f, 1.52857f, FromAbbe(1.52857f, 76.97f), 18.455f },
  { -74.452f, 0.2f, 1.0f, 0.f, 18.455f },
  { 69.783f, 10.4f, 1.49702f, FromAbbe(1.49702f, 81.54f), 18.04f },
  { -82.937f, 0.0f, 1.0f, 0.f, 18.04f },
}}, 24, 1.3f, 24.89f, 18.66f };
inline constexpr Prescription SigmaArt{ {{
  { 59.9687f, 7.5464f, 1.91082f, FromAbbe(1.91082f, 35.25f), 31.22f },
  { 178.4018f, 0.6242f, 1.0f, 0.f, 31.22f },
  { 192.0154f, 2.0f, 1.62588f, FromAbbe(1.62588f, 35.74f), 30.02f },
  { 28.7552f, 16.1562f, 1.0f, 0.f, 22.94f },
  { -67.9622f, 10.7338f, 1.497f, FromAbbe(1.497f, 81.61f), 23.28f },
  { -26.2761f, 1.5f, 1.54814f, FromAbbe(1.54814f, 45.82f), 23.28f },
  { 43.9346f, 10.7524f, 1.883f, FromAbbe(1.883f, 40.81f), 23.28f },
  { -94.4447f, 9.9796f, 1.0f, 0.f, 23.28f },
  { 49.8944f, 3.9121f, 1.92286f, FromAbbe(1.92286f, 20.88f), 21.06f },
  { 108.4891f, 0.15f, 1.0f, 0.f, 21.06f },
  { 47.6761f, 8.0538f, 1.72916f, FromAbbe(1.72916f, 54.67f), 19.87f },
  { -69.2432f, 0.95f, 1.72825f, FromAbbe(1.72825f, 28.32f), 19.87f },
  { 31.4317f, 6.0186f, 1.0f, 0.f, 15.94f },
  { 0.0f, 4.6436f, 0.0f, 0.f, 14.972f },
  { -31.6556f, 0.95f, 1.62588f, FromAbbe(1.62588f, 35.74f), 15.6f },
  { 71.6171f, 4.661f, 1.59282f, FromAbbe(1.59282f, 68.62f), 16.88f },
  { -96.5972f, 0.15f, 1.0f, 0.f, 16.88f },
  { 85.5221f, 6.6571f, 1.59282f, FromAbbe(1.59282f, 68.62f), 17.05f },
  { -40.3262f, 0.95f, 1.51742f, FromAbbe(1.51742f, 52.15f), 17.05f },
  { 57.5543f, 0.9291f, 1.0f, 0.f, 17.05f },
  { 77.4702f, 5.8822f, 1.7725f, FromAbbe(1.7725f, 49.47f), 17.05f, { -9.93095e-07f, 1.24366e-09f, -7.51084e-13f, 0.0f } },
  { -47.5092f, 0.0f, 1.0f, 0.f, 17.05f, { 3.17495e-06f, -1.01711e-10f, 2.0025e-12f, -1.26705e-15f } },
}}, 22, 1.46f, 36.0f, 24.0f };
// US20200012073_Example01P: infinity-focus patent example.
inline constexpr Prescription CanonRF85{ {{
  { 71.655f, 15.57f, 1.618f, FromAbbe(1.618f, 63.4f), 35.1f },
  { -149.456f, 3.0f, 1.72047f, FromAbbe(1.72047f, 34.7f), 35.1f },
  { 112.469f, 14.36f, 1.0f, 0.f, 32.315f },
  { 67.332f, 5.75f, 1.92286f, FromAbbe(1.92286f, 20.9f), 30.76f },
  { 141.236f, 0.3f, 1.0f, 0.f, 30.3f },
  { 39.868f, 11.43f, 1.497f, FromAbbe(1.497f, 81.5f), 27.5f },
  { 200.86f, 6.51f, 1.0f, 0.f, 26.36f },
  { 75.484f, 2.5f, 1.85478f, FromAbbe(1.85478f, 24.8f), 20.62f, { -2.2875e-06f, -2.1286e-10f, 2.6709e-13f, 0.0f } },
  { 27.701f, 10.68f, 1.0f, 0.f, 17.58f },
  { 0.0f, 4.51f, 0.0f, 0.f, 16.55f },
  { -85.831f, 1.5f, 1.85478f, FromAbbe(1.85478f, 24.8f), 15.835f },
  { 33.313f, 9.5f, 1.883f, FromAbbe(1.883f, 40.8f), 15.6f },
  { -68.563f, 2.28f, 1.0f, 0.f, 15.44f },
  { -48.43f, 1.7f, 1.54072f, FromAbbe(1.54072f, 47.2f), 14.925f },
  { 58.169f, 1.0f, 1.60401f, FromAbbe(1.60401f, 20.8f), 17.0f },
  { 77.751f, 6.95f, 1.95375f, FromAbbe(1.95375f, 32.3f), 17.11f },
  { -77.751f, 1.64f, 1.0f, 0.f, 17.7f },
  { 204.839f, 7.0f, 1.95375f, FromAbbe(1.95375f, 32.3f), 18.425f },
  { -48.06f, 2.2f, 1.62004f, FromAbbe(1.62004f, 36.3f), 18.515f },
  { 97.164f, 5.4f, 1.0f, 0.f, 18.2f },
  { -52.101f, 1.65f, 1.68893f, FromAbbe(1.68893f, 31.1f), 18.22f },
  { 237.864f, 0.15f, 1.0f, 0.f, 19.245f },
  { 97.826f, 4.0f, 1.90043f, FromAbbe(1.90043f, 37.4f), 19.775f },
  { -370.202f, 0.0f, 1.0f, 0.f, 19.895f },
}}, 24, 1.24f, 36.f, 24.f };
// US005161060_Example01P: infinity-focus patent example.
inline constexpr Prescription Summilux35{ {{
  { -110.114f, 2.01f, 1.503f, FromAbbe(1.503f, 56.1f), 17.225f },
  { 24.92f, 7.4f, 1.82f, FromAbbe(1.82f, 45.1f), 17.225f },
  { -305.0f, 0.1f, 1.0f, 0.f, 17.225f },
  { 28.346f, 6.07f, 1.82f, FromAbbe(1.82f, 45.1f), 12.06f, { -1.01662e-05f, -3.85859e-08f, -5.57379e-12f, 0.0f } },
  { -57.56f, 1.61f, 1.694f, FromAbbe(1.694f, 31.0f), 12.06f },
  { 16.624f, 4.34f, 1.0f, 0.f, 9.805f },
  { 0.0f, 1.66f, 0.0f, 0.f, 9.7605f },
  { -197.204f, 6.07f, 1.792f, FromAbbe(1.792f, 47.2f), 9.82f },
  { -38.628f, 1.5f, 1.0f, 0.f, 9.82f },
  { -21.142f, 1.72f, 1.652f, FromAbbe(1.652f, 33.6f), 10.91f },
  { 101.985f, 5.86f, 1.82f, FromAbbe(1.82f, 45.1f), 10.91f },
  { -21.905f, 0.11f, 1.0f, 0.f, 10.91f },
  { 60.026f, 5.94f, 1.82f, FromAbbe(1.82f, 45.1f), 13.08f, { -5.31166e-06f, -6.51374e-09f, 3.61001e-11f, 0.0f } },
  { -31.325f, 2.05f, 1.624f, FromAbbe(1.624f, 36.1f), 13.08f },
  { 31.325f, 0.0f, 1.0f, 0.f, 13.08f },
}}, 15, 1.4f, 36.f, 24.f };
// WO2017-138250_Example02P: infinity-focus patent example.
inline constexpr Prescription Planar50{ {{
  { 9999.0f, 3.887f, 1.72341f, FromAbbe(1.72341f, 37.9f), 25.185f },
  { -128.215f, 2.552f, 1.0f, 0.f, 25.185f },
  { 114.514f, 4.946f, 1.85134f, FromAbbe(1.85134f, 40.1f), 19.395f, { -4.5136e-06f, -1.7517e-09f, 1.83e-14f, 0.0f } },
  { -116.373f, 2.178f, 1.51679f, FromAbbe(1.51679f, 64.1f), 19.395f },
  { 28.12f, 8.835f, 1.0f, 0.f, 17.21f },
  { -51.934f, 2.897f, 1.71735f, FromAbbe(1.71735f, 29.5f), 17.21f },
  { 44.703f, 10.94f, 1.72916f, FromAbbe(1.72916f, 54.6f), 19.75f },
  { -54.876f, 1.144f, 1.0f, 0.f, 19.75f },
  { 0.0f, 1.764f, 0.0f, 0.f, 19.291f },
  { 68.142f, 9.35f, 1.59282f, FromAbbe(1.59282f, 68.6f), 21.4f },
  { -54.525f, 2.442f, 1.0f, 0.f, 21.4f },
  { -243.918f, 1.8f, 1.99502f, FromAbbe(1.99502f, 29.3f), 18.505f },
  { 44.296f, 5.45f, 1.0f, 0.f, 17.665f },
  { 105.875f, 3.768f, 1.94594f, FromAbbe(1.94594f, 17.9f), 18.26f },
  { -150.783f, 12.684f, 1.0f, 0.f, 18.26f },
  { 93.568f, 5.822f, 1.80419f, FromAbbe(1.80419f, 46.5f), 17.835f },
  { -62.024f, 0.5f, 1.0f, 0.f, 17.835f },
  { 70.486f, 10.36f, 1.80419f, FromAbbe(1.80419f, 46.5f), 17.34f },
  { -37.614f, 1.931f, 1.80518f, FromAbbe(1.80518f, 25.4f), 17.34f },
  { 34.944f, 8.141f, 1.0f, 0.f, 16.265f },
  { -34.288f, 1.8f, 1.58312f, FromAbbe(1.58312f, 59.3f), 16.265f },
  { -96.201f, 0.0f, 1.0f, 0.f, 17.13f, { 7.3383e-06f, 1.6537e-09f, 2.1185e-11f, -1.5608e-14f } },
}}, 22, 1.45f, 36.f, 24.f };
	// clang-format on
	inline const Prescription& GetPrescription(int preset)
	{
		switch (preset) {
		case 1:
			return SigmaArt;
		case 2:
			return CanonRF85;
		case 3:
			return Summilux35;
		case 4:
			return Planar50;
		default:
			return MasterPrime;
		}
	}
	struct alignas(16) Interface
	{
		float z, radius, aperture, stop;
		float ndBefore, bBefore, ndAfter, bAfter;
		std::array<float, 4> asphere{};
		std::array<float, 4> highOrder{};   // A12 for the modern zoom prescription.
		std::array<float, 4> dispersion{};  // Cubic curvature before/after; nd and Abbe stay in the original fields.
		PhysicalLensCoating::Table coatingTable{};
	};
	struct Model
	{
		std::array<Interface, MaxSurfaces> interfaces{};
		unsigned count = 0;
		float sensorZ = 0, focalLength = 0, pupilRadius = 0;
	};
	/// Focus a public prescription at paraxial infinity; calibrate the entrance pupil.
	inline Model Build(int preset, float fNumber)
	{
		const auto& p = GetPrescription(preset);
		Model m{};
		m.count = p.count;
		float z = 0, nd = 1, b = 0, y = 1, slope = 0, stopY = 1;
		unsigned stopIndex = 0;
		for (unsigned i = 0; i < p.count; ++i) {
			const auto& s = p.surfaces[i];
			bool stop = s.nd == 0;
			float nextND = stop ? nd : s.nd;
			m.interfaces[i] = { z, s.radius, s.semiDiameter, stop ? 1.f : 0.f, nd, b, nextND, stop ? b : s.dispersion, s.asphere };
			m.interfaces[i].dispersion = PhysicalLensDispersion::Pack(nd, b, nextND, stop ? b : s.dispersion);
			if (!stop && std::fmin(nd, nextND) < 1.001f)
				m.interfaces[i].coatingTable = PhysicalLensCoating::Get(std::fmax(nd, nextND), nd > nextND ? b : s.dispersion, unsigned(preset) * 131 + i);
			if (stop) {
				stopY = y;
				stopIndex = i;
			} else if (s.radius != 0)
				slope += (nd - nextND) * y / s.radius;
			nd = nextND;
			b = stop ? b : s.dispersion;
			y += s.thickness * slope / nd;
			z += s.thickness;
		}
		m.focalLength = -1 / slope;
		m.sensorZ = z - y / slope;
		m.pupilRadius = m.focalLength / (2 * std::fmax(fNumber, p.minimumFNumber));
		m.interfaces[stopIndex].aperture = std::abs(stopY) * m.pupilRadius;
		return m;
	}
	struct alignas(16) GhostPath
	{
		std::array<unsigned, 4> indices{};
		std::array<float, 4> sensorMap{};  // A, B, normal-incidence bare/coated path transmission
		// Per-channel mean and standard deviation of 1/lambda^2, plus the
		// integrated-throughput correction at that representative wavelength.
		// Red's fourth component is its short-wave response relative to blue.
		std::array<std::array<float, 4>, 3> spectrum{};
		std::array<std::array<float, 4>, MaxConstraints> constraints{};
	};
	struct alignas(16) Parameters
	{
		unsigned width = 0, height = 0, surfaceCount = 0, pathCount = 0;
		float sensorWidth = 0, sensorHeight = 0, sensorZ = 0, focalLength = 0;
		float pupilRadius = 0, threshold = 0, intensity = 1, coating = 1;
		unsigned blades = 9;
		float rotation = 0, outputWidth = 0, outputHeight = 0;
		std::array<Interface, MaxSurfaces> surfaces{};
		unsigned maxSources = 8;
		float exposureDecode = 1, manualExposure = 1, autoExposure = 0;
		float bladeCurvature = 0;
		float ghostDiffraction = 0, fNumber = 5.6f, beamState = 0;
		float cutoffRelease = .96f, alignmentVariation = 0, lensSeed = 1, reserved = 0;
	};
	static_assert(sizeof(Parameters) == 49392);
	/// Paraxial transfer along the same two-reflection path, used only to focus
	/// sampling on the transmitted pupil and to propagate finite source area.
	/// The actual sample positions/throughputs still use spherical/aspherical Snell tracing.
	inline GhostPath BuildPath(const Model& lens, unsigned back, unsigned front, float coating = 1)
	{
		GhostPath path{};
		path.indices[0] = back;
		path.indices[1] = front;
		float a = 1, b = 0, c = 0, d = 1, z = 0, transmission = 1;
		std::array<float, 3> coated{ 1, 1, 1 };
		std::array<double, 31> spectralTransmission;
		spectralTransmission.fill(1);
		int step = 1, surface = 0;
		unsigned reflections = 0;
		while (surface >= 0 && surface < static_cast<int>(lens.count)) {
			const auto& s = lens.interfaces[surface];
			const float dz = s.z - z;
			a += dz * c;
			b += dz * d;
			z = s.z;
			// Source-independent pupil bounds: evaluate divisions once per lens change.
			if (std::abs(a) >= 1e-5f)
				path.constraints[path.indices[2]++] = { -b / a, (s.aperture * 1.2f + .05f) / std::abs(a), 0, 0 };
			if (!s.stop) {
				const bool bounce = (reflections == 0 && surface == static_cast<int>(back)) || (reflections == 1 && surface == static_cast<int>(front));
				const float curvature = s.radius == 0 ? 0 : 1 / s.radius;
				float r = std::pow((s.ndBefore - s.ndAfter) / (s.ndBefore + s.ndAfter), 2.f);
				transmission *= bounce ? r : 1 - r;
				const float wavelengths[3]{ .650f, .550f, .450f };
				for (unsigned band = 0; band < 3; ++band) {
					float lambda = wavelengths[band];
					double n1 = PhysicalLensDispersion::Index(s.ndBefore, s.bBefore, lambda), n2 = PhysicalLensDispersion::Index(s.ndAfter, s.bAfter, lambda);
					float film = float(std::pow((n1 - n2) / (n1 + n2), 2));
					if (std::fmin(n1, n2) < 1.001f)
						film = float(PhysicalLensCoating::Sample(s.coatingTable, 1, lambda));
					coated[band] *= bounce ? film : 1 - film;
				}
				for (unsigned sample = 0; sample < spectralTransmission.size(); ++sample) {
					double lambda = .4 + .01 * sample;
					double n1 = PhysicalLensDispersion::Index(s.ndBefore, s.bBefore, lambda), n2 = PhysicalLensDispersion::Index(s.ndAfter, s.bAfter, lambda);
					double reflectance = std::pow((n1 - n2) / (n1 + n2), 2);
					if (std::fmin(n1, n2) < 1.001)
						reflectance = std::lerp(reflectance, PhysicalLensCoating::Sample(s.coatingTable, 1, lambda), double(coating));
					spectralTransmission[sample] *= bounce ? reflectance : 1 - reflectance;
				}
				if (bounce) {
					c = -c - 2 * a * curvature;
					d = -d - 2 * b * curvature;
					step = -step;
					++reflections;
				} else {
					const float from = step > 0 ? s.ndBefore : s.ndAfter;
					const float to = step > 0 ? s.ndAfter : s.ndBefore;
					c = (from * c + (from - to) * a * curvature) / to;
					d = (from * d + (from - to) * b * curvature) / to;
				}
			}
			surface += step;
		}
		path.sensorMap = { a + (lens.sensorZ - z) * c, b + (lens.sensorZ - z) * d, transmission, (coated[0] + coated[1] + coated[2]) / 3 };
		for (unsigned band = 0; band < 3; ++band) {
			double mass = 0, mean = 0, second = 0;
			for (unsigned sample = 0; sample < spectralTransmission.size(); ++sample) {
				// Red has a separate violet lobe. Averaging both into one ray can
				// put its representative on the blue side of green under coatings.
				// Trace the long-wave lobe; reuse the blue beam for the short one.
				double weight = band == 0 && sample < 10 ? 0 : PhysicalLensSpectrum::Weights[sample][band] * spectralTransmission[sample];
				double q = 1 / std::pow(.4 + .01 * sample, 2);
				mass += weight;
				mean += weight * q;
				second += weight * q * q;
			}
			mean /= std::max(mass, 1e-30);
			double sigma = std::sqrt(std::max(second / std::max(mass, 1e-30) - mean * mean, 0.));
			double sample = std::clamp((1 / std::sqrt(mean) - .4) * 100, 0., 30.);
			unsigned lo = std::min(unsigned(sample), 29u);
			double atRepresentative = std::lerp(spectralTransmission[lo], spectralTransmission[lo + 1], sample - lo);
			path.spectrum[band] = { float(mean), float(sigma), float(mass / std::max(atRepresentative, 1e-30)), 0 };
		}
		double violet = 0, blue = 0;
		for (unsigned sample = 0; sample < spectralTransmission.size(); ++sample) {
			if (sample < 10)
				violet += PhysicalLensSpectrum::Weights[sample][0] * spectralTransmission[sample];
			blue += PhysicalLensSpectrum::Weights[sample][2] * spectralTransmission[sample];
		}
		path.spectrum[0][3] = float(violet / std::max(blue, 1e-30));
		return path;
	}
}
