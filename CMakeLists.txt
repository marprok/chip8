cmake_minimum_required(VERSION 3.23)

project(chip8_emu)
find_package(SDL2 REQUIRED CONFIG REQUIRED COMPONENTS SDL2)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Uncomment to check the source and binary directories
#message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
#message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")

add_compile_options(-Wall -Wextra -pedantic)
add_executable(${PROJECT_NAME})


target_include_directories(${PROJECT_NAME} PRIVATE src)

target_sources(${PROJECT_NAME} PRIVATE
    src/main.cc
)

target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
