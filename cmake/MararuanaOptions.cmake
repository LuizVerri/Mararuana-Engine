# ==============================================================================
#  MararuanaOptions.cmake — every configurable value in this workspace
# ------------------------------------------------------------------------------
#  THIS is the file to open to configure Mararuana. Everything else in
#  cmake/ is machinery that reads what's declared here — you shouldn't
#  need to go looking through it just to flip a setting.
#
#  Every value here is declared with mar_option() (see MarOptions.cmake),
#  except MARARUANA_APP_ROOT: a folder path, which isn't one of
#  mar_option()'s two supported types (BOOL/STRING), so it's declared with
#  a plain set(CACHE PATH ...) and registered by hand so it still shows up
#  in the configuration summary.
#
#  This file has NO build logic — it only declares values. Turning a value
#  into an actual compile definition/flag happens in MarTargetConfig.cmake
#  (mar_configure_target). To add a new engine option: add one mar_option()
#  call below, then read it inside mar_configure_target().
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# The app
# ------------------------------------------------------------------------------
# The directory playing the role of "the app" that consumes the engine.
# Its include/ and res/ are ALWAYS visible to Mararuana-Engine, unconditionally:
#   - include/CustomEvents/<header>  (see MAR_CUSTOM_EVENTS_HEADER below)
#     is always on the engine's include path.
#   - res/shaders/ is always compiled to SPIR-V — see mar_compile_shaders()
#     in Mararuana-Engine/CMakeLists.txt.
# This holds even when MARARUANA_BUILD_APP=OFF: the ENGINE depends on
# the app providing these two folders, regardless of whether the app's own
# executable target gets built.
set(MARARUANA_APP_ROOT "${CMAKE_SOURCE_DIR}/Sandbox" CACHE PATH
    "Root directory of the application that consumes the engine")
set_property(GLOBAL APPEND PROPERTY MAR_ALL_OPTIONS MARARUANA_APP_ROOT)

# Resolves a relative override (e.g. -DMARARUANA_APP_ROOT=MyGame) against
# the workspace root, so typing a short relative path from the command
# line does what you'd expect instead of resolving against whatever
# directory happened to be current when cmake ran.
cmake_path(ABSOLUTE_PATH MARARUANA_APP_ROOT BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)

if(NOT IS_DIRECTORY "${MARARUANA_APP_ROOT}")
    message(FATAL_ERROR "MARARUANA_APP_ROOT ('${MARARUANA_APP_ROOT}') is not a directory.")
endif()

# Named "BUILD_APP", not "BUILD_SANDBOX": it governs whichever folder
# MARARUANA_APP_ROOT currently points at, which doesn't have to be Sandbox.
mar_option(MARARUANA_BUILD_APP BOOL ON "Build the app executable (see MARARUANA_APP_ROOT)")

# ------------------------------------------------------------------------------
# Engine options
# ------------------------------------------------------------------------------
mar_option(MAR_ENABLE_WINDOW        BOOL   ON   "Enable the window/input subsystem")
mar_option(MAR_SIMD_LEVEL           STRING SSE2 "Target SIMD level" STRINGS NONE SSE2 AVX2)
mar_option(MAR_CUSTOM_EVENTS_HEADER STRING "CustomEvents/CustomEvents.h"
    "Header, relative to the app's include/, registered as MAR_CUSTOM_EVENTS_H")