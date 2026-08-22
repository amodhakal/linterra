#include <charconv>
#include <cstdint>
#include <print>
#include <random>
#include <string_view>

#include "application.h"
#include "noise/noise.h"

int main(int argc, char *argv[]) {
  uint32_t seed;

  if (argc >= 2) {
    const std::string_view input(argv[1]);
    uint64_t parsed = 0;
    const char *first = input.data();
    const char *last = first + input.size();

    // Reject empty, non-numeric, negative, and overflowing inputs.
    if (input.empty() || *first == '-' ||
        std::from_chars(first, last, parsed, 10).ec != std::errc{} ||
        parsed > UINT32_MAX) {
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

  Application linterra("Linterra");
  while (linterra.isRunning()) {
    linterra.update();
  }

  return EXIT_SUCCESS;
}
