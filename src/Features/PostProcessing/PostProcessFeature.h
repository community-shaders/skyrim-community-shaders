#pragma once

#include "Feature.h"

struct PostProcessFeatureConstructor;

struct PostProcessFeature
{
	virtual ~PostProcessFeature() = default;

	bool enabled = true;

	virtual std::string GetType() const = 0;
	std::string name;
	virtual std::string GetDesc() const = 0;
	virtual bool SupportsVR() const { return true; }

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

	virtual inline void Reset(){};
};

struct PostProcessFeatureConstructor
{
	std::function<PostProcessFeature*()> fn;
	std::string name;
	std::string desc;
	static const ankerl::unordered_dense::map<std::string, PostProcessFeatureConstructor>& GetFeatureConstructors();
};