#pragma once

#include "PhysicalLensPresets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

// CPU reference and table generator for direct image formation. Millimetres,
// micrometres and radians throughout. It shares the prime prescriptions with
// flare, but retains A12 and double precision for the zoom's strong aspheres.
namespace PhysicalLensImaging
{
	inline constexpr unsigned MaxSurfaces = 40;
	inline constexpr double Infinity = std::numeric_limits<double>::infinity();
	struct Vec3
	{
		double x = 0, y = 0, z = 0;
		Vec3 operator+(Vec3 b) const { return { x + b.x, y + b.y, z + b.z }; }
		Vec3 operator-(Vec3 b) const { return { x - b.x, y - b.y, z - b.z }; }
		Vec3 operator*(double b) const { return { x * b, y * b, z * b }; }
		double Dot(Vec3 b) const { return x * b.x + y * b.y + z * b.z; }
		Vec3 Normalized() const { return *this * (1 / std::sqrt(Dot(*this))); }
	};
	struct Surface
	{
		double z = 0, radius = 0, aperture = 0;
		double ndBefore = 1, bBefore = 0, ndAfter = 1, bAfter = 0;
		std::array<double, 5> asphere{};  // A4 through A12; spherical base, K = 0.
		bool stop = false;
		const PhysicalLensCoating::Table* coatingTable = nullptr;
	};
	struct Group
	{
		unsigned first = 0, last = 0;
	};
	enum class FocusMechanism
	{
		RigidApproximation,
		InternalGroup,
		LinkedGroups,
		CompensatedGroups
	};
	struct Model
	{
		std::array<Surface, MaxSurfaces> surfaces{};
		unsigned count = 0, stop = 0;
		double sensorZ = 0, focalLength = 0, minimumFNumber = 1, fNumber = 5.6;
		double gateWidth = 36, gateHeight = 24;
		FocusMechanism focusMechanism = FocusMechanism::RigidApproximation;
		std::array<Group, 2> focusGroups{};
		double focusGroupRatio = 1;
	};
	struct Matrix
	{
		double a = 1, b = 0, c = 0, d = 1;
		void Translate(double distance)
		{
			a += distance * c;
			b += distance * d;
		}
	};
	inline double Index(double nd, double dispersion, double wavelength)
	{
		return PhysicalLensDispersion::Index(nd, dispersion, wavelength);
	}
	/// Transfer [height, slope] from the first vertex plane through an interface.
	inline Matrix Transfer(const Model& lens, unsigned lastSurface, double wavelength = .58756)
	{
		Matrix m;
		double z = 0;
		for (unsigned i = 0; i <= lastSurface; ++i) {
			const auto& s = lens.surfaces[i];
			m.Translate(s.z - z);
			z = s.z;
			if (s.stop)
				continue;
			const double n1 = Index(s.ndBefore, s.bBefore, wavelength), n2 = Index(s.ndAfter, s.bAfter, wavelength);
			const double power = s.radius == 0 ? 0 : (n1 - n2) / s.radius;
			m.c = (n1 * m.c + power * m.a) / n2;
			m.d = (n1 * m.d + power * m.b) / n2;
		}
		return m;
	}
	inline Matrix SensorTransfer(const Model& lens, double wavelength = .58756)
	{
		auto m = Transfer(lens, lens.count - 1, wavelength);
		m.Translate(lens.sensorZ - lens.surfaces[lens.count - 1].z);
		return m;
	}
	inline void SetAperture(Model& lens, double fNumber)
	{
		lens.focalLength = -1 / Transfer(lens, lens.count - 1).c;
		lens.fNumber = std::max(fNumber, lens.minimumFNumber);
		lens.surfaces[lens.stop].aperture = std::abs(Transfer(lens, lens.stop).a) * lens.focalLength / (2 * lens.fNumber);
	}
	inline Model Prime(int preset, double fNumber)
	{
		const auto& p = PhysicalLensOptics::GetPrescription(preset);
		Model m;
		m.count = p.count;
		m.minimumFNumber = p.minimumFNumber;
		m.gateWidth = p.gateWidth;
		m.gateHeight = p.gateHeight;
		double z = 0, nd = 1, b = 0;
		for (unsigned i = 0; i < p.count; ++i) {
			const auto& source = p.surfaces[i];
			auto& s = m.surfaces[i];
			s.z = z;
			s.radius = source.radius;
			s.aperture = source.semiDiameter;
			s.stop = source.nd == 0;
			s.ndBefore = nd;
			s.bBefore = b;
			if (s.stop)
				m.stop = i;
			else {
				nd = source.nd;
				b = source.dispersion;
			}
			s.ndAfter = nd;
			s.bAfter = b;
			if (!s.stop && std::min(s.ndBefore, s.ndAfter) < 1.001)
				s.coatingTable = &PhysicalLensCoating::Get(std::max(s.ndBefore, s.ndAfter), s.ndBefore > s.ndAfter ? s.bBefore : s.bAfter, unsigned(preset) * 131 + i);
			std::copy(source.asphere.begin(), source.asphere.end(), s.asphere.begin());
			z += source.thickness;
		}
		const auto transfer = Transfer(m, m.count - 1);
		m.sensorZ = m.surfaces[m.count - 1].z - transfer.a / transfer.c;
		if (preset == 0) {
			// DE102005025204A1 pp. 4-5: surfaces 12..14 and 15..25,
			// excluding the patent's initial dummy plane. The cam is reconstructed
			// by imposing focus and preserving the marginal spherical correction; it is
			// not a measurement of the production Master Prime focus mechanism.
			m.focusMechanism = FocusMechanism::CompensatedGroups;
			m.focusGroups = { Group{ 10, 12 }, Group{ 13, 23 } };
		} else if (preset == 1) {
			// JP2015114366A example 1: G2A and G2B (including the iris).
			// Published INF/400 mm gaps: d9 9.9796/1.2000, d14 6.0186/5.8989.
			// Linear linkage between these two measured design positions; solve
			// the displacement for focus rather than interpolating focus distance.
			m.focusMechanism = FocusMechanism::LinkedGroups;
			m.focusGroups = { Group{ 8, 12 }, Group{ 13, 21 } };
			m.focusGroupRatio = (8.7796 + .1197) / 8.7796;
		} else if (preset == 2) {
			// US20200012073A1 example 1: group 2, surfaces 4..17.
			m.focusMechanism = FocusMechanism::InternalGroup;
			m.focusGroups[0] = { 3, 16 };
		} else if (preset == 4) {
			// WO2017138250A1 example 2, Table 9: variable D11/D15.
			m.focusMechanism = FocusMechanism::InternalGroup;
			m.focusGroups[0] = { 11, 14 };
		}
		SetAperture(m, fNumber);
		return m;
	}
	/// Reflect the axial coordinate about the sensor so the same forward tracer
	/// can follow rays from the film through the lens into object space. x/y stay
	/// fixed; signed sag coefficients, curvature and refractive media reverse.
	inline Model Reverse(const Model& lens)
	{
		Model result = lens;
		result.stop = lens.count - 1 - lens.stop;
		result.sensorZ = lens.sensorZ + 1;
		for (unsigned i = 0; i < lens.count; ++i) {
			auto s = lens.surfaces[lens.count - 1 - i];
			s.z = lens.sensorZ - s.z;
			s.radius = -s.radius;
			for (auto& coefficient : s.asphere) coefficient = -coefficient;
			std::swap(s.ndBefore, s.ndAfter);
			std::swap(s.bBefore, s.bAfter);
			result.surfaces[i] = s;
		}
		return result;
	}
	inline void MoveGroup(Model& lens, Group group, double displacement)
	{
		for (unsigned i = group.first; i <= group.last; ++i) lens.surfaces[i].z += displacement;
	}
	inline bool Ordered(const Model& lens)
	{
		for (unsigned i = 1; i < lens.count; ++i)
			if (lens.surfaces[i].z - lens.surfaces[i - 1].z <= .01)
				return false;
		return lens.sensorZ > lens.surfaces[lens.count - 1].z + .01;
	}
	/// Zero means paraxial focus at an object distance measured from the sensor.
	inline double FocusError(const Model& lens, double distanceMM)
	{
		const auto m = SensorTransfer(lens);
		return m.a + m.b / (distanceMM - lens.sensorZ);
	}
	/// Image-side working f-number, including finite-conjugate exposure loss.
	inline double WorkingFNumber(const Model& lens, double distanceMM)
	{
		const auto pupil = Transfer(lens, lens.stop);
		const auto rear = Transfer(lens, lens.count - 1);
		double inverseDistance = 1 / (distanceMM - lens.sensorZ);
		double radius = lens.surfaces[lens.stop].aperture / std::abs(pupil.a + pupil.b * inverseDistance);
		return 1 / (2 * radius * std::abs(rear.c + rear.d * inverseDistance));
	}

	/// Sag and radial derivative divided by height, including every A12 term.
	inline bool Sag(const Surface& s, double radiusSquared, double& sag, double& gradient)
	{
		double curvature = s.radius == 0 ? 0 : 1 / s.radius;
		double q = 1 - radiusSquared * curvature * curvature;
		if (q <= 0)
			return false;
		double root = std::sqrt(q);
		sag = radiusSquared * curvature / (1 + root);
		gradient = curvature / root;
		double power = radiusSquared;
		for (unsigned i = 0; i < s.asphere.size(); ++i) {
			gradient += (4 + 2 * i) * s.asphere[i] * power;
			power *= radiusSquared;
			sag += s.asphere[i] * power;
		}
		return true;
	}
	inline bool Intersect(const Surface& s, Vec3& origin, Vec3 direction, Vec3& normal)
	{
		if (direction.z <= 1e-9)
			return false;
		double t = (s.z - origin.z) / direction.z;
		for (unsigned i = 0; i < 16; ++i) {
			const Vec3 p = origin + direction * t;
			double sag, gradient;
			if (!Sag(s, p.x * p.x + p.y * p.y, sag, gradient))
				return false;
			double residual = p.z - s.z - sag;
			if (std::abs(residual) < 1e-9) {
				if (t < -1e-8 || !std::isfinite(t))
					return false;
				origin = p;
				normal = Vec3{ gradient * p.x, gradient * p.y, -1 }.Normalized();
				return true;
			}
			double derivative = direction.z - gradient * (p.x * direction.x + p.y * direction.y);
			if (std::abs(derivative) < 1e-10)
				return false;
			t -= residual / derivative;
		}
		return false;
	}
	inline double Reflectance(double n1, double n2, double ci, double ct, double wavelength, double coating, const PhysicalLensCoating::Table* table = nullptr)
	{
		const double rs = (n1 * ci - n2 * ct) / (n1 * ci + n2 * ct);
		const double rp = (n2 * ci - n1 * ct) / (n2 * ci + n1 * ct);
		const double bare = .5 * (rs * rs + rp * rp);
		if (coating == 0 || std::min(n1, n2) > 1.001)
			return bare;
		if (table)
			return std::lerp(bare, PhysicalLensCoating::Sample(*table, n1 < n2 ? ci : ct, wavelength), std::clamp(coating, 0., 1.));
		const double nf = 1.38, sin2 = n1 * n1 / (nf * nf) * (1 - ci * ci);
		if (sin2 >= 1)
			return bare;
		const double cf = std::sqrt(1 - sin2), phase = std::cos(std::numbers::pi * .55 * cf / wavelength);
		const std::array r01{ (n1 * ci - nf * cf) / (n1 * ci + nf * cf), (nf * ci - n1 * cf) / (nf * ci + n1 * cf) };
		const std::array r12{ (nf * cf - n2 * ct) / (nf * cf + n2 * ct), (n2 * cf - nf * ct) / (n2 * cf + nf * ct) };
		double film = 0;
		for (unsigned i = 0; i < 2; ++i) {
			double product = r01[i] * r12[i];
			film += .5 * (r01[i] * r01[i] + r12[i] * r12[i] + 2 * product * phase) / (1 + product * product + 2 * product * phase);
		}
		return std::lerp(bare, film, std::clamp(coating, 0.0, 1.0));
	}
	struct RayHit
	{
		Vec3 sensor, direction;
		double transmission = 0;
		double opticalPath = 0;         // Optical path from the z=0 pupil plane, in mm.
		double apertureClearance = -1;  // Normalized signed distance for pupil-cell integration.
	};
	/// Exact direct-path Snell trace. Pupil coordinates are on z=0; fieldSlope
	/// points from the object toward the lens. Distance is from sensor to object.
	inline RayHit Trace(const Model& lens, double pupilX, double pupilY, double fieldSlopeX, double fieldSlopeY,
		double objectDistanceMM = Infinity, double wavelength = .58756, unsigned blades = 0, double rotation = 0, double coating = 1, double bladeCurvature = 0, double apertureMargin = 0)
	{
		double inverseDistance = 1 / (objectDistanceMM - lens.sensorZ);
		Vec3 direction = Vec3{ fieldSlopeX + pupilX * inverseDistance, fieldSlopeY + pupilY * inverseDistance, 1 }.Normalized();
		Vec3 origin = Vec3{ pupilX, pupilY, 0 } - direction * 100;
		double transmission = 1;
		double clearance = 1;
		double opticalPath = -100 * Index(lens.surfaces[0].ndBefore, lens.surfaces[0].bBefore, wavelength);
		for (unsigned i = 0; i < lens.count; ++i) {
			const auto& s = lens.surfaces[i];
			Vec3 normal;
			const Vec3 previous = origin;
			if (!Intersect(s, origin, direction, normal))
				return {};
			const Vec3 segment = origin - previous;
			opticalPath += std::sqrt(segment.Dot(segment)) * Index(s.ndBefore, s.bBefore, wavelength);
			double radius = std::hypot(origin.x, origin.y);
			double aperture = s.aperture;
			if (s.stop && blades >= 3) {
				double sector = 2 * std::numbers::pi / blades;
				double delta = std::remainder(std::atan2(origin.y, origin.x) - rotation, sector);
				double apothem = std::cos(std::numbers::pi / blades), sag = (1 - apothem) * bladeCurvature;
				double edgeLength = 2 * std::sin(std::numbers::pi / blades);
				double localX = radius * std::cos(delta) / aperture;
				double localY = radius * std::sin(delta) / aperture;
				clearance = std::min(clearance, apothem + sag - localX - 4 * sag * localY * localY / (edgeLength * edgeLength));
			}
			clearance = std::min(clearance, 1 - radius / aperture);
			if (clearance < -apertureMargin)
				return {};
			if (s.stop)
				continue;
			double n1 = Index(s.ndBefore, s.bBefore, wavelength), n2 = Index(s.ndAfter, s.bAfter, wavelength);
			double ci = -normal.Dot(direction), eta = n1 / n2;
			double k = 1 - eta * eta * (1 - ci * ci);
			if (k < 0 || ci < 0)
				return {};
			double ct = std::sqrt(k);
			transmission *= 1 - Reflectance(n1, n2, ci, ct, wavelength, coating, s.coatingTable);
			direction = (direction * eta + normal * (eta * ci - ct)).Normalized();
		}
		if (direction.z <= 0 || lens.sensorZ <= origin.z)
			return {};
		double distance = (lens.sensorZ - origin.z) / direction.z;
		const auto& lastSurface = lens.surfaces[lens.count - 1];
		opticalPath += distance * Index(lastSurface.ndAfter, lastSurface.bAfter, wavelength);
		return { origin + direction * distance, direction, transmission, opticalPath, clearance };
	}

	/// Solve physical focus with a fixed sensor for internal focus mechanisms.
	inline bool Focus(const Model& infinityModel, double distanceMM, Model& result)
	{
		if (!(distanceMM > infinityModel.sensorZ + infinityModel.focalLength))
			return false;
		Model current = infinityModel;
		// The patent publishes floating groups, not the production cam. Keep the
		// original marginal spherical correction while solving paraxial focus.
		// A constant-EFL constraint instead makes this lens strongly aberrated
		// at close focus. Calibrate at full aperture so stopping down never
		// changes mechanical group positions; normal focus breathing remains.
		const auto sphericalError = [](const Model& model, double distance) {
			auto open = model;
			open.surfaces[open.stop].aperture *= open.fNumber / open.minimumFNumber;
			const auto pupil = Transfer(open, open.stop);
			double radius = open.surfaces[open.stop].aperture / std::abs(pupil.a + pupil.b / (distance - open.sensorZ));
			const auto hit = Trace(open, .7 * radius, 0, 0, 0, distance);
			return hit.transmission > 0 ? hit.sensor.x / radius : Infinity;
		};
		const double targetCorrection = current.focusMechanism == FocusMechanism::CompensatedGroups ? sphericalError(infinityModel, Infinity) : 0;
		const bool two = current.focusMechanism == FocusMechanism::CompensatedGroups;
		const auto move = [](Model& model, unsigned group, double delta) {
			if (model.focusMechanism == FocusMechanism::RigidApproximation)
				model.sensorZ += delta;
			else
				MoveGroup(model, model.focusGroups[group], delta);
			if (model.focusMechanism == FocusMechanism::LinkedGroups)
				MoveGroup(model, model.focusGroups[1], delta * model.focusGroupRatio);
		};
		for (unsigned iteration = 0; iteration < 32; ++iteration) {
			double error = FocusError(current, distanceMM);
			double correctionError = two ? (sphericalError(current, distanceMM) - targetCorrection) : 0;
			if (std::abs(error) + std::abs(correctionError) < 1e-10) {
				// Focusing moves the iris with its group, but does not resize it.
				// Recalibrating it to the nominal f-number here would erase real
				// changes in pupil magnification and near-focus exposure loss.
				current.focalLength = -1 / Transfer(current, current.count - 1).c;
				result = current;
				return true;
			}
			constexpr double step = .0001;
			Model x = current;
			move(x, 0, step);
			double a = (FocusError(x, distanceMM) - error) / step;
			double c = two ? (sphericalError(x, distanceMM) - sphericalError(current, distanceMM)) / step : 0;
			double dx = 0, dy = 0;
			if (two) {
				Model y = current;
				move(y, 1, step);
				double b = (FocusError(y, distanceMM) - error) / step;
				double d = (sphericalError(y, distanceMM) - sphericalError(current, distanceMM)) / step;
				double det = a * d - b * c;
				if (std::abs(det) < 1e-14)
					return false;
				dx = (-error * d + b * correctionError) / det;
				dy = (-a * correctionError + error * c) / det;
			} else {
				if (std::abs(a) < 1e-14)
					return false;
				dx = -error / a;
			}
			bool accepted = false;
			for (double damping = 1; damping >= 1.0 / 1024; damping *= .5) {
				Model candidate = current;
				move(candidate, 0, damping * dx);
				if (two)
					move(candidate, 1, damping * dy);
				if (!Ordered(candidate))
					continue;
				double merit = std::abs(FocusError(candidate, distanceMM));
				if (two)
					merit += std::abs((sphericalError(candidate, distanceMM) - targetCorrection));
				if (merit < std::abs(error) + std::abs(correctionError)) {
					current = candidate;
					accepted = true;
					break;
				}
			}
			if (!accepted)
				return false;
		}
		return false;
	}
}
