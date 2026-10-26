#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct LUT : PostProcessFeature
{
	virtual inline std::string GetType() const override { return "LUT"; }
	virtual inline std::string GetDisplayName() const override { return T("feature.post_processing.lut.name", "LUT"); }
	virtual inline std::string GetDesc() const override { return T("feature.post_processing.lut.description", "Look-up table application."); }
	virtual inline bool DrawAfterColorGrading() const override { return true; }

	int LutType = -1;  // -1 - null, 0 - 1d luma, 1 - 1d per channel, 2 - 3d in 2d, 3 - 3d

	std::string errMsg = "";
	std::string tempPath = "";
	/// Source of the last disk read, so scene applies reload only when the path or active pack changes.
	std::string attemptedPath;
	std::string attemptedPackId;

	struct Settings
	{
		std::string LutPath = "";
		float3 InputMin{ 0.f };
		float3 InputMax{ 1.f };
	} settings;

	struct alignas(16) LUTCB
	{
		float3 InputMin;
		float pad;
		float3 InputMax;
		int LutType;
	};
	eastl::unique_ptr<ConstantBuffer> lutCB = nullptr;

	eastl::unique_ptr<Texture2D> texLUT2D = nullptr;
	eastl::unique_ptr<Texture3D> texLUT3D = nullptr;
	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	winrt::com_ptr<ID3D11PixelShader> lutPS = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileRasterShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;
	void UpdateTexture();

	virtual void DrawSettings() override;
	void inline Clear()
	{
		LutType = -1;
		settings.LutPath = "";
		errMsg = "";
		attemptedPath.clear();
		if (texLUT2D)
			texLUT2D.reset();
		if (texLUT3D)
			texLUT3D.reset();
	}
	/** @brief Loads a LUT from a game-relative, absolute or active-pack-relative path. A failed load keeps the path. */
	void ReadTexture(std::string requestedPath);
	/** @brief Error text for a LUT file that cannot be loaded, or empty when its extension and existence check out. */
	static std::string ValidateLutFile(const std::filesystem::path& resolvedPath);

	virtual void Draw(TextureInfo&) override;
};
