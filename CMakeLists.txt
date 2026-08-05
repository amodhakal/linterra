cmake_minimum_required(VERSION 3.20)
project(linterra CXX OBJCXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 26)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
add_executable(linterra src/main.cpp)

target_include_directories(linterra PRIVATE vendor/metal-cpp)
target_link_libraries(linterra
    ${SDL2_LIBRARIES}
    "-framework Metal"
    "-framework MetalKit"
    "-framework Cocoa"
    "-framework QuartzCore"
    "-framework Foundation"
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/shaders.metallib
    COMMAND xcrun -sdk macosx metal -c ${CMAKE_SOURCE_DIR}/src/shaders.metal -o ${CMAKE_BINARY_DIR}/shaders.air
    COMMAND xcrun -sdk macosx metallib ${CMAKE_BINARY_DIR}/shaders.air -o ${CMAKE_BINARY_DIR}/shaders.metallib
    DEPENDS ${CMAKE_SOURCE_DIR}/src/shaders.metal
)

add_custom_target(CompileShaders DEPENDS ${CMAKE_BINARY_DIR}/shaders.metallib)
add_dependencies(linterra CompileShaders)
