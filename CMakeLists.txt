# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(ENV{ADF_PATH} "/home/development/.esp-adf/esp-adf/")
set(ENV{IDF_PATH} "/home/development/.esp-adf/esp-adf/esp-idf/")
include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ChatGPT_ESP32S3_RealtimeSpeech)
set(EXTRA_COMPONENT_DIRS "$ENV{ADF_PATH}/components")
list(APPEND EXTRA_COMPONENT_DIRS "components")

