# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 4.0)
project(havGamePatcherHellbenderTests LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Hellbender native tests require the x86 MSVC generator (-A Win32).")
endif()

option(HAV_BUILD_READINESS_TESTS "Build owned display and surface readiness fixtures" OFF)

if(HAV_BUILD_READINESS_TESTS)
  get_filename_component(HAV_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
  set(address_header "${HAV_PROJECT_ROOT}/out/patches/hellbender/english/gameAddresses.hpp")

  if(NOT EXISTS "${address_header}")
    message(FATAL_ERROR "Prepare the English recipe before building readiness fixtures.")
  endif()

  add_executable(displayReadinessTests displayReadinessTests.cpp)
  target_compile_features(displayReadinessTests PRIVATE cxx_std_23)
  target_compile_definitions(displayReadinessTests PRIVATE NOMINMAX)
  target_compile_options(displayReadinessTests PRIVATE /W4 /WX /utf-8 /O1 /Oi /arch:IA32
    "/FI${address_header}")
  target_include_directories(displayReadinessTests PRIVATE
    "${HAV_PROJECT_ROOT}/include/games/hellbender/core")
  target_link_options(displayReadinessTests PRIVATE /BASE:0x10000000 /DYNAMICBASE:NO /FIXED /OPT:NOICF)
  set_property(TARGET displayReadinessTests PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(HAV_BUILD_TEXTURE_TESTS "Build texture ownership, recovery and per-edition hook fixtures" OFF)
set(HAV_TEXTURE_EDITIONS "English;French;German" CACHE STRING "Editions for texture hook fixtures")

if(HAV_BUILD_TEXTURE_TESTS)
  get_filename_component(HAV_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

  function(AddTextureFixture target fixture edition)
    if(NOT edition MATCHES "^(English|French|German)$")
      message(FATAL_ERROR "Unknown Hellbender texture test edition: ${edition}")
    endif()

    string(TOLOWER "${edition}" edition_slug)
    set(address_header "${HAV_PROJECT_ROOT}/out/patches/hellbender/${edition_slug}/gameAddresses.hpp")

    if(NOT EXISTS "${address_header}")
      message(FATAL_ERROR "Prepare the ${edition} recipe before building its texture tests.")
    endif()

    add_executable(${target} ${fixture}.cpp)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_definitions(${target} PRIVATE NOMINMAX)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /O1 /Oi /arch:IA32
      "/FI${address_header}")
    target_include_directories(${target} PRIVATE "${HAV_PROJECT_ROOT}/include/games/hellbender/core")
    target_link_options(${target} PRIVATE /BASE:0x10000000 /DYNAMICBASE:NO /FIXED /OPT:NOICF)
    set_property(TARGET ${target} PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endfunction()

  # Shared C++ behavior is independent of the edition. Hook wiring is checked per image
  AddTextureFixture(textureTransferTests textureTransferTests English)
  AddTextureFixture(textureRecoveryTests textureRecoveryTests English)

  foreach(edition IN LISTS HAV_TEXTURE_EDITIONS)
    AddTextureFixture("textureHookTests${edition}" textureHookTests "${edition}")
  endforeach()
endif()

option(HAV_BUILD_MOVIE_RECOVERY_TESTS "Build owned mapped-image movie recovery fixtures" OFF)
set(HAV_MOVIE_EDITIONS "English" CACHE STRING "Editions for movie recovery fixtures")

if(HAV_BUILD_MOVIE_RECOVERY_TESTS)
  get_filename_component(HAV_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

  foreach(edition IN LISTS HAV_MOVIE_EDITIONS)
    if(NOT edition MATCHES "^(English|French|German)$")
      message(FATAL_ERROR "Unknown Hellbender movie test edition: ${edition}")
    endif()

    string(TOLOWER "${edition}" edition_slug)
    set(address_header "${HAV_PROJECT_ROOT}/out/patches/hellbender/${edition_slug}/gameAddresses.hpp")

    if(NOT EXISTS "${address_header}")
      message(FATAL_ERROR "Prepare the ${edition} recipe before building movie recovery fixtures.")
    endif()

    set(target "movieRecoveryTests${edition}")
    add_executable(${target} movieRecoveryTests.cpp)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_definitions(${target} PRIVATE NOMINMAX)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /O1 /Oi /arch:IA32
      "/FI${address_header}")
    target_include_directories(${target} PRIVATE
      "${HAV_PROJECT_ROOT}/include/games/hellbender/core")
    target_link_options(${target} PRIVATE /BASE:0x10000000 /DYNAMICBASE:NO /FIXED /OPT:NOICF)
    set_property(TARGET ${target} PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endforeach()
endif()

add_executable(shadingPredicateTests shadingPredicateTests.cpp)
target_compile_features(shadingPredicateTests PRIVATE cxx_std_23)
target_compile_options(shadingPredicateTests PRIVATE /W4 /WX /utf-8)
target_link_options(shadingPredicateTests PRIVATE
  /BASE:0x10000000 /DYNAMICBASE:NO /FIXED /OPT:NOICF)
set_property(TARGET shadingPredicateTests PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(HAV_BUILD_SHELL_TESTS "Build the per-edition shell fullscreen fixtures" OFF)
set(HAV_SHELL_EDITIONS "English;French;German" CACHE STRING "Editions for shell fullscreen fixtures")

if(HAV_BUILD_SHELL_TESTS)
  get_filename_component(HAV_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

  foreach(edition IN LISTS HAV_SHELL_EDITIONS)
    if(NOT edition MATCHES "^(English|French|German)$")
      message(FATAL_ERROR "Unknown Hellbender shell test edition: ${edition}")
    endif()

    string(TOLOWER "${edition}" edition_slug)
    set(address_header "${HAV_PROJECT_ROOT}/out/patches/hellbender/${edition_slug}/gameAddresses.hpp")

    if(NOT EXISTS "${address_header}")
      message(FATAL_ERROR "Prepare the ${edition} recipe before building its shell tests.")
    endif()

    set(target "shellFullscreenTests${edition}")
    add_executable(${target} shellFullscreenTests.cpp)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_definitions(${target} PRIVATE NOMINMAX)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 "/FI${address_header}")
    target_include_directories(${target} PRIVATE "${HAV_PROJECT_ROOT}/include/games/hellbender/core")
    target_link_libraries(${target} PRIVATE user32)
    target_link_options(${target} PRIVATE
      /BASE:0x10000000 /DYNAMICBASE:NO /FIXED /OPT:NOICF)
    set_property(TARGET ${target} PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endforeach()
endif()

option(HAV_BUILD_RECOVERY_TESTS "Build owned fullscreen and renderer recovery fixtures" OFF)
set(HAV_RECOVERY_EDITIONS "English;French;German" CACHE STRING "Editions for recovery fixtures")

if(HAV_BUILD_RECOVERY_TESTS)
  get_filename_component(HAV_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

  foreach(edition IN LISTS HAV_RECOVERY_EDITIONS)
    if(NOT edition MATCHES "^(English|French|German)$")
      message(FATAL_ERROR "Unknown Hellbender recovery test edition: ${edition}")
    endif()

    string(TOLOWER "${edition}" edition_slug)
    set(address_header "${HAV_PROJECT_ROOT}/out/patches/hellbender/${edition_slug}/gameAddresses.hpp")
    set(native_root "${HAV_PROJECT_ROOT}/out/test/hellbenderNative/${edition_slug}")

    foreach(header IN ITEMS "${address_header}"
        "${native_root}/setupNative.hpp" "${native_root}/stampNative.hpp"
        "${native_root}/rendererNative.hpp" "${native_root}/presentationNative.hpp"
        "${native_root}/inputNative.hpp" "${native_root}/frameNative.hpp")
      if(NOT EXISTS "${header}")
        message(FATAL_ERROR "Prepare the ${edition} recipe and native fixture headers first.")
      endif()
    endforeach()

    foreach(fixture IN ITEMS fullscreenSetupTests rendererRecoveryTests frameRecoveryTests modeCompletionTests)
      set(target "${fixture}${edition}")
      add_executable(${target} "${fixture}.cpp")
      target_compile_features(${target} PRIVATE cxx_std_23)
      target_compile_definitions(${target} PRIVATE NOMINMAX)
      target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /O1 /Oi /arch:IA32 /EHsc
        "/FI${address_header}")
      target_include_directories(${target} PRIVATE
        "${HAV_PROJECT_ROOT}/include/games/hellbender/core" "${native_root}")
      target_link_libraries(${target} PRIVATE user32)
      target_link_options(${target} PRIVATE /BASE:0x10000000 /DYNAMICBASE:NO /FIXED /OPT:NOICF)
      set_property(TARGET ${target} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
  endforeach()
endif()
