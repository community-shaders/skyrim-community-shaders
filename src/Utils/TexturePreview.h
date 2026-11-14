#pragma once

#include "Buffer.h"

class TexturePreview
{
public:
	/** @brief Displays a 2D texture or volume slice with mip, channel and range controls. */
	void Draw(ID3D11ShaderResourceView* srv, const char* id, const char* label, float scale, bool scalar = false);
	/** @brief Displays a responsive scalar map; optionally decodes density-weighted channels. */
	void DrawMap(ID3D11ShaderResourceView* srv, const char* id, uint32_t channel, bool densityWeighted, float width);
	/** @brief Releases the preview shader so it can be requested again. */
	void ClearShaderCache();

private:
	struct View
	{
		eastl::unique_ptr<Texture2D> texture;
		int mip = 0;
		int slice = 0;
		int channel = 0;
		float2 range = { 0.f, 1.f };
	};
	struct Parameters
	{
		uint32_t mip, slice, channel, volume;
		float2 range;
		uint32_t densityWeighted = 0;
		uint32_t padding = 0;
	};
	static_assert(sizeof(Parameters) == 32);
	ID3D11ShaderResourceView* Render(ID3D11ShaderResourceView* srv, View& view, const Parameters& data, uint32_t width, uint32_t height);
	ankerl::unordered_dense::map<std::string, View> views;
	eastl::unique_ptr<ConstantBuffer> constants;
	winrt::com_ptr<ID3D11ComputeShader> shader;
	bool requested = false;
};
