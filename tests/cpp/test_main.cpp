// Explicit main() for C++ unit tests.
// Catch2WithMain has linker issues with CMake 4.0 (see tests/shaders/minimal_test.cpp);
// we provide our own session entry point.
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
	return Catch::Session().run(argc, argv);
}
