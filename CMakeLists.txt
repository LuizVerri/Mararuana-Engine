# ==============================================================================
#  Mararuana — top-level CMake workspace
# ------------------------------------------------------------------------------
#  Owns global build configuration and pulls in the two subprojects: the
#  engine itself (Mararuana-Engine, a library) and Sandbox (the example
#  app that consumes it). Each subproject owns its own name and version
#  via its own project() call — see Mararuana-Engine/CMakeLists.txt and
#  Sandbox/CMakeLists.txt.
# ==============================================================================

cmake_minimum_required(VERSION 3.28)

# ------------------------------------------------------------------------------
# Block in-source builds
# ------------------------------------------------------------------------------
# Build artifacts mixed into the source tree make a mess of the repo and
# fight with .gitignore. Always build into a separate directory, e.g.
# `cmake -S . -B build`.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not allowed.\n"
    "Use a separate build directory, for example:\n"
    "  cmake -S . -B build")
endif()

# ------------------------------------------------------------------------------
# Policies
# ------------------------------------------------------------------------------
# A few policies have to be set BEFORE project(), since they change how
# project() itself behaves.

# CMP0141 controls the debug info format MSVC generates. Only exists from
# CMake 3.25 onward, hence the guard.
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()

# ------------------------------------------------------------------------------
# Workspace declaration
# ------------------------------------------------------------------------------
# No VERSION/DESCRIPTION here on purpose: the Engine and Sandbox each own
# their own, declared where they're defined.
project(Mararuana LANGUAGES C CXX)

# ------------------------------------------------------------------------------
# Language standard
# ------------------------------------------------------------------------------
# C++20 everywhere in the tree. Could be loosened later for portability,
# but one standard across the board keeps things simple for now.
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ------------------------------------------------------------------------------
# Workspace-wide CMake modules
# ------------------------------------------------------------------------------
# cmake/MarOptions.cmake       -> mar_option(), the tool that declares options
# cmake/MarResources.cmake     -> mar_target_resources() (the res/ folder)
# cmake/MarShaders.cmake       -> mar_compile_shaders() (GLSL -> SPIR-V)
# cmake/MarTargetConfig.cmake  -> mar_configure_target(), applies options
# cmake/MararuanaOptions.cmake -> every option this workspace actually
#                                  declares — the file to open to configure
#                                  the project. Everything above it is
#                                  generic, reusable machinery; this one is
#                                  Mararuana-specific.
#
# These live at the workspace root rather than inside Mararuana-Engine/cmake
# (which only holds the engine's own find-modules, like Spdlog.cmake)
# because they apply to the whole workspace — engine and Sandbox — not just
# the library.
#
# Included here, before add_subdirectory(), so every macro, function, and
# cache variable is already visible in both subdirectories — in particular,
# Mararuana-Engine needs MARARUANA_APP_ROOT and mar_compile_shaders() the
# moment its own CMakeLists.txt starts running.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(MarOptions)
include(MarResources)
include(MarShaders)
include(MarTargetConfig)
include(MararuanaOptions)

# compile_commands.json, for clangd, clang-tidy, and similar tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type for single-config generators (Makefiles, Ninja).
# Multi-config generators (Visual Studio, Xcode) ignore this.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ------------------------------------------------------------------------------
# Output directories
# ------------------------------------------------------------------------------
# Keeps every generated binary — executables, static/shared libs — in
# predictable subfolders under the build directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ------------------------------------------------------------------------------
# Compiler-specific options
# ------------------------------------------------------------------------------
if(MSVC)
  # Debug info format (needs the CMP0141 policy above to take effect).
  if(POLICY CMP0141)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
  endif()
endif()

# ------------------------------------------------------------------------------
# Subprojects
# ------------------------------------------------------------------------------
add_subdirectory(Mararuana-Engine)

# Uses MARARUANA_APP_ROOT rather than a hardcoded "Sandbox" so pointing
# that variable somewhere else (see cmake/MararuanaOptions.cmake) actually
# builds the new app — not just gives the engine visibility into it. An
# explicit binary dir is required because MARARUANA_APP_ROOT is an
# absolute path, which add_subdirectory() can't derive a build folder
# name from on its own; naming it after the app folder itself keeps the
# default case identical to before (build/Sandbox/).
if(MARARUANA_BUILD_APP)
  get_filename_component(MARARUANA_APP_BINARY_DIR_NAME "${MARARUANA_APP_ROOT}" NAME)
  add_subdirectory("${MARARUANA_APP_ROOT}" "${CMAKE_BINARY_DIR}/${MARARUANA_APP_BINARY_DIR_NAME}")
endif()

# ------------------------------------------------------------------------------
# Configuration summary
# ------------------------------------------------------------------------------
# Prints one block at the end of `cmake -S . -B build` with every
# mar_option() value and each executable's final res/ layout. Purely
# informational — doesn't affect the build.
mar_print_config_summary()