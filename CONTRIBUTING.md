## 🤝 Contributing

Thanks for considering a contribution. This is a from-scratch C++23 / OpenGL
voxel engine, and the project is small enough that a focused pull request gets
reviewed properly.

### Prerequisites

| | |
| --- | --- |
| Compiler | GCC >= 13, Clang >= 17, or MSVC >= 19.33 — the engine uses C++23 `<print>` |
| CMake | 3.20+ (earlier versions cannot represent `CMAKE_CXX_STANDARD 23`) |
| Libraries | GLM and GLFW — system packages, see below |
| Optional | [`just`](https://github.com/casey/just), `clang-format`, `clang-tidy` |

The compiler requirement is enforced at configure time, so an older toolchain
fails immediately with a message naming the reason rather than a confusing
`<print>` error later.

**macOS**

```bash
brew install cmake glfw glm
```

**Debian / Ubuntu**

```bash
sudo apt install build-essential cmake libgl1-mesa-dev libglfw3-dev libglm-dev
```

`libgl1-mesa-dev` matters: `CMakeLists.txt` has `find_package(OpenGL REQUIRED)`,
and a fresh Ubuntu container without it fails at configure time, before any
code is compiled.

**Windows**

```powershell
vcpkg install glfw3 glm --triplet=x64-windows
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

The triplet and the toolchain file are both required — see the
[Windows section of the README](README.md#prerequisites) for the full
sequence, including vcpkg bootstrap and the multi-config output path.

### Build

```bash
git clone https://github.com/amodhakal/linterra.git
cd linterra
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/linterra
```

Or with `just`, if you have it:

```bash
just build    # release build
just dev      # debug build with ASan + UBSan
just run      # build if needed, then launch
```

### Submodules

One submodule is declared, `vendor/metal-cpp`, for the future Metal backend. It
is not built or referenced today, so a plain clone works — but initialise it if
you touch the Metal backend:

```bash
git submodule update --init --recursive
```

Everything else is vendored and committed. Nothing is fetched at configure
time, so a clone builds offline.

### Tests

Two executables, both built by default and both run by `ctest`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- **`linterra_tests`** is the doctest unit suite for the pure, no-GL-context
  subsystems. It needs only GLM at runtime, so it runs anywhere.
- **`linterra_smoke`** covers the GL path — shader compilation, buffer and VAO
  creation, chunk mesh upload, the offscreen framebuffer, teardown. It creates a
  *hidden* window, so it still needs a windowing system. On a headless Linux
  machine:

  ```bash
  xvfb-run -a ctest --test-dir build --output-on-failure
  ```

To build the game alone:

```bash
cmake -S . -B build -DBUILD_TESTS=OFF -DBUILD_SMOKE_TEST=OFF
```

`just test` is a shorthand for building and running the unit suite directly.

#### Adding a test

The unit suite is doctest, vendored at
`vendor/doctest/include/doctest/doctest.h`. Add a `tests/test_<subject>.cpp`,
`#include "doctest/doctest.h"`, and register the file in the `linterra_tests`
source list in `CMakeLists.txt`. Only one translation unit may define
`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`; that is `tests/test_main.cpp`.

A subsystem can only be unit-tested if it links without a GL context. The
GL-free engine sources are collected in `linterra_core` — see the comment above
`LINTERRA_CORE_SOURCES` in `CMakeLists.txt` for the admission rule. A new
source that qualifies should be added there so both the game and the tests link
the same object.

### Shaders

Shaders live in `shaders/` and are compiled by the driver at runtime, so a
syntax error is invisible to the unit suite. Validate before pushing:

```bash
./scripts/validate-shaders.sh
```

This runs in CI on every pull request, and it is also the `validate-shaders`
job. It needs `glslangValidator` (`apt install glslang-tools`, or
`brew install glslang`).

### Code style

`.clang-format` is checked in and the project is formatted with it:

```bash
just fmt          # rewrite in place
just fmt-check    # verify only, non-zero exit if unformatted
```

`.clang-tidy` is checked in and declares the project's naming convention, so
the linter enforces it on new code. Two lint entry points exist:

```bash
just tidy         # run clang-tidy over an existing build tree
just tidy-build   # lint by building with CMAKE_CXX_CLANG_TIDY
```

`just tidy` reads the `compile_commands.json` that CMake emits by default, so
it lints with the same flags your editor's language server uses.

clang-tidy is advisory — nothing in CI runs it, and it is not a merge gate. The
naming rules are calibrated to what the tree already does, but the check set is
`bugprone-*` and `performance-*` with the defaults, so a first run over the
existing code may surface findings that are not yours. Fix what is worth
fixing; do not reformat the tree to satisfy a linter in passing.

Compiler warnings are enabled for all first-party code via the
`linterra_warnings` target (`-Wall -Wextra -Wpedantic -Wshadow`, `/W4` on
MSVC) and are treated as real. They are not currently `-Werror`, so a new
warning will not break someone's build, but it should not survive a review.

### Pull requests

Branch from `main`, target `main`, and keep the change focused — one logical
concern per pull request. Smaller pull requests get reviewed more carefully
than large ones.

Useful things to include:

- A `Closes #N` line in the commit message or PR body.
- How you verified it: which of `ctest`, `just fmt-check`,
  `./scripts/validate-shaders.sh`, and `./build/linterra` you ran.
- Anything you deliberately left out, and where it is tracked.

CI runs on every push and pull request: a Linux and a macOS build, unit tests,
the GL smoke test, shader validation, and an ASan + UBSan pass. A red check
blocks the merge.

### Project layout

| Path | |
| --- | --- |
| `src/` | Engine source |
| `src/renderer/` | `IRenderer` interface and the OpenGL backend in `src/renderer/opengl/` |
| `shaders/` | GLSL, compiled by the driver at runtime |
| `tests/` | Unit suite and the headless smoke test |
| `vendor/` | Committed third-party code; see `vendor/doctest/PROVENANCE.md` |
| `docs/roadmap.md` | Milestone dependency graph and issue lists |

Rendering is meant to go through the `IRenderer` abstraction, so engine code
should not need OpenGL headers directly. Today `src/renderer/opengl/*`,
`src/config.h`, and `src/texture.cpp` still do; the rest of the engine should
not add more, because a GL dependency is what stops a subsystem being unit
tested without a context.

### Licence

MIT — see [LICENSE](LICENSE). By contributing you agree your contribution is
licensed under the same terms.
