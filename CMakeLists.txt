cmake_minimum_required(VERSION 3.16)

project(cpu_temp_mqtt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PahoMqttCpp 1.4.1 CONFIG REQUIRED)

add_executable(cpu_temp_mqtt
    src/main.cpp
)

target_link_libraries(cpu_temp_mqtt PRIVATE
    PahoMqttCpp::paho-mqttpp3
)

target_compile_options(cpu_temp_mqtt PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
