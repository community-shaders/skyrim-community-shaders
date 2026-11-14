#include "AsyncShaderSet.h"

#include "ShaderCache.h"

namespace Util
{
	void AsyncShaderSet::Enqueue(std::wstring a_path, std::string a_entry, Defines a_defines, Stage a_stage, Assign a_assign)
	{
		static_assert(static_cast<int>(Stage::Compute) == static_cast<int>(SIE::ShaderCache::StandaloneShaderClass::Compute));
		++outstanding;
		uint32_t generation;
		{
			std::scoped_lock lock(shared->mutex);
			generation = shared->generation;
		}
		globals::shaderCache->EnqueueStandaloneShaderCompile(std::move(a_path), std::move(a_entry), std::move(a_defines),
			static_cast<SIE::ShaderCache::StandaloneShaderClass>(a_stage),
			[shared = shared, generation, assign = std::move(a_assign)](ID3D11DeviceChild* a_shader) mutable {
				std::scoped_lock lock(shared->mutex);
				if (shared->generation != generation) {
					if (a_shader)
						a_shader->Release();
					return;
				}
				shared->finished.push_back({ std::move(assign), a_shader });
			});
	}

	void AsyncShaderSet::Publish()
	{
		if (!outstanding)
			return;
		std::vector<Finished> finished;
		{
			std::scoped_lock lock(shared->mutex);
			finished.swap(shared->finished);
		}
		assert(finished.size() <= outstanding);
		outstanding -= static_cast<uint32_t>(finished.size());
		for (auto& [assign, shader] : finished) {
			if (shader)
				assign(shader);
		}
	}

	void AsyncShaderSet::Cancel()
	{
		std::scoped_lock lock(shared->mutex);
		++shared->generation;
		for (const auto& entry : shared->finished) {
			if (entry.shader)
				entry.shader->Release();
		}
		shared->finished.clear();
		outstanding = 0;
	}
}
