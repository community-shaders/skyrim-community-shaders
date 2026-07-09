#pragma once

namespace D3DX9MathUpgrade
{
	/**
	 * @brief Redirects the game's d3dx9_42.dll math imports to vectorized DirectXMath implementations.
	 *
	 * SkyrimSE.exe imports nine matrix/vector/plane functions from the 2009-era D3DX9 utility
	 * library and calls them on the per-pass geometry-setup path (BSLightingShader /
	 * BSGrassShader / BSWaterShader / BSSkyShader / BSUtilityShader "Func6", shadow-light
	 * parameter setup, volumetric-lighting params). Live render-thread IP-sampling showed
	 * d3dx9_42.dll costing ~2% of busy-thread samples, with D3DXMatrixTranspose the hottest
	 * entry. DirectXMath provides drop-in SIMD equivalents (same row-major, row-vector
	 * conventions), so the imports are swapped at the executable's IAT by name — version
	 * independent, and it covers every engine call site because all of them route through
	 * the import thunks.
	 *
	 * D3DX edge-case semantics are preserved: D3DXMatrixInverse returns nullptr on a
	 * singular matrix without touching the output, and the normalize functions return a
	 * zero vector/plane for zero-length input.
	 *
	 * Disable with CS_NO_D3DX_UPGRADE=1 for A/B testing.
	 */
	void Install();

	/**
	 * @brief Re-runs the process-wide import sweep (idempotent).
	 *
	 * The load-time sweep only sees modules already in memory; SKSE plugins that load after
	 * CommunityShaders and also import D3DX9 math get their tables rewritten by calling this
	 * again once all plugins are loaded (kDataLoaded).
	 * @param reason Short label for the log line.
	 */
	void Sweep(const char* reason);
}
