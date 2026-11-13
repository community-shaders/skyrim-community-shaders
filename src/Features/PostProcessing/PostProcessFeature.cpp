#include "PostProcessFeature.h"

#include "ShaderCache.h"

namespace
{
	template <typename ShaderT>
	void EnqueueStandaloneCompile(
		const PostProcessFeature& feature,
		const std::filesystem::path& path,
		const std::string& entry,
		const std::vector<std::pair<const char*, const char*>>& defines,
		SIE::ShaderCache::StandaloneShaderClass shaderClass,
		winrt::com_ptr<ShaderT>* programPtr)
	{
		globals::shaderCache->EnqueueStandaloneShaderCompile(
			path.wstring(), entry, defines, shaderClass,
			[weak = feature.weak_from_this(), ptr = programPtr, generation = feature.shaderGeneration.load(std::memory_order_relaxed)](ID3D11DeviceChild* shader) {
				if (!shader)
					return;
				auto self = weak.lock();
				if (!self) {
					shader->Release();
					return;
				}
				std::lock_guard lock(self->shaderMutex);
				if (self->shaderGeneration.load(std::memory_order_relaxed) != generation) {
					shader->Release();
					return;
				}
				ptr->attach(static_cast<ShaderT*>(shader));
			});
	}
}

void PostProcessFeature::CompileComputeShadersAsync(std::wstring_view sourceDir, std::span<const ComputeShaderCompileInfo> infos)
{
	for (const auto& info : infos) {
		auto path = std::filesystem::path(sourceDir) / info.filename;
		EnqueueStandaloneCompile(*this, path, info.entry, info.defines,
			SIE::ShaderCache::StandaloneShaderClass::Compute, info.programPtr);
	}
}

void PostProcessFeature::CompileRasterShadersAsync(
	std::wstring_view sourceDir,
	std::span<const VertexShaderCompileInfo> vsInfos,
	std::span<const PixelShaderCompileInfo> psInfos)
{
	for (const auto& info : vsInfos) {
		auto path = std::filesystem::path(sourceDir) / info.filename;
		EnqueueStandaloneCompile(*this, path, info.entry, info.defines,
			SIE::ShaderCache::StandaloneShaderClass::Vertex, info.programPtr);
	}
	for (const auto& info : psInfos) {
		auto path = std::filesystem::path(sourceDir) / info.filename;
		EnqueueStandaloneCompile(*this, path, info.entry, info.defines,
			SIE::ShaderCache::StandaloneShaderClass::Pixel, info.programPtr);
	}
}
