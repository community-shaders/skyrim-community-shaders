#pragma once

/** @brief Manages the water flowmap texture used for directional water flow rendering. */
class Flowmap
{
public:
	/**
	 * @brief Retrieves the loaded flowmap texture if available.
	 * @param outFlowmapTex Output parameter set to the flowmap texture on success.
	 * @return True if a valid flowmap texture was retrieved, false otherwise.
	 */
	bool TryGetFlowmap(RE::NiPointer<RE::NiSourceTexture>& outFlowmapTex) const;
	/** @brief Returns the flowmap width in pixels. */
	int32_t GetWidth() const { return width; }
	/** @brief Returns the flowmap height in pixels. */
	int32_t GetHeight() const { return height; }
	/** @brief Returns the inverse of the flowmap width (1.0 / width). */
	float GetInverseWidth() const { return invWidth; }
	/** @brief Returns the inverse of the flowmap height (1.0 / height). */
	float GetInverseHeight() const { return invHeight; }
	/** @brief Returns the X offset of the flowmap in cell coordinates. */
	int32_t GetOffsetX() const { return offsetX; }
	/** @brief Returns the Y offset of the flowmap in cell coordinates. */
	int32_t GetOffsetY() const { return offsetY; }
	/** @brief Releases the flowmap texture and resets all dimensions to zero. */
	void Reset();

	/**
	 * @brief Loads an existing flowmap from disk, or generates and loads one if none exists.
	 * @param useMips Whether to generate mipmaps for the flowmap texture.
	 * @return True if the flowmap was successfully loaded or generated.
	 */
	bool LoadOrGenerateFlowmap(bool useMips = true);
	/**
	 * @brief Deletes existing flowmap files, regenerates the flowmap, and loads it.
	 * @param useMips Whether to generate mipmaps for the flowmap texture.
	 * @return True if the flowmap was successfully regenerated and loaded.
	 */
	bool RegenerateAndLoadFlowmap(bool useMips = true);

private:
	RE::NiPointer<RE::NiSourceTexture> flowmapTex = nullptr;
	int32_t width = 0;
	int32_t height = 0;
	float invWidth = 0.0f;
	float invHeight = 0.0f;
	int32_t offsetX = 0;
	int32_t offsetY = 0;

	bool LoadFlowmap();
	static bool GenerateFlowmap(bool useMips);
};