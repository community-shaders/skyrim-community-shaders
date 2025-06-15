#pragma once

// Forward declarations
struct ID3D11ShaderResourceView;
struct ImVec2;
class Menu;

namespace Util
{

	/**
	 * Usage:
	 * if (auto _tt = Util::HoverTooltipWrapper()){
	 *     ImGui::Text("What the tooltip says.");
	 * }
	*/
	class HoverTooltipWrapper
	{
	private:
		bool hovered;

	public:
		HoverTooltipWrapper();
		~HoverTooltipWrapper();
		inline operator bool() { return hovered; }
	};

	/**
	 * Usage:
	 * {
     *      auto _ = DisableGuard(disableThis);
     *      ... Some settings ...
     * }
	*/
	class DisableGuard
	{
	private:
		bool disable;

	public:
		DisableGuard(bool disable);
		~DisableGuard();
	};

	bool PercentageSlider(const char* label, float* data, float lb = 0.f, float ub = 100.f, const char* format = "%.1f %%");
	ImVec2 GetNativeViewportSizeScaled(float scale);

	// Icon loading functions
	bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, ImVec2& out_size);
	bool InitializeMenuIcons(Menu* menu);

	// Text rendering helpers for clearer title text
	void RenderSharpText(const char* text, bool alignToPixelGrid = true, float scale = 1.0f);
	void RenderAlignedTextWithLogo(ID3D11ShaderResourceView* logoTexture, const ImVec2& logoSize, const char* text, float textScale = 1.5f);
}  // namespace Util
