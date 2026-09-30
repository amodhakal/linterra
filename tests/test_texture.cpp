#include <cstdint>

#include "doctest/doctest.h"
#include "texture.h"

// The power-of-two texture dimension check, which GL_REPEAT wrapping requires.
//
// The original test was `(n & (n - 1)) != 0` in src/texture.cpp. That is the
// standard bit trick and it is correct for every positive n, but it cannot
// reject zero:
//
//     0 & (0 - 1)  ==  0 & 0xFFFFFFFF  ==  0
//
// Every bit of zero is already zero, so the condition is false and a
// 0-sized dimension passes. That matters because the dimensions are not
// compile-time constants: they come out of stbi_load decoding external image
// files, and a truncated or malformed file yields width 0 or height 0 rather
// than an error.

TEST_SUITE("TextureDimensions") {
  TEST_CASE("powers of two are accepted") {
    for (const std::int32_t n : {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024,
                                 2048, 4096}) {
      CAPTURE(n);
      CHECK(isPowerOfTwoDimension(n));
    }
  }

  TEST_CASE("non-powers of two are rejected") {
    for (const std::int32_t n : {3, 5, 6, 7, 9, 10, 11, 12, 13, 15, 17, 100,
                                 300, 1000, 3000}) {
      CAPTURE(n);
      CHECK_FALSE(isPowerOfTwoDimension(n));
    }
  }

  TEST_CASE("zero is rejected, which the bare bit trick cannot do") {
    // The regression. With only `(n & (n - 1)) != 0` these all pass, and a
    // zero-sized texture array is created and bound.
    CHECK_FALSE(isPowerOfTwoDimension(0));
  }

  TEST_CASE("negative dimensions are rejected") {
    // A sign bit makes the bit trick meaningless; -2 & -3 is 0, so a naive
    // check would accept it.
    for (const std::int32_t n : {-1, -2, -4, -8, -16}) {
      CAPTURE(n);
      CHECK_FALSE(isPowerOfTwoDimension(n));
    }
  }

  TEST_CASE("the full set of powers of two is exhaustive up to 4096") {
    // Cross-checks the bit trick against a definition that does not share its
    // blind spot, so a change to the implementation cannot quietly agree with
    // itself.
    for (std::int32_t n = -8; n <= 4096; ++n) {
      bool expected = false;
      if (n > 0) {
        std::int32_t remainder = n;
        while (remainder % 2 == 0) {
          remainder /= 2;
        }
        expected = (remainder == 1);
      }
      CAPTURE(n);
      CHECK(isPowerOfTwoDimension(n) == expected);
    }
  }
}
