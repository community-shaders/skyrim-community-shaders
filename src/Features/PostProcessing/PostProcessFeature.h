#pragma once

#include "Feature.h"

struct PostProcessing;

struct PostProcessFeature
{
	virtual ~PostProcessFeature() = default;

	bool enabled = true;
	PostProcessing* owner = nullptr;

	virtual std::string GetType() const = 0;
	std::string name;
	virtual std::string GetDesc() const = 0;
	virtual bool SupportsVR() const { return true; }
	virtual bool DrawBeforeUpscaling() const { return false; }
	virtual bool DrawAfterColorGrading() const { return false; }
	virtual bool DisableInMainLoadingMenu() const { return false; }

	/// Whether this feature is visible in the menu. Hidden features (e.g. composite passes) return false.
	virtual bool IsVisible() const { return true; }

	/// Whether this feature's enabled state is automatically managed based on other features.
	virtual bool IsAutoEnabled() const { return false; }

	/// Called each frame for auto-enabled features to update their enabled state.
	virtual void UpdateAutoEnabled() {}

	/// Whether this feature writes its result back to the main pipeline texture.
	/// If false, the feature performs internal work but does not replace inout_tex.
	virtual bool WritesToMainTexture() const { return true; }

	virtual inline void SetupResources() = 0;
	virtual void ClearShaderCache() = 0;
	virtual void RestoreDefaultSettings() = 0;

	virtual void LoadSettings(json& o_json) = 0;
	virtual void SaveSettings(json& o_json) = 0;
	virtual void DrawSettings() = 0;

	struct TextureInfo
	{
		ID3D11Texture2D* tex = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
	};
	virtual void Draw(TextureInfo& inout_tex) = 0;  // read from last pass, do the thing, and replace it with output texture

	virtual inline void Reset() {};
};
