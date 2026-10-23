// Single translation unit that compiles dr_mp3.
//
// dr_mp3 is a single header with its implementation inside, selected by a macro,
// so the macro is defined here and nowhere else; Mp3Decode.cpp includes the
// header for its declarations only. Kept apart for the same reason as
// StbVorbisImplementation.cpp, TinyGltfImplementation.cpp and
// VmaImplementation.cpp: CMakeLists.txt silences this TU's warnings without
// silencing the decoder that uses it.
//
// DR_MP3_NO_STDIO: the engine opens its files itself (AudioClip::LoadMp3 reads
// them whole, as LoadOgg does), so the library's own <stdio> file functions would
// be dead weight and an extra way for a path to be opened.
//
// The pragmas repeat the silence for MSVC inside the file, because CMake's /W0
// reaches only a build that goes through VENDORED_SOURCES - a single-file
// compile check at /W4 would otherwise report somebody else's code as the
// engine's.

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define DR_MP3_NO_STDIO
#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
