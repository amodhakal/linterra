// Pins the C++/GLSL uniform contract for the terrain compute shader.
//
// Shader::setUniform* drops any write whose name was never passed to
// newUniform, and the compute shader keeps the GLSL default for a uniform
// nothing ever writes. Neither produces an error: glGetError() stays clean
// because no illegal GL call is made, so a mismatch is invisible at runtime
// and in CI. That is exactly how uSlot shipped unregistered -- every GPU chunk
// computed base == 0 and overwrote SSBO slot 0 (#125), and the only symptom was
// wrong terrain.
//
// So this checks the contract statically, in the GL-free unit suite, where it
// runs on every platform including the headless Linux CI runner. The GL-side
// counterpart (a uniform resolving to a real location in a linked program)
// lives in tests/smoke_main.cpp, because that needs a context.

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "doctest/doctest.h"
#include "io.h"

namespace {

// Collect the identifiers declared as `uniform <type> <name>;` in a GLSL
// source. Deliberately a narrow regex rather than a real parser: it only has
// to handle the declarations this project writes, and a test that silently
// stopped matching anything would be worse than no test -- hence the
// non-empty assertion below.
std::set<std::string> declaredUniforms(std::string_view glsl) {
  std::set<std::string> names;
  std::istringstream lines{std::string(glsl)};
  std::string line;
  while (std::getline(lines, line)) {
    // Strip a trailing comment so a commented-out declaration cannot register
    // as live. Block comments are not handled; this project does not use them
    // for uniform declarations.
    const auto comment = line.find("//");
    if (comment != std::string::npos) {
      line.erase(comment);
    }
    std::istringstream words(line);
    std::string keyword;
    if (!(words >> keyword) || keyword != "uniform") {
      continue;
    }
    std::string type;
    std::string name;
    if (!(words >> type >> name)) {
      continue;
    }
    // Strip any array suffix, e.g. `uniform float uFog[4];`.
    if (const auto bracket = name.find('['); bracket != std::string::npos) {
      name.erase(bracket);
    }
    // The declaration must be terminated, so a `uniform` inside a larger
    // expression is not mistaken for a declaration.
    std::string rest;
    std::getline(words, rest);
    for (const char c : rest) {
      if (c != ' ' && c != '\t') {
        name.clear();
        break;
      }
    }
    if (!name.empty() && name.back() == ';') {
      names.insert(name.substr(0, name.size() - 1));
    }
  }
  return names;
}

std::set<std::string> registeredUniforms() {
  return {Constants::TERRAIN_COMPUTE_UNIFORMS.begin(),
          Constants::TERRAIN_COMPUTE_UNIFORMS.end()};
}

}  // namespace

TEST_SUITE("TerrainComputeUniforms") {
  TEST_CASE("the GLSL declares uniforms, so the scan above is not a no-op") {
    // If this fails, declaredUniforms() stopped matching and every assertion
    // below would pass vacuously.
    const auto declared = declaredUniforms(
        IO::getFullFileContents(Constants::TERRAIN_COMPUTE_PATH));
    CHECK(declared.size() >= 9);
  }

  TEST_CASE("every uniform the compute shader declares is registered") {
    // This is the #125 regression: uSlot is declared in terrain.comp and set
    // by Chunk::generateHeightMapGPU, but was never registered, so the write
    // was dropped and every chunk used slot 0.
    const auto declared = declaredUniforms(
        IO::getFullFileContents(Constants::TERRAIN_COMPUTE_PATH));
    const auto registered = registeredUniforms();

    for (const auto &name : declared) {
      CAPTURE(name);
      CHECK_MESSAGE(registered.count(name) == 1,
                    "uniform declared in terrain.comp but missing from "
                    "Constants::TERRAIN_COMPUTE_UNIFORMS; writes to it are "
                    "dropped silently");
    }
  }

  TEST_CASE("no registered uniform is absent from the compute shader") {
    // The inverse. A name registered but not declared resolves to -1, which
    // used to be stored as if valid and then silently ignored.
    const auto declared = declaredUniforms(
        IO::getFullFileContents(Constants::TERRAIN_COMPUTE_PATH));
    const auto registered = registeredUniforms();

    for (const auto &name : registered) {
      CAPTURE(name);
      CHECK_MESSAGE(declared.count(name) == 1,
                    "uniform registered in C++ but not declared in "
                    "terrain.comp; it resolves to location -1");
    }
  }

  TEST_CASE("uSlot is registered, because it selects the SSBO slot") {
    // Spelled out separately so the #125 regression is unambiguous if it ever
    // comes back. terrain.comp:127 computes `uSlot * uExtSide * uExtSide` as
    // the write base, so a default of 0 sends every chunk to slot 0.
    const auto registered = registeredUniforms();
    CHECK(registered.count("uSlot") == 1);
  }
}
