// Single translation unit that compiles stb_image.
//
// stb_image is a header with its implementation inside, so the implementation is
// defined here and nowhere else; TextureRegistry.cpp and ThumbnailCache.cpp include it
// for the declarations only. It used to be defined in TextureRegistry.cpp, which is the
// engine's own code and so is compiled at the engine's warning level - and stb's
// "variable 'invalid_chunk' set but not used" was reported as the engine's warning.
// Kept apart for the same reason as StbVorbisImplementation.cpp, TinyGltfImplementation.cpp
// and VmaImplementation.cpp: CMakeLists.txt silences this TU's warnings without silencing
// the loader that uses it.
//
// STBI_FAILURE_USERMSG makes stbi_failure_reason() return sentences a person can read
// ("unknown PNG chunk type") instead of terse codes; it affects the implementation only,
// which is why it lives with it.
//
// The pragmas repeat the silence for MSVC inside the file, because CMake's /W0 reaches
// only a build that goes through VENDORED_SOURCES - see StbVorbisImplementation.cpp.

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
