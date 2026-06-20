#pragma once
#include <array>
#include <d3d11.h>
#include <winrt/base.h>

namespace Util
{
	/**
	 * @brief Look up the matching SRV for a given render target view.
	 * @param a_rtv The render target view to look up.
	 * @return The corresponding shader resource view, or nullptr if not found.
	 */
	ID3D11ShaderResourceView* GetSRVFromRTV(ID3D11RenderTargetView* a_rtv);

	/**
	 * @brief Look up the matching RTV for a given shader resource view.
	 * @param a_srv The shader resource view to look up.
	 * @return The corresponding render target view, or nullptr if not found.
	 */
	ID3D11RenderTargetView* GetRTVFromSRV(ID3D11ShaderResourceView* a_srv);

	/**
	 * @brief Get the Skyrim render target name associated with an SRV.
	 * @param a_srv The shader resource view to identify.
	 * @return The render target enum name, or "NONE" if not found.
	 */
	std::string GetNameFromSRV(ID3D11ShaderResourceView* a_srv);

	/**
	 * @brief Get the Skyrim render target name associated with an RTV.
	 * @param a_rtv The render target view to identify.
	 * @return The render target enum name, or "NONE" if not found.
	 */
	std::string GetNameFromRTV(ID3D11RenderTargetView* a_rtv);

	/**
	 * @brief Set a debug name on a D3D11 resource for RenderDoc debuggability.
	 * @param Resource The D3D11 device child to name.
	 * @param Format A printf-style format string for the name.
	 */
	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...);

	/**
	 * @brief Compile an HLSL shader from file and create the appropriate D3D11 shader object.
	 * @param FilePath Path to the HLSL source file.
	 * @param Defines Preprocessor macro name/value pairs to pass to the compiler.
	 * @param ProgramType Shader model target (e.g. "ps_5_0", "vs_5_0", "cs_5_0").
	 * @param Program Entry point function name (defaults to "main").
	 * @return The compiled shader object, or nullptr on failure.
	 */
	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program = "main");

	/**
	 * @brief Apply an alpha-blended highlight tint to a texture via CPU staging copy.
	 * @param texture The texture to tint.
	 * @param isHighlighted When false the function is a no-op.
	 * @param highlightColor RGBA colour to blend, each component in [0, 1].
	 */
	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted, const std::array<float, 4>& highlightColor = { 1.0f, 0.5f, 0.0f, 0.3f });

	/**
	 * @brief Create an RGBA overlay texture and its render target view.
	 * @param device The D3D11 device to use.
	 * @param width Texture width in pixels.
	 * @param height Texture height in pixels.
	 * @param outTex Receives the created texture.
	 * @param outRTV Receives the created render target view.
	 * @return S_OK on success, or an error HRESULT.
	 */
	HRESULT CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV);

	/**
	 * @brief Save a GPU texture to a DDS file on disk.
	 * @param device The D3D11 device.
	 * @param context The immediate device context used for staging.
	 * @param path Destination file path (parent directories are created if needed).
	 * @param tex The texture to save.
	 * @return S_OK on success, or an error HRESULT.
	 */
	HRESULT SaveTextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, ID3D11Texture2D* tex);

	/**
	 * @brief Load a DDS texture from disk and create a texture with its SRV.
	 * @param device The D3D11 device.
	 * @param path Path to the DDS file.
	 * @param outTex Receives the loaded texture.
	 * @param outSRV Receives the shader resource view for the texture.
	 * @return S_OK on success, or an error HRESULT.
	 */
	HRESULT LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& path, ID3D11Texture2D** outTex, ID3D11ShaderResourceView** outSRV);

	/**
	 * @brief Get the current scene depth SRV, preferring terrain-blended depth when active.
	 *
	 * The caller does NOT own the returned pointer.
	 *
	 * @param prefer16bit When false (default) returns R32_FLOAT for compute shaders doing
	 *        arithmetic on depth; when true returns R16_UNORM for pixel shaders via
	 *        slot 17 / SharedData::GetDepth.
	 * @return The depth SRV, or nullptr if unavailable.
	 */
	ID3D11ShaderResourceView* GetCurrentSceneDepthSRV(bool prefer16bit = false);
}  // namespace Util
