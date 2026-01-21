// Common utilities for shader tests
#pragma once

#include <Framework/ShaderTestFixture.h>
#include <filesystem>

#ifdef _WIN32
#	include <windows.h>
#endif

namespace ShaderTest
{
	/// Get the directory containing the test executable
	/// This is portable across different working directories and drive letters
	inline std::filesystem::path GetExecutableDirectory()
	{
#ifdef _WIN32
		wchar_t buffer[MAX_PATH];
		GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(buffer).parent_path();
#else
		// For non-Windows platforms, fall back to current_path
		// (though these tests are Windows-only due to D3D12)
		return std::filesystem::current_path();
#endif
	}

	/// Get the shader directory mappings for tests
	/// Looks for Shaders directory relative to the test executable
	inline std::vector<stf::VirtualShaderDirectoryMapping> GetShaderDirectoryMappings()
	{
		// Map /Shaders to the Shaders directory next to the executable
		// The CMake build copies shaders to the same directory as the executable
		auto exeDir = GetExecutableDirectory();
		auto shaderPath = exeDir / "Shaders";

		// Verify the Shaders directory exists
		if (!std::filesystem::exists(shaderPath)) {
			throw std::runtime_error(
				"Shaders directory not found at: " + shaderPath.string() +
				"\n"
				"Expected shader files to be copied by CMake build.\n"
				"Executable directory: " +
				exeDir.string());
		}

		// Return both /Shaders and /Test mappings
		// /Test is used by ShaderTestFramework's built-in test utilities
		return {
			stf::VirtualShaderDirectoryMapping{ "/Shaders", shaderPath },
			stf::VirtualShaderDirectoryMapping{ "/Test", shaderPath }
		};
	}

	/// Get standard fixture description for hardware testing
	inline stf::ShaderTestFixture::FixtureDesc GetFixtureDesc()
	{
		return stf::ShaderTestFixture::FixtureDesc{
			.Mappings = GetShaderDirectoryMappings(),
			.GPUDeviceParams{
				.DebugLevel = stf::GPUDevice::EDebugLevel::Off,       // Disable debug layer (may conflict with some drivers)
				.DeviceType = stf::GPUDevice::EDeviceType::Hardware,  // Use real GPU instead of WARP
				.EnableGPUCapture = false }
		};
	}
}
