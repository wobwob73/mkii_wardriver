# ESP-IDF project entry. PlatformIO (framework = espidf) sets IDF_PATH and
# drives this via project.cmake, mirroring the other ESP-IDF/PlatformIO Leaves.
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(leaf_ble)
