# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "1.7.6")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(RAINMAKER_ENABLED 1)
project(xiaozhi)

idf_build_set_property(CXX_COMPILE_OPTIONS "-DCHIP_CONFIG_MAX_ATTRIBUTE_STORE_ELEMENT_SIZE=2560" APPEND)
