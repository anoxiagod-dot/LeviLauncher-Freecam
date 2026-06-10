cmake_minimum_required(VERSION 3.22)
project(freecam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(freecam SHARED freecam.cpp)

target_include_directories(freecam PRIVATE
    ${PRELOADER_SRC}
)

find_library(log-lib log)
find_library(egl-lib EGL)

target_link_libraries(freecam
    PRIVATE
    ${PRELOADER_SRC}/../lib/ARM64/libGlossHook.a
    ${log-lib}
    ${egl-lib}
)

set_target_properties(freecam PROPERTIES
    OUTPUT_NAME "libfreecam"
    PREFIX ""
)
