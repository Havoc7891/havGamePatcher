# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 4.0)

project(havGamePatcher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MinGW LTO fails to link wxTextCtrlBase's multiple-inheritance thunks.
# Use section removal there and optimize across libraries on other toolchains.
if(MINGW)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL OFF)
elseif(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT patcher_ipo_supported OUTPUT patcher_ipo_error LANGUAGES CXX)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ${patcher_ipo_supported})
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ${patcher_ipo_supported})

  if(NOT patcher_ipo_supported)
    message(STATUS "Link-time optimization unavailable. Using unused-section removal only.")
    message(VERBOSE "${patcher_ipo_error}")
  endif()
endif()

if(MSVC)
  add_compile_options(
    "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release,MinSizeRel>>:/Gy>"
    "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release,MinSizeRel>>:/Gw>"
  )
  add_link_options(
    "$<$<CONFIG:Release,MinSizeRel>:/OPT:REF>"
    "$<$<CONFIG:Release,MinSizeRel>:/OPT:ICF>"
  )
elseif(MINGW)
  add_compile_options(
    "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release,MinSizeRel>>:-ffunction-sections>"
    "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release,MinSizeRel>>:-fdata-sections>"
  )
  add_link_options(
    "$<$<CONFIG:Release,MinSizeRel>:-Wl,--gc-sections>"
    "$<$<CONFIG:Release,MinSizeRel>:-s>"
  )
endif()

# Keep the application and wxWidgets on the same static MSVC runtime
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
endif()

include(FetchContent)

# wxWidgets configuration
set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)

# Apply our own release flags without wxWidgets' optional /GS- override
set(wxBUILD_OPTIMISE OFF CACHE BOOL "" FORCE)
set(wxBUILD_STRIPPED_RELEASE ON CACHE BOOL "" FORCE)

foreach(feature IN ITEMS
  # Separate libraries that the patcher does not use
  AUI HTML MEDIACTRL PROPGRID RIBBON RICHTEXT STC WEBVIEW XRC XML OPENGL

  # Printing, help, and document management
  PRINTING_ARCHITECTURE POSTSCRIPT AFM_FOR_POSTSCRIPT HELP MS_HTML_HELP WXHTML_HELP
  DOC_VIEW_ARCHITECTURE MDI MDI_ARCHITECTURE FILE_HISTORY

  # Optional artwork, rendering, and media features
  ARTPROVIDER_STD ARTPROVIDER_TANGO MARKUP SPELLCHECK METAFILE SVG SOUND ACTIVEX
  ANIMATIONCTRL GIF PCX TGA IFF PNM XPM

  # Networking and IPC
  WEBREQUEST WEBREQUEST_WINHTTP WEBREQUEST_CURL SOCKETS IPV6 URL PROTOCOL
  PROTOCOL_HTTP PROTOCOL_FTP PROTOCOL_FILE FS_INET IPC DIALUP_MANAGER

  # Archives and virtual filesystems
  ARCHIVE_STREAMS ZIPSTREAM TARSTREAM FILESYSTEM FS_ARCHIVE FS_ZIP DEBUGREPORT
)
  set(wxUSE_${feature} OFF CACHE BOOL "" FORCE)
endforeach()

# Native Windows resources load the icon without external image codecs.
# Keep IMAGE, native artwork, OLE, accessibility, and drag-and-drop support.
foreach(library IN ITEMS LIBPNG LIBJPEG LIBTIFF LIBWEBP NANOSVG LUNASVG EXPAT REGEX ZLIB)
  set(wxUSE_${library} OFF CACHE STRING "" FORCE)
endforeach()

FetchContent_Declare(
  wxWidgets
  GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
  GIT_TAG v3.3.4
  GIT_SHALLOW TRUE
  GIT_SUBMODULES_RECURSE TRUE
)

FetchContent_MakeAvailable(wxWidgets)

# Shared patch engine and registered games
add_subdirectory(src/core)

# Application
add_executable(havGamePatcher WIN32
  src/havGamePatcher.cpp
  include/havGamePatcherInfo.hpp
  resources/havGamePatcher.rc
)

target_include_directories(havGamePatcher PRIVATE include resources)

set_source_files_properties(resources/havGamePatcher.rc PROPERTIES
  COMPILE_DEFINITIONS "$<$<CONFIG:Debug>:PATCHER_DEBUG_BUILD>"
  OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/havGamePatcher.ico;${CMAKE_CURRENT_SOURCE_DIR}/resources/havGamePatcher.manifest;${CMAKE_CURRENT_SOURCE_DIR}/include/havGamePatcherInfo.hpp"
)

target_link_libraries(havGamePatcher PRIVATE
  havGamePatcherCore
  wx::core
  wx::base
)

if(MSVC)
  target_compile_options(havGamePatcher PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
  )

  # The resource file already embeds the complete application manifest
  target_link_options(havGamePatcher PRIVATE /MANIFEST:NO)
elseif(MINGW)
  # Static MinGW runtime linking
  target_link_options(havGamePatcher PRIVATE
    -static
  )
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT havGamePatcher)
