#pragma once
#include "PhysicalLensModel.h"

namespace PhysicalLensImaging
{
	// Canon US20190278068A1, numerical example 5 (2019).
	// https://patents.google.com/patent/US20190278068A1/en
	// Associated with RF24-70 F2.8 by the Optical Bench catalogue; this is the
	// public patent, not a measured production copy. No proprietary coating data.
	// All 37 surfaces, clear diameters and five A12 aspheres are retained.
	inline Model CanonZoomAtNode(unsigned node, double fNumber)
	{
		struct Row
		{
			double radius, thickness, nd, vd, diameter;
		};
		// clang-format off
		constexpr std::array<Row, 37> rows{{
			{232.357,2.10,1.80809,22.8,68.69}, {90.381,5.78,1.77250,49.6,65.95},
			{323.713,.15,1,0,65.38}, {59.139,7.05,1.72916,54.7,60.67}, {166.984,0,1,0,59.46},
			{67.802,1.40,1.88300,40.8,38.53}, {18.663,8.67,1,0,29.19},
			{-115.164,1.20,1.49700,81.5,28.34}, {21.412,3.87,1.85478,24.8,24.87},
			{47.653,4.17,1,0,23.86}, {-48.039,1.00,1.72916,54.7,22.68}, {425.782,.29,1,0,22.04},
			{93.568,6.03,1.59270,35.3,21.73}, {-24.518,1.05,1.83481,42.7,22.50}, {-71.957,0,1,0,23.85},
			{0,.40,0,0,25.91}, {78.059,4.31,1.76385,48.5,26.86}, {-59.447,.15,1,0,27.13},
			{40.155,7.40,1.49700,81.5,26.92}, {-33.807,1.10,2.00069,25.5,26.27}, {-143.919,0,1,0,26.33},
			{-63.149,1.65,1.58313,59.4,26.06}, {45.825,1.92,1.76182,26.5,26.27}, {84.251,0,1,0,26.24},
			{26.393,8.28,1.49700,81.5,27.41}, {-69.360,.35,1,0,27.25},
			{39.335,1.15,1.73800,32.3,26.90}, {26.899,7.03,1.53775,74.7,26.14}, {-73.879,.15,1,0,25.50},
			{-602.944,1.70,1.85400,40.4,25.28}, {131.941,0,1,0,24.90},
			{60.209,.90,1.80400,46.6,25.51}, {23.878,0,1,0,25.06},
			{-52.714,1.70,1.58313,59.4,29.85}, {-3211.285,.15,1,0,32.97},
			{317.277,3.23,2.00100,29.1,35.27}, {-114.700,0,1,0,35.82}
		}};
		constexpr std::array<unsigned, 7> variable{4,14,20,23,30,32,36};
		constexpr double gaps[3][7]{
			{.80,16.15,2.26,11.08,2.16,12.96,14.37},
			{17.98,7.07,2.23,4.97,1.42,13.70,26.78},
			{29.93,2.40,2.54,2.87,1.33,13.79,35.69}
		};
		// clang-format on
		Model m;
		m.count = unsigned(rows.size());
		m.stop = 15;
		m.minimumFNumber = 2.91;
		m.focusMechanism = FocusMechanism::InternalGroup;
		m.focusGroups[0] = { 31, 32 };  // Patent group L6 moves toward the image.
		double z = 0, nd = 1, b = 0;
		for (unsigned i = 0; i < m.count; ++i) {
			const auto& row = rows[i];
			auto& s = m.surfaces[i];
			s.z = z;
			s.radius = row.radius;
			s.aperture = row.diameter * .5;
			s.ndBefore = nd;
			s.bBefore = b;
			s.stop = row.nd == 0;
			if (!s.stop) {
				nd = row.nd;
				b = row.vd == 0 ? 0 : (nd - 1) / row.vd / (1 / (.48613 * .48613) - 1 / (.65627 * .65627));
			}
			s.ndAfter = nd;
			s.bAfter = b;
			if (!s.stop && std::min(s.ndBefore, s.ndAfter) < 1.001)
				s.coatingTable = &PhysicalLensCoating::Get(std::max(s.ndBefore, s.ndAfter), s.ndBefore > s.ndAfter ? s.bBefore : s.bAfter, 5 * 131 + i);
			double thickness = row.thickness;
			for (unsigned v = 0; v < variable.size(); ++v)
				if (i == variable[v])
					thickness = gaps[std::min(node, 2u)][v];
			z += thickness;
		}
		m.sensorZ = z;  // Preserve the patent's BF for independent validation.
		m.surfaces[21].asphere = { 3.01280e-6, -9.03767e-10, 5.61555e-11, -4.27609e-13, 9.23668e-16 };
		m.surfaces[29].asphere = { -5.55314e-5, 2.14060e-7, -4.57909e-11, -2.74784e-12, 6.52571e-15 };
		m.surfaces[30].asphere = { -3.45440e-5, 2.65378e-7, -1.27555e-10, -2.40980e-12, 7.25838e-15 };
		m.surfaces[33].asphere = { -3.28022e-5, 1.26827e-7, -2.01507e-10, -1.63600e-12, 4.86504e-15 };
		m.surfaces[34].asphere = { -3.30256e-5, 1.44829e-7, -5.66682e-10, 8.17897e-13, -2.19929e-16 };
		SetAperture(m, fNumber);
		return m;
	}
	/// Continuous cam approximation between the three published zoom positions.
	/// Interpolate vertex positions, then refocus L6 at the interpolated sensor
	/// plane. A request is a cam position [0,1], not an invented focal-length scale;
	/// the resulting EFL is computed from the actual refracting surfaces.
	inline bool CanonZoom(double position, double fNumber, double distanceMM, Model& result)
	{
		double t = std::clamp(position, 0.0, 1.0) * 2;
		unsigned segment = t < 1 ? 0 : 1;
		t -= segment;
		Model a = CanonZoomAtNode(segment, fNumber), b = CanonZoomAtNode(segment + 1, fNumber);
		for (unsigned i = 0; i < a.count; ++i) a.surfaces[i].z = std::lerp(a.surfaces[i].z, b.surfaces[i].z, t);
		a.sensorZ = std::lerp(a.sensorZ, b.sensorZ, t);
		SetAperture(a, fNumber);
		return Focus(a, distanceMM, result);
	}
	/// Shared configuration for image mapping and reflected paths. Focus never
	/// substitutes a different lens; the same moved surfaces feed both shaders.
	inline bool Configure(int preset, double fNumber, double zoomPosition, double focusDistanceMM, Model& result, double& projectionFocal)
	{
		Model infinity;
		if (preset == 5) {
			if (!CanonZoom(zoomPosition, fNumber, Infinity, infinity))
				return false;
			SetAperture(infinity, fNumber);
		} else
			infinity = Prime(preset, fNumber);
		if (!Focus(infinity, focusDistanceMM, result))
			return false;
		// Normalize the green paraxial image scale at the current conjugate.
		// Zoom/focus change the optics, but framing belongs to the game camera.
		const auto reverse = Reverse(result);
		const auto pupil = Transfer(reverse, reverse.stop, .55);
		auto transfer = SensorTransfer(reverse, .55);
		const double slope = transfer.c - transfer.d * pupil.a / pupil.b;
		const double height = transfer.a - transfer.b * pupil.a / pupil.b;
		projectionFocal = -1 / (slope + (height - reverse.sensorZ * slope) / focusDistanceMM);
		return true;
	}
	inline PhysicalLensOptics::Model GhostModel(const Model& model, double projectionFocal)
	{
		static_assert(MaxSurfaces <= PhysicalLensOptics::MaxSurfaces);
		PhysicalLensOptics::Model ghost;
		ghost.count = model.count;
		ghost.sensorZ = float(model.sensorZ);
		ghost.focalLength = float(projectionFocal);
		ghost.pupilRadius = float(model.surfaces[model.stop].aperture / std::abs(Transfer(model, model.stop).a));
		for (unsigned i = 0; i < model.count; ++i) {
			const auto& s = model.surfaces[i];
			auto& target = ghost.interfaces[i];
			target = { float(s.z), float(s.radius), float(s.aperture), s.stop ? 1.f : 0.f,
				float(s.ndBefore), float(s.bBefore), float(s.ndAfter), float(s.bAfter) };
			for (unsigned j = 0; j < 4; ++j) target.asphere[j] = float(s.asphere[j]);
			target.highOrder[0] = float(s.asphere[4]);
			target.dispersion = PhysicalLensDispersion::Pack(s.ndBefore, s.bBefore, s.ndAfter, s.bAfter);
			if (s.coatingTable)
				target.coatingTable = *s.coatingTable;
		}
		return ghost;
	}
}
