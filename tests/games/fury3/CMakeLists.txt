# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 4.0)
project(havGamePatcherFury3Tests LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Fury3 native tests require the x86 MSVC generator (-A Win32).")
endif()

get_filename_component(HAV_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
option(HAV_BUILD_MOVIE_TESTS "Build the optional Windows MCI movie fixture" OFF)
set(fixtures inputHookTests fullscreenTests)

if(HAV_BUILD_MOVIE_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  list(APPEND fixtures movieTests)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/owned checkerboard.avi"
    COMMAND "${Python3_EXECUTABLE}"
      "${HAV_PROJECT_ROOT}/scripts/games/fury3/generateTestVideo.py"
      "${CMAKE_CURRENT_BINARY_DIR}/owned checkerboard.avi"
    DEPENDS "${HAV_PROJECT_ROOT}/scripts/games/fury3/generateTestVideo.py"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/owned checkerboard.json"
    VERBATIM)
  add_custom_target(testVideo DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/owned checkerboard.avi")
endif()

foreach(fixture IN LISTS fixtures)
  add_executable(${fixture} "${fixture}.cpp")
  target_compile_features(${fixture} PRIVATE cxx_std_23)
  target_compile_options(${fixture} PRIVATE /W4 /WX /utf-8)
  target_include_directories(${fixture} PRIVATE "${HAV_PROJECT_ROOT}/include/games/fury3/core")
  target_link_libraries(${fixture} PRIVATE user32 gdi32)
  target_link_options(${fixture} PRIVATE /BASE:0x10000000 /DYNAMICBASE:NO /FIXED /OPT:NOICF)
  set_property(TARGET ${fixture} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endforeach()

if(HAV_BUILD_MOVIE_TESTS)
  target_link_libraries(movieTests PRIVATE winmm)
  add_dependencies(movieTests testVideo)
endif()
