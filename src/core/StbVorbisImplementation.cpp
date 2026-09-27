// Single translation unit that compiles stb_vorbis.
//
// stb_vorbis ships as a .c file with its implementation inside, so the file is
// included whole here and nowhere else; AudioClip.cpp includes it with
// STB_VORBIS_HEADER_ONLY for the declarations. Kept apart for the same reason
// as TinyGltfImplementation.cpp and VmaImplementation.cpp: CMakeLists.txt
// silences this TU's warnings without silencing the loader that uses it.
//
// The pragmas repeat that silence for MSVC inside the file, because CMake's
// /W0 reaches only a build that goes through VENDORED_SOURCES - a single-file
// compile check at /W4 would otherwise report somebody else's code as the
// engine's. The level alone is not enough: C4701 comes from the code
// generator's flow analysis, which the push does not reach, so it is named.

#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4701)
#endif

#include <stb_vorbis.c>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
