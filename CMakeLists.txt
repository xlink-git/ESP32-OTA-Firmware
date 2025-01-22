# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "V1.0")

add_compile_options(-Wno-unused-but-set-variable -Wno-empty-body)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32ota)
