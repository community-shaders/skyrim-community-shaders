// from ShortFuse
namespace Tonemap
{
	namespace ACES
	{

		// color conversion matrices
		static const float3x3 BT709_TO_XYZ_MAT = float3x3(
			0.4123907993f, 0.3575843394f, 0.1804807884f,
			0.2126390059f, 0.7151686788f, 0.0721923154f,
			0.0193308187f, 0.1191947798f, 0.9505321522f);

		static const float3x3 XYZ_TO_BT709_MAT = float3x3(
			3.2409699419f, -1.5373831776f, -0.4986107603f,
			-0.9692436363f, 1.8759675015f, 0.0415550574f,
			0.0556300797f, -0.2039769589f, 1.0569715142f);

		static const float3x3 BT2020_TO_XYZ_MAT = float3x3(
			0.6369580483f, 0.1446169036f, 0.1688809752f,
			0.2627002120f, 0.6779980715f, 0.0593017165f,
			0.0000000000f, 0.0280726930f, 1.0609850577f);

		static const float3x3 XYZ_TO_BT2020_MAT = float3x3(
			1.7166511880f, -0.3556707838f, -0.2533662814f,
			-0.6666843518f, 1.6164812366f, 0.0157685458f,
			0.0176398574f, -0.0427706133f, 0.9421031212f);

		static const float3x3 AP0_TO_XYZ_MAT = float3x3(
			0.9525523959f, 0.0000000000f, 0.0000936786f,
			0.3439664498f, 0.7281660966f, -0.0721325464f,
			0.0000000000f, 0.0000000000f, 1.0088251844f);

		static const float3x3 XYZ_TO_AP0_MAT = float3x3(
			1.0498110175f, 0.0000000000f, -0.0000974845f,
			-0.4959030231f, 1.3733130458f, 0.0982400361f,
			0.0000000000f, 0.0000000000f, 0.9912520182f);

		static const float3x3 AP1_TO_XYZ_MAT = float3x3(
			0.6624541811f, 0.1340042065f, 0.1561876870f,
			0.2722287168f, 0.6740817658f, 0.0536895174f,
			-0.0055746495f, 0.0040607335f, 1.0103391003f);

		static const float3x3 XYZ_TO_AP1_MAT = float3x3(
			1.6410233797f, -0.3248032942f, -0.2364246952f,
			-0.6636628587f, 1.6153315917f, 0.0167563477f,
			0.0117218943f, -0.0082844420f, 0.9883948585f);

		// With Bradford
		static const float3x3 AP1_TO_BT709_MAT = float3x3(
			1.7050509927, -0.6217921207, -0.0832588720,
			-0.1302564175, 1.1408047366, -0.0105483191,
			-0.0240033568, -0.1289689761, 1.1529723329);

		// With Bradford
		static const float3x3 AP1_TO_BT2020_MAT = float3x3(
			1.0258247477f, -0.0200531908f, -0.0057715568f,
			-0.0022343695f, 1.0045865019f, -0.0023521324f,
			-0.0050133515f, -0.0252900718f, 1.0303034233f);

		static const float3x3 BT709_TO_AP1_MAT = float3x3(
			0.6130974024, 0.3395231462, 0.0473794514,
			0.0701937225, 0.9163538791, 0.0134523985,
			0.0206155929, 0.1095697729, 0.8698146342);

		// With Bradford
		static const float3x3 BT2020_TO_AP1_MAT = float3x3(
			0.9748949779f, 0.0195991086f, 0.0055059134f,
			0.0021795628f, 0.9955354689f, 0.0022849683f,
			0.0047972397f, 0.0245320166f, 0.9706707437f);

		// chromatic adaptation method: von Kries
		// chromatic adaptation transform: Bradford
		static const float3x3 D65_TO_D60_CAT = float3x3(
			1.01303493f, 0.00610525766f, -0.0149709433f,
			0.00769822997f, 0.998163342f, -0.00503203831f,
			-0.00284131732f, 0.00468515651f, 0.924506127f);

		// chromatic adaptation method: von Kries
		// chromatic adaptation transform: Bradford
		static const float3x3 D60_TO_D65_MAT = float3x3(
			0.987223982f, -0.00611322838f, 0.0159532874f,
			-0.00759837171f, 1.00186145f, 0.00533003592f,
			0.00307257706f, -0.00509596150f, 1.08168065f);

		static const float3x3 AP1_TO_BT709D60_MAT = mul(XYZ_TO_BT709_MAT, AP1_TO_XYZ_MAT);
		static const float3x3 AP1_TO_BT2020D60_MAT = mul(XYZ_TO_BT2020_MAT, AP1_TO_XYZ_MAT);
		static const float3x3 AP1_TO_AP1D65_MAT = mul(XYZ_TO_AP1_MAT, mul(D60_TO_D65_MAT, AP1_TO_XYZ_MAT));

		static const float3x3 BT709_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));

		static const float3x3 AP0_TO_AP1_MAT = mul(XYZ_TO_AP1_MAT, AP0_TO_XYZ_MAT);
		static const float3x3 AP1_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, AP1_TO_XYZ_MAT);

		static const float3x3 RRT_SAT_MAT = float3x3(
			0.9708890, 0.0269633, 0.00214758,
			0.0108892, 0.9869630, 0.00214758,
			0.0108892, 0.0269633, 0.96214800);

		static const float3x3 ODT_SAT_MAT = float3x3(
			0.949056, 0.0471857, 0.00375827,
			0.019056, 0.9771860, 0.00375827,
			0.019056, 0.0471857, 0.93375800);

		static const float3x3 M = float3x3(
			0.5, -1.0, 0.5,
			-1.0, 1.0, 0.0,
			0.5, 0.5, 0.0);

		float Rgb2Yc(float3 rgb)
		{
			const float yc_radius_weight = 1.75;
			// Converts RGB to a luminance proxy, here called YC
			// YC is ~ Y + K * Chroma
			// Constant YC is a cone-shaped surface in RGB space, with the tip on the
			// neutral axis, towards white.
			// YC is normalized: RGB 1 1 1 maps to YC = 1
			//
			// ycRadiusWeight defaults to 1.75, although can be overridden in function
			// call to rgb_2_yc
			// ycRadiusWeight = 1 -> YC for pure cyan, magenta, yellow == YC for neutral
			// of same value
			// ycRadiusWeight = 2 -> YC for pure red, green, blue  == YC for  neutral of
			// same value.

			float r = rgb[0];
			float g = rgb[1];
			float b = rgb[2];

			float chroma = sqrt(b * (b - g) + g * (g - r) + r * (r - b));

			return (b + g + r + yc_radius_weight * chroma) / 3.;
		}

		float Rgb2Saturation(float3 rgb)
		{
			float minrgb = min(min(rgb.r, rgb.g), rgb.b);
			float maxrgb = max(max(rgb.r, rgb.g), rgb.b);
			return (max(maxrgb, 1e-10) - max(minrgb, 1e-10)) / max(maxrgb, 1e-2);
		}

		// Sigmoid function in the range 0 to 1 spanning -2 to +2.
		float SigmoidShaper(float x)
		{
			float t = max(1 - abs(0.5 * x), 0);
			float y = 1 + sign(x) * (1 - t * t);
			return 0.5 * y;
		}

		float GlowFwd(float yc_in, float glow_gain_in, float glow_mid)
		{
			float glow_gain_out;

			if (yc_in <= 2. / 3. * glow_mid) {
				glow_gain_out = glow_gain_in;
			} else if (yc_in >= 2 * glow_mid) {
				glow_gain_out = 0;
			} else {
				glow_gain_out = glow_gain_in * (glow_mid / yc_in - 0.5);
			}

			return glow_gain_out;
		}

		// Transformations from RGB to other color representations
		float Rgb2Hue(float3 rgb)
		{
			const float aces_pi = 3.14159265359f;
			// Returns a geometric hue angle in degrees (0-360) based on RGB values.
			// For neutral colors, hue is undefined and the function will return a quiet NaN value.
			float hue;
			if (rgb.r == rgb.g && rgb.g == rgb.b) {
				hue = 0.0;  // RGB triplets where RGB are equal have an undefined hue
			} else {
				hue = (180.0f / aces_pi) * atan2(sqrt(3.0f) * (rgb.g - rgb.b), 2.0f * rgb.r - rgb.g - rgb.b);
			}

			if (hue < 0.0f) {
				hue = hue + 360.0f;
			}

			return clamp(hue, 0, 360.f);
		}

		float CenterHue(float hue, float center_h)
		{
			float hue_centered = hue - center_h;
			if (hue_centered < -180.) {
				hue_centered += 360;
			} else if (hue_centered > 180.) {
				hue_centered -= 360;
			}
			return hue_centered;
		}

		float3 YToLinCV(float3 y, float y_max, float y_min)
		{
			return (y - y_min) / (y_max - y_min);
		}

		// Transformations between CIE XYZ tristimulus values and CIE x,y
		// chromaticity coordinates
		float3 XYZToXyY(float3 xyz)
		{
			float3 xy_y;
			float divisor = (xyz[0] + xyz[1] + xyz[2]);
			if (divisor == 0.f)
				divisor = 1e-10f;
			xy_y[0] = xyz[0] / divisor;
			xy_y[1] = xyz[1] / divisor;
			xy_y[2] = xyz[1];

			return xy_y;
		}

		float3 XyYToXYZ(float3 xy_y)
		{
			float3 xyz;
			xyz[0] = xy_y[0] * xy_y[2] / max(xy_y[1], 1e-10);
			xyz[1] = xy_y[2];
			xyz[2] = (1.0 - xy_y[0] - xy_y[1]) * xy_y[2] / max(xy_y[1], 1e-10);

			return xyz;
		}

		static const float DIM_SURROUND_GAMMA = 0.9811;

		float3 DarkToDim(float3 xyz, float dim_surround_gamma = DIM_SURROUND_GAMMA)
		{
			float3 xy_y = XYZToXyY(xyz);
			xy_y.z = clamp(xy_y.z, 0.0, 65504.0f);
			xy_y.z = pow(xy_y.z, DIM_SURROUND_GAMMA);
			return XyYToXYZ(xy_y);
		}

		static const float MIN_STOP_SDR = -6.5;
		static const float MAX_STOP_SDR = 6.5;

		static const float MIN_STOP_RRT = -15.0;
		static const float MAX_STOP_RRT = 18.0;

		static const float MIN_LUM_SDR = 0.02;
		static const float MAX_LUM_SDR = 48.0;

		static const float MIN_LUM_RRT = 0.0001;
		static const float MAX_LUM_RRT = 10000.0;

		static const float2x2 MIN_LUM_TABLE = float2x2(
			log10(MIN_LUM_RRT), MIN_STOP_RRT,
			log10(MIN_LUM_SDR), MIN_STOP_SDR);

		static const float2x2 MAX_LUM_TABLE = float2x2(
			log10(MAX_LUM_SDR), MAX_STOP_SDR,
			log10(MAX_LUM_RRT), MAX_STOP_RRT);

		float Interpolate1D(float2x2 table, float p)
		{
			if (p < table[0].x) {
				return table[0].y;
			} else if (p >= table[1].x) {
				return table[1].y;
			} else {
				// p = clamp(p, table[0].x, table[1].x);
				float s = (p - table[0].x) / (table[1].x - table[0].x);
				return table[0].y * (1 - s) + table[1].y * s;
			}
		}

		float3 LinCv2Y(float3 lin_cv, float y_max, float y_min)
		{
			return lin_cv * (y_max - y_min) + y_min;
		}

		float LookUpAcesMin(float min_lum_log10)
		{
			return 0.18 * exp2(Interpolate1D(MIN_LUM_TABLE, min_lum_log10));
		}

		float LookUpAcesMax(float max_lum_log10)
		{
			return 0.18 * exp2(Interpolate1D(MAX_LUM_TABLE, max_lum_log10));
		}

		float SSTS(
			float x,
			float3 y_min, float3 y_mid, float3 y_max,
			float3 coefs_low_a, float3 coefs_low_b,
			float3 coefs_high_a, float3 coefs_high_b)
		{
			static const uint N_KNOTS_LOW = 4;
			static const uint N_KNOTS_HIGH = 4;

			float coefs_low[6];

			coefs_low[0] = coefs_low_a.x;
			coefs_low[1] = coefs_low_a.y;
			coefs_low[2] = coefs_low_a.z;
			coefs_low[3] = coefs_low_b.x;
			coefs_low[4] = coefs_low_b.y;
			coefs_low[5] = coefs_low_b.z;
			float coefs_high[6];
			coefs_high[0] = coefs_high_a.x;
			coefs_high[1] = coefs_high_a.y;
			coefs_high[2] = coefs_high_a.z;
			coefs_high[3] = coefs_high_b.x;
			coefs_high[4] = coefs_high_b.y;
			coefs_high[5] = coefs_high_b.z;

			// Check for negatives or zero before taking the log. If negative or zero,
			// set to HALF_MIN.
			float log_x = log10(max(x, asfloat(0x00800000)));

			float log_y;

			if (log_x > y_max.x) {
				// Above max breakpoint (overshoot)
				// If MAX_PT slope is 0, this is just a straight line and always returns
				// maxLum
				// y = mx+b
				// log_y = computeGraphY(C.Max.z, log_x, (C.Max.y) - (C.Max.z * (C.Max.x)));
				log_y = y_max.y;
			} else if (log_x >= y_mid.x) {
				// Part of Midtones area (Must have slope)
				float knot_coord = (N_KNOTS_HIGH - 1) * (log_x - y_mid.x) / (y_max.x - y_mid.x);
				uint j = knot_coord;
				float t = knot_coord - j;

				float3 cf = float3(coefs_high[j], coefs_high[j + 1], coefs_high[j + 2]);

				float3 monomials = float3(t * t, t, 1.0);
				log_y = dot(monomials, mul(M, cf));
			} else if (log_x > y_min.x) {
				float knot_coord = (N_KNOTS_LOW - 1) * (log_x - y_min.x) / (y_mid.x - y_min.x);
				uint j = knot_coord;
				float t = knot_coord - j;

				float3 cf = float3(coefs_low[j], coefs_low[j + 1], coefs_low[j + 2]);

				float3 monomials = float3(t * t, t, 1.0);
				log_y = dot(monomials, mul(M, cf));
			} else {  //(log_x <= (C.Min.x))
				// Below min breakpoint (undershoot)
				// log_y = computeGraphY(C.Min.z, log_x, ((C.Min.y) - C.Min.z * (C.Min.x)));
				log_y = y_min.y;
			}

			return pow(10.0, log_y);
		}

		static const float LIM_CYAN = 1.147f;
		static const float LIM_MAGENTA = 1.264f;
		static const float LIM_YELLOW = 1.312f;
		static const float THR_CYAN = 0.815f;
		static const float THR_MAGENTA = 0.803f;
		static const float THR_YELLOW = 0.880f;
		static const float PWR = 1.2f;

		float GamutCompressChannel(float dist, float lim, float thr, float pwr)
		{
			float compr_dist;
			float scl;
			float nd;
			float p;

			if (dist < thr) {
				compr_dist = dist;  // No compression below threshold
			} else {
				// Calculate scale factor for y = 1 intersect
				scl = (lim - thr) / pow(pow((1.0 - thr) / (lim - thr), -pwr) - 1.0, 1.0 / pwr);

				// Normalize distance outside threshold by scale factor
				nd = (dist - thr) / scl;
				p = pow(nd, pwr);

				compr_dist = thr + scl * nd / (pow(1.0 + p, 1.0 / pwr));  // Compress
			}

			return compr_dist;
		}

		float3 GamutCompress(float3 lin_ap1)
		{
			// Achromatic axis
			float ach = max(lin_ap1.r, max(lin_ap1.g, lin_ap1.b));
			float abs_ach = abs(ach);
			// Distance from the achromatic axis for each color component aka inverse RGB ratios
			float3 dist = ach ? (ach - lin_ap1) / abs_ach : 0;

			// Compress distance with parameterized shaper function
			float3 compr_dist = float3(
				GamutCompressChannel(dist.r, LIM_CYAN, THR_CYAN, PWR),
				GamutCompressChannel(dist.g, LIM_MAGENTA, THR_MAGENTA, PWR),
				GamutCompressChannel(dist.b, LIM_YELLOW, THR_YELLOW, PWR));

			// Recalculate RGB from compressed distance and achromatic
			float3 compr_lin_ap1 = ach - compr_dist * abs_ach;

			return compr_lin_ap1;
		}

		float3 RRT(float3 aces)
		{
			static const float3 AP1_RGB2Y = AP1_TO_XYZ_MAT[1].rgb;

			// --- Glow module --- //
			// "Glow" module constants
			static const float RRT_GLOW_GAIN = 0.05;
			static const float RRT_GLOW_MID = 0.08;
			float saturation = Rgb2Saturation(aces);
			float yc_in = Rgb2Yc(aces);
			const float s = SigmoidShaper((saturation - 0.4) / 0.2);
			float added_glow = 1.0 + GlowFwd(yc_in, RRT_GLOW_GAIN * s, RRT_GLOW_MID);
			aces *= added_glow;

			// --- Red modifier --- //
			// Red modifier constants
			static const float RRT_RED_SCALE = 0.82;
			static const float RRT_RED_PIVOT = 0.03;
			static const float RRT_RED_HUE = 0.;
			static const float RRT_RED_WIDTH = 135.;
			float hue = Rgb2Hue(aces);
			const float centered_hue = CenterHue(hue, RRT_RED_HUE);
			float hue_weight;
			{
				// hueWeight = cubic_basis_shaper(centeredHue, RRT_RED_WIDTH);
				hue_weight = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centered_hue / RRT_RED_WIDTH));
				hue_weight *= hue_weight;
			}

			aces.r += hue_weight * saturation * (RRT_RED_PIVOT - aces.r) * (1. - RRT_RED_SCALE);

			// --- ACES to RGB rendering space --- //
			aces = clamp(aces, 0, 65535.0f);
			float3 rgb_pre = mul(AP0_TO_AP1_MAT, aces);
			rgb_pre = clamp(rgb_pre, 0, 65504.0f);

			// --- Global desaturation --- //
			// rgbPre = mul( RRT_SAT_MAT, rgbPre);
			static const float RRT_SAT_FACTOR = 0.96f;
			rgb_pre = lerp(dot(rgb_pre, AP1_RGB2Y).xxx, rgb_pre, RRT_SAT_FACTOR);

			return rgb_pre;
		}

		float3 ODTToneMap(float3 rgb_pre, float min_y, float max_y)
		{
			const float min_lum = min_y;
			const float max_lum = max_y;
			// Aces-dev has more expensive version
			// AcesParams PARAMS = init_aces_params(minY, maxY);

			static const float2x2 BENDS_LOW_TABLE = float2x2(
				MIN_STOP_RRT, 0.18, MIN_STOP_SDR, 0.35);

			static const float2x2 BENDS_HIGH_TABLE = float2x2(
				MAX_STOP_SDR, 0.89, MAX_STOP_RRT, 0.90);

			float min_lum_log10 = log10(min_lum);
			float max_lum_log10 = log10(max_lum);
			const float aces_min = LookUpAcesMin(min_lum_log10);
			const float aces_max = LookUpAcesMax(max_lum_log10);
			// float3 MIN_PT = float3(lookup_ACESmin(minLum), minLum, 0.0);
			static const float3 MID_PT = float3(0.18, 4.8, 1.55);
			// float3 MAX_PT = float3(lookup_ACESmax(maxLum), maxLum, 0.0);
			// float coefs_low[5];
			// float coefs_high[5];
			float3 coefs_low_a;
			float3 coefs_low_b;
			float3 coefs_high_a;
			float3 coefs_high_b;

			float2 log_min = float2(log10(aces_min), min_lum_log10);
			static const float2 LOG_MID = float2(log10(MID_PT.xy));
			float2 log_max = float2(log10(aces_max), max_lum_log10);

			float knot_inc_low = (LOG_MID.x - log_min.x) / 3.;
			// float halfKnotInc = (logMid.x - log_min.x) / 6.;

			// Determine two lowest coefficients (straddling minPt)
			// coefs_low[0] = (MIN_PT.z * (log_min.x- 0.5 * knot_inc_low)) + ( log_min.y - MIN_PT.z * log_min.x);
			// coefs_low[1] = (MIN_PT.z * (log_min.x+ 0.5 * knot_inc_low)) + ( log_min.y - MIN_PT.z * log_min.x);
			// NOTE: if slope=0, then the above becomes just
			coefs_low_a.x = log_min.y;
			coefs_low_a.y = coefs_low_a.x;
			// leaving it as a variable for now in case we decide we need non-zero slope extensions

			// Determine two highest coefficients (straddling midPt)
			float min_coef = (LOG_MID.y - MID_PT.z * LOG_MID.x);
			coefs_low_b.x = (MID_PT.z * (LOG_MID.x - 0.5 * knot_inc_low)) + (LOG_MID.y - MID_PT.z * LOG_MID.x);
			coefs_low_b.y = (MID_PT.z * (LOG_MID.x + 0.5 * knot_inc_low)) + (LOG_MID.y - MID_PT.z * LOG_MID.x);
			coefs_low_b.z = coefs_low_b.y;

			// Middle coefficient (which defines the "sharpness of the bend") is linearly interpolated
			float pct_low = Interpolate1D(BENDS_LOW_TABLE, log2(aces_min / 0.18));
			coefs_low_a.z = log_min.y + pct_low * (LOG_MID.y - log_min.y);

			float knot_inc_high = (log_max.x - LOG_MID.x) / 3.0f;
			// float halfKnotInc = (log_max.x - logMid.x) / 6.;

			// Determine two lowest coefficients (straddling midPt)
			// float minCoef = ( logMid.y - MID_PT.z * logMid.x);
			coefs_high_a.x = (MID_PT.z * (LOG_MID.x - 0.5 * knot_inc_high)) + min_coef;
			coefs_high_a.y = (MID_PT.z * (LOG_MID.x + 0.5 * knot_inc_high)) + min_coef;

			// Determine two highest coefficients (straddling maxPt)
			// coefs_high[3] = (MAX_PT.z * (log_max.x-0.5*knotIncHigh)) + ( log_max.y - MAX_PT.z * log_max.x);
			// coefs_high[4] = (MAX_PT.z * (log_max.x+0.5*knotIncHigh)) + ( log_max.y - MAX_PT.z * log_max.x);
			// NOTE: if slope=0, then the above becomes just
			coefs_high_b.x = log_max.y;
			coefs_high_b.y = coefs_high_b.x;
			coefs_high_b.z = coefs_high_b.y;
			// leaving it as a variable for now in case we decide we need non-zero slope extensions

			// Middle coefficient (which defines the "sharpness of the bend") is linearly interpolated

			float pct_high = Interpolate1D(BENDS_HIGH_TABLE, log2(aces_max / 0.18));
			coefs_high_a.z = LOG_MID.y + pct_high * (log_max.y - LOG_MID.y);

			float3 rgb_post = float3(
				SSTS(rgb_pre.x, float3(log_min.x, log_min.y, 0), float3(LOG_MID.x, LOG_MID.y, MID_PT.z), float3(log_max.x, log_max.y, 0), coefs_low_a, coefs_low_b, coefs_high_a, coefs_high_b),
				SSTS(rgb_pre.y, float3(log_min.x, log_min.y, 0), float3(LOG_MID.x, LOG_MID.y, MID_PT.z), float3(log_max.x, log_max.y, 0), coefs_low_a, coefs_low_b, coefs_high_a, coefs_high_b),
				SSTS(rgb_pre.z, float3(log_min.x, log_min.y, 0), float3(LOG_MID.x, LOG_MID.y, MID_PT.z), float3(log_max.x, log_max.y, 0), coefs_low_a, coefs_low_b, coefs_high_a, coefs_high_b));

			// Nits to Linear
			float3 linear_cv = YToLinCV(rgb_post, max_y, min_y);
			return clamp(rgb_post, 0.0, 65535.0f);
		}

		float3 ODT(float3 rgb_pre, float min_y, float max_y, float3x3 odt_matrix = AP1_TO_BT709_MAT)
		{
			float3 tonescaled = ODTToneMap(rgb_pre, min_y, max_y);

			float3 output_color = mul(odt_matrix, tonescaled);

			return output_color;
		}

		// ACES with
		// Reference Rendering Transform
		// Output Display Transform
		float3 RRTAndODT(float3 color, float min_y, float max_y, float3x3 odt_matrix = AP1_TO_BT709_MAT)
		{
			color = mul(BT709_TO_AP0_MAT, color);
			color = RRT(color);
			color = ODT(color, min_y, max_y, odt_matrix);
			return color;
		}

		// ACES for Scene-Linear BT709 with:
		// Reference Gamma Compression
		// Reference Rendering Transform
		// Output Display Transform
		float3 RGCAndRRTAndODT(float3 color, float min_y, float max_y, float3x3 odt_matrix = AP1_TO_BT709_MAT)
		{
			color = mul(BT709_TO_AP1_MAT, color);          // BT709 to AP1
			color = GamutCompress(color);                  // Compresses to AP1
			color = mul(AP1_TO_AP0_MAT, color);            // Convert to AP0
			color = RRT(color);                            // RRT AP0 => AP1
			color = ODT(color, min_y, max_y, odt_matrix);  // ODT AP1 => Matrix
			return color;
		}
	}  // namespace ACES
}  // namespace Tonemap