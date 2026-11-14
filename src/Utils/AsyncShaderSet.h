#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Util
{
	/**
	 * @brief Shaders compiled on the shader cache's pool and handed to their targets on the owner's thread.
	 * Only Add and Publish write the targets, so the owner reads them without locking.
	 */
	class AsyncShaderSet
	{
	public:
		using Defines = std::vector<std::pair<const char*, const char*>>;

		/**
		 * @brief Clears a_target and queues its compile; the shader lands at the first Publish after it finishes.
		 * @param a_defines Name/value pairs that must outlive the compile (string literals do).
		 */
		template <class T>
		void Add(winrt::com_ptr<T>& a_target, std::wstring a_path, Defines a_defines = {}, std::string a_entry = "main")
		{
			a_target = nullptr;
			Enqueue(std::move(a_path), std::move(a_entry), std::move(a_defines), StageOf<T>(),
				[&a_target](ID3D11DeviceChild* a_shader) { a_target.attach(static_cast<T*>(a_shader)); });
		}

		/** @brief Hands every finished shader to its target. */
		void Publish();

		/** @brief True while a queued compile has not been published. */
		bool Pending() const { return outstanding != 0; }

		/** @brief Forgets every queued compile; results still in flight are released on arrival. */
		void Cancel();

	private:
		/** @brief Order matches SIE::ShaderCache::StandaloneShaderClass. */
		enum class Stage
		{
			Vertex,
			Pixel,
			Compute
		};

		template <class T>
		static constexpr Stage StageOf()
		{
			if constexpr (std::is_same_v<T, ID3D11VertexShader>)
				return Stage::Vertex;
			else if constexpr (std::is_same_v<T, ID3D11PixelShader>)
				return Stage::Pixel;
			else {
				static_assert(std::is_same_v<T, ID3D11ComputeShader>, "AsyncShaderSet compiles vertex, pixel and compute shaders");
				return Stage::Compute;
			}
		}

		using Assign = std::function<void(ID3D11DeviceChild*)>;

		struct Finished
		{
			Assign assign;
			ID3D11DeviceChild* shader;  ///< Owned reference, or null when the compile failed.
		};

		/** @brief State the pool's callbacks reach, kept alive by them past the set's lifetime. */
		struct Shared
		{
			std::mutex mutex;
			uint32_t generation = 0;
			std::vector<Finished> finished;
		};

		void Enqueue(std::wstring a_path, std::string a_entry, Defines a_defines, Stage a_stage, Assign a_assign);

		std::shared_ptr<Shared> shared = std::make_shared<Shared>();
		uint32_t outstanding = 0;
	};
}
