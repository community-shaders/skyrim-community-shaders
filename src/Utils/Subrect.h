#pragma once

#include <imgui.h>
#include <vector>

namespace Util::Subrect
{
	/** @brief A sub-region of a texture expressed in normalised UV coordinates [0,1]. */
	struct UVRegion
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 1.0f;
		float h = 1.0f;
	};

	/** @brief A sub-region of a texture expressed in absolute pixel coordinates. */
	struct PixelRegion
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t w = 1;
		uint32_t h = 1;
	};

	/** @brief A named crop preset storing a UV region and its display name. */
	struct Preset
	{
		std::string name;
		UVRegion uv;
	};

	/**
	 * @brief Interactive sub-rectangle selection controller for cropping a texture.
	 *
	 * Provides an ImGui editor for picking a crop region in UV space [0,1],
	 * with support for named presets (e.g. Left Eye / Right Eye / Full Frame).
	 */
	class Controller
	{
	public:
		/**
		 * @brief Load crop settings (UV region and presets) from a JSON object.
		 * @param a_json The JSON source to read from.
		 */
		void LoadSettings(const json& a_json);

		/**
		 * @brief Save current crop settings (UV region and presets) to a JSON object.
		 * @param a_json The JSON target to write into.
		 */
		void SaveSettings(json& a_json) const;

		/**
		 * @brief Provide default presets used when no user presets exist yet.
		 *
		 * Replaces the built-in "Full Frame" placeholder used when JSON has no
		 * CropPresets entry. User edits and deletions of presets persist across saves.
		 *
		 * @param defaults The default presets to seed.
		 */
		void SeedDefaultPresets(std::vector<Preset> defaults);

		/**
		 * @brief Draw the interactive crop editor widget using ImGui.
		 *
		 * @param previewSrv SRV of the texture to display as the preview.
		 * @param previewTexture The texture resource (used to query dimensions).
		 * @param uvVisibleWidth Fraction of the texture width visible in the preview window.
		 * @param uvStartX Starting U coordinate for the visible preview window.
		 * @param imageRenderCallback Optional ImDrawList callback queued around the preview
		 *        image draw for overriding blend state (paired with ImDrawCallback_ResetRenderState).
		 */
		void DrawEditor(ID3D11ShaderResourceView* previewSrv, ID3D11Texture2D* previewTexture,
			float uvVisibleWidth = 1.0f, float uvStartX = 0.0f,
			ImDrawCallback imageRenderCallback = nullptr);

		/**
		 * @brief Resolve the current crop UV region to pixel coordinates.
		 * @param width The full texture width in pixels.
		 * @param height The full texture height in pixels.
		 * @return The crop region in pixel coordinates.
		 */
		PixelRegion GetPixelRegion(uint32_t width, uint32_t height) const;

		/** @brief Get the current crop region in UV coordinates. */
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
