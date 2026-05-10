#pragma once

#include <vector>

namespace Util::Subrect
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

	struct Preset
	{
		std::string name;
		UVRegion uv;
	};

	// Stateful component for "user picks a sub-rectangle of an image". Stereo-agnostic;
	// callers pass per-eye dimensions if their source is a side-by-side stereo image.
	class Controller
	{
	public:
		void LoadSettings(const json& a_json);
		void SaveSettings(json& a_json) const;
		// previewSrv/previewTexture: optional — when provided, draws a clickable thumbnail
		// the user can drag on. uvVisibleWidth: fraction of the texture width to show
		// (e.g. 0.5 to display only the left eye of an SBS stereo source).
		void DrawEditor(ID3D11ShaderResourceView* previewSrv, ID3D11Texture2D* previewTexture, float uvVisibleWidth);

		// Resolves the current crop UV against an arbitrary pixel size.
		PixelRegion GetPixelRegion(uint32_t width, uint32_t height) const;

		const UVRegion& GetUV() const { return currentUV; }

	private:
		std::vector<Preset> presets;
		int selectedPresetIndex = 0;
		char newPresetName[64] = "";

		UVRegion currentUV{};

		bool isDraggingCrop = false;
		float dragStartUV[2] = { 0.0f, 0.0f };

		void EnsureDefaultPreset();
		void ClampCurrentUV();
		void ApplyPreset(int index);
	};
}  // namespace Util::Subrect
