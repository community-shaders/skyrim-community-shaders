#pragma once

#include "Feature.h"
#include "ShaderCache.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <span>

struct PostProcessing;

/// Owned via shared_ptr: async compile callbacks (CompileComputeShadersAsync)
/// hold a weak_ptr to `this`, so one firing after teardown safely no-ops.
struct PostProcessFeature : public std::enable_shared_from_this<PostProcessFeature>
{
	virtual ~PostProcessFeature() = default;

	bool enabled = true;
	PostProcessing* owner = nullptr;

	/// One compute-shader entry point to compile via CompileComputeShadersAsync().
	struct ComputeShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry = "main";
	};

	/// One vertex-shader entry point to compile via CompileRasterShadersAsync().
	struct VertexShaderCompileInfo
	{
		winrt::com_ptr<ID3D11VertexShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry = "main";
	};

	/// One pixel-shader entry point to compile via CompileRasterShadersAsync().
	struct PixelShaderCompileInfo
	{
		winrt::com_ptr<ID3D11PixelShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry = "main";
	};

	/// Compile callbacks fire on the shader-cache pool while Draw() reads on the
	/// render thread; guards every compute-shader com_ptr member below.
	mutable std::mutex shaderMutex;

	/// Bumped by ClearShaderCache; a compile callback started before the bump
	/// discards its result instead of attaching a superseded shader.
	std::atomic<uint64_t> shaderGeneration{ 0 };

	/// @brief Call at the start of ClearShaderCache(), before re-enqueuing
	///        compiles, so in-flight callbacks from the previous generation
	///        release their shader instead of attaching it.
	void BumpShaderGeneration() { shaderGeneration.fetch_add(1, std::memory_order_relaxed); }

	/// @brief Enqueues every entry in `infos` on ShaderCache's async compute-shader
	///        path (see ShaderCache::EnqueueComputeShaderCompile). Returns
	///        immediately; each shader attaches to its programPtr under
	///        shaderMutex once its own compile/cache-load completes, unless
	///        ClearShaderCache() has since bumped shaderGeneration (result
	///        released instead) or `this` has since been destroyed (callback
	///        captures a weak_ptr, not `this`, and no-ops if it can't lock).
	/// @param sourceDir Directory the entries' filenames are relative to (e.g.
	///        "Data\\Shaders\\PostProcessing\\DoF").
	void CompileComputeShadersAsync(std::wstring_view sourceDir, std::span<const ComputeShaderCompileInfo> infos);

	/// @brief Same as CompileComputeShadersAsync(), but for the raster (VS/PS)
	///        fullscreen passes. Entries compile through
	///        ShaderCache::EnqueueStandaloneShaderCompile with the matching shader
	///        class and attach under the same shaderMutex / shaderGeneration contract.
	void CompileRasterShadersAsync(
		std::wstring_view sourceDir,
		std::span<const VertexShaderCompileInfo> vsInfos,
		std::span<const PixelShaderCompileInfo> psInfos);

	/// @brief Thread-safe readiness check for a Draw() dispatch/draw: true only if
	///        every listed shader has already attached. Takes shaderMutex.
	template <typename ShaderT>
	bool AllShadersReady(std::initializer_list<const winrt::com_ptr<ShaderT>*> shaders) const
	{
		std::lock_guard lock(shaderMutex);
		for (auto* shader : shaders) {
			if (!*shader)
				return false;
		}
		return true;
	}

	virtual std::string GetType() const = 0;
	virtual std::string GetDisplayName() const { return GetType(); }
	std::string name;
	virtual std::string GetDesc() const = 0;
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
