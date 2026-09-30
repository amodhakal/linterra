# Justfile for the Linterra C++ graphics engine
# Usage:
#   just              # list all available recipes
#   just build        # configure + build (release)
#   just dev          # configure + build (debug with sanitizers)
#   just test         # build and run unit tests
#   just run          # build (if needed) and run the engine
#   just clean        # remove build directory
#   just fmt          # format source with clang-format
#   just fmt-check    # verify formatting without writing
#   just tidy         # run clang-tidy over an existing build tree

# --- Configuration ----------------------------------------------------------
build_dir := "build"
cxx := "clang++"

# --- Recipes ----------------------------------------------------------------

build:
  # Configure once, then compile. Uses the existing build/ directory.
  # Pass extra cmake args via: just build -- -DUSE_VULKAN=ON
  cmake -S . -B {{build_dir}} -DCMAKE_CXX_COMPILER={{cxx}} -DCMAKE_BUILD_TYPE=Release
  cmake --build {{build_dir}} --parallel

dev:
  # Debug build with address + UB sanitizers
  cmake -S . -B {{build_dir}} -DCMAKE_CXX_COMPILER={{cxx}} -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
  cmake --build {{build_dir}} --parallel

test:
  # Build tests if needed, then run
  cmake -S . -B {{build_dir}} -DCMAKE_CXX_COMPILER={{cxx}} -DCMAKE_BUILD_TYPE=Debug
  cmake --build {{build_dir}} --parallel --target linterra_tests
  ./{{build_dir}}/linterra_tests

run:
  # Build (if stale) then launch the engine binary
  cmake --build {{build_dir}} --parallel
  ./{{build_dir}}/linterra

clean:
  rm -rf {{build_dir}}

fmt:
  # Requires: clang-format
  # The predicates are grouped: without the parentheses `-o` binds loosely and
  # any predicate added later would only apply to the second branch. The
  # `.hpp` list matters -- the renderer interface headers are all .hpp.
  find src tests -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    | xargs clang-format -i -style=file

fmt-check:
  # Requires: clang-format. Fails if anything is unformatted.
  find src tests -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    | xargs clang-format --dry-run --Werror -style=file

tidy:
  # Requires: clang-tidy. Standalone lint pass over an existing build tree.
  # CMake now emits compile_commands.json by default (see CMakeLists.txt), so
  # this reads the same compilation database clangd and editors use.
  # Configure first if the build directory does not exist yet.
  test -f {{build_dir}}/compile_commands.json \
    || cmake -S . -B {{build_dir}} -DCMAKE_BUILD_TYPE=Debug
  run-clang-tidy -p {{build_dir}} \
    'src/.*\.cpp$' 'src/.*\.hpp$'

tidy-build:
  # Requires: clang-tidy. Lints by building, which works without
  # run-clang-tidy or an existing compile_commands.json, but compiles
  # everything rather than only reporting diagnostics.
  cmake -S . -B {{build_dir}}-tidy \
    -DCMAKE_CXX_COMPILER={{cxx}} \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_CLANG_TIDY="clang-tidy"
  cmake --build {{build_dir}}-tidy --parallel

# --- Shorthands -------------------------------------------------------------
b := "build"
r := "run"
t := "test"
