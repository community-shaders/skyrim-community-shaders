#pragma once

#include "Globals.h"

namespace UpscalingCamera
{
	/** @brief The world camera's unjittered transforms for a frame, in row-vector (DirectXMath) convention. */
	struct WorldCamera
	{
		DirectX::SimpleMath::Matrix viewInverse;
		DirectX::SimpleMath::Matrix projectionUnjittered;
		DirectX::SimpleMath::Vector3 posAdjust;
	};

	namespace detail
	{
		// Flat (SE/AE) layout of a BSGraphics::State::cameraDataCacheA entry; the ALL build of CommonLib compiles these fields out.
		struct CameraStateEntry
		{
			RE::NiCamera* referenceCamera;
			std::uint64_t pad08;
			RE::BSGraphics::ViewData viewData;
			RE::NiPoint3 posAdjust;
			RE::NiPoint3 currentPosAdjust;
			RE::NiPoint3 previousPosAdjust;
			bool useJitter;
			std::uint32_t numData;
			std::uint32_t pad28C;
		};
		static_assert(offsetof(CameraStateEntry, viewData) == 0x10);
		static_assert(offsetof(CameraStateEntry, posAdjust) == 0x260);
		static_assert(offsetof(CameraStateEntry, previousPosAdjust) == 0x278);
		static_assert(sizeof(CameraStateEntry) == 0x290);

		/**
		 * @brief Reads the world camera from the engine camera table, which keys entries on (camera, useJitter):
		 * the jittered entry the scene draws with, else the unjittered one.
		 * @return False when the table has no entry for the world camera.
		 */
		inline bool ReadCameraTable(WorldCamera& a_out)
		{
			const auto* camera = RE::Main::WorldRootCamera();
			if (!camera || !globals::game::graphicsState)
				return false;
			const auto& table = globals::game::graphicsState->GetRuntimeData().cameraDataCacheA;
			const auto* entries = reinterpret_cast<const CameraStateEntry*>(table.data());
			const CameraStateEntry* match = nullptr;
			for (std::uint32_t i = 0; i < table.size(); ++i) {
				const auto& entry = entries[i];
				if (entry.referenceCamera != camera)
					continue;
				match = &entry;
				if (entry.useJitter)
					break;
			}
			if (!match)
				return false;
			// The table stores row-vector matrices: viewProj = view * proj, so proj = view^-1 * viewProj.
			const auto& view = match->viewData;
			const DirectX::SimpleMath::Matrix viewMatrix = view.viewMat;
			const DirectX::SimpleMath::Matrix viewProjection = view.viewProjMatrixUnjittered;
			a_out.viewInverse = viewMatrix.Invert();
			a_out.projectionUnjittered = a_out.viewInverse * viewProjection;
			a_out.posAdjust = DirectX::SimpleMath::Vector3(match->posAdjust.x, match->posAdjust.y, match->posAdjust.z);
			return true;
		}
	}

	/**
	 * @brief The world camera for upscaling and frame generation.
	 *
	 * frameBufferCached holds whichever camera uploaded PerFrame last, which can be the first-person or a
	 * screen-space camera. Reads the world camera's entry from the engine camera table, and falls
	 * back to the PerFrame snapshot taken after the world batches draw when the table has no entry.
	 */
	inline WorldCamera GetWorldCamera()
	{
		WorldCamera camera;
		if (detail::ReadCameraTable(camera))
			return camera;
		// The constant buffer stores column-vector matrices.
		const auto& frameBuffer = globals::game::GetWorldFrameBuffer();
		const auto& posAdjust = frameBuffer.GetCameraPosAdjust();
		camera.viewInverse = frameBuffer.GetCameraViewInverse().Transpose();
		camera.projectionUnjittered = frameBuffer.GetCameraProjUnjittered().Transpose();
		camera.posAdjust = DirectX::SimpleMath::Vector3(posAdjust.x, posAdjust.y, posAdjust.z);
		return camera;
	}
}
