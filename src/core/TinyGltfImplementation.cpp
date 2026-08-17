// Single translation unit that compiles tinygltf.
//
// Kept separate from GltfLoader.cpp so the vendored library's warnings can be
// silenced in CMakeLists.txt without also silencing warnings in the loader that
// uses it. Same arrangement as VmaImplementation.cpp.
//
// TINYGLTF_NO_STB_IMAGE matters: tinygltf ships its own stb_image copy, and
// TextureRegistry.cpp already defines STB_IMAGE_IMPLEMENTATION. Two definitions
// would collide at link time. Images are loaded from resolved paths by
// TextureRegistry instead.

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include <tiny_gltf.h>
