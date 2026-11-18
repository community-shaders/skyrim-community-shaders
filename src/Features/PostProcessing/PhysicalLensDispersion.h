#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Visible-band dispersion curvature fitted from public Sellmeier catalogues.
// These are nd/Vd-matched substitutes, not identification of proprietary glass.
// Fits use 301 uniformly spaced wavelengths over 400-700 nm.
// The prescription's nd and F-C Abbe difference are retained exactly. Materials
// without a close catalogue match retain the original two-term Cauchy model.
// SCHOTT June 2025: https://www.schott.com/en-us/products/optical-glass
// OHARA May 2026: https://www.ohara-gmbh.com/en/dialog/downloads.html
namespace PhysicalLensDispersion
{
	struct Fit
	{
		double nd, b, c, d;
	};
	inline constexpr double Qd = 1 / (.58756 * .58756);
	inline constexpr double Qf = 1 / (.48613 * .48613), Qc = 1 / (.65627 * .65627);
	inline constexpr Fit Fits[]{
		{ 1.48751, 0.00362623709523, -0.000252234529804, 1.86117505577e-05 },  // Schott FK5, prescription Vd=70.4
		{ 1.497, 0.00319333224884, -0.000138544840871, 1.08893940235e-05 },    // Ohara S-FPL51, prescription Vd=81.5
		{ 1.497, 0.00318902803922, -0.00013474185789, 1.06048307243e-05 },     // Schott N-PK52A, prescription Vd=81.61
		{ 1.49702, 0.00319189417933, -0.000138482449269, 1.08844901474e-05 },  // Ohara S-FPL51, prescription Vd=81.54
		{ 1.51679, 0.00422183638774, -0.000251574395771, 1.95026471185e-05 },  // Schott BK7, prescription Vd=64.1
		{ 1.51742, 0.00519558226575, -0.000190765486824, 1.94774411865e-05 },  // Schott KF6, prescription Vd=52.15
		{ 1.52857, 0.00359605520826, -0.000135748669795, 1.10833344948e-05 },  // Schott PK51A, prescription Vd=76.97
		{ 1.52875, 0.00359727981415, -0.000135794897845, 1.10871088298e-05 },  // Schott PK51A, prescription Vd=76.97
		{ 1.53775, 0.00376968571726, -0.000154329637862, 1.24560705208e-05 },  // Ohara S-FPM3, prescription Vd=74.7
		{ 1.54072, 0.00599895717559, -0.000189250103901, 2.33727520757e-05 },  // Ohara S-TIL2, prescription Vd=47.2
		{ 1.54814, 0.00626443254985, -0.000212599581606, 2.76696843799e-05 },  // Schott N-LLF1, prescription Vd=45.82
		{ 1.58312, 0.00514930445835, -0.000204958478212, 1.76182685175e-05 },  // Ohara S-BAL42, prescription Vd=59.3
		{ 1.58313, 0.00514072375305, -0.000218148774173, 1.84189637252e-05 },  // Ohara L-BAL42, prescription Vd=59.4
		{ 1.5927, 0.00879236175962, -0.000195026958512, 4.07865358526e-05 },   // Ohara S-FTM16, prescription Vd=35.3
		{ 1.59282, 0.00452526543331, -0.000123030782082, 1.13330072987e-05 },  // Schott N-PSK57, prescription Vd=68.6
		{ 1.59282, 0.00452394649847, -0.000122994923503, 1.1329704178e-05 },   // Schott N-PSK57, prescription Vd=68.62
		{ 1.618, 0.00510439816747, -0.00020617353685, 1.7455296209e-05 },      // Schott N-PSK53A, prescription Vd=63.4
		{ 1.62004, 0.00894454821475, -0.000117254511505, 2.94266257187e-05 },  // Ohara PBM2Y, prescription Vd=36.3
		{ 1.62588, 0.00917026429502, -0.000111423545341, 3.03852672363e-05 },  // Schott F1, prescription Vd=35.74
		{ 1.65416, 0.00864381054168, -0.000254825905917, 3.44334040124e-05 },  // Schott KZFSN5, prescription Vd=39.63
		{ 1.67276, 0.0109374168391, -9.01315046329e-05, 3.70508139923e-05 },   // Schott SF5, prescription Vd=32.21
		{ 1.68893, 0.0116000546349, -0.000182722126034, 5.19663941447e-05 },   // Ohara S-TIM28, prescription Vd=31.1
		{ 1.71735, 0.0127336941261, -7.32366537834e-05, 4.49288118075e-05 },   // Schott SF1, prescription Vd=29.5
		{ 1.71743, 0.0126835202271, -0.000184216703932, 5.77877310862e-05 },   // Schott N-SF1, prescription Vd=29.62
		{ 1.72005, 0.00863030088017, -0.000174857738756, 2.77428446069e-05 },  // Ohara S-LAM52, prescription Vd=43.69
		{ 1.72047, 0.0108725585185, -0.000187852694802, 4.00105156435e-05 },   // Schott N-KZFS8, prescription Vd=34.7
		{ 1.72341, 0.00999518011998, -0.000185842380061, 3.81234146262e-05 },  // Ohara S-BAH28, prescription Vd=37.9
		{ 1.72825, 0.0134658127531, -0.000113148467302, 5.50834654861e-05 },   // Schott SF10, prescription Vd=28.32
		{ 1.72916, 0.00699319310069, -0.000298780369647, 2.594456514e-05 },    // Ohara S-LAL18N, prescription Vd=54.6
		{ 1.72916, 0.0069842389482, -0.000294337858963, 2.56528360035e-05 },   // Ohara S-LAL18, prescription Vd=54.67
		{ 1.72916, 0.0069804084698, -0.000294176430521, 2.56387668064e-05 },   // Ohara S-LAL18, prescription Vd=54.7
		{ 1.738, 0.0119646270671, -0.000188083328382, 4.68140932807e-05 },     // Ohara S-NBH53V, prescription Vd=32.3
		{ 1.74104, 0.00737175846756, -0.00029116402727, 2.63408000183e-05 },   // Ohara S-LAL61, prescription Vd=52.64
		{ 1.75528, 0.0143403268373, -5.25183338587e-05, 5.2182857273e-05 },    // Schott SF4, prescription Vd=27.58
		{ 1.76182, 0.0150539969223, -0.000200347214793, 7.5159604611e-05 },    // Ohara S-TIH14, prescription Vd=26.5
		{ 1.76385, 0.00824729769626, -0.00020706226486, 2.49834937515e-05 },   // Ohara S-LAH96, prescription Vd=48.5
		{ 1.7725, 0.0081771488735, -0.000271379520286, 2.71628471143e-05 },    // Ohara S-LAH66N, prescription Vd=49.47
		{ 1.7725, 0.00815571683008, -0.000267661906757, 2.69302836211e-05 },   // Ohara S-LAH66, prescription Vd=49.6
		{ 1.804, 0.00903473583724, -0.000251878124648, 2.86219217153e-05 },    // Ohara S-LAH65V, prescription Vd=46.6
		{ 1.80419, 0.00905630504262, -0.000239881428488, 2.76083937511e-05 },  // Schott N-LASF44, prescription Vd=46.5
		{ 1.80518, 0.0165998662605, -0.000189206797096, 8.22650428515e-05 },   // Schott SFL6, prescription Vd=25.4
		{ 1.80809, 0.0185596683913, -0.000338809398821, 0.000117702512486 },   // Ohara S-NPH1, prescription Vd=22.8
		{ 1.83481, 0.0102377635421, -0.000229688642543, 3.16254567766e-05 },   // Ohara S-LAH55, prescription Vd=42.7
		{ 1.854, 0.0110693426824, -0.000176467322953, 3.09200683515e-05 },     // Ohara L-LAH85V, prescription Vd=40.4
		{ 1.85478, 0.0180487861023, -0.00017580113466, 8.33304103375e-05 },    // Ohara S-NBH56, prescription Vd=24.8
		{ 1.883, 0.0113330255366, -0.000162075380317, 2.88020171791e-05 },     // Ohara S-LAH58, prescription Vd=40.8
		{ 1.883, 0.0113302485149, -0.000162035665693, 2.8794959591e-05 },      // Ohara S-LAH58, prescription Vd=40.81
		{ 1.92286, 0.0231446519955, -0.000421781696887, 0.00015779913399 },    // Schott N-SF66, prescription Vd=20.88
		{ 1.92286, 0.0231225040032, -0.000421378078038, 0.000157648130034 },   // Schott N-SF66, prescription Vd=20.9
		{ 1.95375, 0.0154624160776, -0.000134232322612, 5.18458540007e-05 },   // Ohara S-LAH98, prescription Vd=32.3
		{ 2.001, 0.0180130151949, -0.000129341968597, 6.7079719981e-05 },      // Ohara S-LAH99W, prescription Vd=29.1
	};
	inline std::array<double, 2> Correction(double nd, double b)
	{
		if (b == 0)
			return {};
		auto at = std::lower_bound(std::begin(Fits), std::end(Fits), nd - 2e-6,
			[](const Fit& f, double n) { return f.nd < n; });
		for (; at != std::end(Fits) && at->nd <= nd + 2e-6; ++at)
			if (std::abs(at->b - b) <= std::max(1e-8, std::abs(b) * 1e-5))
				return { at->c, at->d };
		return {};
	}
	inline std::array<float, 4> Pack(double n1, double b1, double n2, double b2)
	{
		auto a = Correction(n1, b1), b = Correction(n2, b2);
		return { float(a[0]), float(a[1]), float(b[0]), float(b[1]) };
	}
	inline double Index(double nd, double b, double wavelength)
	{
		auto correction = Correction(nd, b);
		double q = 1 / (wavelength * wavelength);
		return nd + (q - Qd) * (b + correction[0] * (q + Qd - Qf - Qc) +
								   correction[1] * (q * q + q * Qd + Qd * Qd - Qf * Qf - Qf * Qc - Qc * Qc));
	}
}
