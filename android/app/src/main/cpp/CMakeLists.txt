cmake_minimum_required(VERSION 3.22.1)
project(zelda_native LANGUAGES CXX)

add_library(zelda_native SHARED native_port.cpp)

target_compile_features(zelda_native PRIVATE cxx_std_20)
target_compile_options(zelda_native PRIVATE -Wall -Wextra -Wpedantic)
