#pragma once

#include <vector>

namespace Subrect
{
	struct UVRegion
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 1.0f;
		float h = 1.0f;
	};

	struct PixelRegion
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t w = 1;
		uint32_t h = 1;
	};

	struct StereoPixelRegions
	{
		PixelRegion leftEye;
		PixelRegion rightEye;
	};

	struct Preset
	{
		std::string name;
		UVRegion leftEye;
		UVRegion rightEye;
	};

	class Controller
	{
	public:
		void LoadSettings(const json& a_json);
		void SaveSettings(json& a_json) const;
		void DrawEditor(ID3D11ShaderResourceView* previewSrv, ID3D11Texture2D* previewTexture, float eyeRatio);

		PixelRegion GetLeftEyePixelRegion(uint32_t fullTextureWidth, uint32_t fullTextureHeight) const;
		StereoPixelRegions GetStereoPixelRegions(uint32_t fullTextureWidth, uint32_t fullTextureHeight) const;

		const UVRegion& GetLeftEyeUV() const { return currentLeftEyeUV; }
		const UVRegion& GetRightEyeUV() const { return currentRightEyeUV; }

	private:
		std::vector<Preset> presets;
		int selectedPresetIndex = 0;
		char newPresetName[64] = "";

		UVRegion currentLeftEyeUV{};
		UVRegion currentRightEyeUV{};

		bool isDraggingCrop = false;
		float dragStartUV[2] = { 0.0f, 0.0f };

		void EnsureDefaultPreset();
		void SyncRightEyeUV();
		void ClampCurrentUV();
		void ApplyPreset(int index);
	};
}