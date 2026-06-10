cmake_minimum_required(VERSION 3.22)
project(freecam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Путь к preloader SDK передаётся из yml через -DPRELOADER_SDK=...
# По умолчанию ищем рядом с папкой мода
if(NOT DEFINED PRELOADER_SDK)
    set(PRELOADER_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../preloader-sdk")
endif()

add_library(freecam SHARED
    freecam.cpp
)

target_include_directories(freecam PRIVATE
    ${PRELOADER_SDK}/src
    ${PRELOADER_SDK}/src/pl
)

find_library(log-lib log)
find_library(egl-lib EGL)

target_link_libraries(freecam
    PRIVATE
    # GlossHook статическая библиотека
    ${PRELOADER_SDK}/lib/ARM64/libGlossHook.a
    ${log-lib}
    ${egl-lib}
    # GetPreloaderInput резолвится в рантайме через dlsym — не линкуем
)

set_target_properties(freecam PROPERTIES
    OUTPUT_NAME "libfreecam"
    PREFIX ""
)
