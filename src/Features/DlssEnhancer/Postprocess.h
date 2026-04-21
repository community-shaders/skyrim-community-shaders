#pragma once

struct Upscaling;

namespace DlssEnhancer
{
	class Postprocess
	{
	public:
		// Stage3: DLSS postprocess replacement (sharpening and related post chain).
		static bool ApplyDlssSharpening(Upscaling& upscaling);
	};
}
