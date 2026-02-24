#pragma once
#include <cstdint>
#include <string>

namespace VRDetection
{
	enum class RuntimeType
	{
		Unknown,
		SteamVR,
		OpenComposite
	};

	struct OpenVRDetectionResult
	{
		bool isAvailable = false;
		bool isCompatible = false;

		// Interface probing results
		bool hasOverlayInterface = false;
		bool hasSystemInterface = false;
		bool hasCompositorInterface = false;

		// File-based info
		std::string dllPath;
		std::string version;
		uint64_t fileSize = 0;
		std::string modificationTime;

		// Detection metadata
		RuntimeType runtimeType = RuntimeType::Unknown;
		bool probingSucceeded = false;
	};

	// Runtime interface probing via VR_IsInterfaceVersionValid
	bool ProbeRuntimeInterfaces(OpenVRDetectionResult& result);

	// Gather DLL metadata (path, version, size, timestamp)
	void GatherDLLInfo(OpenVRDetectionResult& result);

	// Detect runtime type (SteamVR vs OpenComposite)
	RuntimeType DetectRuntimeType(const std::string& dllPath, const std::string& version, uint64_t fileSize);

	// Full detection via interface probing
	OpenVRDetectionResult Detect();

	const char* RuntimeTypeToString(RuntimeType type);
}
