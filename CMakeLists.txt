# CXX_STANDARD 23 is only understood by CMake 3.20 and later; on 3.16-3.19 the
# value is silently unknown and the build degrades or errors out.
cmake_minimum_required(VERSION 3.20)
project(linterra VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json by default so clangd, clang-tidy, and IDE
# tooling work out of the box. Only meaningful for single-config generators
# (Makefiles, Ninja); Xcode and the Visual Studio generators produce a
# .vcxproj instead.
#
# This is set as a normal variable rather than via option() because
# CMAKE_EXPORT_COMPILE_COMMANDS is a CMAKE_-prefixed name: the generator
# pre-creates a (blank) cache entry for it, which makes option() a no-op.
# The blank check defaults it to ON while still letting an explicit
# -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF win.
if(CMAKE_EXPORT_COMPILE_COMMANDS STREQUAL "")
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# The engine uses C++23 <print>, which is not available on the older toolchains
# CMake would otherwise happily accept. Fail at configure time with a message
# that names the actual cause rather than letting <print> surface later as a
# confusing "not a member of std" error.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
    message(FATAL_ERROR
      "Linterra needs C++23 <print>: GCC >= 13 or Clang >= 17 "
      "(found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}).")
  endif()
elseif(MSVC)
  if(MSVC_VERSION LESS 1933)
    message(FATAL_ERROR
      "Linterra needs C++23 <print>: MSVC >= 19.33 "
      "(found MSVC ${MSVC_VERSION}).")
  endif()
endif()

option(USE_OPENGL "Use OpenGL renderer (currently the only implemented backend)" ON)
option(USE_METAL "Use Metal renderer (not implemented yet)" OFF)
option(USE_VULKAN "Use Vulkan renderer (not implemented yet)" OFF)

# Sanitizer support, e.g. -DLINTERRA_SANITIZE=address,undefined
#
# An INTERFACE target rather than raw CMAKE_CXX_FLAGS, for two reasons: the
# flags have to reach the link line as well as the compile line, and this tree
# contains C sources (vendor/glad/src/glad.c) that CMAKE_CXX_FLAGS would never
# instrument. Empty by default, so a normal build is unaffected.
set(LINTERRA_SANITIZE "" CACHE STRING
    "Comma-separated sanitizers to enable (e.g. address,undefined)")
if(LINTERRA_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR
      "LINTERRA_SANITIZE is not wired up for MSVC; use /fsanitize=address "
      "via CMAKE_CXX_FLAGS instead.")
  endif()
  add_library(linterra_sanitize INTERFACE)
  target_compile_options(linterra_sanitize INTERFACE
    -fsanitize=${LINTERRA_SANITIZE} -fno-omit-frame-pointer -g)
  target_link_options(linterra_sanitize INTERFACE
    -fsanitize=${LINTERRA_SANITIZE})
endif()

if(NOT USE_OPENGL)
  message(WARNING
    "USE_OPENGL=OFF does not currently exclude the OpenGL backend: every "
    "source under src/ is picked up by the glob below, and no other backend "
    "is implemented. Backend selection is still a runtime concern "
    "(createRenderer/RenderBackend) — see #88 for the Metal/Vulkan work.")
endif()

# Warning flags shared by every first-party target, so the game and the tests
# cannot drift apart. Vendored third-party code is compiled as SYSTEM include
# and is deliberately excluded from these.
add_library(linterra_warnings INTERFACE)
target_compile_options(linterra_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# ---------------------------------------------------------------------------
# linterra_core — the GL-free half of the engine
#
# Everything listed here compiles without an OpenGL or GLFW header and needs
# no GL context at runtime, which is what makes it linkable into both the game
# and the headless test binaries. config.h guards the glad/GLFW includes behind
# LINTERRA_NO_OPENGL, so these are compiled with that defined.
#
# The point of the library is that the game and the tests compile the *same*
# objects. Previously linterra_tests named src/camera.cpp and src/frustum.cpp
# directly, producing a second, independent copy that could silently drift
# from the game's.
#
# To add a source here it must compile with -DLINTERRA_NO_OPENGL and must not
# reference a GL type. src/chunk.cpp and everything above it are excluded
# because chunk.h includes <glad/glad.h> unconditionally.
# ---------------------------------------------------------------------------
set(LINTERRA_CORE_SOURCES
  src/camera.cpp
  src/frustum.cpp
  src/io.cpp
  src/player.cpp
  src/threadpool.cpp
)

add_library(linterra_core STATIC ${LINTERRA_CORE_SOURCES})

target_include_directories(linterra_core PUBLIC
    src
    vendor/noise/include
)

target_compile_definitions(linterra_core PRIVATE LINTERRA_NO_OPENGL)

target_link_libraries(linterra_core
    PUBLIC glm::glm
    PRIVATE linterra_warnings
)
if(LINTERRA_SANITIZE)
  target_link_libraries(linterra_core PRIVATE linterra_sanitize)
endif()

# Single source of truth: the glob below already covers everything under
# src/, including src/renderer/opengl/*. The explicit USE_OPENGL list that used
# to sit here re-declared those same paths, so a removed source would still be
# listed and break the build while a new one was silently picked up.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "vendor/glad/src/glad.c" "vendor/noise/src/noise.cpp" "vendor/imgui/imgui.cpp" "vendor/imgui/imgui_draw.cpp" "vendor/imgui/imgui_tables.cpp" "vendor/imgui/imgui_widgets.cpp" "vendor/imgui/imgui_demo.cpp" "vendor/imgui/backends/imgui_impl_glfw.cpp" "vendor/imgui/backends/imgui_impl_opengl3.cpp")

# The core sources are now owned by linterra_core; compiling them here as well
# would give the game duplicate symbol definitions at link time. GLOB_RECURSE
# yields absolute paths even for a relative pattern, so this filters on the
# path rather than removing the relative names from the list.
list(FILTER SOURCES EXCLUDE REGEX "src/(camera|frustum|io|player|threadpool)\\.cpp$")

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    vendor/glad/include
    vendor/noise/include
    vendor/images/include
    vendor/imgui
    vendor/imgui/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    linterra_warnings
    linterra_core
    OpenGL::GL
    glfw
    glm::glm
)
if(LINTERRA_SANITIZE)
  target_link_libraries(${PROJECT_NAME} PRIVATE linterra_sanitize)
endif()

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
    
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
)

# ---------------------------------------------------------------------------
# Unit tests (doctest)
#
# Covers the pure (no-GL-context) subsystems: procedural noise, camera math,
# and frustum culling. These are the math-heavy, regression-prone parts.
#
# The engine code under test comes from linterra_core, the same static library
# the game links, so a test can never pass against a private copy of the code
# that the game does not actually use. No OpenGL/GLFW runtime is required to
# build or run this target.
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build the unit test suite" ON)

if(BUILD_TESTS)
  add_executable(linterra_tests
    tests/test_main.cpp
    tests/test_noise.cpp
    tests/test_camera.cpp
    tests/test_chunk_octree.cpp
    tests/test_frustum.cpp
    tests/test_gpu_slot_pool.cpp
    tests/test_heightmap.cpp
    tests/test_io.cpp
    tests/test_packed_vertex.cpp
    tests/test_player.cpp
    tests/test_shader_uniforms.cpp
    tests/test_threadpool.cpp
    tests/test_texture.cpp
    tests/test_frametime.cpp
    tests/test_ground_query.cpp
    vendor/noise/src/noise.cpp
  )

  target_include_directories(linterra_tests PRIVATE
    tests
    vendor/doctest/include
    src
    vendor/noise/include
    # texture.h is included for isPowerOfTwoDimension, which is constexpr and
    # needs no link, but the header itself pulls in stb_image.
    vendor/images/include
  )

  target_compile_definitions(linterra_tests PRIVATE LINTERRA_NO_OPENGL)

  set_target_properties(linterra_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )

  target_link_libraries(linterra_tests PRIVATE
    linterra_warnings
    linterra_core
    glm::glm
  )
  if(LINTERRA_SANITIZE)
    target_link_libraries(linterra_tests PRIVATE linterra_sanitize)
  endif()

  # test_shader_uniforms.cpp parses terrain.comp to check the C++ uniform list
  # against the GLSL, so this target needs the shaders beside the executable
  # the same way the game and the smoke test do.
  add_custom_command(
      TARGET linterra_tests POST_BUILD

      COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
      "$<TARGET_FILE_DIR:linterra_tests>/shaders"
  )

  enable_testing()
  add_test(NAME linterra_tests COMMAND linterra_tests)
endif()

# ---------------------------------------------------------------------------
# Headless smoke test
#
# The doctest suite above covers the pure math only. This one covers the GL
# path -- shader compilation, buffer and VAO creation, chunk mesh upload, draw
# submission, and teardown -- by creating a hidden GLFW window and driving the
# real engine classes. It needs a windowing system, so on a headless Linux
# runner it has to be run under xvfb-run.
#
# Separate from linterra_tests because it has an entirely different
# requirement: no GL context versus a real one.
# ---------------------------------------------------------------------------
option(BUILD_SMOKE_TEST "Build the headless GL smoke test" ON)

if(BUILD_SMOKE_TEST)
  # The whole engine except main(), which owns the game's own main(), and
  # application.cpp, which is the GLFW event-loop and ImGui wiring. The smoke
  # test drives IRenderer directly, so it does not need the event loop; the
  # offscreen framebuffer path it would have exercised is reachable through
  # IRenderer::resizeOffscreenTarget and is called explicitly instead.
  file(GLOB_RECURSE SMOKE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
  list(FILTER SMOKE_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
  list(FILTER SMOKE_SOURCES EXCLUDE REGEX "src/application\\.cpp$")
  list(FILTER SMOKE_SOURCES EXCLUDE
       REGEX "src/(camera|frustum|io|player|threadpool)\\.cpp$")

  add_executable(linterra_smoke
    tests/smoke_main.cpp
    ${SMOKE_SOURCES}
    vendor/glad/src/glad.c
    vendor/noise/src/noise.cpp
  )

  target_include_directories(linterra_smoke PRIVATE
    src
    vendor/glad/include
    vendor/noise/include
    vendor/images/include
  )

  target_link_libraries(linterra_smoke PRIVATE
    linterra_warnings
    linterra_core
    OpenGL::GL
    glfw
    glm::glm
  )

  # The shader paths in src/config.h are relative and resolve against the
  # executable's directory, so the smoke binary needs the same asset copy the
  # game gets.
  add_custom_command(
      TARGET linterra_smoke POST_BUILD

      COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/resources"
      "$<TARGET_FILE_DIR:linterra_smoke>/resources"

      COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
      "$<TARGET_FILE_DIR:linterra_smoke>/shaders"
  )

  enable_testing()
  add_test(NAME linterra_smoke COMMAND linterra_smoke)
endif()
