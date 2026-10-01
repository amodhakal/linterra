#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <print>
#include <random>
#include <string_view>

#include "application.h"
#include "noise/noise.h"

int main(int argc, char *argv[]) {
  uint32_t seed;

  if (argc >= 2) {
    const std::string_view input(argv[1]);
    const char *first = input.data();
    const char *last = first + input.size();
    uint64_t parsed = 0;
    std::from_chars_result result = std::from_chars(first, last, parsed, 10);

    // Reject empty, non-numeric, negative, overflowing, and partially
    // numeric inputs (e.g. "12abc").
    if (input.empty() || *first == '-' || result.ec != std::errc{} ||
        result.ptr != last || parsed > UINT32_MAX) {
      std::println(stderr,
                   "Invalid seed '{}': expected an integer in [0, {}].",
                   input, UINT32_MAX);
      return EXIT_FAILURE;
    }
    seed = static_cast<uint32_t>(parsed);
    std::println("World seed: {} (user-provided)", seed);
  } else {
    // Strong entropy source; avoid rand()'s weak RAND_MAX-limited output.
    static std::mt19937 rng(std::random_device{}());
    seed = rng();
    std::println("World seed: {} (random)", seed);
  }

  Noise::setSeed(seed);

  // Application's constructor throws on several distinct paths -- window
  // creation, GLAD init, shader compilation, texture loading, and anything
  // IO raises for a missing file -- and none of them was caught here. An
  // exception escaping main reaches the implicit catch(...) in the CRT's
  // startup code, so the user got "terminate called after throwing an
  // instance of 'std::runtime_error'" plus SIGABRT: a message printed by the
  // runtime rather than by this program, a core dump, and no hint which
  // resource was missing or where it was looked for (#144).
  //
  // Worse, the leak: ~Application is never reached when the constructor
  // throws, so ImGui_ImplOpenGL3_Shutdown / ImGui_ImplGlfw_Shutdown /
  // ImGui::DestroyContext do not run and windowing is not terminated. The
  // already-constructed members are destroyed (that part is guaranteed), but
  // the GLFW connection and GL context leak and the window stays mapped on
  // screen, frozen, until the OS reaps the process -- a force-quit on macOS.
  try {
    Application linterra("Linterra");
    while (linterra.isRunning()) {
      linterra.update();
    }
  } catch (const std::exception &e) {
    std::println(stderr, "Linterra failed to start: {}", e.what());
    return EXIT_FAILURE;
  } catch (...) {
    std::println(stderr, "Linterra failed to start: unknown error");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
