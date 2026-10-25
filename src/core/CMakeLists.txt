# SPDX-License-Identifier: MIT

add_library(havGamePatcherCore STATIC
  havGamePatcherCore.cpp
  gameRegistry.cpp
  ../../include/core/havGamePatcherCore.hpp
  ../../include/core/gameRegistry.hpp
  ../../include/core/patchDefinition.hpp
  ../games/hellbender/patches.cpp
  ../../include/games/hellbender/patches.hpp
  ../games/fury3/patches.cpp
  ../../include/games/fury3/patches.hpp
)

target_compile_features(havGamePatcherCore PUBLIC cxx_std_23)
target_include_directories(havGamePatcherCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
target_link_libraries(havGamePatcherCore PUBLIC bcrypt)

if(MSVC)
  target_compile_options(havGamePatcherCore PRIVATE /utf-8)
  set_property(TARGET havGamePatcherCore PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
