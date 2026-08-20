# Contributing

Thanks for looking. This is a solo project that is meant to be readable, so the
bar for a change is less "does it work" than "will the next person understand
why it is like that".

## Before you start

- **Build and run the tests first.** `ctest --test-dir build -C Debug` should be
  thirty green suites before you change anything. If it is not, that is the
  bug worth reporting.
- **Install the Vulkan SDK.** Without the validation layers, invalid Vulkan
  usage does not produce an error — it produces an access violation, or nothing
  at all until the code runs on someone else's driver. Two showstopper bugs
  survived six commits in this repository for exactly that reason. See
  [AGENTS.md](AGENTS.md).

## What a change should carry

**A test, if the bug could come back.** Every suite in `tests/` exists because
something actually broke; the header comment on each one says what. A fix
without a test that fails before it is a fix that gets undone.

**A reason, in the code.** Comments here explain *why*, not *what* — the
constraint, the alternative that was rejected, the failure that motivated the
line. `// increment i` is noise; `// Cull against the LIGHT's frustum, not the
camera's: an object behind the viewer can still cast into view` is the reason
the next person does not "simplify" it.

**Zero warnings.** MSVC builds at `/W4`, GCC and Clang at `-Wall -Wextra
-Wpedantic`. Vendored code under `third_party/` is excluded from that; the
project's own code never is.

## Things that are easy to get wrong here

- **Shaders.** CMake regenerates the `.spv` blobs only when it finds `glslc`.
  Without it, your GLSL edit is silently ignored and the committed blob is used
  instead. Commit the regenerated blobs alongside the source.
- **The uniform buffer layout.** `UniformBufferObject` is declared in
  `VulkanPipeline.hpp` and again in `shader.vert`, `shader.frag` and
  `grid.vert`. All four move together, or the shaders read the buffer at the
  wrong offsets — which produces wrong lighting, not an error.
- **Frame ordering.** `SupersonicApp::Run` is commented where it is
  load-bearing. The offscreen target may only be recreated at the top of the
  frame, and world transforms are resolved twice on purpose.
- **Serialization.** A component that is not written by `SceneSerializer` is
  silently destroyed by Play/Stop and by undo. If you add a component with
  authored data, add it to the serializer and to `test_undo` in the same change.

## Style

Four spaces, no tabs, `PascalCase` for types and public methods, `camelCase`
for locals and private members, `m_` prefix on member variables, `k` prefix on
compile-time constants. Match the file you are editing; it is more important
that a file be internally consistent than that it match a rule.

## Commits and pull requests

Write the commit message for someone reading `git log` in a year: what changed,
and what was wrong before. CI is set to manual dispatch to conserve Actions minutes; run it from the
Actions tab before opening a PR. It builds on Windows and Linux and runs the suites on
both, so a pull request that goes red there will not be merged until it is
green.

## Licence

Contributions are accepted under the [MIT licence](LICENSE) that covers the rest
of the project.
