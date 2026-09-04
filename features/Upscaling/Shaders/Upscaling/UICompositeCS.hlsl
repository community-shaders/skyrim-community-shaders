/**
 * @file UICompositeCS.hlsl
 * @brief Composites the premultiplied-alpha UI texture onto the HUD-less back buffer.
 *
 * XeSS-FG composites the UI onto *interpolated* frames only; the frame the application
 * presents is displayed verbatim. Because the HDR/FG path renders the UI into a separate
 * texture and leaves the wrapped back buffer HUD-less, the presented frame has to be
 * composited here or the UI would only appear on generated frames (half-rate flicker).
 *
 * The blend must be bit-identical to the one XeSS-FG applies internally
 * (xess_fg_developer_guide_english.md, "UI Composition Modes"):
 *
 *     FinalColor.RGB = UIonly.RGB + (1 - UIonly.Alpha) * HUDlessColor.RGB
 *
 * so it runs on whatever encoding both textures already share - gamma in SDR, PQ/BT.2020
 * in HDR (UIBrightnessCS encodes the UI before this pass). Any other blend space would
 * make real and generated frames disagree on semi-transparent UI.
 */

Texture2D<float4> HUDLessTex : register(t0);
Texture2D<float4> UITex : register(t1);

RWTexture2D<float4> CompositeOutput : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint width, height;
	CompositeOutput.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float4 hudless = HUDLessTex[dispatchID.xy];
	float4 ui = UITex[dispatchID.xy];

	float3 composited = ui.rgb + (1.0 - ui.a) * hudless.rgb;

	CompositeOutput[dispatchID.xy] = float4(saturate(composited), 1.0);
}
