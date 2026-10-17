#ifndef __PROCEDURAL_AURORA_DEPENDENCY_HLSL__
#define __PROCEDURAL_AURORA_DEPENDENCY_HLSL__

// Procedural aurora, drawn into the sky dome by Sky.hlsl. Each sky pixel finds where it falls on the aurora ribbons
// (see Render) and shades the curtain there. The vanilla ribbons are culled on the CPU while the feature is on.

namespace ProceduralAurora
{
#if defined(PSHADER)
	float Hash11(float p)
	{
		p = frac(p * 0.1031);
		p *= p + 33.33;
		p *= p + p;
		return frac(p);
	}

	float Hash21(float2 p)
	{
		float3 p3 = frac(p.xyx * 0.1031);
		p3 += dot(p3, p3.yzx + 33.33);
		return frac((p3.x + p3.y) * p3.z);
	}

	float Noise(float2 p)
	{
		float2 i = floor(p);
		float2 f = frac(p);
		float2 w = f * f * (3.0 - 2.0 * f);
		float a = Hash21(i);
		float b = Hash21(i + float2(1.0, 0.0));
		float c = Hash21(i + float2(0.0, 1.0));
		float d = Hash21(i + float2(1.0, 1.0));
		return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
	}

	// Value noise plus its derivative along x
	float2 NoiseDx(float2 p)
	{
		float2 i = floor(p);
		float2 f = frac(p);
		float2 w = f * f * (3.0 - 2.0 * f);
		float dw = 6.0 * f.x * (1.0 - f.x);
		float a = Hash21(i);
		float b = Hash21(i + float2(1.0, 0.0));
		float c = Hash21(i + float2(0.0, 1.0));
		float d = Hash21(i + float2(1.0, 1.0));
		float value = lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
		float dx = lerp(b - a, d - c, w.y) * dw;
		return float2(value, dx);
	}

	// Four-octave fbm plus its derivative along x
	float2 FbmDx(float2 p)
	{
		float2 result = 0.0;
		float amplitude = 0.5;
		float scale = 1.0;
		[unroll] for (int i = 0; i < 4; i++)
		{
			float2 n = NoiseDx(p);
			result += amplitude * float2(n.x, n.y * scale);
			p = p * 2.03 + float2(17.1, 3.7);
			scale *= 2.03;
			amplitude *= 0.5;
		}
		return result / 0.9375;
	}

	float Fbm2(float2 p)
	{
		return (0.5 * Noise(p) + 0.25 * Noise(p * 2.03 + float2(17.1, 3.7))) / 0.75;
	}

	// Each octave fades to its mean once it is finer than about a pixel, so distant rays do not sparkle
	float Rays(float x, float t, float pixelWidth, float sharpnessScale)
	{
		float rays = 0.0;
		float octaveScale[3] = { 1.0, 2.37, 5.11 };
		float octaveWeight[3] = { 0.55, 0.30, 0.15 };
		float octaveSharpness[3] = { 6.0, 8.0, 11.0 };
		float octaveSpeed[3] = { 0.05, 0.1, 0.2 };

		[unroll] for (int i = 0; i < 3; i++)
		{
			float scale = octaveScale[i];
			float lod = saturate(1.5 - pixelWidth * scale * 1.5);
			float ridge = 1.0 - abs(Noise(float2(x * scale + 11.3 * i, t * octaveSpeed[i])) * 2.0 - 1.0);
			float sharpness = octaveSharpness[i] * sharpnessScale;
			rays += octaveWeight[i] * lerp(1.0 / (sharpness + 1.0), pow(saturate(ridge), sharpness), lod);
		}
		return rays * 2.2;
	}

	// Colors arrive as OKLCh, prepared on the CPU (see ProceduralAurora::GetCommonBufferData)
	float3 LchToLab(float3 lch)
	{
		float s, c;
		sincos(lch.z, s, c);
		return float3(lch.x, lch.y * c, lch.y * s);
	}

	float3 OkLabToLinear(float3 lab)
	{
		float l = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
		float m = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
		float s = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;
		l = l * l * l;
		m = m * m * m;
		s = s * s * s;
		return max(float3(
					   4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
					   -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
					   -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s),
			0.0);
	}

	// The sky dome is shaded in display space
	float3 OkLabToDisplay(float3 lab)
	{
		return pow(OkLabToLinear(lab), 1.0 / 2.2);
	}

	// One curtain, shaded as glowing gas lit from its lower edge. Time runs through the noise rather than along the
	// curtain, so the pattern evolves in place instead of sliding sideways.
	// u: position along the ribbon, uWidth: its screen-space width, h: 0 lower edge .. 1 upper edge,
	// mask: fade toward the ribbon ends, zenith: 0 horizon .. 1 straight up. Returns display-space color.
	float3 Shade(float u, float uWidth, float ribbon, float h, float mask, float zenith)
	{
		const float storm = SharedData::proceduralAuroraSettings.BodyColor.w;  // 0 quiet night .. 1 great storm
		const float t = SharedData::proceduralAuroraSettings.Time;
		const float seed = SharedData::proceduralAuroraSettings.Seed;
		const float brightness = SharedData::proceduralAuroraSettings.Brightness * lerp(0.75, 1.0, storm);
		// Seen from straight below, a curtain would read as a stripe, so soften it there
		const float softness = saturate(SharedData::proceduralAuroraSettings.Softness + zenith * 0.6);
		const float haze = SharedData::proceduralAuroraSettings.Atmosphere.y;
		const float nebula = SharedData::proceduralAuroraSettings.Atmosphere.z;

		const float rseed = frac(seed * 0.0713 + ribbon * 0.317) * 64.0;  // small, so noise coordinates keep precision

		// Quiet nights dim some ribbons
		float ribbonRoll = Hash11(ribbon * 12.9898 + seed * 78.233);
		float ribbonStrength = lerp(0.55, 1.0, smoothstep(lerp(0.45, -0.2, storm), lerp(0.75, 0.15, storm), ribbonRoll)) * lerp(0.8, 1.15, Hash11(ribbon * 4.17 + seed * 3.31));

		// Folds: where the bend compresses the curtain, the sheet is seen edge-on and brightens
		const float foldStrength = SharedData::proceduralAuroraSettings.FoldStrength * lerp(0.7, 1.2, storm);
		float2 fold = FbmDx(float2(u * 0.6 + rseed, t * 0.006));
		float xs = u + (fold.x - 0.5) * 1.2 * foldStrength;
		float compression = abs(1.0 + fold.y * 0.6 * 1.2 * foldStrength);
		float foldGlow = clamp(1.0 / max(compression, 0.4), 0.6, 1.5);

		// Domain-warped noise for the gas, drifting upwards
		float2 p = float2(xs * 3.0 + rseed, h * 1.4);
		float2 q = float2(Fbm2(p * 0.6 + float2(0.0, -t * 0.02)), Fbm2(p * 0.6 + float2(3.1, 7.7 - t * 0.026)));
		float gas = Fbm2(p + (q - 0.5) * 2.0 + float2(0.0, -t * 0.035));
		gas += (Noise(float2(p.x * 3.1 + q.x * 3.0 + 5.3, p.y * 2.6 + q.y * 3.0 - t * 0.07)) - 0.5) * 0.3;
		float smoke = smoothstep(0.3, 0.72, gas);

		// The lower edge only rises from the ribbon's own, and wisps reach at most 0.41 below it, so everything fades
		// out before the h > -0.4 cut-off in Render
		float meander = smoothstep(0.25, 0.75, Fbm2(float2(u * 0.4 + rseed * 2.3, t * 0.005)));
		float edge = 0.04 + 0.12 * Noise(float2(xs * 1.3 + rseed, t * 0.015)) + 0.4 * meander * saturate(SharedData::proceduralAuroraSettings.FoldStrength);
		float border = smoothstep(-0.12 - 0.08 * softness, 0.04 + 0.04 * softness, h - edge + (saturate(gas) - 0.5) * lerp(0.3, 0.42, softness));
		float3 shaded = 0.0;
		[branch] if (border > 0.0)
		{
			float hp = max(h - edge, 0.0);

			// Ragged fade toward the ribbon ends
			mask = pow(saturate(mask), 1.0 + softness);
			[branch] if (mask < 1.0)
				mask *= lerp(Noise(float2(u * 3.0 + rseed, h * 2.0 - t * 0.03)), 1.0, mask * mask);

			// Rays bend a little with the gas and come in groups
			float rayFrequency = 28.0 * SharedData::proceduralAuroraSettings.RayDetail;
			float rayCoord = (xs + (q.x - 0.5) * 0.05 * saturate(h)) * rayFrequency + rseed * 3.0;
			float rays = Rays(rayCoord, t, uWidth * rayFrequency * max(compression, 0.05), lerp(0.5, 0.3, softness));
			rays *= 0.3 + 1.4 * smoothstep(0.3, 0.8, Noise(float2(rayCoord / 7.0 + 3.1, t * 0.03)));
			const float rayStrength = SharedData::proceduralAuroraSettings.RayStrength * lerp(0.7, 1.15, storm);

			// Rays carry the light higher up the curtain
			float decay = (1.7 - 0.7 * saturate(rays * rayStrength) * (0.4 + 0.6 * smoke)) * lerp(1.0, 1.15, softness);
			// Ends below the h < 1.2 cut-off in Render
			float curtainTop = (1.0 - smoothstep(0.35 - 0.1 * softness, 0.85, hp + (saturate(gas) - 0.5) * 0.3)) * (1.0 - smoothstep(0.85, 1.1, h));
			float curtain = exp(-hp * decay) * curtainTop * (0.25 + 0.75 * smoke) * 1.4 * (lerp(0.75, 0.65, softness) + rayStrength * 0.5 * rays);
			// Bright lower edge
			float band = exp(-hp * 9.0) * (0.35 + 0.65 * smoke) * (0.8 + 0.2 * saturate(rays)) * 1.8;
			float glow = exp(-hp * 1.1) * curtainTop * (0.35 + 0.65 * smoke) * haze * 0.35;
			// Top color glow
			const float crownStart = SharedData::proceduralAuroraSettings.CrownColor.w;
			float crown = smoothstep(crownStart * 0.5, crownStart + 0.35, hp) * (1.0 - smoothstep(0.6, 1.05, hp + (saturate(gas) - 0.5) * 0.3)) * (1.0 - smoothstep(0.9, 1.15, h)) * smoke * nebula * lerp(0.6, 1.2, storm);

			// A fainter sheet behind the curtain gives it depth
			float hk = h - edge - 0.1 - 0.12 * Noise(float2(xs * 0.9 + rseed + 7.0, t * 0.012));
			float backGas = Noise(float2(xs * 7.0 + rseed + 19.0, h * 1.4 - t * 0.03));
			float back = smoothstep(-0.25, 0.25, hk) * exp(-max(hk, 0.0) * 2.2) * curtainTop * (0.25 + 0.75 * backGas) * 0.21;

			// Patches of activity along the ribbon, wider on stormy nights
			float activity = smoothstep(lerp(0.45, 0.05, storm), lerp(0.85, 0.55, storm), Fbm2(float2(u * 0.5 + rseed * 1.7, t * 0.008)));
			activity = 0.5 + 0.5 * activity * sqrt(activity);
			float pulse = 0.85 + 0.15 * Noise(float2(xs * 2.5 + rseed, t * 0.12));
			float intensity = border * activity * pulse * foldGlow * ribbonStrength * lerp(1.0, 0.7, zenith) * brightness;

			// The top color is added as its own light rather than blended through the hue range, which would show
			// rainbow stripes between distant hues
			float fringe = saturate((1.0 - smoothstep(0.02, 0.2, hp + (gas - 0.5) * 0.1)) * SharedData::proceduralAuroraSettings.FringeColor.w * lerp(0.45, 1.25, storm) * Noise(float2(xs * 0.9 + rseed + 5.0, t * 0.02)));
			float up = smoothstep(crownStart, crownStart + 0.5, hp);
			float lowerLight = curtain + band + back + glow * (1.0 - 0.6 * up);
			float upperLight = crown + glow * 0.6 * up;
			float3 lch = lerp(SharedData::proceduralAuroraSettings.BodyColor.xyz, SharedData::proceduralAuroraSettings.FringeColor.xyz, fringe);
			float3 color = (OkLabToDisplay(LchToLab(lch)) * lowerLight + OkLabToDisplay(LchToLab(SharedData::proceduralAuroraSettings.CrownColor.xyz)) * upperLight) * intensity;

			// Soft limit so bright folds keep their hue instead of clipping to white
			float peak = max(color.r, max(color.g, color.b));
			color /= 1.0 + peak * 0.8;

			shaded = color * (mask * lerp(1.0, 0.8, softness));
		}
		return shaded;
	}
	// Ribbon layout traced from the vanilla aurora meshes (meshes\sky\skyrimaurora*.nif). Each ribbon runs one way
	// around the sky, so its edges are stored as elevations sampled evenly by azimuth: one lookup per ribbon, no seams.
	static const uint RibbonCount = 5;
	static const uint RibbonPoints = 24;
	static const float UPerRadian = 1.5;  // pattern density along a ribbon, per radian of its lower edge
	static const float PatternScale = 0.8;
	static const float HeightScale = 1.15;  // curtains are taller than the vanilla ribbons; the lower edge stays put
	static const float TopCap = 1.4;        // highest elevation a grown upper edge may reach, keeping clear of the zenith
	// x: start azimuth (radians, clockwise from north), y: azimuth span, z/w: elevation bounds for a quick reject
	static const float4 RibbonRange[RibbonCount] = {
		float4(-7.95048, 6.14505, 0.37891, 1.57945),
		float4(-1.50435, 2.83411, 0.18368, 0.92936),
		float4(-1.13323, 1.85635, 0.14106, 0.61399),
		float4(1.48162, 3.03386, 0.20890, 1.24834),
		float4(-4.92460, 2.83661, 0.15720, 0.76510)
	};
	// x: lower edge elevation, y: upper edge elevation (radians), z: distance along the lower edge times UPerRadian
	static const float3 RibbonTable[RibbonCount * RibbonPoints] = {
		// ribbon 0
		float3(0.49331, 0.71438, 0.0000), float3(0.58937, 0.86336, 0.3720), float3(0.68868, 0.97647, 0.7258), float3(0.78446, 1.05185, 1.0549),
		float3(0.85950, 1.09901, 1.3494), float3(0.90078, 1.12119, 1.6116), float3(0.90796, 1.12335, 1.8591), float3(0.88673, 1.11351, 2.1106),
		float3(0.84424, 1.09955, 2.3776), float3(0.79336, 1.09130, 2.6612), float3(0.75618, 1.10077, 2.9525), float3(0.75680, 1.13733, 3.2435),
		float3(0.80386, 1.20286, 3.5365), float3(0.88116, 1.28696, 3.8266), float3(0.96097, 1.37025, 4.0963), float3(1.02428, 1.43650, 4.3343),
		float3(1.06505, 1.47546, 4.5441), float3(1.08301, 1.48016, 4.7365), float3(1.07810, 1.44981, 4.9249), float3(1.04762, 1.38863, 5.1246),
		float3(0.98403, 1.29724, 5.3556), float3(0.87364, 1.15851, 5.6461), float3(0.70390, 0.93774, 6.0251), float3(0.48714, 0.63762, 6.4883),
		// ribbon 1
		float3(0.28759, 0.41008, 0.0000), float3(0.29577, 0.44456, 0.1774), float3(0.30462, 0.47951, 0.3545), float3(0.31934, 0.51991, 0.5318),
		float3(0.34844, 0.57281, 0.7118), float3(0.39313, 0.63388, 0.8966), float3(0.44259, 0.68808, 1.0810), float3(0.48517, 0.72963, 1.2582),
		float3(0.51699, 0.76523, 1.4272), float3(0.53832, 0.79613, 1.5900), float3(0.54927, 0.81368, 1.7490), float3(0.54904, 0.81273, 1.9066),
		float3(0.53536, 0.79429, 2.0662), float3(0.50432, 0.75646, 2.2332), float3(0.45574, 0.69783, 2.4126), float3(0.40150, 0.63059, 2.5993),
		float3(0.35876, 0.57444, 2.7825), float3(0.33281, 0.53691, 2.9607), float3(0.31696, 0.51268, 3.1374), float3(0.30445, 0.49308, 3.3144),
		float3(0.29228, 0.46991, 3.4920), float3(0.27912, 0.44022, 3.6704), float3(0.26434, 0.40691, 3.8499), float3(0.24825, 0.37302, 4.0303),
		// ribbon 2
		float3(0.21645, 0.31312, 0.0000), float3(0.22030, 0.32917, 0.1183), float3(0.22370, 0.34527, 0.2365), float3(0.22651, 0.36165, 0.3546),
		float3(0.22885, 0.37817, 0.4726), float3(0.23088, 0.39386, 0.5905), float3(0.23283, 0.40708, 0.7084), float3(0.23497, 0.41643, 0.8262),
		float3(0.23776, 0.42264, 0.9440), float3(0.24202, 0.42982, 1.0617), float3(0.24876, 0.44480, 1.1796), float3(0.25777, 0.47152, 1.2976),
		float3(0.26583, 0.49978, 1.4152), float3(0.26844, 0.50882, 1.5320), float3(0.26442, 0.49162, 1.6489), float3(0.25675, 0.46265, 1.7665),
		float3(0.24889, 0.43769, 1.8843), float3(0.24241, 0.41915, 2.0021), float3(0.23737, 0.40151, 2.1200), float3(0.23327, 0.38175, 2.2378),
		float3(0.22953, 0.36153, 2.3558), float3(0.22568, 0.34288, 2.4739), float3(0.22138, 0.32576, 2.5921), float3(0.21668, 0.30930, 2.7105),
		// ribbon 3
		float3(0.31596, 0.50836, 0.0000), float3(0.36664, 0.60755, 0.2013), float3(0.41265, 0.70869, 0.3969), float3(0.45132, 0.80033, 0.5857),
		float3(0.48188, 0.86363, 0.7682), float3(0.50503, 0.89162, 0.9458), float3(0.52359, 0.89848, 1.1203), float3(0.54195, 0.90937, 1.2929),
		float3(0.56305, 0.94163, 1.4642), float3(0.58562, 0.99188, 1.6337), float3(0.60536, 1.03745, 1.8001), float3(0.61814, 1.05654, 1.9632),
		float3(0.62167, 1.04782, 2.1243), float3(0.61510, 1.02413, 2.2858), float3(0.59811, 0.99573, 2.4503), float3(0.57069, 0.96305, 2.6203),
		float3(0.53355, 0.91612, 2.7977), float3(0.48919, 0.84475, 2.9826), float3(0.44248, 0.75436, 3.1727), float3(0.39895, 0.66241, 3.3647),
		float3(0.36147, 0.58036, 3.5568), float3(0.32886, 0.50854, 3.7493), float3(0.29806, 0.44291, 3.9431), float3(0.26719, 0.37983, 4.1386),
		// ribbon 4
		float3(0.22767, 0.32473, 0.0000), float3(0.23491, 0.34757, 0.1804), float3(0.24127, 0.37176, 0.3604), float3(0.24666, 0.39761, 0.5401),
		float3(0.25158, 0.42295, 0.7195), float3(0.25663, 0.44449, 0.8987), float3(0.26242, 0.46190, 1.0778), float3(0.26946, 0.47920, 1.2565),
		float3(0.27805, 0.50131, 1.4351), float3(0.28807, 0.53065, 1.6134), float3(0.29883, 0.56647, 1.7912), float3(0.30909, 0.60260, 1.9684),
		float3(0.31743, 0.62695, 2.1448), float3(0.32274, 0.63047, 2.3206), float3(0.32452, 0.61520, 2.4960), float3(0.32287, 0.59097, 2.6713),
		float3(0.31812, 0.56662, 2.8471), float3(0.31057, 0.54292, 3.0233), float3(0.30035, 0.51382, 3.2004), float3(0.28750, 0.47584, 3.3785),
		float3(0.27196, 0.43222, 3.5578), float3(0.25366, 0.38750, 3.7386), float3(0.23272, 0.34324, 3.9208), float3(0.21002, 0.29918, 4.1045)
	};

	// Distant arcs near the horizon: copies of lower ribbons turned to the other side of the sky.
	// x: ribbon to copy, y: azimuth turn (radians), z: elevation scale, w: pattern seed
	static const uint DistantCount = 2;
	static const float4 DistantArcs[DistantCount] = {
		float4(1.0, 3.14159, 0.42, 6.0),
		float4(4.0, 3.14159, 0.42, 7.0)
	};
	static const float DistantPatternScale = 1.6;
	static const float DistantBrightness = 0.6;
	static const float WaveAmplitude = 0.1;  // radians
	static const float WaveDensity = 4.0;

	float CatmullRom(float p0, float p1, float p2, float p3, float t)
	{
		return 0.5 * (2.0 * p1 + (p2 - p0) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t + (3.0 * (p1 - p2) + p3 - p0) * t * t * t);
	}

	// Returns x: u, y: h (0 lower edge .. 1 upper edge, may run past either), z: fade toward the ends (0 off the ribbon)
	float3 LookupRibbon(uint i, float azimuth, float elevation)
	{
		const float4 range = RibbonRange[i];
		if (elevation < range.z || elevation > range.w)
			return 0.0;
		float a = azimuth - range.x;
		a -= 6.2831853 * floor(a / 6.2831853);
		const float s = a / range.y;  // 0 .. 1 along the ribbon
		if (s > 1.0)
			return 0.0;

		const float f = s * (RibbonPoints - 1);
		const uint k = min((uint)f, RibbonPoints - 2);
		const float t = f - k;
		const uint base = i * RibbonPoints;
		const float3 p1 = RibbonTable[base + k];
		const float3 p2 = RibbonTable[base + k + 1];
		const float3 p0 = RibbonTable[base + (k > 0 ? k - 1 : k)];
		const float3 p3 = RibbonTable[base + min(k + 2, RibbonPoints - 1)];
		const float lower = CatmullRom(p0.x, p1.x, p2.x, p3.x, t);
		const float vanillaUpper = CatmullRom(p0.y, p1.y, p2.y, p3.y, t);
		const float upper = min(lower + (vanillaUpper - lower) * HeightScale, max(vanillaUpper, TopCap));
		const float h = (elevation - lower) / max(upper - lower, 0.02);
		const float ends = smoothstep(0.0, 0.07, s) * (1.0 - smoothstep(0.93, 1.0, s));
		return float3(lerp(p1.z, p2.z, t) * PatternScale, h, ends);
	}

	float HorizonFade(float3 dir)
	{
		return smoothstep(-0.03, 0.06, dir.z);
	}

	// dir: normalized world-space view direction. pixelAngle: length(fwidth(dir)), taken by the caller in uniform
	// control flow; the caller skips directions below the horizon (dir.z <= -0.03). Returns display-space color to add
	// onto the sky.
	float3 Render(float3 dir, float pixelAngle)
	{
		const float fade = SharedData::proceduralAuroraSettings.Atmosphere.x;  // vanilla aurora visibility, 0..1

		const float uWidth = UPerRadian * PatternScale * pixelAngle;
		const float azimuth = atan2(dir.x, dir.y);
		const float elevation = asin(saturate(dir.z));
		const float zenith = smoothstep(0.7, 0.97, dir.z);
		// Whole ribbons snake up and down. The waves are read around a ring, which has no seam, and fade out before
		// the zenith, where every azimuth meets.
		const float2 ring = dir.xy * (WaveDensity / max(length(dir.xy), 1e-4));
		const float waveScale = WaveAmplitude * saturate(SharedData::proceduralAuroraSettings.FoldStrength) * (1.0 - smoothstep(1.2, 1.45, elevation));
		const float t = SharedData::proceduralAuroraSettings.Time;
		const float seed = SharedData::proceduralAuroraSettings.Seed;

		float3 color = 0.0;
		// One loop for ribbons and distant arcs, so Shade is only inlined once
		[loop] for (uint j = 0; j < RibbonCount + DistantCount; j++)
		{
			const bool distant = j >= RibbonCount;
			const float4 arc = distant ? DistantArcs[distant ? j - RibbonCount : 0] : float4((float)j, 0.0, 1.0, (float)j);
			const float ribbonElevation = elevation / arc.z;
			const float4 range = RibbonRange[(uint)arc.x];
			[branch] if (ribbonElevation < range.z - WaveAmplitude || ribbonElevation > range.w + WaveAmplitude)
				continue;
			const float waveSeed = frac(seed * 0.0713 + arc.w * 0.317) * 64.0;
			const float wave = (Fbm2(ring + float2(waveSeed, waveSeed * 0.37 + t * 0.004)) - 0.5) * 2.0 * waveScale;
			const float3 hit = LookupRibbon((uint)arc.x, azimuth - arc.y, ribbonElevation - wave);
			[branch] if (hit.z > 0.001 && hit.y > -0.4 && hit.y < 1.2)
			{
				// Squeezed toward the horizon, distant arcs need the extra softness of an overhead curtain
				const float patternScale = distant ? DistantPatternScale : 1.0;
				color += Shade(hit.x * patternScale, uWidth * patternScale, arc.w, hit.y, hit.z, distant ? max(zenith, 0.5) : zenith) * (distant ? DistantBrightness : 1.0);
			}
		}

		float peak = max(color.r, max(color.g, color.b));
		color /= 1.0 + peak * 0.5;
		return color * fade * HorizonFade(dir);
	}
#endif
}

#endif  // __PROCEDURAL_AURORA_DEPENDENCY_HLSL__
