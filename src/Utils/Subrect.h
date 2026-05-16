#pragma once

#include <imgui.h>
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

	// "User picks a sub-rectangle of an image" controller. Crop UV is in [0,1]
	// of the source the caller passes to GetPixelRegion(). Hosts that want
	// preset-based eye selection seed Left/Right/Full Frame via SeedDefaultPresets.
	class Controller
	{
	public:
		void LoadSettings(const json& a_json);
		void SaveSettings(json& a_json) const;

		// Replaces the built-in "Full Frame" placeholder used when JSON has no
		// CropPresets entry yet. Empty-case only - user edits/deletions persist.
		void SeedDefaultPresets(std::vector<Preset> defaults);

		// uvStartX/uvVisibleWidth window the preview onto a sub-region of the
		// texture; crop UV stays in [0,1] of that window. imageRenderCallback,
		// when non-null, is queued via ImDrawList::AddCallback around the
		// preview Image draw (paired with ImDrawCallback_ResetRenderState) so
		// hosts can override blend state for the image specifically.
		void DrawEditor(ID3D11ShaderResourceView* previewSrv, ID3D11Texture2D* previewTexture,
			float uvVisibleWidth = 1.0f, float uvStartX = 0.0f,
			ImDrawCallback imageRenderCallback = nullptr);

		// Resolves the crop UV against an arbitrary pixel size.
		PixelRegion GetPixelRegion(uint32_t width, uint32_t height) const;

		const UVRegion& GetUV() const { return currentUV; }

	private:
		std::vector<Preset> presets;
		std::vector<Preset> seededDefaults;
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
