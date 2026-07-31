# Justfile for the Linterra C++ graphics engine
# Usage:
#   just              # list all available recipes
#   just build        # configure + build (release)
#   just dev          # configure + build (debug with sanitizers)
#   just test         # build and run unit tests
#   just run          # build (if needed) and run the engine
#   just clean        # remove build directory
#   just fmt          # format source with clang-format
#   just clippy       # run clang-tidy on the codebase

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
  find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i -style=file

clippy check-tidy:
  # Requires: clang-tidy
  cmake -S . -B {{build_dir}} -DCMAKE_CXX_COMPILER={{cxx}} -DCMAKE_CXX_CLANG_TIDY="clang-tidy"
  cmake --build {{build_dir}} --parallel

# --- Shorthands -------------------------------------------------------------
b := "build"
r := "run"
t := "test"
