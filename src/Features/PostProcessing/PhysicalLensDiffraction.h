#pragma once

#include "PhysicalLensImagingGPU.h"
#include <complex>
#include <span>
#include <vector>

// Scalar reciprocal pupil propagation. This reference separates actual wave
// interference from the geometric ghost splats; it contains no star texture.
namespace PhysicalLensDiffraction
{
	using namespace PhysicalLensImaging;
	using Complex = std::complex<double>;
	struct WavePupil
	{
		unsigned size = 0;
		double directionStep = 0;  // Spacing of transverse direction cosines.
		std::vector<Complex> field;
	};
	struct PointSpread
	{
		unsigned size = 0;
		double sensorStepMM = 0, pupilEnergy = 0, transformEnergy = 0;
		std::vector<double> intensity;
	};
	inline void Fourier(std::span<Complex> values)
	{
		const size_t n = values.size();
		for (size_t i = 1, j = 0; i < n; ++i) {
			size_t bit = n >> 1;
			for (; j & bit; bit >>= 1) j ^= bit;
			j ^= bit;
			if (i < j)
				std::swap(values[i], values[j]);
		}
		for (size_t length = 2; length <= n; length <<= 1) {
			const Complex step = std::polar(1., -2 * std::numbers::pi / length);
			for (size_t start = 0; start < n; start += length) {
				Complex phase = 1;
				for (size_t j = 0; j < length / 2; ++j) {
					const auto even = values[start + j], odd = phase * values[start + j + length / 2];
					values[start + j] = even + odd;
					values[start + j + length / 2] = even - odd;
					phase *= step;
				}
			}
		}
	}
	inline PointSpread Propagate(const WavePupil& pupil, double wavelengthUM)
	{
		PointSpread psf;
		psf.size = pupil.size;
		psf.sensorStepMM = wavelengthUM * .001 / (pupil.size * pupil.directionStep);
		auto spectrum = pupil.field;
		for (auto value : pupil.field) psf.pupilEnergy += std::norm(value);
		for (unsigned y = 0; y < pupil.size; ++y) Fourier(std::span(spectrum).subspan(y * pupil.size, pupil.size));
		std::vector<Complex> column(pupil.size);
		for (unsigned x = 0; x < pupil.size; ++x) {
			for (unsigned y = 0; y < pupil.size; ++y) column[y] = spectrum[y * pupil.size + x];
			Fourier(column);
			for (unsigned y = 0; y < pupil.size; ++y) spectrum[y * pupil.size + x] = column[y];
		}
		psf.intensity.resize(spectrum.size());
		for (unsigned y = 0; y < pupil.size; ++y)
			for (unsigned x = 0; x < pupil.size; ++x) {
				double value = std::norm(spectrum[y * pupil.size + x]);
				psf.transformEnergy += value;
				psf.intensity[((y + pupil.size / 2) % pupil.size) * pupil.size + (x + pupil.size / 2) % pupil.size] = value;
			}
		for (auto& value : psf.intensity) value /= std::max(psf.transformEnergy, 1e-30);
		return psf;
	}
	inline double PupilCoverage(double clearance, double gx, double gy)
	{
		double a = std::max(std::abs(gx), std::abs(gy)), b = std::min(std::abs(gx), std::abs(gy));
		double t = std::max(.5 * (a + b) - std::abs(clearance), 0.);
		double area = t >= b ? (t - .5 * b) / std::max(a, 1e-20) : t * t / std::max(2 * a * b, 1e-20);
		return clearance >= 0 ? 1 - area : area;
	}
	// Reciprocity supplies the pupil phase: trace from a sensor point to the
	// conjugate object wavefront. No depth buffer or scene defocus is involved.
	inline WavePupil BuildPupil(const Model& forward, double focusDistanceMM, double wavelengthUM,
		unsigned blades, double rotation, double coating, bool aberrations = true,
		unsigned pupilResolution = 0, unsigned padding = 0)
	{
		WavePupil pupil;
		const auto reverse = Reverse(forward);
		if (pupilResolution == 0)
			pupilResolution = DiffractionPupilSize(reverse.surfaces[reverse.stop].aperture / Transfer(reverse, reverse.stop, .55).b);
		if (padding == 0)
			padding = 512 / pupilResolution;
		pupil.size = pupilResolution * padding;
		const auto transfer = Transfer(reverse, reverse.stop, wavelengthUM);
		double slope = 1.2 * reverse.surfaces[reverse.stop].aperture / transfer.b;
		double extent = std::abs(slope) / std::sqrt(1 + slope * slope);
		pupil.directionStep = 2 * extent / pupilResolution;
		pupil.field.resize(pupil.size * pupil.size);
		double piston = 0;
		bool hasPiston = false;
		auto sample = [&](double qx, double qy, double margin) -> std::pair<Complex, double> {
			double qz = std::sqrt(std::max(1 - qx * qx - qy * qy, 0.));
			if (qz == 0)
				return { {}, -1 };
			auto hit = Trace(reverse, 0, 0, qx / qz, qy / qz, Infinity, wavelengthUM, blades, rotation, coating, forward.minimumFNumber / forward.fNumber, margin);
			if (hit.transmission <= 0)
				return { {}, -1 };
			double referenceDistance = -hit.sensor.z;
			if (std::isfinite(focusDistanceMM)) {
				Vec3 object = { 0, 0, focusDistanceMM };
				Vec3 delta = hit.sensor - object;
				referenceDistance = (hit.sensor.Dot(hit.sensor) - 2 * focusDistanceMM * hit.sensor.z) / (std::sqrt(delta.Dot(delta)) + focusDistanceMM);
			}
			double path = hit.opticalPath + referenceDistance;
			if (!hasPiston) {
				piston = path;
				hasPiston = true;
			}
			double phase = aberrations ? 2 * std::numbers::pi * (path - piston) / (wavelengthUM * .001) : 0;
			return { std::polar(std::sqrt(hit.transmission), phase), hit.apertureClearance };
		};
		std::vector<double> clearance(pupil.size * pupil.size, -1);
		for (unsigned y = 0; y < pupilResolution; ++y)
			for (unsigned x = 0; x < pupilResolution; ++x) {
				double qx = (x + .5 - pupilResolution * .5) * pupil.directionStep;
				double qy = (y + .5 - pupilResolution * .5) * pupil.directionStep;
				auto [value, distance] = sample(qx, qy, 4.8 / pupilResolution);
				unsigned column = x + (pupil.size - pupilResolution) / 2, row = y + (pupil.size - pupilResolution) / 2;
				pupil.field[row * pupil.size + column] = value;
				clearance[row * pupil.size + column] = distance;
			}
		for (unsigned y = 1; y + 1 < pupil.size; ++y)
			for (unsigned x = 1; x + 1 < pupil.size; ++x) {
				unsigned i = y * pupil.size + x;
				double coverage = clearance[i] >= 0 ? 1 : 0;
				if (std::abs(clearance[i]) < 2.4 / pupilResolution && std::norm(pupil.field[i]) > 0)
					coverage = PupilCoverage(clearance[i], .5 * (clearance[i + 1] - clearance[i - 1]), .5 * (clearance[i + pupil.size] - clearance[i - pupil.size]));
				pupil.field[i] *= coverage;
			}
		return pupil;
	}
}
