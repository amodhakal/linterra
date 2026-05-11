cmake_minimum_required(VERSION 3.16)
project(linterra VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_OPENGL "Use OpenGL renderer" ON)
option(USE_METAL "Use Metal renderer (not implemented yet)" OFF)
option(USE_VULKAN "Use Vulkan renderer (not implemented yet)" OFF)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "vendor/glad/src/glad.c" "vendor/noise/src/noise.cpp")

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
