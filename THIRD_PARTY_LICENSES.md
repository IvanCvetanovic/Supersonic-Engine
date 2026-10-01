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
| [GLM](https://github.com/g-truc/glm) | 1.0.1 | The Happy Bunny License **or** MIT, at your option | © 2005 G-Truc Creation |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.93.0 WIP, docking branch | MIT | © 2014–2026 Omar Cornut |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | 1.92.5 WIP | MIT | © 2016–2026 Cedric Guillemet and contributors |
| [stb](https://github.com/nothings/stb) (`stb_image`, `stb_image_write`, `stb_vorbis` 1.22) | `stb_image` 2.30, `stb_image_write` 1.16 | Public domain (Unlicense) **or** MIT, at your option | Sean Barrett |
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
third_party/imguizmo/ImGuizmo.h        MIT, at the top of the file
third_party/stb/stb_image.h            dual public-domain / MIT, at the foot of the file
third_party/stb/stb_image_write.h      dual public-domain / MIT, at the foot of the file
third_party/stb/stb_vorbis.c           dual public-domain / MIT, at the foot of the file
third_party/tinygltf/tiny_gltf.h       MIT, at the top of the file
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

`third_party/imgui/misc/fonts/` also holds five example typefaces that ImGui
ships beside its sources. Nothing in this engine's build or its packaged output
loads them - the editor's own faces are the two above - but they are in the tree,
so a copy of the repository redistributes them. Their licences, as ImGui's own
`docs/FONTS.md` records them:

| File | Author | License |
|---|---|---|
| `Roboto-Medium.ttf` | Christian Robertson | Apache-2.0 |
| `Cousine-Regular.ttf` | Steve Matteson (digitized data © 2010 Google Corporation) | SIL OFL 1.1 |
| `DroidSans.ttf` | Steve Matteson | Apache-2.0 |
| `ProggyTiny.ttf` | Tristan Grimmer | MIT |
| `Karla-Regular.ttf` | Jonathan Pinhorn | SIL OFL 1.1 |

## Assets

Everything under `assets/` — shaders, branding, procedural textures and the
glTF test fixtures — is the engine's own work and falls under the project
licence in [LICENSE](LICENSE). The one exception worth checking before a public
release is `assets/audio/ambient.wav`, which predates this file; confirm its
provenance if you did not author it yourself.

## Reporting an omission

If something is attributed incorrectly or is missing here, that is a bug worth
filing. Open an issue and it will be corrected.
