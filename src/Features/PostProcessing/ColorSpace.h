#pragma once

// via https://www.colour-science.org/

inline const auto& getAvailableColorSpaces()
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
		return { 0.32168f, 0.33767f };  // ACES White (approx D60)
	if (space == "DCI-P3")
		return { 0.31400f, 0.35100f };  // DCI White
	// sRGB, BT.709, BT.2020, XYZ: D65
	return { 0.31270f, 0.32900f };
}

inline DirectX::SimpleMath::Matrix getRGBMatrix(std::string_view in_space, std::string_view out_space)
{
	static ankerl::unordered_dense::map<std::string, DirectX::XMFLOAT3X3> maps = {
		{ "sRGB-XYZ",
			{ 0.4123908f, 0.35758434f, 0.18048079f,
				0.21263901f, 0.71516868f, 0.07219232f,
				0.01933082f, 0.11919478f, 0.95053215f } },
		{ "XYZ-sRGB",
			{ 3.24096994f, -1.53738318f, -0.49861076f,
				-0.96924364f, 1.8759675f, 0.04155506f,
				0.05563008f, -0.20397696f, 1.05697151f } },

		{ "BT2020-XYZ",
			{ 6.36958048e-01f, 1.44616904e-01f, 1.68880975e-01f,
				2.62700212e-01f, 6.77998072e-01f, 5.93017165e-02f,
				4.99410657e-17f, 2.80726930e-02f, 1.06098506e+00f } },
		{ "XYZ-BT2020",
			{ 1.71665119f, -0.35567078f, -0.25336628f,
				-0.66668435f, 1.61648124f, 0.01576855f,
				0.01763986f, -0.04277061f, 0.94210312f } },

		{ "DCI-P3-XYZ",
			{ 4.45169816e-01f, 2.77134409e-01f, 1.72282670e-01f,
				2.09491678e-01f, 7.21595254e-01f, 6.89130679e-02f,
				-3.63410132e-17f, 4.70605601e-02f, 9.07355394e-01f } },
		{ "XYZ-DCI-P3",
			{ 2.72539403f, -1.01800301f, -0.4401632f,
				-0.79516803f, 1.68973205f, 0.02264719f,
				0.04124189f, -0.08763902f, 1.10092938f } },

		{ "ACEScg-XYZ",
			{ 0.66245418f, 0.13400421f, 0.15618769f,
				0.27222872f, 0.67408177f, 0.05368952f,
				-0.00557465f, 0.00406073f, 1.0103391f } },
		{ "XYZ-ACEScg",
			{ 1.64102338f, -0.32480329f, -0.2364247f,
				-0.66366286f, 1.61533159f, 0.01675635f,
				0.01172189f, -0.00828444f, 0.98839486f } },
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
