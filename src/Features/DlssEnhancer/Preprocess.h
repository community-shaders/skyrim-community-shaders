#pragma once

struct Upscaling;

namespace DlssEnhancer
{
	class Preprocess
	{
	public:
		static bool EncodeUpscalingTextures(Upscaling& upscaling);
	};
}
