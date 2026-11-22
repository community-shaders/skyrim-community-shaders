#pragma once
#include "PhysicalLensZoom.h"

namespace PhysicalLensImaging
{
	/// Similarity transform in millimetres; A4..A12 scale as length^(1-order).
	inline void ScaleOptics(Model& model, double scale)
	{
		for (unsigned i = 0; i < model.count; ++i) {
			auto& s = model.surfaces[i];
			s.z *= scale;
			s.radius *= scale;
			s.aperture *= scale;
			for (unsigned j = 0; j < s.asphere.size(); ++j)
				s.asphere[j] /= std::pow(scale, 3 + 2 * j);
		}
		model.sensorZ *= scale;
		model.focalLength *= scale;
	}

	/// Circumscribed radius of the camera's equal-area rounded aperture.
	/// Same polygon/circle interpolation and area normalization as DoF.
	inline double CameraApertureExtent(unsigned blades, double roundness)
	{
		const double pi = std::numbers::pi, sector = 2 * pi / blades;
		const double circum = std::sqrt(2 * pi / (blades * std::sin(sector)));
		double meanSquare = 0;
		for (unsigned i = 0; i < 2048; ++i) {
			double delta = ((i + .5) / 2048 - .5) * sector;
			double r = std::lerp(circum * std::cos(pi / blades) / std::cos(delta), 1., roundness);
			meanSquare += r * r / 2048;
		}
		return std::lerp(circum, 1., roundness) / std::sqrt(meanSquare);
	}

	/// Match camera focal length: native zoom cam inside its range, equivalent
	/// optical scaling outside it (also used for fixed-focal-length presets).
	inline bool CameraInfinity(int preset, double focalMM, double fNumber, Model& infinity)
	{
		if (preset == 5) {
			double lo = 0, hi = 1;
			for (unsigned i = 0; i < 24; ++i) {
				double mid = (lo + hi) * .5;
				if (!CanonZoom(mid, fNumber, Infinity, infinity))
					return false;
				if (infinity.focalLength < focalMM)
					lo = mid;
				else
					hi = mid;
			}
			if (!CanonZoom((lo + hi) * .5, fNumber, Infinity, infinity))
				return false;
		} else {
			infinity = Prime(preset, fNumber);
		}
		ScaleOptics(infinity, focalMM / infinity.focalLength);
		return true;
	}
	/// Reuse the selected zoom configuration during autofocus pulls.
	inline bool FocusCamera(const Model& infinity, double fNumber, double focusMM,
		double apertureExtent, Model& result, double& projectionFocal)
	{
		const double focalMM = infinity.focalLength;
		if (!Focus(infinity, focusMM, result))
			return false;
		// Camera f-number owns entrance-pupil area. Enlarging the clear glass
		// apertures for faster-than-native requests makes an equivalent lens,
		// rather than silently clamping the camera's aperture to the preset.
		double pupilScale = std::max(infinity.minimumFNumber / fNumber, 1.);
		for (unsigned i = 0; i < result.count; ++i)
			if (!result.surfaces[i].stop)
				result.surfaces[i].aperture *= pupilScale;
		result.fNumber = fNumber;
		result.surfaces[result.stop].aperture = std::abs(Transfer(result, result.stop).a) *
		                                        focalMM / (2 * fNumber) * apertureExtent;
		const auto reverse = Reverse(result);
		const auto pupil = Transfer(reverse, reverse.stop, .55);
		const auto transfer = SensorTransfer(reverse, .55);
		const double slope = transfer.c - transfer.d * pupil.a / pupil.b;
		const double height = transfer.a - transfer.b * pupil.a / pupil.b;
		// Preserve the camera's framing; focus breathing never writes FOV.
		projectionFocal = -1 / (slope + (height - reverse.sensorZ * slope) / focusMM);
		return std::isfinite(projectionFocal) && projectionFocal > 0;
	}
	/// Configure a complete camera-coupled lens, also used by offline validation.
	inline bool ConfigureCamera(int preset, double focalMM, double fNumber, double focusMM,
		unsigned blades, double roundness, Model& result, double& projectionFocal)
	{
		Model infinity;
		return CameraInfinity(preset, focalMM, fNumber, infinity) &&
		       FocusCamera(infinity, fNumber, focusMM, CameraApertureExtent(blades, roundness), result, projectionFocal);
	}

}
