#pragma once
#include "PhysicalLensModel.h"

namespace PhysicalLensImaging
{
	struct alignas(16) GPUInterface
	{
		std::array<float, 4> geometry{}, media{}, asphere{}, highOrder{}, dispersion{};
		PhysicalLensCoating::Table coatingTable{};
	};
	struct alignas(16) GPUParameters
	{
		unsigned surfaceCount = 0, rayCount = 0;
		float sensorZ = 0, coating = 1;
		unsigned blades = 0;
		float rotation = 0, bladeCurvature = 0, padding = 0;
		std::array<GPUInterface, MaxSurfaces> surfaces{};
	};
	struct GPURay
	{
		std::array<float, 4> pupilField{};
		std::array<float, 4> conjugate{};  // Inverse front-vertex object distance, wavelength.
	};
	struct GPUHit
	{
		std::array<float, 4> sensorEnergy{}, direction{};
	};
	static_assert(sizeof(GPUInterface) == 1232 && sizeof(GPUParameters) == 49312);
	struct alignas(16) CameraTableParameters
	{
		unsigned width = 33, height = 19, samples = 128;
		float inverseFocusDistance = 0;
		float gateWidth = 36, gateHeight = 24, projectionFocal = 50, rayPlaneZ = 0;
		std::array<std::array<float, 4>, 3> pupil{};
	};
	static_assert(sizeof(CameraTableParameters) == 80);
	struct alignas(16) ImageParameters
	{
		unsigned width = 0, height = 0, gridWidth = 33, gridHeight = 19;
	};
	static_assert(sizeof(ImageParameters) == 16);
	struct alignas(16) DiffractionParameters
	{
		// PSF texel size in output pixels (xy) and normalized image UV (zw).
		std::array<std::array<float, 4>, 3> sampling{};
		unsigned imageWidth = 0, imageHeight = 0;
		float enabled = 0;
		unsigned pupilSize = 256;
		std::array<float, 4> surface{};                          // Legacy scatter, stable lens seed, entrance-pupil radius (mm), dirt opacity.
		std::array<float, 4> starburst{};                        // Size coefficient, integrated gain at f/5.6, F-Number, reconstruction mode (0 optical, 1 legacy paper, 2 detailed paper).
		std::array<float, 4> pupilDirt{ 1, 1, 1, 3.14159265f };  // Density, size, scratch length, orientation spread (radians).
	};
	static_assert(sizeof(DiffractionParameters) == 112);
	inline unsigned DiffractionPupilSize(double paraxialCone)
	{
		// Fast lenses have wider aberrated wings, but a smaller diffraction core
		// in physical units. Trade zero padding for twice the pupil density while
		// keeping the FFT and its memory footprint fixed at 512 squared.
		return std::abs(paraxialCone) > .2 ? 512 : 256;
	}
	inline DiffractionParameters DiffractionTable(const CameraTableParameters& camera, unsigned width, unsigned height, float fNumber = 0, float size = 10, float intensity = .02f, unsigned pupilResolution = 0)
	{
		DiffractionParameters p;
		p.starburst = { size, intensity, fNumber, fNumber > 0 ? 2.f : 0.f };
		p.imageWidth = width;
		p.imageHeight = height;
		p.enabled = 1;
		p.pupilSize = pupilResolution ? pupilResolution : DiffractionPupilSize(camera.pupil[1][2] / camera.pupil[1][1]);
		constexpr double wavelengths[]{ .65, .55, .45 };
		for (unsigned band = 0; band < 3; ++band) {
			double slope = 1.2 * camera.pupil[band][2] / camera.pupil[band][1];
			double extent = std::abs(slope) / std::sqrt(1 + slope * slope);
			double step = wavelengths[band] * .001 * p.pupilSize / (1024 * extent);
			// Hullin equations 2-4: a fixed unit pupil, with width proportional
			// to wavelength and f-number. The 2.4-radius pupil spans 1.2 diameters.
			if (fNumber > 0)
				step = wavelengths[band] * .001 * fNumber * p.pupilSize / (512 * 1.2) * size;
			p.sampling[band] = { float(step * width / camera.gateWidth), float(step * height / camera.gateHeight), float(step / camera.gateWidth), float(step / camera.gateHeight) };
		}
		return p;
	}
	inline CameraTableParameters CameraTable(const Model& forward, double projectionFocal, double aspect, double focusDistance = Infinity)
	{
		CameraTableParameters p;
		p.gateWidth = float(std::min(forward.gateWidth, forward.gateHeight * aspect));
		p.gateHeight = float(p.gateWidth / aspect);
		p.projectionFocal = float(projectionFocal);
		p.inverseFocusDistance = float(1 / focusDistance);
		const auto reverse = Reverse(forward);
		p.rayPlaneZ = float(reverse.sensorZ);
		constexpr double wavelengths[]{ .65, .55, .45 };
		for (unsigned band = 0; band < 3; ++band) {
			const auto transfer = Transfer(reverse, reverse.stop, wavelengths[band]);
			double radius = reverse.surfaces[reverse.stop].aperture;
			double relativeCone = 2 * forward.fNumber * radius / transfer.b;
			p.pupil[band] = { float(transfer.a), float(transfer.b), float(radius), float(relativeCone * relativeCone) };
		}
		return p;
	}
	static_assert(sizeof(GPURay) == 32 && sizeof(GPUHit) == 32);
	inline GPUParameters UploadParameters(const Model& model, unsigned rayCount, unsigned blades = 0, float rotation = 0, float coating = 1)
	{
		GPUParameters parameters;
		parameters.surfaceCount = model.count;
		parameters.rayCount = rayCount;
		parameters.sensorZ = float(model.sensorZ);
		parameters.coating = coating;
		parameters.blades = blades;
		parameters.rotation = rotation;
		parameters.bladeCurvature = float(model.minimumFNumber / model.fNumber);
		for (unsigned i = 0; i < model.count; ++i) {
			const auto& s = model.surfaces[i];
			auto& g = parameters.surfaces[i];
			g.geometry = { float(s.z), float(s.radius), float(s.aperture), s.stop ? 1.f : 0.f };
			g.media = { float(s.ndBefore), float(s.bBefore), float(s.ndAfter), float(s.bAfter) };
			for (unsigned j = 0; j < 4; ++j) g.asphere[j] = float(s.asphere[j]);
			g.highOrder[0] = float(s.asphere[4]);
			g.dispersion = PhysicalLensDispersion::Pack(s.ndBefore, s.bBefore, s.ndAfter, s.bAfter);
			if (s.coatingTable)
				g.coatingTable = *s.coatingTable;
		}
		return parameters;
	}
}
