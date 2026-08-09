cmake_minimum_required(VERSION 3.16)
project(linterra VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_OPENGL "Use OpenGL renderer" ON)
option(USE_METAL "Use Metal renderer (not implemented yet)" OFF)
option(USE_VULKAN "Use Vulkan renderer (not implemented yet)" OFF)

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
find_package(glm REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "vendor/glad/src/glad.c" "vendor/noise/src/noise.cpp" "vendor/imgui/imgui.cpp" "vendor/imgui/imgui_draw.cpp" "vendor/imgui/imgui_tables.cpp" "vendor/imgui/imgui_widgets.cpp" "vendor/imgui/imgui_demo.cpp" "vendor/imgui/backends/imgui_impl_sdl3.cpp" "vendor/imgui/backends/imgui_impl_opengl3.cpp")

if(USE_OPENGL)
  list(APPEND SOURCES
    src/renderer/renderer_factory.cpp
    src/renderer/opengl/opengl_buffer.cpp
    src/renderer/opengl/opengl_vertex_array.cpp
    src/renderer/opengl/opengl_shader.cpp
    src/renderer/opengl/opengl_texture.cpp
    src/renderer/opengl/opengl_renderer.cpp
  )
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    vendor/glad/include
    vendor/noise/include
    vendor/images/include
    vendor/imgui
    vendor/imgui/backends
    ${SDL3_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    ${SDL3_LIBRARIES}
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

  target_link_libraries(linterra_tests PRIVATE glm::glm)

  enable_testing()
  add_test(NAME linterra_tests COMMAND linterra_tests)
endif()
