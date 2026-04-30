cmake_minimum_required(VERSION 3.16)
project(linterra VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime PATHS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime/lib" NO_DEFAULT_PATH)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "vendor/glad/src/glad.c" "vendor/noise/src/noise.cpp")
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    vendor/glad/include
    vendor/noise/include
    vendor/images/include
    vendor/onnxruntime/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    glfw
    glm::glm
    ${ONNXRUNTIME_LIBRARY}
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
