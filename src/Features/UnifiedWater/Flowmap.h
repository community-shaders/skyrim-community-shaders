#pragma once

class WaterCache;

class Flowmap
{
public:
	bool TryGetFlowmap(RE::NiPointer<RE::NiSourceTexture>& outFlowmapTex) const;
	int32_t GetWidth() const { return width; }
	int32_t GetHeight() const { return height; }
	float GetInverseWidth() const { return invWidth; }
	float GetInverseHeight() const { return invHeight; }
	int32_t GetOffsetX() const { return offsetX; }
	int32_t GetOffsetY() const { return offsetY; }
	void Reset();

	bool LoadOrGenerateFlowmap(WaterCache* waterCache, bool useMips = true);
	bool RegenerateAndLoadFlowmap(WaterCache* waterCache, bool useMips = true);

private:
	RE::NiPointer<RE::NiSourceTexture> flowmapTex = nullptr;
	int32_t width = 0;
	int32_t height = 0;
	float invWidth = 0.0f;
	float invHeight = 0.0f;
	int32_t offsetX = 0;
	int32_t offsetY = 0;

	bool LoadFlowmap();
	static bool GenerateFlowmap(WaterCache* waterCache, bool useMips);
};