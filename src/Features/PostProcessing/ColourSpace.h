#pragma once

// via https://www.colour-science.org/

inline const auto& getAvailableColourSpaces()
{
	static auto spaces = std::array{
		"sRGB",
		"BT709",
		"BT2020",
		"DCI-P3",
		"XYZ",
		"ACEScg"
	};
	return spaces;
}

// Native white point chromaticity (CIE xy) for each color space
inline DirectX::XMFLOAT2 getWhitePoint(std::string_view space)
{
	if (space == "ACEScg")
		return { 0.32168f, 0.33767f };  // ACES White (≈D60)
	if (space == "DCI-P3")
		return { 0.31400f, 0.35100f };  // DCI White
	// sRGB, BT.709, BT.2020, XYZ: D65
	return { 0.31270f, 0.32900f };
}

inline DirectX::SimpleMath::Matrix getRGBMatrix(std::string_view in_space, std::string_view out_space)
{
	static ankerl::unordered_dense::map<std::string, DirectX::XMFLOAT3X3> maps = {
		{ "sRGB-XYZ",
			{ 0.4123908, 0.35758434, 0.18048079,
				0.21263901, 0.71516868, 0.07219232,
				0.01933082, 0.11919478, 0.95053215 } },
		{ "XYZ-sRGB",
			{ 3.24096994, -1.53738318, -0.49861076,
				-0.96924364, 1.8759675, 0.04155506,
				0.05563008, -0.20397696, 1.05697151 } },

		{ "BT2020-XYZ",
			{ 6.36958048e-01, 1.44616904e-01, 1.68880975e-01,
				2.62700212e-01, 6.77998072e-01, 5.93017165e-02,
				4.99410657e-17, 2.80726930e-02, 1.06098506e+00 } },
		{ "XYZ-BT2020",
			{ 1.71665119, -0.35567078, -0.25336628,
				-0.66668435, 1.61648124, 0.01576855,
				0.01763986, -0.04277061, 0.94210312 } },

		{ "DCI-P3-XYZ",
			{ 4.45169816e-01, 2.77134409e-01, 1.72282670e-01,
				2.09491678e-01, 7.21595254e-01, 6.89130679e-02,
				-3.63410132e-17, 4.70605601e-02, 9.07355394e-01 } },
		{ "XYZ-DCI-P3",
			{ 2.72539403, -1.01800301, -0.4401632,
				-0.79516803, 1.68973205, 0.02264719,
				0.04124189, -0.08763902, 1.10092938 } },

		{ "ACEScg-XYZ",
			{ 0.66245418, 0.13400421, 0.15618769,
				0.27222872, 0.67408177, 0.05368952,
				-0.00557465, 0.00406073, 1.0103391 } },
		{ "XYZ-ACEScg",
			{ 1.64102338, -0.32480329, -0.2364247,
				-0.66366286, 1.61533159, 0.01675635,
				0.01172189, -0.00828444, 0.98839486 } },
	};
	static std::once_flag flag;
	std::call_once(flag, [&]() {
		maps["BT709-XYZ"] = maps["sRGB-XYZ"];
		maps["XYZ-BT709"] = maps["XYZ-sRGB"];
	});

	if (in_space == out_space)
		return DirectX::SimpleMath::Matrix::Identity;

	if (in_space == "XYZ" || out_space == "XYZ")
		return DirectX::SimpleMath::Matrix{ maps[std::format("{}-{}", in_space, out_space)] };
	else {
		DirectX::SimpleMath::Matrix a = maps[std::format("{}-XYZ", in_space)];
		DirectX::SimpleMath::Matrix b = maps[std::format("XYZ-{}", out_space)];
		auto c = DirectX::XMMatrixMultiply(b, a);
		return c;
	}
}