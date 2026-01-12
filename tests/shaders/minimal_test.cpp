// Explicit main() function for shader tests
// NOTE: Catch2WithMain has linking issues with CMake 4.0, so we provide our own main()
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
	return Catch::Session().run(argc, argv);
}
