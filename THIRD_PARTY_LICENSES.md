# Third-Party Licenses

Supersonic Engine vendors every dependency under `third_party/` rather than
fetching them at build time. That means a copy of this repository redistributes
them, and their copyright notices have to travel with it — which is what this
file is for.

All of them are permissive. None is copyleft, and none imposes a condition on
what you may build with the engine beyond keeping the notices below.

| Component | Version | License | Copyright |
|---|---|---|---|
| [EnTT](https://github.com/skypjack/entt) | 3.13.2 | MIT | © 2017–2023 Michele Caini |
| [GLFW](https://www.glfw.org/) | 3.4 | zlib/libpng | © 2002–2006 Marcus Geelnard, © 2006–2019 Camilla Löwy |
| [GLM](https://github.com/g-truc/glm) | — | The Happy Bunny License **or** MIT, at your option | © 2005 G-Truc Creation |
| [Dear ImGui](https://github.com/ocornut/imgui) | docking branch | MIT | © 2014–2026 Omar Cornut |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | — | MIT | © 2016–2026 Cedric Guillemet and contributors |
| [stb](https://github.com/nothings/stb) (`stb_image`, `stb_image_write`, `stb_vorbis` 1.22) | — | Public domain (Unlicense) **or** MIT, at your option | Sean Barrett |
| [dr_mp3](https://github.com/mackron/dr_libs) (`dr_libs`) | 0.7.4 | Public domain (Unlicense) **or** MIT No Attribution (MIT-0), at your option; its minimp3 core is CC0 | David Reid; minimp3 by Lion (lieff) |
| [tinygltf](https://github.com/syoyo/tinygltf) | — | MIT | © 2015–present Syoyo Fujita, Aurélien Chatelain and contributors |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | 3.1.0 | MIT | © 2017–2024 Advanced Micro Devices, Inc. |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | 1.3.290 | Apache-2.0 **or** MIT, at your option | © 2015–2023 The Khronos Group Inc. |
| [Inter](https://github.com/rsms/inter) | 4.0 | SIL Open Font License 1.1 | © 2016 The Inter Project Authors |
| [Font Awesome Free](https://fontawesome.com) (Solid) | 6.x | SIL Open Font License 1.1 (fonts) | © Fonticons, Inc. |

## Full texts

Each dependency ships its own licence text in the tree:

```
third_party/entt-3.13.2/LICENSE
third_party/glfw-3.4/LICENSE.md
third_party/glm/copying.txt
third_party/imgui/LICENSE.txt
third_party/VulkanMemoryAllocator-3.1.0/LICENSE.txt
third_party/Vulkan-Headers-1.3.290/LICENSE.md
third_party/Vulkan-Headers-1.3.290/LICENSES/          Apache-2.0.txt, MIT.txt
```

Five carry their licence in a comment in the file rather than in a separate file:

```
third_party/dr_libs/dr_mp3.h           dual public-domain (Unlicense) / MIT-0, then minimp3's CC0 note, at the foot of the file
third_party/imguizmo/ImGuizmo.h        MIT, at the top of the file
third_party/stb/stb_image.h            dual public-domain / MIT, at the foot of the file
third_party/stb/stb_vorbis.c           dual public-domain / MIT, at the foot of the file
third_party/tinygltf/tiny_gltf.h       MIT, at the top of the file
```

## dr_mp3

`third_party/dr_libs/dr_mp3.h` is the decoder behind `AudioClip::LoadMp3` on
every platform but Windows (Windows decodes MP3 through Media Foundation). It
is kept byte for byte as it was taken, and `.gitattributes` marks it `-text` so
no checkout rewrites its line endings.

| | |
|---|---|
| Version | `DRMP3_VERSION_STRING` `0.7.4`; the header's own banner reads "v0.7.4 - TBD", i.e. a development revision after v0.7.3 (Penumbra's note on its copy says it carries the fixes for an overflow and a heap-buffer over-read in the Xing/Info tag parser; that is its statement, not checked here) |
| Upstream | https://github.com/mackron/dr_libs (`dr_mp3.h`), by David Reid, based on [minimp3](https://github.com/lieff/minimp3) by Lion (lieff) |
| Taken from | Penumbra's copy at `game/third_party/dr_mp3/dr_mp3.h`, which is the file its game ships. Penumbra's `dr_mp3.c` records the upstream revision as commit `51e61d308dde6b437fce0c5fabb32cd86b40f4d7`, 2026-08-31; that revision is Penumbra's statement and was not checked against upstream when this was added. The checksum below is of the file |
| Size, sha256 | 209,166 bytes, `997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f` |
| Licence | The choice of the two alternatives below, both stated at the end of the file. Neither asks for attribution; the notices are kept here and in the file |
| Compiled | once, in `src/core/DrMp3Implementation.cpp`, with `DR_MP3_NO_STDIO` (the engine opens its files itself) and with its warnings silenced like the other vendored implementations; the engine's own decoder, `src/core/Mp3Decode.cpp`, includes only its declarations |
| A game with its own copy | The include path to this header is private to `SupersonicCore`, but the library's plain C `drmp3_*` symbols are in `SupersonicCore.lib`/`.a`. A game that links its own dr_mp3 as a static library (either order), or as an object file before the engine's archive, links; as an object file after the engine's archive it gets "multiple definition of `drmp3_*`" (GNU ld, measured against Penumbra's `dr_mp3.c`; MSVC's linker was not tried). When it moves to this engine it can drop its copy |

The licence text, as it stands at the end of the file (the minimp3 notice follows):

```
This software is available as a choice of the following licenses. Choose
whichever you prefer.

===============================================================================
ALTERNATIVE 1 - Public Domain (www.unlicense.org)
===============================================================================
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>

===============================================================================
ALTERNATIVE 2 - MIT No Attribution
===============================================================================
Copyright 2023 David Reid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

    https://github.com/lieff/minimp3
    To the extent possible under law, the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide.
    This software is distributed without any warranty.
    See <http://creativecommons.org/publicdomain/zero/1.0/>.
```

## Fonts

The editor embeds two typefaces as compressed byte arrays in
`src/editor/fonts/`, rather than loading them from disk — a packaged build has
to carry its own text, and `GamePackager` produces standalone folders.

Both are subset to what the editor draws (Latin plus thirty-six icon glyphs),
which is why the embedded arrays are a fraction of the original files. Subsetting
does not change the licence.

```
src/editor/fonts/Inter-OFL.txt              Inter, SIL OFL 1.1
src/editor/fonts/FontAwesome-LICENSE.txt    Font Awesome Free
```

Font Awesome Free is multi-licensed: the **font files are SIL OFL 1.1**, the
icons themselves are CC BY 4.0, and its code is MIT. Only the font is used here.

## Code bundled inside those dependencies

`tinygltf` vendors further components of its own, each under its own notice
inside the file:

- `third_party/tinygltf/json.hpp` — [nlohmann/json](https://github.com/nlohmann/json), MIT,
  which itself incorporates the Grisu2 float printer (MIT, © 2009 Florian Loitsch)
  and a UTF-8 decoder (MIT, © 2008–2009 Björn Höhrmann).
- Base64 decoding by René Nyffenegger, and a portion adapted from
  [dlib](http://dlib.net) (Boost Software License, © 2003 Davis E. King) — both
  noted inline in `tiny_gltf.h`.

Dear ImGui embeds the ProggyClean bitmap font by Tristan Grimmer, covered by
ImGui's own MIT licence.

## Assets

Everything under `assets/` — shaders, branding, procedural textures and the
glTF test fixtures — is the engine's own work and falls under the project
licence in [LICENSE](LICENSE). The one exception worth checking before a public
release is `assets/audio/ambient.wav`, which predates this file; confirm its
provenance if you did not author it yourself.

## Reporting an omission

If something is attributed incorrectly or is missing here, that is a bug worth
filing. Open an issue and it will be corrected.
