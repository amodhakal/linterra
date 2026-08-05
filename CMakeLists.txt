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
