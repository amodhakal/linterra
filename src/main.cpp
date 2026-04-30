#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <print>

#include "application.h"
#include "noise/noise.h"

int main(int argc, char *argv[]) {
  uint32_t seed;

  if (argc >= 2) {
    seed = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    std::println("World seed: {} (user-provided)", seed);
  } else {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    seed = static_cast<uint32_t>(std::rand());
    seed = (seed << 4 ^ seed >> 4) & (seed << 8 ^ seed >> 8);
    seed |= (seed << 6 ^ seed >> 5) & (seed << 6 ^ seed >> 9);
    std::println("World seed: {} (random)", seed);
  }

  Noise::setSeed(seed);

  Application linterra("Linterra");
  while (linterra.isRunning()) {
    linterra.update();
  }

  return EXIT_SUCCESS;
}
