# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 4.0)
project(havGamePatcherCoreTests LANGUAGES CXX)

add_subdirectory(../src/core core)

add_executable(coreIntegrationTests coreIntegrationTests.cpp)
target_compile_features(coreIntegrationTests PRIVATE cxx_std_23)
target_compile_definitions(coreIntegrationTests PRIVATE
  HAV_TEST_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(coreIntegrationTests PRIVATE havGamePatcherCore)

if(MSVC)
  target_compile_options(havGamePatcherCore PRIVATE /W4 /WX)
  target_compile_options(coreIntegrationTests PRIVATE /W4 /WX /utf-8)
  set_property(TARGET coreIntegrationTests PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(MINGW)
  target_compile_options(havGamePatcherCore PRIVATE -Wall -Wextra -Werror)
  target_compile_options(coreIntegrationTests PRIVATE -Wall -Wextra -Werror)
  target_link_options(coreIntegrationTests PRIVATE -municode -static)
endif()
