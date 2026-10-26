#include "Features/RemoteControl/NvPerfHost.h"

#include "Globals.h"
#include "State.h"
#include "Utils/FileSystem.h"

namespace NvPerf::Host
{
	ID3D11Device* Device()
	{
		return globals::d3d::device;
	}

	ID3D11DeviceContext* Context()
	{
		return globals::d3d::context;
	}

	std::uint64_t FrameCount()
	{
		return globals::state->frameCount;
	}

	bool FrameAnnotations()
	{
		return globals::state->frameAnnotations;
	}

	void SetFrameAnnotations(bool enabled)
	{
		globals::state->frameAnnotations = enabled;
	}

	std::filesystem::path LogDirectory()
	{
		return Util::PathHelpers::GetLogPath().parent_path();
	}

	std::filesystem::path DataDirectory()
	{
		return Util::PathHelpers::GetCommunityShaderPath();
	}
}
