#include "StreamlineRuntime.h"

bool StreamlineRuntime::Load(const std::filesystem::path& a_path)
{
	if (interposer)
		return true;
	interposer = LoadLibraryExW(a_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	return interposer != nullptr;
}

void StreamlineRuntime::Unload()
{
	if (interposer) {
		FreeLibrary(interposer);
		interposer = nullptr;
	}
}
