cmake_minimum_required(VERSION 3.16)

project(cpu_temp_mqtt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpu_temp_mqtt
    src/main.cpp
)

target_compile_options(cpu_temp_mqtt PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
