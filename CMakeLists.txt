# CXX_STANDARD 23 is only understood by CMake 3.20 and later; on 3.16-3.19 the
# value is silently unknown and the build degrades or errors out.
cmake_minimum_required(VERSION 3.20)
project(linterra VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Single source of truth: the glob below already covers everything under
# src/, including src/renderer/opengl/*. The explicit USE_OPENGL list that used
# to sit here re-declared those same paths, so a removed source would still be
# listed and break the build while a new one was silently picked up.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "vendor/glad/src/glad.c" "vendor/noise/src/noise.cpp" "vendor/imgui/imgui.cpp" "vendor/imgui/imgui_draw.cpp" "vendor/imgui/imgui_tables.cpp" "vendor/imgui/imgui_widgets.cpp" "vendor/imgui/imgui_demo.cpp" "vendor/imgui/backends/imgui_impl_glfw.cpp" "vendor/imgui/backends/imgui_impl_opengl3.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE linterra_warnings)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    vendor/glad/include
    vendor/noise/include
    vendor/images/include
    vendor/imgui
    vendor/imgui/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    glfw
    glm::glm
)

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
# and frustum culling. These are the math-heavy, regression-prone parts. The
# test executable links only the sources it needs, so no OpenGL/GLFW runtime
# is required to build or run it.
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build the unit test suite" ON)

if(BUILD_TESTS)
  add_executable(linterra_tests
    tests/test_main.cpp
    tests/test_noise.cpp
    tests/test_camera.cpp
    tests/test_frustum.cpp
    vendor/noise/src/noise.cpp
    src/camera.cpp
    src/frustum.cpp
  )

  target_include_directories(linterra_tests PRIVATE
    tests
    vendor/doctest/include
    src
    vendor/noise/include
  )

  target_compile_definitions(linterra_tests PRIVATE LINTERRA_NO_OPENGL)

  set_target_properties(linterra_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )

  target_link_libraries(linterra_tests PRIVATE linterra_warnings glm::glm)

  enable_testing()
  add_test(NAME linterra_tests COMMAND linterra_tests)
endif()
