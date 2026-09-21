cmake_minimum_required(VERSION 3.13)

# Standard Pico SDK bootstrap. Fetch this file from the Pico SDK
# (pico-sdk/external/pico_sdk_import.cmake) and place it alongside this
# CMakeLists.txt before building -- not vendored in this repo since it is
# boilerplate tied to the Pico SDK version you install, not project code.
include(pico_sdk_import.cmake)

project(makeshift_flipper_pico C CXX ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(main)
